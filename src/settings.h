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
    // Symbology groups (all ON by default); see formatgroups.h for the bits.
    Q_PROPERTY(bool formatRetail READ formatRetail WRITE setFormatRetail NOTIFY formatRetailChanged)
    Q_PROPERTY(bool formatLinear READ formatLinear WRITE setFormatLinear NOTIFY formatLinearChanged)
    Q_PROPERTY(bool formatMatrix READ formatMatrix WRITE setFormatMatrix NOTIFY formatMatrixChanged)
    Q_PROPERTY(bool formatPdf417 READ formatPdf417 WRITE setFormatPdf417 NOTIFY formatPdf417Changed)
    /** The four groups as a FormatGroup bit mask — what the decoder reads. */
    Q_PROPERTY(quint32 formatMask READ formatMask NOTIFY formatMaskChanged)

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
    bool formatRetail() const;
    void setFormatRetail(bool on);
    bool formatLinear() const;
    void setFormatLinear(bool on);
    bool formatMatrix() const;
    void setFormatMatrix(bool on);
    bool formatPdf417() const;
    void setFormatPdf417(bool on);
    quint32 formatMask() const;

signals:
    void soundEnabledChanged();
    void vibrationEnabledChanged();
    void productLookupEnabledChanged();
    void torchOnChanged();
    void formatRetailChanged();
    void formatLinearChanged();
    void formatMatrixChanged();
    void formatPdf417Changed();
    void formatMaskChanged();

private:
    static QString settingsFilePath();

    QSettings m_settings;
    bool m_soundEnabled;
    bool m_vibrationEnabled;
    bool m_productLookupEnabled;
    bool m_torchOn;
    bool m_formatRetail;
    bool m_formatLinear;
    bool m_formatMatrix;
    bool m_formatPdf417;
};

#endif // SETTINGS_H
