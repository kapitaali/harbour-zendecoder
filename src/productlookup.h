#ifndef PRODUCTLOOKUP_H
#define PRODUCTLOOKUP_H

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

/*
 * Optional product lookup: GTINs via Open Food Facts (falling back to
 * Open Products Facts for non-food), ISBN books via Open Library, GS1
 * element strings via their (01) GTIN through OFF.
 *
 * Offline-first: only fires when the user enabled it in Settings and the
 * code matches one of the supported shapes (see lookupSupported()).
 * Results are cached by the caller in History (setProductName). No
 * tracking upload — one GET per unknown code with a product User-Agent.
 * Network permission is declared in the .desktop Sailjail profile; the
 * privacy policy states decoding is on-device and network is only used
 * for this lookup.
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

    QNetworkAccessManager m_net;
};

#endif // PRODUCTLOOKUP_H
