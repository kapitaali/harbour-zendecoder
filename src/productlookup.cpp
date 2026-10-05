#include "productlookup.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
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
    if (looksLikeIsbn(code)) {
        // Books route to Open Library, not OFF: a 978/979 EAN-13 is also
        // GTIN-shaped, but OFF rarely holds books while OL answers title
        // + authors keyless in one call.
        QString clean = code;
        clean.remove(QLatin1Char('-')).remove(QLatin1Char(' '));
        QUrl url(QStringLiteral("https://openlibrary.org/api/books?bibkeys=ISBN:%1&format=json&jscmd=data").arg(clean));
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("harbour-zendecoder/0.2 (SailfishOS)"));
        req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
        QNetworkReply *reply = m_net.get(req);
        reply->setProperty("query", code);   // signal matching (ResultPage)
        reply->setProperty("source", QStringLiteral("openlibrary"));
        qInfo("book lookup requested: %s (ptr=%p)", qPrintable(code),
              static_cast<void *>(reply));
        return;
    }
    QString gtin = code;
    if (!looksLikeGtin(gtin)) {
        // GS1 element strings ("(01)2739...") carry the GTIN in AI 01.
        gtin = gs1Gtin(code);
    }
    if (!looksLikeGtin(gtin)) {
        emit notFound(code);
        return;
    }
    QUrl url(QStringLiteral("https://world.openfoodfacts.org/api/v2/product/%1.json").arg(gtin));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("harbour-zendecoder/0.2 (SailfishOS)"));
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply *reply = m_net.get(req);
    // Signals carry the scanned text (what ResultPage matches on), not the
    // extracted GTIN; the log keeps both.
    reply->setProperty("query", code);
    reply->setProperty("barcode", gtin);
    reply->setProperty("source", QStringLiteral("off"));
    qInfo("product lookup requested: %s (gtin=%s, ptr=%p)", qPrintable(code),
          qPrintable(gtin), static_cast<void *>(reply));
}

bool ProductLookup::looksLikeIsbn(const QString &text) const
{
    QString s = text.trimmed();
    s.remove(QLatin1Char('-')).remove(QLatin1Char(' '));
    // ISBN-13: 13 digits in the book ranges, valid check digit.
    if (s.length() == 13 && (s.startsWith(QStringLiteral("978"))
                             || s.startsWith(QStringLiteral("979")))) {
        int sum = 0;
        for (int i = 0; i < 13; ++i) {
            if (!s.at(i).isDigit())
                return false;
            const int d = s.at(i).digitValue();
            sum += (i % 2 == 0) ? d : 3 * d;
        }
        return sum % 10 == 0;
    }
    // ISBN-10: 9 digits + digit/X, valid mod-11 check.
    if (s.length() == 10) {
        int sum = 0;
        for (int i = 0; i < 9; ++i) {
            if (!s.at(i).isDigit())
                return false;
            sum += (10 - i) * s.at(i).digitValue();
        }
        const QChar last = s.at(9).toUpper();
        if (last == QLatin1Char('X'))
            sum += 10;
        else if (last.isDigit())
            sum += last.digitValue();
        else
            return false;
        return sum % 11 == 0;
    }
    return false;
}

QString ProductLookup::gs1Gtin(const QString &text) const
{
    // GS1 element string: the GTIN lives in AI (01), 14 digits.
    static const QRegularExpression re(QStringLiteral("\\(01\\)(\\d{14})"));
    const QRegularExpressionMatch m = re.match(text.trimmed());
    return m.hasMatch() ? m.captured(1) : QString();
}

bool ProductLookup::lookupSupported(const QString &text) const
{
    const QString code = text.trimmed();
    return looksLikeGtin(code) || looksLikeIsbn(code)
            || looksLikeGtin(gs1Gtin(code));
}

void ProductLookup::onFinished(QNetworkReply *reply)
{
    // "query" is the scanned text the UI matches signals on; "barcode" is
    // the code actually sent to the API (same thing except GS1 extracts).
    const QString query = reply->property("query").toString();
    const QString code = reply->property("barcode").toString().isEmpty()
            ? query : reply->property("barcode").toString();
    const QString source = reply->property("source").toString();

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
        emit lookupError(query, errorString);
        return;
    }

    if (source == QStringLiteral("openlibrary"))
        return parseOpenLibrary(query, body);

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        qInfo("product lookup %s: unparseable reply (%d bytes)",
              qPrintable(code), int(body.size()));
        emit notFound(query);
        return;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("status")).toInt() != 1) {
        if (source == QStringLiteral("off")) {
            // OFF is food-focused; non-food products (office supplies,
            // electronics, ...) live in its sister project with the same
            // API shape. One chained request before giving up.
            qInfo("product lookup %s: not food, trying Open Products Facts",
                  qPrintable(code));
            QUrl url(QStringLiteral("https://world.openproductsfacts.org/api/v2/product/%1.json").arg(code));
            QNetworkRequest req(url);
            req.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("harbour-zendecoder/0.2 (SailfishOS)"));
            req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
            QNetworkReply *retry = m_net.get(req);
            retry->setProperty("query", query);
            retry->setProperty("barcode", code);
            retry->setProperty("source", QStringLiteral("opf"));
            return;
        }
        qInfo("product lookup %s: not in Open Food/Products Facts", qPrintable(code));
        emit notFound(query);
        return;
    }
    const QJsonObject product = root.value(QStringLiteral("product")).toObject();
    const QString name = product.value(QStringLiteral("product_name")).toString();
    const QString brands = product.value(QStringLiteral("brands")).toString();
    if (name.isEmpty() && brands.isEmpty()) {
        qInfo("product lookup %s: in database, no name or brand",
              qPrintable(code));
        emit notFound(query);
        return;
    }
    qInfo("product lookup %s: %s (%s)", qPrintable(code), qPrintable(name),
          qPrintable(brands));
    emit found(query, name, brands);
}

void ProductLookup::parseOpenLibrary(const QString &query, const QByteArray &body)
{
    // {"ISBN:<code>": {"title": ..., "authors": [{"name": ...}]}}.
    // Unknown ISBNs come back as {} (or unparseable) — a miss, not a fault.
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        qInfo("book lookup %s: unparseable reply (%d bytes)",
              qPrintable(query), int(body.size()));
        emit notFound(query);
        return;
    }
    const QJsonObject root = doc.object();
    if (root.isEmpty()) {
        qInfo("book lookup %s: not in Open Library", qPrintable(query));
        emit notFound(query);
        return;
    }
    const QJsonObject book = root.begin().value().toObject();
    const QString title = book.value(QStringLiteral("title")).toString();
    QStringList authors;
    const QJsonArray list = book.value(QStringLiteral("authors")).toArray();
    for (int i = 0; i < list.count(); ++i) {
        const QString name = list.at(i).toObject()
                .value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            authors.append(name);
    }
    if (title.isEmpty() && authors.isEmpty()) {
        qInfo("book lookup %s: record has no title or authors",
              qPrintable(query));
        emit notFound(query);
        return;
    }
    qInfo("book lookup %s: %s (%s)", qPrintable(query), qPrintable(title),
          qPrintable(authors.join(QStringLiteral(", "))));
    emit found(query, title, authors.join(QStringLiteral(", ")));
}
