#ifndef PRODUCTLOOKUP_H
#define PRODUCTLOOKUP_H

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>

/*
 * Optional product lookup for EAN/UPC codes via Open Food Facts.
 *
 * Offline-first: only fires when the user enabled it in Settings and the
 * code looks like a GTIN (8/12/13/14 digits). Results are cached by the
 * caller in History (setProductName). No tracking upload — a single GET
 * per unknown barcode with a product User-Agent. Network permission is
 * declared in the .desktop Sailjail profile; the privacy policy states
 * decoding is on-device and network is only used for this lookup.
 */
class ProductLookup : public QObject
{
    Q_OBJECT
public:
    explicit ProductLookup(QObject *parent = nullptr);

    Q_INVOKABLE bool looksLikeGtin(const QString &text) const;
    Q_INVOKABLE void lookup(const QString &barcode);

signals:
    void found(const QString &barcode, const QString &name, const QString &brands);
    void notFound(const QString &barcode);
    void lookupError(const QString &barcode, const QString &message);

private slots:
    void onFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager m_net;
};

#endif // PRODUCTLOOKUP_H
