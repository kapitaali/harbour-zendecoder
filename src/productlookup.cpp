#include "productlookup.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

ProductLookup::ProductLookup(QObject *parent)
    : QObject(parent)
{
    connect(&m_net, &QNetworkAccessManager::finished,
            this, &ProductLookup::onFinished);
}

bool ProductLookup::looksLikeGtin(const QString &text) const
{
    static const QRegularExpression re(QStringLiteral("^\\d{8}$|^\\d{12}$|^\\d{13}$|^\\d{14}$"));
    return re.match(text.trimmed()).hasMatch();
}

void ProductLookup::lookup(const QString &barcode)
{
    const QString code = barcode.trimmed();
    if (!looksLikeGtin(code)) {
        emit notFound(code);
        return;
    }
    QUrl url(QStringLiteral("https://world.openfoodfacts.org/api/v2/product/%1.json").arg(code));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("harbour-zendecoder/0.1 (SailfishOS)"));
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply *reply = m_net.get(req);
    reply->setProperty("barcode", code);
    qInfo("product lookup requested: %s (ptr=%p)", qPrintable(code),
          static_cast<void *>(reply));
}

void ProductLookup::onFinished(QNetworkReply *reply)
{
    const QString code = reply->property("barcode").toString();

    // Qt 5.6 delivered this slot twice for one request (log: identical
    // timestamps, same reply). Answer only the first delivery — otherwise
    // found/notFound reach the UI and history twice.
    if (reply->property("handled").toBool()) {
        qInfo("product lookup duplicate finished ignored: %s (ptr=%p)",
              qPrintable(code), static_cast<void *>(reply));
        reply->deleteLater();
        return;
    }
    reply->setProperty("handled", true);

    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    const QByteArray body = reply->readAll();
    qInfo("product lookup finished: %s (ptr=%p) err=%d body=%d bytes",
          qPrintable(code), static_cast<void *>(reply), int(error),
          int(body.size()));
    reply->deleteLater();

    // Open Food Facts answers an unknown product with HTTP 404 and a real
    // body ({"status":0,"status_verbose":"product not found"}). That is a
    // RESULT, not a fault — the parser below turns it into notFound() ("No
    // product found"), which reads far better than a "Lookup failed" toast.
    // Everything else that isn't NoError is a genuine transport failure.
    const bool transportFailure = error != QNetworkReply::NoError
            && error != QNetworkReply::ContentNotFoundError;
    if (transportFailure) {
        qInfo("product lookup %s failed: %s", qPrintable(code),
              qPrintable(errorString));
        emit lookupError(code, errorString);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        qInfo("product lookup %s: unparseable reply (%d bytes)",
              qPrintable(code), int(body.size()));
        emit notFound(code);
        return;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("status")).toInt() != 1) {
        qInfo("product lookup %s: not in Open Food Facts", qPrintable(code));
        emit notFound(code);
        return;
    }
    const QJsonObject product = root.value(QStringLiteral("product")).toObject();
    const QString name = product.value(QStringLiteral("product_name")).toString();
    const QString brands = product.value(QStringLiteral("brands")).toString();
    if (name.isEmpty() && brands.isEmpty()) {
        qInfo("product lookup %s: in database, no name or brand",
              qPrintable(code));
        emit notFound(code);
        return;
    }
    qInfo("product lookup %s: %s (%s)", qPrintable(code), qPrintable(name),
          qPrintable(brands));
    emit found(code, name, brands);
}
