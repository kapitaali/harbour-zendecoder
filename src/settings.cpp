#include "settings.h"
#include "formatgroups.h"

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
    // Every symbology group is on by default (PLAN.md); the keys only
    // appear in the file once the user has moved a switch.
    , m_formatRetail(m_settings.value(QStringLiteral("formatRetail"), true).toBool())
    , m_formatLinear(m_settings.value(QStringLiteral("formatLinear"), true).toBool())
    , m_formatMatrix(m_settings.value(QStringLiteral("formatMatrix"), true).toBool())
    , m_formatPdf417(m_settings.value(QStringLiteral("formatPdf417"), true).toBool())
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

bool Settings::formatRetail() const { return m_formatRetail; }
void Settings::setFormatRetail(bool on)
{
    if (m_formatRetail == on)
        return;
    m_formatRetail = on;
    m_settings.setValue(QStringLiteral("formatRetail"), on);
    m_settings.sync();
    emit formatRetailChanged();
    emit formatMaskChanged();
}

bool Settings::formatLinear() const { return m_formatLinear; }
void Settings::setFormatLinear(bool on)
{
    if (m_formatLinear == on)
        return;
    m_formatLinear = on;
    m_settings.setValue(QStringLiteral("formatLinear"), on);
    m_settings.sync();
    emit formatLinearChanged();
    emit formatMaskChanged();
}

bool Settings::formatMatrix() const { return m_formatMatrix; }
void Settings::setFormatMatrix(bool on)
{
    if (m_formatMatrix == on)
        return;
    m_formatMatrix = on;
    m_settings.setValue(QStringLiteral("formatMatrix"), on);
    m_settings.sync();
    emit formatMatrixChanged();
    emit formatMaskChanged();
}

bool Settings::formatPdf417() const { return m_formatPdf417; }
void Settings::setFormatPdf417(bool on)
{
    if (m_formatPdf417 == on)
        return;
    m_formatPdf417 = on;
    m_settings.setValue(QStringLiteral("formatPdf417"), on);
    m_settings.sync();
    emit formatPdf417Changed();
    emit formatMaskChanged();
}

quint32 Settings::formatMask() const
{
    quint32 mask = 0;
    if (m_formatRetail)
        mask |= FormatGroup::Retail;
    if (m_formatLinear)
        mask |= FormatGroup::Linear;
    if (m_formatMatrix)
        mask |= FormatGroup::Matrix;
    if (m_formatPdf417)
        mask |= FormatGroup::Pdf417;
    return mask;
}
