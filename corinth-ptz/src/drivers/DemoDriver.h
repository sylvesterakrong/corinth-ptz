#pragma once

#include "CameraDriver.h"
#include <QHash>

class QTimer;

// Simulated PTZ camera so the whole app can be used without hardware.
// Movement, presets, command latency and the Settings XML pages all respond.
class DemoDriver : public CameraDriver
{
    Q_OBJECT
public:
    explicit DemoDriver(const CameraConfig &cfg, QObject *parent = nullptr);

    QString kind() const override { return QStringLiteral("Demo"); }

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
    void ack(const QString &label);
    int latency() const;
    void tick();

    QTimer *m_timer = nullptr;
    double m_az = 0, m_el = -2, m_zoom = 1;
    int m_vPan = 0, m_vTilt = 0, m_vZoom = 0;
    bool m_hasTarget = false;
    double m_tAz = 0, m_tEl = 0;
    QHash<int, QPair<double, double>> m_presets;
    QPair<double, double> m_home{0, 0};
    QHash<QString, QByteArray> m_docs;
};

// A camera with video only (no PTZ control), e.g. a fixed RTSP camera.
class RtspOnlyDriver : public CameraDriver
{
    Q_OBJECT
public:
    explicit RtspOnlyDriver(const CameraConfig &cfg, QObject *parent = nullptr);
    QString kind() const override { return QStringLiteral("RTSP only"); }
    bool hasPtz() const override { return false; }

    void continuousMove(int, int, int, const QString &label) override { fail(label); }
    void gotoPreset(int, const QString &label) override { fail(label); }
    void setPreset(int, const QString &, const QString &label) override { fail(label); }
    void gotoHome(const QString &label) override { fail(label); }
    void setHome(const QString &label) override { fail(label); }
    void focus(int, const QString &label) override { fail(label); }
    void iris(int, const QString &label) override { fail(label); }
    void onePushFocus(const QString &label) override { fail(label); }
    void setAux(const QString &, bool, const QString &label) override { fail(label); }
    void queryStatus() override {}
    void ping() override;
    QString streamUrl(bool mainStream, int channel) const override;

private:
    void fail(const QString &label) { emit commandDone(label, false, 0, QStringLiteral("RTSP-only camera: no PTZ control")); }
};
