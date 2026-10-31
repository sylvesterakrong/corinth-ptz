#include "DemoDriver.h"

#include <QTimer>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QtMath>

DemoDriver::DemoDriver(const CameraConfig &cfg, QObject *parent)
    : CameraDriver(cfg, parent), m_timer(new QTimer(this))
{
    m_presets.insert(1, {0, -2});
    m_presets.insert(2, {270, -2});
    m_presets.insert(3, {90, -2});
    m_presets.insert(4, {180, -2});
    m_presets.insert(5, {0, 0});

    m_docs.insert(QStringLiteral("/ISAPI/System/deviceInfo"),
                  "<DeviceInfo><deviceName>Demo-PTZ</deviceName><model>DEMO-PTZ (simulated)</model>"
                  "<serialNumber>DEMO0001</serialNumber><firmwareVersion>V0.0 demo</firmwareVersion></DeviceInfo>");
    m_docs.insert(QStringLiteral("/ISAPI/System/time"),
                  "<Time><timeMode>NTP</timeMode><timeZone>CST+0:00:00</timeZone></Time>");
    m_docs.insert(QStringLiteral("/ISAPI/System/time/ntpServers/1"),
                  "<NTPServer><id>1</id><addressingFormatType>ipaddress</addressingFormatType>"
                  "<ipAddress>192.168.1.10</ipAddress><synchronizeInterval>60</synchronizeInterval></NTPServer>");
    m_docs.insert(QStringLiteral("/ISAPI/System/Network/interfaces/1/ipAddress"),
                  "<IPAddress><ipVersion>v4</ipVersion><addressingType>static</addressingType>"
                  "<ipAddress>192.168.1.64</ipAddress><subnetMask>255.255.255.0</subnetMask></IPAddress>");
    m_docs.insert(QStringLiteral("/ISAPI/Streaming/channels/101"),
                  "<StreamingChannel><id>101</id><Video><videoCodecType>H.265</videoCodecType>"
                  "<videoResolutionWidth>1920</videoResolutionWidth><videoResolutionHeight>1080</videoResolutionHeight>"
                  "<videoQualityControlType>VBR</videoQualityControlType><vbrUpperCap>4096</vbrUpperCap>"
                  "<maxFrameRate>2500</maxFrameRate><GovLength>50</GovLength></Video></StreamingChannel>");
    m_docs.insert(QStringLiteral("/ISAPI/Streaming/channels/102"),
                  "<StreamingChannel><id>102</id><Video><videoCodecType>H.264</videoCodecType>"
                  "<videoResolutionWidth>640</videoResolutionWidth><videoResolutionHeight>360</videoResolutionHeight>"
                  "<videoQualityControlType>VBR</videoQualityControlType><vbrUpperCap>768</vbrUpperCap>"
                  "<maxFrameRate>1500</maxFrameRate><GovLength>30</GovLength></Video></StreamingChannel>");
    m_docs.insert(QStringLiteral("/ISAPI/Image/channels/1/color"),
                  "<Color><brightnessLevel>50</brightnessLevel><contrastLevel>50</contrastLevel>"
                  "<saturationLevel>50</saturationLevel></Color>");
    m_docs.insert(QStringLiteral("/ISAPI/Image/channels/1/sharpness"),
                  "<Sharpness><SharpnessLevel>50</SharpnessLevel></Sharpness>");
    m_docs.insert(QStringLiteral("/ISAPI/Image/channels/1/IrcutFilter"),
                  "<IrcutFilter><IrcutFilterType>auto</IrcutFilterType></IrcutFilter>");
    m_docs.insert(QStringLiteral("/ISAPI/Image/channels/1/WDR"),
                  "<WDR><mode>close</mode><WDRLevel>50</WDRLevel></WDR>");
    m_docs.insert(QStringLiteral("/ISAPI/Image/channels/1/whiteBalance"),
                  "<WhiteBalance><WhiteBalanceStyle>auto</WhiteBalanceStyle></WhiteBalance>");
    m_docs.insert(QStringLiteral("/ISAPI/ContentMgmt/Storage"),
                  "<storage><hddList><hdd><id>1</id><capacity>61056</capacity><freeSpace>50432</freeSpace>"
                  "<status>ok</status></hdd></hddList></storage>");

    connect(m_timer, &QTimer::timeout, this, &DemoDriver::tick);
    m_timer->start(50);
}

int DemoDriver::latency() const
{
    return 40 + int(QRandomGenerator::global()->bounded(100));
}

void DemoDriver::ack(const QString &label)
{
    const int ms = latency();
    QTimer::singleShot(ms, this, [this, label, ms]() { emit commandDone(label, true, ms, QString()); });
}

