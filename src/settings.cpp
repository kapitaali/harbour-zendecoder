#include "settings.h"

#include <QDir>
#include <QFileInfo>

namespace {
const char kOrg[] = "harbour.zendecoder";
const char kFile[] = "harbour-zendecoder.conf";
} // namespace

QString Settings::settingsFilePath()
{
    // Sailjail grants the app its data dir; the config path (XDG_CONFIG_HOME
    // based) is whitelisted but not pre-created, so resolve explicitly and
    // create the parent.
    QString base = qgetenv("XDG_CONFIG_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    const QString dir = base + QLatin1Char('/') + QLatin1String(kOrg);
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + QLatin1String(kFile);
}

Settings::Settings(QObject *parent)
    : QObject(parent)
    , m_settings(settingsFilePath(), QSettings::IniFormat)
    , m_soundEnabled(m_settings.value(QStringLiteral("soundEnabled"), true).toBool())
    , m_vibrationEnabled(m_settings.value(QStringLiteral("vibrationEnabled"), true).toBool())
    , m_productLookupEnabled(m_settings.value(QStringLiteral("productLookupEnabled"), true).toBool())
    , m_torchOn(false)
{
}

bool Settings::soundEnabled() const { return m_soundEnabled; }
void Settings::setSoundEnabled(bool on)
{
    if (m_soundEnabled == on)
        return;
    m_soundEnabled = on;
    m_settings.setValue(QStringLiteral("soundEnabled"), on);
    m_settings.sync();
    emit soundEnabledChanged();
}

bool Settings::vibrationEnabled() const { return m_vibrationEnabled; }
void Settings::setVibrationEnabled(bool on)
{
    if (m_vibrationEnabled == on)
        return;
    m_vibrationEnabled = on;
    m_settings.setValue(QStringLiteral("vibrationEnabled"), on);
    m_settings.sync();
    emit vibrationEnabledChanged();
}

bool Settings::productLookupEnabled() const { return m_productLookupEnabled; }
void Settings::setProductLookupEnabled(bool on)
{
    if (m_productLookupEnabled == on)
        return;
    m_productLookupEnabled = on;
    m_settings.setValue(QStringLiteral("productLookupEnabled"), on);
    m_settings.sync();
    emit productLookupEnabledChanged();
}

bool Settings::torchOn() const { return m_torchOn; }
void Settings::setTorchOn(bool on)
{
    // Torch is session state (camera flash mode), not persisted.
    if (m_torchOn == on)
        return;
    m_torchOn = on;
    emit torchOnChanged();
}
