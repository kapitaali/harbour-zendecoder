#include "productlookup.h"

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
}

void ProductLookup::onFinished(QNetworkReply *reply)
{
    const QString code = reply->property("barcode").toString();
    if (reply->error() != QNetworkReply::NoError) {
        emit lookupError(code, reply->errorString());
        reply->deleteLater();
        return;
    }
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        emit notFound(code);
        return;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("status")).toInt() != 1) {
        emit notFound(code);
        return;
    }
    const QJsonObject product = root.value(QStringLiteral("product")).toObject();
    const QString name = product.value(QStringLiteral("product_name")).toString();
    const QString brands = product.value(QStringLiteral("brands")).toString();
    if (name.isEmpty() && brands.isEmpty()) {
        emit notFound(code);
        return;
    }
    emit found(code, name, brands);
}