void DemoDriver::tick()
{
    // Speed 100 is roughly 60 deg/s, scaled down when zoomed in.
    const double dt = 0.05;
    const double scale = 60.0 / qMax(1.0, m_zoom * 0.5);
    if (m_hasTarget) {
        const double dAz = std::remainder(m_tAz - m_az, 360.0);
        const double dEl = m_tEl - m_el;
        const double step = 90.0 * dt;
        m_az += qBound(-step, dAz, step);
        m_el += qBound(-step, dEl, step);
        if (qAbs(dAz) < 0.2 && qAbs(dEl) < 0.2) { m_az = m_tAz; m_el = m_tEl; m_hasTarget = false; }
    } else {
        m_az += m_vPan / 100.0 * scale * dt;
        m_el += m_vTilt / 100.0 * scale * dt;
    }
    m_zoom = qBound(1.0, m_zoom + m_vZoom / 100.0 * 8.0 * dt, 30.0);
    m_az = std::fmod(m_az + 360.0, 360.0);
    m_el = qBound(-90.0, m_el, 90.0);
}

void DemoDriver::continuousMove(int pan, int tilt, int zoom, const QString &label)
{
    m_hasTarget = false;
    m_vPan = pan; m_vTilt = tilt; m_vZoom = zoom;
    ack(label);
}

void DemoDriver::gotoPreset(int id, const QString &label)
{
    if (!m_presets.contains(id)) {
        QTimer::singleShot(latency(), this, [this, label]() { emit commandDone(label, false, 60, QStringLiteral("Preset not set")); });
        return;
    }
    m_vPan = m_vTilt = m_vZoom = 0;
    m_tAz = m_presets[id].first; m_tEl = m_presets[id].second; m_hasTarget = true;
    ack(label);
}

void DemoDriver::setPreset(int id, const QString &, const QString &label)
{
    m_presets.insert(id, {m_az, m_el});
    ack(label);
}

void DemoDriver::gotoHome(const QString &label)
{
    m_vPan = m_vTilt = m_vZoom = 0;
    m_tAz = m_home.first; m_tEl = m_home.second; m_hasTarget = true;
    ack(label);
}

void DemoDriver::setHome(const QString &label) { m_home = {m_az, m_el}; ack(label); }
void DemoDriver::focus(int, const QString &label) { ack(label); }
void DemoDriver::iris(int, const QString &label) { ack(label); }
void DemoDriver::onePushFocus(const QString &label) { ack(label); }
void DemoDriver::setAux(const QString &, bool, const QString &label) { ack(label); }

void DemoDriver::queryStatus()
{
    emit statusUpdated(m_az, m_el, m_zoom);
}

void DemoDriver::ping()
{
    const int ms = latency();
    QTimer::singleShot(ms, this, [this, ms]() { emit pingResult(true, ms); });
}

QString DemoDriver::streamUrl(bool mainStream, int channel) const
{
    // Demo cameras can still point at a real RTSP source for video testing.
    const QString custom = mainStream ? m_cfg.mainUrl : m_cfg.subUrl;
    Q_UNUSED(channel);
    return custom.isEmpty() ? (mainStream ? QString() : m_cfg.mainUrl) : custom;
}

void DemoDriver::request(const QString &method, const QString &path, const QByteArray &body,
                         HttpCallback cb)
{
    const QString key = path.section(QLatin1Char('?'), 0, 0);
    const int ms = latency();
    QByteArray reply;
    bool ok = true;
    int status = 200;
    if (method.compare(QLatin1String("GET"), Qt::CaseInsensitive) == 0) {
        if (m_docs.contains(key)) reply = m_docs.value(key);
        else { ok = false; status = 404; reply = "Not available in the demo camera"; }
    } else {
        if (m_docs.contains(key) && !body.isEmpty())
            m_docs.insert(key, body);
        reply = "<ResponseStatus><statusCode>1</statusCode><statusString>OK</statusString></ResponseStatus>";
    }
    QTimer::singleShot(ms, this, [cb, ok, status, reply, ms]() { if (cb) cb(ok, status, reply, ms); });
}

// ---------------------------------------------------------------------------

RtspOnlyDriver::RtspOnlyDriver(const CameraConfig &cfg, QObject *parent)
    : CameraDriver(cfg, parent)
{
}

void RtspOnlyDriver::ping()
{
    // Measure a TCP connect to the RTSP port as the link probe.
    auto *sock = new QTcpSocket(this);
    auto *t = new QElapsedTimer();
    t->start();
    auto finish = [this, sock, t](bool ok) {
        if (sock->property("corinthDone").toBool())
            return;
        sock->setProperty("corinthDone", true);
        const int ms = int(t->elapsed());
        delete t;
        sock->abort();
        sock->deleteLater();
        emit pingResult(ok, ms);
    };
    connect(sock, &QTcpSocket::connected, this, [finish]() { finish(true); });
    connect(sock, &QTcpSocket::errorOccurred, this, [finish]() { finish(false); });
    sock->connectToHost(m_cfg.ip, quint16(m_cfg.rtspPort));
}

QString RtspOnlyDriver::streamUrl(bool mainStream, int) const
{
    const QString u = mainStream ? m_cfg.mainUrl : (m_cfg.subUrl.isEmpty() ? m_cfg.mainUrl : m_cfg.subUrl);
    return rtspWithCredentials(u);
}
