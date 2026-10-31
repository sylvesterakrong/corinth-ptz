#include "IsapiDriver.h"

static const QByteArray kXml = QByteArrayLiteral("application/xml");

IsapiDriver::IsapiDriver(const CameraConfig &cfg, QObject *parent)
    : CameraDriver(cfg, parent)
{
}

void IsapiDriver::command(const QString &method, const QString &path, const QByteArray &body,
                          const QString &label)
{
    http(method, path, body, kXml, [this, label](bool ok, int status, const QByteArray &data, int ms) {
        QString err;
        if (!ok) {
            const QString sub = Xml::tagText(QString::fromUtf8(data), QStringLiteral("subStatusCode"));
            const QString st = Xml::tagText(QString::fromUtf8(data), QStringLiteral("statusString"));
            err = status ? QStringLiteral("HTTP %1 %2").arg(status).arg(sub.isEmpty() ? st : sub)
                         : QString::fromUtf8(data).left(120);
        }
        emit commandDone(label, ok, ms, err);
    });
}

void IsapiDriver::continuousMove(int pan, int tilt, int zoom, const QString &label)
{
    const QByteArray body = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<PTZData><pan>%1</pan><tilt>%2</tilt><zoom>%3</zoom></PTZData>")
        .arg(pan).arg(tilt).arg(zoom).toUtf8();
    command(QStringLiteral("PUT"), QStringLiteral("/ISAPI/PTZCtrl/channels/1/continuous"), body, label);
}

void IsapiDriver::gotoPreset(int id, const QString &label)
{
    command(QStringLiteral("PUT"), QStringLiteral("/ISAPI/PTZCtrl/channels/1/presets/%1/goto").arg(id), {}, label);
}

void IsapiDriver::setPreset(int id, const QString &name, const QString &label)
{
    const QByteArray body = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<PTZPreset><id>%1</id><presetName>%2</presetName></PTZPreset>")
        .arg(id).arg(Xml::escape(name)).toUtf8();
    command(QStringLiteral("PUT"), QStringLiteral("/ISAPI/PTZCtrl/channels/1/presets/%1").arg(id), body, label);
}

void IsapiDriver::gotoHome(const QString &label)
{
    command(QStringLiteral("PUT"), QStringLiteral("/ISAPI/PTZCtrl/channels/1/homeposition/goto"), {}, label);
}

void IsapiDriver::setHome(const QString &label)
{
    command(QStringLiteral("PUT"), QStringLiteral("/ISAPI/PTZCtrl/channels/1/homeposition"), {}, label);
}

void IsapiDriver::focus(int dir, const QString &label)
{
    const QByteArray body = QStringLiteral("<FocusData><focus>%1</focus></FocusData>").arg(dir * 60).toUtf8();
    command(QStringLiteral("PUT"), QStringLiteral("/ISAPI/System/Video/inputs/channels/1/focus"), body, label);
}

void IsapiDriver::iris(int dir, const QString &label)
{
    const QByteArray body = QStringLiteral("<IrisData><iris>%1</iris></IrisData>").arg(dir * 60).toUtf8();
    command(QStringLiteral("PUT"), QStringLiteral("/ISAPI/System/Video/inputs/channels/1/iris"), body, label);
}

void IsapiDriver::onePushFocus(const QString &label)
{
    // Hikvision spells this endpoint "foucs" in its own firmware.
    command(QStringLiteral("PUT"), QStringLiteral("/ISAPI/PTZCtrl/channels/1/onepushfoucs/start"), {}, label);
}

void IsapiDriver::setAux(const QString &name, bool on, const QString &label)
{
    // Standard ISAPI aux control covers the wiper and the supplementary light.
    // Stabilisation, heater and heading lock are vendor-specific on gimbal
    // cameras: capture them from the camera's web page and add them here.
    QString type;
    int id = 1;
    if (name == QLatin1String("wiper")) { type = QStringLiteral("WIPER"); id = 1; }
    else if (name == QLatin1String("ir")) { type = QStringLiteral("LIGHT"); id = 1; }

    if (type.isEmpty()) {
        emit commandDone(label, false, 0,
                         QStringLiteral("Not in standard ISAPI. Map it from a DevTools capture."));
        return;
    }
    const QByteArray body = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<PTZAux><id>%1</id><type>%2</type><status>%3</status></PTZAux>")
        .arg(id).arg(type, on ? QStringLiteral("on") : QStringLiteral("off")).toUtf8();
    command(QStringLiteral("PUT"), QStringLiteral("/ISAPI/PTZCtrl/channels/1/auxcontrols/%1").arg(id), body, label);
}

void IsapiDriver::queryStatus()
{
    if (m_statusInFlight)
        return;
    m_statusInFlight = true;
    http(QStringLiteral("GET"), QStringLiteral("/ISAPI/PTZCtrl/channels/1/status"), {}, {},
         [this](bool ok, int, const QByteArray &data, int) {
             m_statusInFlight = false;
             if (!ok) return;
             const QString xml = QString::fromUtf8(data);
             // ISAPI reports tenths: azimuth 0..3600, elevation -900..2700, zoom x10.
             bool okA = false, okE = false, okZ = false;
             const double az = Xml::tagText(xml, QStringLiteral("azimuth")).toDouble(&okA) / 10.0;
             const double el = Xml::tagText(xml, QStringLiteral("elevation")).toDouble(&okE) / 10.0;
             const double zm = Xml::tagText(xml, QStringLiteral("absoluteZoom")).toDouble(&okZ) / 10.0;
             if (okA && okE)
                 emit statusUpdated(az, el, okZ ? zm : 1.0);
         }, 2000);
}

void IsapiDriver::ping()
{
    http(QStringLiteral("GET"), QStringLiteral("/ISAPI/System/deviceInfo"), {}, {},
         [this](bool ok, int status, const QByteArray &, int ms) {
             // A 401 still proves the link is up, but means the login is wrong.
             emit pingResult(ok || status == 401, ms);
         }, 2000);
}

QString IsapiDriver::streamUrl(bool mainStream, int channel) const
{
    const QString custom = mainStream ? m_cfg.mainUrl : m_cfg.subUrl;
    if (!custom.isEmpty() && channel == 1)
        return rtspWithCredentials(custom);
    const int ch = qBound(1, channel, 4) * 100 + (mainStream ? 1 : 2);
    return rtspWithCredentials(QStringLiteral("rtsp://%1:%2/Streaming/Channels/%3")
                                   .arg(m_cfg.ip).arg(m_cfg.rtspPort).arg(ch));
}

void IsapiDriver::request(const QString &method, const QString &path, const QByteArray &body,
                          HttpCallback cb)
{
    http(method, path, body, body.isEmpty() ? QByteArray() : kXml, std::move(cb), 5000);
}
