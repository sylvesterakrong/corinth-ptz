#pragma once

#include "CameraDriver.h"

// Hikvision ISAPI (and Hikvision-OEM firmware such as the CJ3000).
// Every path and XML body comes from the camera profile (profiles/hikvision-isapi.json
// plus the user's override files), so a captured endpoint is added by editing the
// profile, not this class. See README > "Camera profiles".
class IsapiDriver : public CameraDriver
{
    Q_OBJECT
public:
    explicit IsapiDriver(const CameraConfig &cfg, QObject *parent = nullptr);

    QString kind() const override { return QStringLiteral("ISAPI"); }

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
    void request(const QString &method, const QString &path, const QByteArray &body,
                 HttpCallback cb) override;

    bool hasChannel2() const override;
    bool supportsZoomLevel() const override { return m_profile.has(QStringLiteral("ptz.absoluteZoom")); }
    void setZoomLevel(double times, const QString &label) override;
    bool supportsPoint() const override { return m_profile.has(QStringLiteral("ptz.point")); }
    bool supportsBoxZoom() const override { return m_profile.has(QStringLiteral("ptz.point")); }
    bool supportsPresetRename() const override;
    void pointAt(double nx, double ny, const QString &label) override;
    void boxZoom(double x1, double y1, double x2, double y2, const QString &label) override;
    void renamePreset(int id, const QString &name, const QString &label) override;
    void queryPresets() override;
    void queryAux() override;
    QVariantList auxItems() const override { return profileAuxItems(); }

private:
    QVariantMap vars() const;
    // Sends the profile entry `key`; `v` adds to the common variables.
    void send(const QString &key, const QVariantMap &v, const QString &label, bool changesPresets = false);
    void command(const QString &method, const QString &path, const QByteArray &body,
                 const QString &label, bool changesPresets = false);
    bool m_statusInFlight = false;
    double m_lastAz = 0, m_lastEl = 0;   // from the last status answer, so an absolute zoom keeps pan and tilt
    bool m_positionKnown = false;
};
