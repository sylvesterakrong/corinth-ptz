#pragma once

#include "CameraDriver.h"
#include <QStringList>

// ONVIF Profile S driver: PTZ ContinuousMove/Stop/Presets/Home/Status and
// GetStreamUri, with WS-Security UsernameToken (PasswordDigest) auth.
// Works with most PTZ cameras regardless of vendor. Enable ONVIF and create an
// ONVIF user on the camera first (Network > Advanced > Integration Protocol on
// Hikvision-style firmware).
class OnvifDriver : public CameraDriver
{
    Q_OBJECT
public:
    explicit OnvifDriver(const CameraConfig &cfg, QObject *parent = nullptr);

    QString kind() const override { return QStringLiteral("ONVIF"); }

    void continuousMove(int pan, int tilt, int zoom, const QString &label) override;
    void gotoPreset(int id, const QString &label) override;
    void setPreset(int id, const QString &name, const QString &label) override;
    void gotoHome(const QString &label) override;
    void setHome(const QString &label) override;
    void focus(int dir, const QString &label) override;
    void iris(int dir, const QString &label) override;
    void onePushFocus(const QString &label) override;
    void setAux(const QString &name, bool on, const QString &label) override;
    void queryStatus() override;
    void ping() override;
    QString streamUrl(bool mainStream, int channel) const override;

private:
    void init();
    void soap(const QString &endpoint, const QString &body, bool auth, HttpCallback cb);
    void ptzCommand(const QString &body, const QString &label);
    QString securityHeader() const;
    QString deviceUrl() const;

    QString m_mediaUrl;
    QString m_ptzUrl;
    QStringList m_profiles;
    QString m_mainUri;
    QString m_subUri;
    bool m_initRunning = false;
    bool m_statusInFlight = false;
};
