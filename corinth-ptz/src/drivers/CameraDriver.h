#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QVariantMap>
#include <functional>

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
    bool shown = true;

    static CameraConfig fromMap(const QVariantMap &m);
    QVariantMap toMap(bool includePassword) const;
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

    const CameraConfig &config() const { return m_cfg; }

signals:
    void commandDone(const QString &label, bool ok, int ms, const QString &error);
    void statusUpdated(double azimuthDeg, double elevationDeg, double zoomX);
    void pingResult(bool ok, int ms);
    void streamUrlsChanged();

protected:
    // HTTP helper with Digest/Basic auth handled by Qt (QAuthenticator).
    void http(const QString &method, const QString &path, const QByteArray &body,
              const QByteArray &contentType, HttpCallback cb, int timeoutMs = 3000);
    void httpUrl(const QString &method, const QUrl &url, const QByteArray &body,
                 const QByteArray &contentType, HttpCallback cb, int timeoutMs = 3000);
    QString baseUrl() const;
    QString rtspWithCredentials(const QString &url) const;

    CameraConfig m_cfg;
    QNetworkAccessManager *m_nam = nullptr;
};

// Small XML helpers shared by the drivers (Hikvision XML uses unprefixed tags).
namespace Xml {
QString tagText(const QString &xml, const QString &tag);
QString attr(const QString &xml, const QString &tag, const QString &attribute);
QString escape(const QString &s);
}
