#ifndef SETTINGS_H
#define SETTINGS_H

#include <QObject>
#include <QSettings>

/*
 * Persistent app settings.
 *
 * Backed by an explicitly named INI file at the one config location that is
 * both whitelisted and actually writable inside the Sailjail/firejail
 * sandbox. QSettings' usual org/app form would resolve to
 * ~/.config/harbour-zendecoder, which the launch profile whitelists but
 * never creates — writes there fail silently.
 */
class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool soundEnabled READ soundEnabled WRITE setSoundEnabled NOTIFY soundEnabledChanged)
    Q_PROPERTY(bool vibrationEnabled READ vibrationEnabled WRITE setVibrationEnabled NOTIFY vibrationEnabledChanged)
    Q_PROPERTY(bool productLookupEnabled READ productLookupEnabled WRITE setProductLookupEnabled NOTIFY productLookupEnabledChanged)
    Q_PROPERTY(bool torchOn READ torchOn WRITE setTorchOn NOTIFY torchOnChanged)

public:
    explicit Settings(QObject *parent = nullptr);

    bool soundEnabled() const;
    void setSoundEnabled(bool on);
    bool vibrationEnabled() const;
    void setVibrationEnabled(bool on);
    bool productLookupEnabled() const;
    void setProductLookupEnabled(bool on);
    bool torchOn() const;
    void setTorchOn(bool on);

signals:
    void soundEnabledChanged();
    void vibrationEnabledChanged();
    void productLookupEnabledChanged();
    void torchOnChanged();

private:
    static QString settingsFilePath();

    QSettings m_settings;
    bool m_soundEnabled;
    bool m_vibrationEnabled;
    bool m_productLookupEnabled;
    bool m_torchOn;
};

#endif // SETTINGS_H
