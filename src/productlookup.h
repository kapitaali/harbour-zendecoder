#ifndef PRODUCTLOOKUP_H
#define PRODUCTLOOKUP_H

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

/*
 * Optional product lookup.
 *
 * GTINs are asked of a chain of keyless databases, stopping at the
 * first hit (see kFacts[] in the .cpp):
 *   Open Food Facts -> Open Beauty Facts -> Open Pet Food Facts ->
 *   Open Products Facts -> UPCitemdb (commercial index, trial tier,
 *   local daily quota kept so we never exceed its stated 100/day).
 * ISBN books go to Open Library; GS1 element strings contribute their
 * (01) GTIN to the same chain.
 *
 * Offline-first: only fires when the user enabled it in Settings and the
 * code matches one of the supported shapes (see lookupSupported()).
 * Results are cached by the caller in History (setProductName). No
 * tracking upload — sequential GETs carrying only the code, with a
 * product User-Agent. Network permission is declared in the .desktop
 * Sailjail profile; the privacy policy names every destination.
 */
class ProductLookup : public QObject
{
    Q_OBJECT
public:
    explicit ProductLookup(QObject *parent = nullptr);

    Q_INVOKABLE bool looksLikeGtin(const QString &text) const;
    Q_INVOKABLE bool looksLikeIsbn(const QString &text) const;
    Q_INVOKABLE QString gs1Gtin(const QString &text) const;
    Q_INVOKABLE bool lookupSupported(const QString &text) const;
    Q_INVOKABLE void lookup(const QString &barcode);

signals:
    void found(const QString &barcode, const QString &name, const QString &brands);
    void notFound(const QString &barcode);
    void lookupError(const QString &barcode, const QString &message);

private slots:
    void onFinished(QNetworkReply *reply);

private:
    void parseOpenLibrary(const QString &query, const QByteArray &body);
    void parseUpcItemDb(const QString &query, const QByteArray &body);
    void requestFacts(const QString &query, const QString &gtin, int chain);
    bool requestUpcItemDb(const QString &query, const QString &gtin);

    QNetworkAccessManager m_net;
};

#endif // PRODUCTLOOKUP_H
