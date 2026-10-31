#pragma once

#include "CameraDriver.h"

// Hikvision ISAPI (and Hikvision-OEM firmware such as the CJ3000).
// Endpoints follow the public ISAPI conventions. OEM firmware can differ, so
// confirm each one against a browser DevTools capture of the camera's own web
// page (see README > "Confirming endpoints").
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

private:
    void command(const QString &method, const QString &path, const QByteArray &body,
                 const QString &label);
    bool m_statusInFlight = false;
};
