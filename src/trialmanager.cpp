#include "trialmanager.h"

#include <QDateTime>
#include <QDir>

namespace {
// Pro license secret. Lives in secrets/prosecret.h (repo-root, gitignored,
// never committed or pushed) as `#define PRO_SECRET "..."` — delivered to
// buyers via the Ko-fi digital product, entered once in Settings and stored
// locally. Public clones build without that file: PRO_SECRET is empty and
// no key validates (trial mode still works). Deliberately kept out of QML
// (only the .cpp comparison sees it). v2 replaces this with per-email
// signed keys.
#if __has_include("../secrets/prosecret.h")
#include "../secrets/prosecret.h"
#else
#define PRO_SECRET ""
#endif
const char kProSecret[] = PRO_SECRET;
const int kTrialDays = 14;
} // namespace

QString TrialManager::settingsFilePath()
{
    QString base = qgetenv("XDG_CONFIG_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    const QString dir = base + QStringLiteral("/harbour.zendecoder");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/harbour-zendecoder.conf");
}

TrialManager::TrialManager(QObject *parent)
    : QObject(parent)
    , m_settings(settingsFilePath(), QSettings::IniFormat)
{
    if (!m_settings.contains(QStringLiteral("firstRun"))) {
        m_settings.setValue(QStringLiteral("firstRun"),
                            QDateTime::currentDateTime().toString(Qt::ISODate));
        m_settings.sync();
    }
}

QString TrialManager::kofiUrl() const
{
#ifdef KOFI_URL
    return QStringLiteral(KOFI_URL);
#else
    return QString();
#endif
}

int TrialManager::trialDaysLeft() const
{
#ifdef PRO_SEED_BUILD
    return kTrialDays;
#else
    const QDateTime first = QDateTime::fromString(
        m_settings.value(QStringLiteral("firstRun")).toString(), Qt::ISODate);
    if (!first.isValid())
        return 0;
    const QDateTime now = QDateTime::currentDateTime();
    if (now < first)
        return 0; // clock rolled back: fail closed to free mode
    const int elapsed = first.daysTo(now);
    const int left = kTrialDays - elapsed;
    return left < 0 ? 0 : left;
#endif
}

bool TrialManager::hasValidKey() const
{
#ifdef PRO_SEED_BUILD
    return true;
#else
    // Empty stored keys never validate — otherwise a public build (empty
    // PRO_SECRET, see above) would unlock Pro on an empty entry.
    const QString stored = storedKey().trimmed();
    return !stored.isEmpty() && stored == QLatin1String(kProSecret);
#endif
}

bool TrialManager::isPro() const
{
#ifdef PRO_SEED_BUILD
    return true;
#else
    return hasValidKey() || trialDaysLeft() > 0;
#endif
}

QString TrialManager::mode() const
{
#ifdef PRO_SEED_BUILD
    return QStringLiteral("SeedPro");
#else
    if (hasValidKey())
        return QStringLiteral("ProUnlocked");
    if (trialDaysLeft() > 0)
        return QStringLiteral("Trial");
    return QStringLiteral("Free");
#endif
}

QString TrialManager::storedKey() const
{
    return m_settings.value(QStringLiteral("licenseKey")).toString();
}

bool TrialManager::submitKey(const QString &key)
{
#ifdef PRO_SEED_BUILD
    Q_UNUSED(key)
    emit changed();
    return true;
#else
    m_settings.setValue(QStringLiteral("licenseKey"), key.trimmed());
    m_settings.sync();
    emit changed();
    return hasValidKey();
#endif
}

void TrialManager::clearKey()
{
    m_settings.remove(QStringLiteral("licenseKey"));
    m_settings.sync();
    emit changed();
}
