#ifndef TRIALMANAGER_H
#define TRIALMANAGER_H

#include <QObject>
#include <QSettings>

/*
 * Pro trial + license key state.
 *
 * Seed builds (PRO_SEED_BUILD defined in the .pro) report isPro() == true
 * unconditionally and mode == SeedPro: the shipped app is full Pro while
 * the store / Ko-fi pipeline is being set up.
 *
 * Once PRO_SEED_BUILD is removed, the logic becomes a 14-day trial from
 * first launch plus an offline license key:
 *   isPro() = hasValidKey() || trialDaysLeft() > 0
 * v0.1 key check is a single shared secret delivered via Ko-fi (obfuscated
 * in the .cpp, never in QML). v2 path: per-email signed keys, public key
 * embedded, offline keygen script kept by the developer.
 *
 * firstRun lives in the same sandbox-proven INI file as Settings. Reinstall
 * wipes it (Sailjail data dir is removed with the package) and restarts the
 * trial — accepted for v1 at this market size.
 */
class TrialManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isPro READ isPro NOTIFY changed)
    Q_PROPERTY(int trialDaysLeft READ trialDaysLeft NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QString kofiUrl READ kofiUrl CONSTANT)
public:
    explicit TrialManager(QObject *parent = nullptr);

    bool isPro() const;
    int trialDaysLeft() const;
    QString mode() const;
    QString kofiUrl() const;

    Q_INVOKABLE bool hasValidKey() const;
    Q_INVOKABLE bool submitKey(const QString &key);
    Q_INVOKABLE void clearKey();

signals:
    void changed();

private:
    static QString settingsFilePath();
    QString storedKey() const;

    QSettings m_settings;
};

#endif // TRIALMANAGER_H
