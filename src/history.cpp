#include "history.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>

namespace {
const char kConnection[] = "zendecoder-history";
const char kOrg[] = "harbour.zendecoder";
const char kApp[] = "harbour-zendecoder";
// Qt 5.6 (Sailfish) has no Qt::ISODateWithMs (added in 5.8): use an
// explicit milliseconds format for both directions.
const char kStampFormat[] = "yyyy-MM-ddTHH:mm:ss.zzz";

QString escapeCsv(const QString &s)
{
    QString out = s;
    if (out.contains(QLatin1Char('"')))
        out.replace(QLatin1String("\""), QLatin1String("\"\""));
    if (out.contains(QLatin1Char(',')) || out.contains(QLatin1Char('"'))
            || out.contains(QLatin1Char('\n')))
        out = QLatin1Char('"') + out + QLatin1Char('"');
    return out;
}
} // namespace

History::History(QObject *parent)
    : QAbstractListModel(parent)
{
}

QString History::databasePath()
{
    const QString dir = QDir::homePath()
            + QStringLiteral("/.local/share/") + QLatin1String(kOrg)
            + QLatin1Char('/') + QLatin1String(kApp);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/scans.db");
}

QString History::documentPath(const QString &fileName)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/Documents");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + fileName;
}

bool History::initialize()
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QLatin1String(kConnection));
    db.setDatabaseName(databasePath());
    if (!db.open()) {
        qWarning() << "history: cannot open database" << db.lastError().text();
        return false;
    }
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS scans ("
                               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                               "timestamp TEXT NOT NULL, "
                               "format TEXT NOT NULL, "
                               "value TEXT NOT NULL, "
                               "product TEXT NOT NULL DEFAULT '')"))) {
        qWarning() << "history: cannot create table" << q.lastError().text();
        return false;
    }
    refresh();
    return true;
}

void History::refresh()
{
    beginResetModel();
    m_entries.clear();
    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("SELECT id, timestamp, format, value, product "
                              "FROM scans ORDER BY id DESC"))) {
        while (q.next()) {
            ScanEntry e;
            e.id = q.value(0).toInt();
            e.timestamp = QDateTime::fromString(q.value(1).toString(), QLatin1String(kStampFormat));
            if (!e.timestamp.isValid())
                e.timestamp = QDateTime::fromString(q.value(1).toString(), Qt::ISODate);
            e.format = q.value(2).toString();
            e.value = q.value(3).toString();
            e.productName = q.value(4).toString();
            m_entries.append(e);
        }
    }
    endResetModel();
    emit countChanged();
}

int History::addScan(const QString &format, const QString &value,
                     const QString &productName)
{
    if (value.isEmpty())
        return -1;
    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO scans (timestamp, format, value, product) "
                             "VALUES (?, ?, ?, ?)"));
    q.addBindValue(QDateTime::currentDateTime().toString(QLatin1String(kStampFormat)));
    q.addBindValue(format.isEmpty() ? QStringLiteral("TEXT") : format);
    q.addBindValue(value);
    // A null QString binds as SQL NULL and trips the NOT NULL constraint
    // (the column default only applies when no value is given at all).
    q.addBindValue(productName.isNull() ? QStringLiteral("") : productName);
    if (!q.exec()) {
        qWarning() << "history: insert failed" << q.lastError().text();
        return -1;
    }
    const int id = q.lastInsertId().toInt();
    refresh();
    return id;
}

void History::deleteScan(int id)
{
    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM scans WHERE id = ?"));
    q.addBindValue(id);
    q.exec();
    refresh();
}

void History::clearAll()
{
    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.exec(QStringLiteral("DELETE FROM scans"));
    refresh();
}

void History::setProductName(int id, const QString &productName)
{
    QSqlDatabase db = QSqlDatabase::database(QLatin1String(kConnection));
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE scans SET product = ? WHERE id = ?"));
    q.addBindValue(productName);
    q.addBindValue(id);
    q.exec();
    refresh();
}

QString History::exportCsv()
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const QString path = documentPath(QStringLiteral("zendecoder-export-%1.csv").arg(stamp));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    QTextStream out(&f);
    out.setCodec("UTF-8");
    out << "id,timestamp,format,value,product\n";
    for (const ScanEntry &e : m_entries) {
        out << e.id << ','
            << escapeCsv(e.timestamp.toString(Qt::ISODate)) << ','
            << escapeCsv(e.format) << ','
            << escapeCsv(e.value) << ','
            << escapeCsv(e.productName) << '\n';
    }
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return path;
}

QString History::exportJson()
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const QString path = documentPath(QStringLiteral("zendecoder-export-%1.json").arg(stamp));
    QJsonArray arr;
    for (const ScanEntry &e : m_entries) {
        QJsonObject o;
        o[QStringLiteral("id")] = e.id;
        o[QStringLiteral("timestamp")] = e.timestamp.toString(Qt::ISODate);
        o[QStringLiteral("format")] = e.format;
        o[QStringLiteral("value")] = e.value;
        o[QStringLiteral("product")] = e.productName;
        arr.append(o);
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return path;
}

int History::count() const
{
    return m_entries.size();
}

int History::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant History::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return QVariant();
    const ScanEntry &e = m_entries.at(index.row());
    switch (role) {
    case IdRole: return e.id;
    case TimestampRole: return e.timestamp;
    case FormatRole: return e.format;
    case ValueRole: return e.value;
    case ProductNameRole: return e.productName;
    default: return QVariant();
    }
}

QHash<int, QByteArray> History::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "scanId";
    roles[TimestampRole] = "timestamp";
    roles[FormatRole] = "format";
    roles[ValueRole] = "value";
    roles[ProductNameRole] = "productName";
    return roles;
}
