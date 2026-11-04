#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

#include "CameraProfile.h"

class QNetworkAccessManager;
class QNetworkReply;

// Connection settings for one camera, as saved in Settings > App > Cameras.
struct CameraConfig
{
    QString id;
    QString name;
    QString ip;
    QString proto;      // "ISAPI", "ONVIF", "RTSP only", "Demo"
    int httpPort = 80;
    int rtspPort = 554;
    QString user;
    QString pass;
    QString mainUrl;    // optional override, e.g. rtsp://192.168.1.64:554/Streaming/Channels/101
    QString subUrl;
    QString flvUrl;           // optional HTTP-FLV main stream (e.g. http://IP/101.flv)
    QString thermalMainUrl;   // optional overrides for channel 2 (thermal)
    QString thermalSubUrl;
    bool shown = true;

    static CameraConfig fromMap(const QVariantMap &m);
    QVariantMap toMap(bool includePassword) const;
    // No username and no password: the camera has authentication switched off.
    bool noCredentials() const { return user.isEmpty() && pass.isEmpty(); }
};

using HttpCallback = std::function<void(bool ok, int status, const QByteArray &body, int ms)>;

// Base class for every camera protocol.
// Speeds are -100..100 (the ISAPI convention); drivers convert as needed.
// Every command carries a label, echoed back in commandDone so the UI can show
// "Last command: <label> . Ack 118 ms".
class CameraDriver : public QObject
{
    Q_OBJECT
public:
    explicit CameraDriver(const CameraConfig &cfg, QObject *parent = nullptr);
    ~CameraDriver() override;

    virtual QString kind() const = 0;
    virtual bool hasPtz() const { return true; }

    virtual void continuousMove(int pan, int tilt, int zoom, const QString &label) = 0;
    virtual void gotoPreset(int id, const QString &label) = 0;
    virtual void setPreset(int id, const QString &name, const QString &label) = 0;
    virtual void gotoHome(const QString &label) = 0;
    virtual void setHome(const QString &label) = 0;
    virtual void focus(int dir, const QString &label) = 0;     // -1 near, +1 far, 0 stop
    virtual void iris(int dir, const QString &label) = 0;      // -1 close, +1 open, 0 stop
    virtual void onePushFocus(const QString &label) = 0;
    virtual void setAux(const QString &name, bool on, const QString &label) = 0;
    virtual void queryStatus() = 0;
    virtual void ping() = 0;

    // channel: 1 = visible, 2 = thermal (dual-light cameras)
    virtual QString streamUrl(bool mainStream, int channel) const = 0;

    // Raw HTTP access for the Settings pages (ISAPI paths). Drivers that cannot
    // serve it return ok=false.
    virtual void request(const QString &method, const QString &path,
                         const QByteArray &body, HttpCallback cb);

    // ---- optional abilities (the defaults say "not supported") ---------------
    virtual bool hasChannel2() const { return !m_cfg.thermalMainUrl.isEmpty() || !m_cfg.thermalSubUrl.isEmpty(); }
    virtual bool supportsPoint() const { return false; }
    virtual bool supportsBoxZoom() const { return false; }
    virtual bool supportsPresetRename() const { return false; }
    // nx, ny: 0..1 inside the visible picture, origin top-left.
    virtual void pointAt(double nx, double ny, const QString &label);
    virtual void boxZoom(double x1, double y1, double x2, double y2, const QString &label);
    // Rename a stored preset without moving it.
    virtual void renamePreset(int id, const QString &name, const QString &label);
    // Go to an absolute zoom level, 1.0 = widest, up to the lens's maximum (40x on the CJ3000).
    virtual bool supportsZoomLevel() const { return false; }
    virtual void setZoomLevel(double times, const QString &label);
    virtual void queryPresets() {}                 // answers with presetsListed
    virtual void queryAux() {}                     // answers with auxState per readable switch
    // [{ id, label, supported, readable }] in display order
    virtual QVariantList auxItems() const { return {}; }

    const CameraProfile &profile() const { return m_profile; }
    void reloadProfile();

    // Can the Settings page behind this (ISAPI-style) path be read and written by this
    // driver? ISAPI and Demo: yes. ONVIF: only what the standard services cover. The pages
    // never send a request for a path the driver says it cannot serve.
    virtual bool supportsSettingsEndpoint(const QString &path) const { Q_UNUSED(path); return true; }
    // What the pinned HTTPS certificate is filed under: the camera's id, or ip:port before it is saved.
    QString pinKey() const;

    const CameraConfig &config() const { return m_cfg; }

signals:
    void commandDone(const QString &label, bool ok, int ms, const QString &error);
    void statusUpdated(double azimuthDeg, double elevationDeg, double zoomX);
    void pingResult(bool ok, int ms);
    void streamUrlsChanged();
    void presetsListed(bool ok, const QVariantList &presets, const QString &error);   // [{ id, name }]
    void presetsDirty();                           // the camera's preset list changed
    void auxState(const QString &name, bool on);
    void profileChanged();
    // HTTPS with a certificate that is not pinned (changed = a different one is pinned).
    void certificateUntrusted(const QString &fingerprint, bool changed);

protected:
    QString thermalOverride(bool mainStream) const;
    QVariantList profileAuxItems() const;
    void unsupported(const QString &label, const QString &what);

    // HTTP helper with Digest/Basic auth handled by Qt (QAuthenticator).
    void http(const QString &method, const QString &path, const QByteArray &body,
              const QByteArray &contentType, HttpCallback cb, int timeoutMs = 3000);
    void httpUrl(const QString &method, const QUrl &url, const QByteArray &body,
                 const QByteArray &contentType, HttpCallback cb, int timeoutMs = 3000);
    QString baseUrl() const;
    QString rtspWithCredentials(const QString &url) const;

    CameraConfig m_cfg;
    CameraProfile m_profile;
    QNetworkAccessManager *m_nam = nullptr;
    bool m_useHttps = false;    // the camera redirected HTTP to HTTPS: stay on HTTPS
    int m_httpsPort = 443;
};

// Small XML helpers shared by the drivers (Hikvision XML uses unprefixed tags).
namespace Xml {
QString tagText(const QString &xml, const QString &tag);
QString attr(const QString &xml, const QString &tag, const QString &attribute);
QString escape(const QString &s);
}
