#ifndef HISTORY_H
#define HISTORY_H

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QString>

struct ScanEntry {
    int id = -1;
    QDateTime timestamp;
    QString format;
    QString value;
    QString productName;
};

/*
 * Scan history: SQLite store + list model for QML.
 *
 * Lives at ~/.local/share/harbour.zendecoder/harbour-zendecoder/scans.db
 * (the Sailjail data dir, created on every launch). v0.1 keeps it simple:
 * add/list/clear/delete + CSV/JSON export into ~/Documents. No thumbnails
 * yet — those come with the static decoder (format + crop available).
 */
class History : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TimestampRole,
        FormatRole,
        ValueRole,
        ProductNameRole
    };

    explicit History(QObject *parent = nullptr);

    bool initialize();
    Q_INVOKABLE int addScan(const QString &format, const QString &value,
                            const QString &productName = QString());
    Q_INVOKABLE void deleteScan(int id);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE QString exportCsv();
    Q_INVOKABLE QString exportJson();
    Q_INVOKABLE void setProductName(int id, const QString &productName);

    int count() const;

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    void refresh();
    static QString databasePath();
    static QString documentPath(const QString &fileName);

    QList<ScanEntry> m_entries;
};

#endif // HISTORY_H
