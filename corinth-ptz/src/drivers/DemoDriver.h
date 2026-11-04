#pragma once

#include "CameraDriver.h"
#include <QHash>
#include <QMap>

class QTimer;

// Simulated PTZ camera so the whole app can be used without hardware.
// Movement, presets, aux switches, command latency and the Settings XML pages
// all respond. It reads the same camera profile as the ISAPI driver.
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

    bool supportsZoomLevel() const override { return true; }
    void setZoomLevel(double times, const QString &label) override;
    bool supportsPoint() const override { return true; }
    bool supportsBoxZoom() const override { return true; }
    bool supportsPresetRename() const override { return true; }
    void pointAt(double nx, double ny, const QString &label) override;
    void boxZoom(double x1, double y1, double x2, double y2, const QString &label) override;
    void renamePreset(int id, const QString &name, const QString &label) override;
    void queryPresets() override;
    void queryAux() override;
    QVariantList auxItems() const override { return profileAuxItems(); }

private:
    void ack(const QString &label, bool changesPresets = false);
    int latency() const;
    void tick();

    QTimer *m_timer = nullptr;
    double m_az = 0, m_el = -2, m_zoom = 1;
    int m_vPan = 0, m_vTilt = 0, m_vZoom = 0;
    bool m_hasTarget = false;
    double m_tAz = 0, m_tEl = 0;
    bool m_hasZoomTarget = false;
    double m_tZoom = 1;
    QMap<int, QPair<double, double>> m_presets;
    QMap<int, QString> m_presetNames;
    QHash<QString, bool> m_aux;
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
    bool supportsSettingsEndpoint(const QString &) const override { return false; }

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
