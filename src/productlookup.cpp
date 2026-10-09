#include "productlookup.h"
#include "settings.h"

#include <QDate>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace {
// The four Open Facts siblings share one API and one JSON schema, one
// index each: food, cosmetics, pet food, everything else. Order = how
// likely a random retail code is to live there; the chain stops at the
// first hit, so a normal food scan is still exactly one request.
struct FactsSource { const char *host; const char *name; };
const FactsSource kFacts[] = {
    {"https://world.openfoodfacts.org", "off"},
    {"https://world.openbeautyfacts.org", "obf"},
    {"https://world.openpetfoodfacts.org", "opff"},
    {"https://world.openproductsfacts.org", "opf"},
};
const int kFactsCount = int(sizeof(kFacts) / sizeof(kFacts[0]));

// Last resort: UPCitemdb's keyless trial tier (100 requests/day by the
// service). We keep our own day-stamped counter and never cross it.
const int kUpcDailyLimit = 100;
} // namespace

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
    requestFacts(code, gtin, 0);
}

void ProductLookup::requestFacts(const QString &query, const QString &gtin,
                                 int chain)
{
    const FactsSource &src = kFacts[chain];
    QUrl url(QStringLiteral("%1/api/v2/product/%2.json")
                 .arg(QString::fromLatin1(src.host), gtin));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("harbour-zendecoder/0.2 (SailfishOS)"));
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply *reply = m_net.get(req);
    // Signals carry the scanned text (what ResultPage matches on), not the
    // extracted GTIN; the log keeps both.
    reply->setProperty("query", query);
    reply->setProperty("barcode", gtin);
    reply->setProperty("source", QString::fromLatin1(src.name));
    reply->setProperty("chain", chain);
    qInfo("product lookup requested: %s (gtin=%s, source=%s, chain=%d, ptr=%p)",
          qPrintable(query), qPrintable(gtin), src.name, chain,
          static_cast<void *>(reply));
}

bool ProductLookup::requestUpcItemDb(const QString &query, const QString &gtin)
{
    // The trial tier is keyless but capped (100/day). Our own day-stamped
    // counter in the app's INI file keeps a day full of unknown codes from
    // crossing it; when the quota is spent the honest answer is simply
    // "No product found" — not a fake error.
    QSettings s(Settings::settingsFilePath(), QSettings::IniFormat);
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    if (s.value(QStringLiteral("upcitemdbDay")).toString() != today) {
        s.setValue(QStringLiteral("upcitemdbDay"), today);
        s.setValue(QStringLiteral("upcitemdbCount"), 0);
    }
    const int used = s.value(QStringLiteral("upcitemdbCount")).toInt();
    if (used >= kUpcDailyLimit) {
        qInfo("product lookup %s: UPCitemdb quota spent for today (%d/%d)",
              qPrintable(gtin), used, kUpcDailyLimit);
        emit notFound(query);
        return false;
    }
    s.setValue(QStringLiteral("upcitemdbCount"), used + 1);

    QUrl url(QStringLiteral("https://api.upcitemdb.com/prod/trial/lookup?upc=%1")
                 .arg(gtin));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("harbour-zendecoder/0.2 (SailfishOS)"));
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply *reply = m_net.get(req);
    reply->setProperty("query", query);
    reply->setProperty("barcode", gtin);
    reply->setProperty("source", QStringLiteral("upcitemdb"));
    reply->setProperty("chain", kFactsCount);
    qInfo("product lookup requested: %s (gtin=%s, source=upcitemdb, "
          "%d/%d today, ptr=%p)", qPrintable(query), qPrintable(gtin),
          used + 1, kUpcDailyLimit, static_cast<void *>(reply));
    return true;
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
    const int chain = reply->property("chain").toInt();
    const int http =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

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

    // Open Facts answers an unknown product with HTTP 404 and a real body
    // ({"status":0,"status_verbose":"product not found"}). That is a
    // RESULT, not a fault — it advances the chain below and, at the end of
    // it, becomes notFound() ("No product found"), which reads far better
    // than a "Lookup failed" toast. UPCitemdb validates check digits and
    // answers 400 INVALID_UPC for a well-shaped but bogus code: also a
    // result. Anything else that isn't NoError is a genuine fault — except
    // a 429, which is a rate limit and deserves the error path.
    const bool upcClientError = source == QStringLiteral("upcitemdb")
            && http >= 400 && http < 500 && http != 429;
    const bool transportFailure = error != QNetworkReply::NoError
            && error != QNetworkReply::ContentNotFoundError
            && !upcClientError;
    if (transportFailure) {
        qInfo("product lookup %s failed: %s", qPrintable(code),
              qPrintable(errorString));
        emit lookupError(query, errorString);
        return;
    }

    if (source == QStringLiteral("openlibrary"))
        return parseOpenLibrary(query, body);
    if (source == QStringLiteral("upcitemdb"))
        return parseUpcItemDb(query, body);

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        qInfo("product lookup %s: unparseable reply (%d bytes)",
              qPrintable(code), int(body.size()));
        emit notFound(query);
        return;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("status")).toInt() != 1) {
        // A miss in one sibling index says nothing about the next one:
        // keep walking the chain, then fall through to UPCitemdb.
        const QString verbose = root.value(QStringLiteral("status_verbose"))
                                        .toString();
        if (chain + 1 < kFactsCount) {
            qInfo("product lookup %s: %s (%s) — trying %s",
                  qPrintable(code), qPrintable(verbose), qPrintable(source),
                  kFacts[chain + 1].name);
            requestFacts(query, code, chain + 1);
            return;
        }
        qInfo("product lookup %s: %s (%s) — open databases exhausted, "
              "trying UPCitemdb", qPrintable(code), qPrintable(verbose),
              qPrintable(source));
        requestUpcItemDb(query, code);   // emits notFound on quota miss
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

void ProductLookup::parseUpcItemDb(const QString &query, const QByteArray &body)
{
    // {"code":"OK","total":n,"items":[{"title":...,"brand":...,...}]}.
    // total 0 (or anything but OK) means the code is unknown to the index:
    // a miss, not a fault. The trial index is crowd-scraped, so treat the
    // first item's title as a suggestion and keep brand separate.
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    const QJsonObject root = doc.isObject() ? doc.object() : QJsonObject();
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    if (root.value(QStringLiteral("code")).toString() != QLatin1String("OK")
            || items.isEmpty()) {
        qInfo("product lookup %s: not in UPCitemdb", qPrintable(query));
        emit notFound(query);
        return;
    }
    const QJsonObject item = items.at(0).toObject();
    const QString title = item.value(QStringLiteral("title")).toString();
    const QString brand = item.value(QStringLiteral("brand")).toString();
    if (title.isEmpty() && brand.isEmpty()) {
        qInfo("product lookup %s: in UPCitemdb, no title or brand",
              qPrintable(query));
        emit notFound(query);
        return;
    }
    qInfo("product lookup %s: %s (%s) [upcitemdb]", qPrintable(query),
          qPrintable(title), qPrintable(brand));
    emit found(query, title, brand);
}
