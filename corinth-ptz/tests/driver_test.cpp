// ISAPI and ONVIF drivers against an in-process fake camera: what is sent on the
// wire (paths, bodies), what is parsed back (presets, aux state, status, image
// settings), and the camera-profile override files. This checks the drivers
// against the protocol as the code understands it, not against a real camera.
#include "drivers/CameraDriver.h"
#include "drivers/CameraProfile.h"
#include "drivers/IsapiDriver.h"
#include "drivers/OnvifDriver.h"
#include "drivers/TlsPin.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <functional>

static int g_fail = 0;

static void check(bool ok, const QString &what)
{
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << what;
    if (!ok) ++g_fail;
}

static bool waitFor(const std::function<bool()> &cond, int timeoutMs = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

struct Req
{
    QString method, path;
    QByteArray body;
    QString line() const { return method + QLatin1Char(' ') + path; }
};

class FakeCamera : public QObject
{
public:
    using Handler = std::function<void(const Req &, int &status, QByteArray &reply)>;
    FakeCamera()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *s = m_server.nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, this, [this, s]() { onData(s); });
        });
        m_server.listen(QHostAddress::LocalHost, 0);
    }
    quint16 port() const { return m_server.serverPort(); }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(port()); }
    Handler handler;
    QList<Req> log;

    // The last request whose "METHOD path" line matches.
    const Req *find(const QString &line, int from = 0) const
    {
        for (int i = int(log.size()) - 1; i >= from; --i)
            if (log.at(i).line() == line) return &log.at(i);
        return nullptr;
    }
    bool saw(const QString &line) const { return find(line) != nullptr; }
    // Position of the first request whose line contains `text` and whose body contains `body`.
    int indexOf(const QString &lineHas, const QString &bodyHas = QString()) const
    {
        for (int i = 0; i < log.size(); ++i)
            if (log.at(i).line().contains(lineHas) && QString::fromUtf8(log.at(i).body).contains(bodyHas)) return i;
        return -1;
    }

private:
    void onData(QTcpSocket *s)
    {
        QByteArray &buf = m_buf[s];
        buf += s->readAll();
        const int end = buf.indexOf("\r\n\r\n");
        if (end < 0) return;
        const QByteArray head = buf.left(end);
        int len = 0;
        const QRegularExpressionMatch lm = QRegularExpression(QStringLiteral("content-length:\\s*(\\d+)"),
                                                              QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(head));
        if (lm.hasMatch()) len = lm.captured(1).toInt();
        if (buf.size() < end + 4 + len) return;
        const QList<QByteArray> first = head.left(head.indexOf("\r\n")).split(' ');
        Req r{QString::fromLatin1(first.value(0)), QString::fromLatin1(first.value(1)), buf.mid(end + 4, len)};
        m_buf.remove(s);
        log << r;
        int status = 404;
        QByteArray reply = "not found";
        if (handler) handler(r, status, reply);
        const QByteArray text = status == 200 ? "OK" : "Error";
        s->write("HTTP/1.1 " + QByteArray::number(status) + " " + text + "\r\nContent-Type: application/xml\r\nContent-Length: "
                 + QByteArray::number(reply.size()) + "\r\nConnection: close\r\n\r\n" + reply);
        s->disconnectFromHost();
    }
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buf;
};

struct Done { QString label; bool ok; QString error; };

static const char *kOk = "<ResponseStatus><statusCode>1</statusCode><statusString>OK</statusString></ResponseStatus>";

// ---- ISAPI ---------------------------------------------------------------------

static void testIsapi()
{
    FakeCamera cam;
    cam.handler = [](const Req &r, int &status, QByteArray &reply) {
        const QString l = r.line();
        status = 200;
        reply = kOk;
        if (l == QLatin1String("GET /ISAPI/PTZCtrl/channels/1/presets"))
            reply = "<PTZPresetList><PTZPreset><id>1</id><enabled>true</enabled><presetName>Bow</presetName></PTZPreset>"
                    "<PTZPreset><id>2</id><enabled>true</enabled><presetName>Old name</presetName></PTZPreset>"
                    "<PTZPreset><id>3</id><enabled>false</enabled><presetName>unused</presetName></PTZPreset></PTZPresetList>";
        else if (l == QLatin1String("GET /ISAPI/PTZCtrl/channels/1/presets/2"))
            reply = "<PTZPreset><id>2</id><enabled>true</enabled><presetName>Old name</presetName>"
                    "<AbsoluteHigh><elevation>100</elevation><azimuth>450</azimuth></AbsoluteHigh></PTZPreset>";
        else if (l == QLatin1String("GET /ISAPI/PTZCtrl/channels/1/auxcontrols"))
            reply = "<PTZAuxList><PTZAux><id>1</id><type>WIPER</type><status>on</status></PTZAux>"
                    "<PTZAux><id>2</id><type>LIGHT</type><status>off</status></PTZAux></PTZAuxList>";
        else if (l == QLatin1String("GET /ISAPI/PTZCtrl/channels/1/status"))
            reply = "<PTZStatus><AbsoluteHigh><elevation>150</elevation><azimuth>900</azimuth><absoluteZoom>20</absoluteZoom></AbsoluteHigh></PTZStatus>";
        else if (l.startsWith(QLatin1String("GET /ISAPI/System/status")))
            reply = "<DeviceStatus><deviceUpTime>3600</deviceUpTime></DeviceStatus>";
        else if (l == QLatin1String("PUT /nope"))
            { status = 403; reply = "<ResponseStatus><statusCode>4</statusCode><subStatusCode>notSupport</subStatusCode></ResponseStatus>"; }
    };

    CameraConfig cfg;
    cfg.id = QStringLiteral("test");
    cfg.proto = QStringLiteral("ISAPI");
    cfg.ip = QStringLiteral("127.0.0.1");
    cfg.httpPort = cam.port();
    cfg.thermalMainUrl = QStringLiteral("rtsp://127.0.0.1:8554/thermal");
    IsapiDriver d(cfg);

    QList<Done> done;
    QObject::connect(&d, &CameraDriver::commandDone, [&](const QString &label, bool ok, int, const QString &err) { done.append({label, ok, err}); });
    auto result = [&](const QString &label) -> const Done * {
        waitFor([&] { for (const Done &x : done) if (x.label == label) return true; return false; });
        for (const Done &x : done) if (x.label == label) return &x;
        return nullptr;
    };

    // move
    d.continuousMove(10, -20, 5, QStringLiteral("move"));
    const Done *mv = result(QStringLiteral("move"));
    const Req *rq = cam.find(QStringLiteral("PUT /ISAPI/PTZCtrl/channels/1/continuous"));
    check(mv && mv->ok && rq && rq->body.contains("<pan>10</pan><tilt>-20</tilt><zoom>5</zoom>"),
          QStringLiteral("continuous move goes to the profile's path with the speeds filled in"));

    // preset list: disabled slots are dropped
    bool listed = false, listedOk = false;
    QVariantList presets;
    QObject::connect(&d, &CameraDriver::presetsListed, [&](bool ok, const QVariantList &l, const QString &) { listed = true; listedOk = ok; presets = l; });
    d.queryPresets();
    waitFor([&] { return listed; });
    check(listedOk && presets.size() == 2 && presets.at(1).toMap().value(QStringLiteral("name")).toString() == QLatin1String("Old name"),
          QStringLiteral("preset list parsed, disabled slot dropped (%1 presets)").arg(presets.size()));

    // rename: read the preset, change only the name, write it back
    bool dirty = false;
    QObject::connect(&d, &CameraDriver::presetsDirty, [&] { dirty = true; });
    check(d.supportsPresetRename(), QStringLiteral("ISAPI profile supports renaming presets"));
    d.renamePreset(2, QStringLiteral("Bow & Stern"), QStringLiteral("rename"));
    const Done *rn = result(QStringLiteral("rename"));
    const int iGet = cam.indexOf(QStringLiteral("GET /ISAPI/PTZCtrl/channels/1/presets/2"));
    const int iPut = cam.indexOf(QStringLiteral("PUT /ISAPI/PTZCtrl/channels/1/presets/2"));
    const Req *put = iPut >= 0 ? &cam.log.at(iPut) : nullptr;
    check(rn && rn->ok && iGet >= 0 && iPut > iGet && put
              && put->body.contains("<presetName>Bow &amp; Stern</presetName>")
              && put->body.contains("<AbsoluteHigh><elevation>100</elevation><azimuth>450</azimuth></AbsoluteHigh>"),
          QStringLiteral("rename keeps the camera's own XML (position fields) and escapes the name"));
    check(waitFor([&] { return dirty; }), QStringLiteral("a rename tells the app the preset list changed"));

    // aux: the wiper is mapped, stabilisation is not
    d.setAux(QStringLiteral("wiper"), true, QStringLiteral("wiper on"));
    const Done *w = result(QStringLiteral("wiper on"));
    const Req *wr = cam.find(QStringLiteral("PUT /ISAPI/PTZCtrl/channels/1/auxcontrols/1"));
    check(w && w->ok && wr && wr->body.contains("<type>WIPER</type><status>on</status>"), QStringLiteral("wiper command filled from the profile"));
    d.setAux(QStringLiteral("stab"), true, QStringLiteral("stab on"));
    const Done *st = result(QStringLiteral("stab on"));
    check(st && !st->ok && st->error.contains(QLatin1String("aux.stab.set")),
          QStringLiteral("an unmapped switch fails with a pointer to the profile key"));

    // aux state read-back picks the right block out of the list
    QHash<QString, bool> auxStates;
    QObject::connect(&d, &CameraDriver::auxState, [&](const QString &n, bool on) { auxStates[n] = on; });
    d.queryAux();
    waitFor([&] { return auxStates.contains(QStringLiteral("wiper")) && auxStates.contains(QStringLiteral("ir")); });
    check(auxStates.value(QStringLiteral("wiper")) == true && auxStates.value(QStringLiteral("ir")) == false,
          QStringLiteral("aux state read back per type (wiper on, IR off)"));

    // status
    double az = -1, el = -1, zm = -1;
    QObject::connect(&d, &CameraDriver::statusUpdated, [&](double a, double e, double z) { az = a; el = e; zm = z; });
    d.queryStatus();
    waitFor([&] { return az >= 0; });
    check(qFuzzyCompare(az, 90.0) && qFuzzyCompare(el, 15.0) && qFuzzyCompare(zm, 2.0),
          QStringLiteral("status parsed from tenths of a degree (az %1 el %2 zoom %3)").arg(az).arg(el).arg(zm));

    // zoom level: absolute, in tenths, keeping the pan and tilt the camera last reported
    check(d.supportsZoomLevel(), QStringLiteral("ISAPI profile can set a zoom level"));
    d.setZoomLevel(12.5, QStringLiteral("zoomlevel"));
    result(QStringLiteral("zoomlevel"));
    const Req *zl = cam.find(QStringLiteral("PUT /ISAPI/PTZCtrl/channels/1/absolute"));
    check(zl && zl->body.contains("<absoluteZoom>125</absoluteZoom>") && zl->body.contains("<azimuth>900</azimuth>") && zl->body.contains("<elevation>150</elevation>"),
          QStringLiteral("zoom level 12.5x is sent as 125 tenths with the camera's own pan (90.0) and tilt (15.0)"));

    // click to point, box zoom
    d.pointAt(0.5, 0.25, QStringLiteral("point"));
    result(QStringLiteral("point"));
    const Req *pt = cam.find(QStringLiteral("PUT /ISAPI/PTZCtrl/channels/1/position3D"));
    check(pt && pt->body.contains("<StartPoint><positionX>128</positionX><positionY>64</positionY></StartPoint>")
              && pt->body.contains("<EndPoint><positionX>128</positionX><positionY>64</positionY></EndPoint>"),
          QStringLiteral("click to point sends start = end in the 0..255 grid"));
    d.boxZoom(0.1, 0.1, 0.6, 0.5, QStringLiteral("box"));
    result(QStringLiteral("box"));
    const Req *bz = cam.find(QStringLiteral("PUT /ISAPI/PTZCtrl/channels/1/position3D"));
    check(bz && bz->body.contains("<positionX>26</positionX><positionY>26</positionY></StartPoint>")
              && bz->body.contains("<positionX>153</positionX><positionY>128</positionY></EndPoint>"),
          QStringLiteral("box zoom sends the two corners as dragged"));

    // camera error text
    d.request(QStringLiteral("PUT"), QStringLiteral("/nope"), "x", [](bool, int, const QByteArray &, int) {});
    check(waitFor([&] { return cam.saw(QStringLiteral("PUT /nope")); }), QStringLiteral("raw requests reach the camera"));

    // streams: default numbering and thermal override
    check(d.streamUrl(true, 2).startsWith(QLatin1String("rtsp://127.0.0.1:8554/thermal")), QStringLiteral("thermal main stream uses the override URL"));
    check(d.streamUrl(false, 2).contains(QLatin1String("/Streaming/Channels/202")), QStringLiteral("thermal sub stream falls back to channel 202"));
    check(d.streamUrl(true, 1).contains(QLatin1String("/Streaming/Channels/101")), QStringLiteral("visible main stream is channel 101"));
    check(d.hasChannel2(), QStringLiteral("ISAPI camera offers the second channel"));

    // ---- profile override files -------------------------------------------------
    const QString dir = CameraProfile::userFolder();
    QDir().mkpath(dir);
    {
        QFile f(dir + QStringLiteral("/test.json"));
        f.open(QIODevice::WriteOnly);
        f.write("{\"aux\":{\"stab\":{\"label\":\"Stabilisation\",\"set\":{\"method\":\"PUT\",\"path\":\"/vendor/stab\","
                "\"body\":\"<S>{state}</S>\"},\"on\":\"enable\",\"off\":\"disable\"}},\"ptz\":{\"point\":null},"
                "\"channels\":1}");
    }
    d.reloadProfile();
    d.setAux(QStringLiteral("stab"), true, QStringLiteral("stab on 2"));
    const Done *st2 = result(QStringLiteral("stab on 2"));
    const Req *sr = cam.find(QStringLiteral("PUT /vendor/stab"));
    check(st2 && st2->ok && sr && sr->body == "<S>enable</S>",
          QStringLiteral("a user profile file maps a captured endpoint with no code change"));
    check(!d.supportsPoint(), QStringLiteral("null in a user profile removes a built-in entry"));
    check(d.profile().has(QStringLiteral("aux.wiper.set")), QStringLiteral("untouched built-in entries survive the override"));
    check(d.profile().sources().size() == 2 && d.profile().error().isEmpty(), QStringLiteral("profile reports both layers, no errors"));
    check(d.auxItems().size() == 5, QStringLiteral("aux list comes from the profile (%1 items)").arg(d.auxItems().size()));

    {
        QFile f(dir + QStringLiteral("/test.json"));
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write("{ this is not json");
    }
    d.reloadProfile();
    check(!d.profile().error().isEmpty() && d.profile().has(QStringLiteral("ptz.point")),
          QStringLiteral("a broken user file is reported and ignored; the built-in profile still works"));
    QFile::remove(dir + QStringLiteral("/test.json"));
}

// ---- ONVIF ---------------------------------------------------------------------

static void testOnvif()
{
    FakeCamera cam;
    const QString base = cam.url();
    cam.handler = [base](const Req &r, int &status, QByteArray &reply) {
        status = 200;
        const QString b = QString::fromUtf8(r.body);
        auto has = [&b](const char *s) { return b.contains(QLatin1String(s)); };
        reply = "<s:Envelope><s:Body><ok/></s:Body></s:Envelope>";
        if (has("<GetCapabilities"))
            reply = QStringLiteral("<s:Envelope><s:Body><tds:Capabilities>"
                                   "<tt:Media><tt:XAddr>%1/onvif/media</tt:XAddr></tt:Media>"
                                   "<tt:PTZ><tt:XAddr>%1/onvif/ptz</tt:XAddr></tt:PTZ>"
                                   "<tt:Imaging><tt:XAddr>%1/onvif/imaging</tt:XAddr></tt:Imaging>"
                                   "</tds:Capabilities></s:Body></s:Envelope>").arg(base).toUtf8();
        else if (has("<GetProfiles"))
            reply = "<s:Envelope><s:Body><trt:GetProfilesResponse>"
                    "<trt:Profiles fixed=\"true\" token=\"Profile_1\"><tt:Name>main</tt:Name>"
                    "<tt:VideoSourceConfiguration token=\"VSC_1\"><tt:SourceToken>VideoSource_1</tt:SourceToken></tt:VideoSourceConfiguration>"
                    "<tt:VideoEncoderConfiguration token=\"VEC_1\"><tt:Encoding>H264</tt:Encoding></tt:VideoEncoderConfiguration></trt:Profiles>"
                    "<trt:Profiles fixed=\"true\" token=\"Profile_2\"><tt:Name>sub</tt:Name>"
                    "<tt:VideoSourceConfiguration token=\"VSC_1\"><tt:SourceToken>VideoSource_1</tt:SourceToken></tt:VideoSourceConfiguration>"
                    "<tt:VideoEncoderConfiguration token=\"VEC_2\"><tt:Encoding>H264</tt:Encoding></tt:VideoEncoderConfiguration></trt:Profiles>"
                    "</trt:GetProfilesResponse></s:Body></s:Envelope>";
        else if (has("<GetStreamUri"))
            reply = has("Profile_1") ? "<r><trt:MediaUri><tt:Uri>rtsp://127.0.0.1:554/main</tt:Uri></trt:MediaUri></r>"
                                      : "<r><trt:MediaUri><tt:Uri>rtsp://127.0.0.1:554/sub</tt:Uri></trt:MediaUri></r>";
        else if (has("<GetPresets"))
            reply = "<r><tptz:Preset token=\"1\"><tt:Name>Bow</tt:Name></tptz:Preset>"
                    "<tptz:Preset token=\"abc\"><tt:Name>Stern</tt:Name></tptz:Preset></r>";
        else if (has("<GetImagingSettings"))
            reply = "<r><timg:ImagingSettings><tt:Brightness>55.4</tt:Brightness><tt:ColorSaturation>40</tt:ColorSaturation>"
                    "<tt:Contrast>60</tt:Contrast><tt:IrCutFilter>AUTO</tt:IrCutFilter><tt:Sharpness>30</tt:Sharpness>"
                    "<tt:WideDynamicRange><tt:Mode>OFF</tt:Mode><tt:Level>50</tt:Level></tt:WideDynamicRange>"
                    "<tt:BacklightCompensation><tt:Mode>ON</tt:Mode></tt:BacklightCompensation>"
                    "<tt:Exposure><tt:Mode>AUTO</tt:Mode></tt:Exposure><tt:Focus><tt:AutoFocusMode>MANUAL</tt:AutoFocusMode></tt:Focus>"
                    "<tt:WhiteBalance><tt:Mode>AUTO</tt:Mode></tt:WhiteBalance></timg:ImagingSettings></r>";
        else if (has("<GetVideoEncoderConfigurationOptions"))
            reply = "<r><trt:Options><tt:H264><tt:ResolutionsAvailable><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:ResolutionsAvailable>"
                    "<tt:ResolutionsAvailable><tt:Width>1280</tt:Width><tt:Height>720</tt:Height></tt:ResolutionsAvailable>"
                    "<tt:GovLengthRange><tt:Min>1</tt:Min><tt:Max>150</tt:Max></tt:GovLengthRange>"
                    "<tt:FrameRateRange><tt:Min>1</tt:Min><tt:Max>25</tt:Max></tt:FrameRateRange></tt:H264>"
                    "<tt:H265><tt:ResolutionsAvailable><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:ResolutionsAvailable></tt:H265>"
                    "<tt:Extension><tt:H264><tt:BitrateRange><tt:Min>64</tt:Min><tt:Max>8192</tt:Max></tt:BitrateRange></tt:H264></tt:Extension></trt:Options></r>";
        else if (has("<GetOptions"))
            reply = "<r><timg:ImagingOptions><tt:BacklightCompensation><tt:Mode>OFF</tt:Mode><tt:Mode>ON</tt:Mode></tt:BacklightCompensation>"
                    "<tt:Brightness><tt:Min>0</tt:Min><tt:Max>100</tt:Max></tt:Brightness><tt:Contrast><tt:Min>0</tt:Min><tt:Max>100</tt:Max></tt:Contrast>"
                    "<tt:Exposure><tt:Mode>AUTO</tt:Mode><tt:Mode>MANUAL</tt:Mode></tt:Exposure>"
                    "<tt:Focus><tt:AutoFocusModes>AUTO</tt:AutoFocusModes><tt:AutoFocusModes>MANUAL</tt:AutoFocusModes></tt:Focus>"
                    "<tt:IrCutFilterModes>ON</tt:IrCutFilterModes><tt:IrCutFilterModes>OFF</tt:IrCutFilterModes><tt:IrCutFilterModes>AUTO</tt:IrCutFilterModes>"
                    "</timg:ImagingOptions></r>";
        else if (has("<GetNTP"))
            reply = "<r><tds:NTPInformation><tt:FromDHCP>false</tt:FromDHCP><tt:NTPManual><tt:Type>IPv4</tt:Type><tt:IPv4Address>192.168.1.10</tt:IPv4Address></tt:NTPManual></tds:NTPInformation></r>";
        else if (has("<GetSystemDateAndTime"))
            reply = "<r><tds:SystemDateAndTime><tt:DateTimeType>NTP</tt:DateTimeType></tds:SystemDateAndTime></r>";
        else if (has("<GetStatus"))
            reply = "<r><tptz:PTZStatus><tt:Position><tt:PanTilt x=\"0.5\" y=\"0.25\"/><tt:Zoom x=\"0.5\"/></tt:Position></tptz:PTZStatus></r>";
        else if (has("<GetDeviceInformation"))
            reply = "<r><tds:Manufacturer>Acme</tds:Manufacturer><tds:Model>CJ3000</tds:Model>"
                    "<tds:FirmwareVersion>V5.7</tds:FirmwareVersion><tds:SerialNumber>SN123</tds:SerialNumber></r>";
        else if (has("<GetVideoEncoderConfiguration"))
            reply = "<r><trt:Configuration token=\"VEC_1\"><tt:Encoding>H264</tt:Encoding>"
                    "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
                    "<tt:RateControl><tt:FrameRateLimit>25</tt:FrameRateLimit><tt:BitrateLimit>4096</tt:BitrateLimit></tt:RateControl>"
                    "<tt:H264><tt:GovLength>50</tt:GovLength></tt:H264></trt:Configuration></r>";
    };

    CameraConfig cfg;
    cfg.id = QStringLiteral("otest");
    cfg.proto = QStringLiteral("ONVIF");
    cfg.ip = QStringLiteral("127.0.0.1");
    cfg.httpPort = cam.port();
    cfg.user = QStringLiteral("admin");
    cfg.pass = QStringLiteral("x");
    OnvifDriver d(cfg);

    check(waitFor([&] { return d.streamUrl(true, 1).contains(QLatin1String("/main")) && d.streamUrl(false, 1).contains(QLatin1String("/sub")); }),
          QStringLiteral("ONVIF reads profiles and both stream addresses"));

    QList<Done> done;
    QObject::connect(&d, &CameraDriver::commandDone, [&](const QString &label, bool ok, int, const QString &err) { done.append({label, ok, err}); });
    auto result = [&](const QString &label) -> const Done * {
        waitFor([&] { for (const Done &x : done) if (x.label == label) return true; return false; });
        for (const Done &x : done) if (x.label == label) return &x;
        return nullptr;
    };

    // presets: numeric tokens keep their number, others get the lowest free one
    QVariantList presets;
    bool listed = false;
    QObject::connect(&d, &CameraDriver::presetsListed, [&](bool, const QVariantList &l, const QString &) { listed = true; presets = l; });
    d.queryPresets();
    waitFor([&] { return listed; });
    check(presets.size() == 2 && presets.at(0).toMap().value(QStringLiteral("id")).toInt() == 1
              && presets.at(1).toMap().value(QStringLiteral("id")).toInt() == 2,
          QStringLiteral("ONVIF presets listed, non-numeric token mapped to id 2"));
    d.gotoPreset(2, QStringLiteral("goto2"));
    result(QStringLiteral("goto2"));
    check(cam.indexOf(QStringLiteral("/onvif/ptz"), QStringLiteral("<PresetToken>abc</PresetToken>")) >= 0,
          QStringLiteral("going to a preset uses the camera's own token"));

    // focus through the Imaging service
    d.focus(1, QStringLiteral("focus far"));
    result(QStringLiteral("focus far"));
    d.focus(0, QStringLiteral("focus stop"));
    result(QStringLiteral("focus stop"));
    check(cam.indexOf(QStringLiteral("/onvif/imaging"), QStringLiteral("<Speed>0.50</Speed>")) >= 0
              && cam.indexOf(QStringLiteral("/onvif/imaging"), QStringLiteral("<VideoSourceToken>VideoSource_1</VideoSourceToken>")) >= 0
              && cam.indexOf(QStringLiteral("/onvif/imaging"), QStringLiteral("<Stop")) >= 0,
          QStringLiteral("focus moves and stops through the Imaging service"));

    // zoom level and the zoom readout
    double zoomRead = -1;
    QObject::connect(&d, &CameraDriver::statusUpdated, [&](double, double, double z) { zoomRead = z; });
    d.queryStatus();
    const bool gotZoom = waitFor([&] { return zoomRead > 0; });
    check(gotZoom && qAbs(zoomRead - 20.5) < 0.01,
          QStringLiteral("ONVIF zoom position 0.5 reads as 20.5x (assumes a linear 1x..40x range) (%1)").arg(zoomRead));
    d.setZoomLevel(21.0, QStringLiteral("zoomlevel"));
    result(QStringLiteral("zoomlevel"));
    const int iAbs = cam.indexOf(QStringLiteral("/onvif/ptz"), QStringLiteral("<AbsoluteMove"));
    const QString absBody = iAbs >= 0 ? QString::fromUtf8(cam.log.at(iAbs).body) : QString();
    check(iAbs >= 0 && absBody.contains(QLatin1String("x=\"0.5128\"")) && absBody.contains(QLatin1String("x=\"0.5000\" y=\"0.2500\"")),
          QStringLiteral("zoom level 21x is an AbsoluteMove to zoom position 0.5128 that re-sends the current pan and tilt (0.5, 0.25)"));

    // click to point
    d.pointAt(0.75, 0.4, QStringLiteral("point"));
    result(QStringLiteral("point"));
    check(cam.indexOf(QStringLiteral("/onvif/ptz"), QStringLiteral("x=\"0.250\" y=\"0.100\"")) >= 0
              && cam.indexOf(QStringLiteral("/onvif/ptz"), QStringLiteral("TranslationSpaceFov")) >= 0,
          QStringLiteral("click to point is a field-of-view relative move"));

    // unsupported things say so
    d.iris(1, QStringLiteral("iris"));
    check(result(QStringLiteral("iris")) && !result(QStringLiteral("iris"))->ok, QStringLiteral("iris reports it is not available over ONVIF"));

    // Settings pages over ONVIF
    struct Reply { bool done = false, ok = false; int status = 0; QString body; };
    auto call = [&](const QString &method, const QString &path, const QByteArray &body = QByteArray()) {
        auto r = std::make_shared<Reply>();
        d.request(method, path, body, [r](bool ok, int status, const QByteArray &data, int) {
            r->done = true; r->ok = ok; r->status = status; r->body = QString::fromUtf8(data);
        });
        waitFor([&] { return r->done; });
        return *r;
    };
    Reply color = call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Image/channels/1/color"));
    check(color.ok && color.body.contains(QLatin1String("<brightnessLevel>55</brightnessLevel>"))
              && color.body.contains(QLatin1String("<contrastLevel>60</contrastLevel>"))
              && color.body.contains(QLatin1String("<saturationLevel>40</saturationLevel>")),
          QStringLiteral("image color read over ONVIF comes back as ISAPI-shaped XML (%1)").arg(color.body));
    check(call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Image/channels/1/IrcutFilter")).body.contains(QLatin1String("<IrcutFilterType>auto</IrcutFilterType>"))
              && call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Image/channels/1/WDR")).body.contains(QLatin1String("<mode>close</mode>"))
              && call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Image/channels/1/whiteBalance")).body.contains(QLatin1String("<WhiteBalanceStyle>auto</WhiteBalanceStyle>")),
          QStringLiteral("day/night, WDR and white balance map to the ISAPI names"));
    Reply put = call(QStringLiteral("PUT"), QStringLiteral("/ISAPI/Image/channels/1/color"),
                     "<Color><brightnessLevel>70</brightnessLevel><contrastLevel>60</contrastLevel><saturationLevel>40</saturationLevel></Color>");
    const int iSet = cam.indexOf(QStringLiteral("/onvif/imaging"), QStringLiteral("<SetImagingSettings"));
    const QString setBody = iSet >= 0 ? QString::fromUtf8(cam.log.at(iSet).body) : QString();
    const int pb = setBody.indexOf(QLatin1String(">70</Brightness>")), ps = setBody.indexOf(QLatin1String("<ColorSaturation")), pc = setBody.indexOf(QLatin1String("<Contrast"));
    check(put.ok && pb > 0 && pb < ps && ps < pc && setBody.contains(QLatin1String("<ForcePersistence>true</ForcePersistence>")),
          QStringLiteral("image write becomes SetImagingSettings in schema order"));
    call(QStringLiteral("PUT"), QStringLiteral("/ISAPI/Image/channels/1/WDR"), "<WDR><mode>open</mode></WDR>");
    check(cam.indexOf(QStringLiteral("/onvif/imaging"), QStringLiteral("<WideDynamicRange")) >= 0
              && cam.indexOf(QStringLiteral("/onvif/imaging"), QStringLiteral("<Mode>ON</Mode>")) >= 0,
          QStringLiteral("WDR open becomes WideDynamicRange Mode ON"));

    Reply stream = call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Streaming/channels/101"));
    check(stream.ok && stream.body.contains(QLatin1String("<videoCodecType>H.264</videoCodecType>"))
              && stream.body.contains(QLatin1String("<videoResolutionWidth>1920</videoResolutionWidth>"))
              && stream.body.contains(QLatin1String("<maxFrameRate>2500</maxFrameRate>"))
              && stream.body.contains(QLatin1String("<vbrUpperCap>4096</vbrUpperCap>"))
              && stream.body.contains(QLatin1String("<GovLength>50</GovLength>")),
          QStringLiteral("stream encoder values read over ONVIF (%1)").arg(stream.body));
    // writing the encoder: read the camera's own configuration, change only what was sent, write it all back
    Reply sput = call(QStringLiteral("PUT"), QStringLiteral("/ISAPI/Streaming/channels/101"),
                      "<StreamingChannel><Video><videoCodecType>H.264</videoCodecType><videoResolutionWidth>1280</videoResolutionWidth>"
                      "<videoResolutionHeight>720</videoResolutionHeight><maxFrameRate>1500</maxFrameRate><vbrUpperCap>2048</vbrUpperCap>"
                      "<GovLength>25</GovLength></Video></StreamingChannel>");
    const int iSetEnc = cam.indexOf(QStringLiteral("/onvif/media"), QStringLiteral("<SetVideoEncoderConfiguration"));
    const QString enc = iSetEnc >= 0 ? QString::fromUtf8(cam.log.at(iSetEnc).body) : QString();
    check(sput.ok && iSetEnc >= 0 && enc.contains(QLatin1String("token=\"VEC_1\"")) && enc.contains(QLatin1String(">1280</tt:Width>"))
              && enc.contains(QLatin1String(">720</tt:Height>")) && enc.contains(QLatin1String(">15</tt:FrameRateLimit>"))
              && enc.contains(QLatin1String(">2048</tt:BitrateLimit>")) && enc.contains(QLatin1String(">25</tt:GovLength>"))
              && enc.contains(QLatin1String("xmlns:tt=")) && enc.contains(QLatin1String("<ForcePersistence>true</ForcePersistence>")),
          QStringLiteral("changing the stream rewrites the camera's own configuration with only the changed values (1280x720, 15 fps, 2048 kbps, GOP 25)"));
    check(enc.contains(QLatin1String("<tt:Encoding>H264</tt:Encoding>")), QStringLiteral("what was not changed (the codec) is sent back as the camera had it"));

    Reply scaps = call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Streaming/channels/101/capabilities"));
    check(scaps.ok && scaps.body.contains(QLatin1String("<videoCodecType opt=\"H.265,H.264\"/>"))
              && scaps.body.contains(QStringLiteral("<videoResolution opt=\"1920×1080,1280×720\"/>"))
              && scaps.body.contains(QLatin1String("<maxFrameRate opt=\"2500,2000,1500,1200,1000,800,500,100\"/>"))
              && scaps.body.contains(QLatin1String("<GovLength min=\"1\" max=\"150\"/>")) && scaps.body.contains(QLatin1String("<vbrUpperCap min=\"64\" max=\"8192\"/>")),
          QStringLiteral("the camera's option ranges become opt lists: codecs, resolutions, frame rates, GOP and bitrate limits (%1)").arg(scaps.body));

    // imaging extras: backlight compensation, exposure mode, focus mode
    check(call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Image/channels/1/BLC")).body.contains(QLatin1String("<enabled>true</enabled>"))
              && call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Image/channels/1/exposure")).body.contains(QLatin1String("<ExposureType>auto</ExposureType>"))
              && call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Image/channels/1/focusMode")).body.contains(QLatin1String("<focusStyle>MANUAL</focusStyle>")),
          QStringLiteral("backlight compensation, exposure mode and focus mode are read"));
    call(QStringLiteral("PUT"), QStringLiteral("/ISAPI/Image/channels/1/BLC"), "<BLC><enabled>false</enabled></BLC>");
    call(QStringLiteral("PUT"), QStringLiteral("/ISAPI/Image/channels/1/focusMode"), "<FocusConfiguration><focusStyle>AUTO</focusStyle></FocusConfiguration>");
    check(cam.indexOf(QStringLiteral("/onvif/imaging"), QStringLiteral("<BacklightCompensation")) >= 0 && cam.indexOf(QStringLiteral("/onvif/imaging"), QStringLiteral("<Mode>OFF</Mode>")) >= 0
              && cam.indexOf(QStringLiteral("/onvif/imaging"), QStringLiteral("<AutoFocusMode>AUTO</AutoFocusMode>")) >= 0,
          QStringLiteral("and written as BacklightCompensation Mode and Focus AutoFocusMode"));
    check(call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Image/channels/1/exposure/capabilities")).body.contains(QLatin1String("opt=\"auto,manual\""))
              && call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Image/channels/1/focusMode/capabilities")).body.contains(QLatin1String("opt=\"AUTO,MANUAL\""))
              && call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Image/channels/1/IrcutFilter/capabilities")).body.contains(QLatin1String("opt=\"day,night,auto\""))
              && call(QStringLiteral("GET"), QStringLiteral("/ISAPI/Image/channels/1/color/capabilities")).body.contains(QLatin1String("<brightnessLevel min=\"0\" max=\"100\"/>")),
          QStringLiteral("the image menus and slider ranges come from the camera's own options"));

    // clock
    check(call(QStringLiteral("GET"), QStringLiteral("/ISAPI/System/time")).body.contains(QLatin1String("<timeMode>NTP</timeMode>"))
              && call(QStringLiteral("GET"), QStringLiteral("/ISAPI/System/time/ntpServers/1")).body.contains(QLatin1String("<ipAddress>192.168.1.10</ipAddress>")),
          QStringLiteral("time mode and NTP server are read through the Device service"));
    call(QStringLiteral("PUT"), QStringLiteral("/ISAPI/System/time/ntpServers/1"), "<NTPServer><ipAddress>10.0.0.9</ipAddress></NTPServer>");
    check(cam.indexOf(QStringLiteral("/onvif/device_service"), QStringLiteral("<IPv4Address xmlns=\"http://www.onvif.org/ver10/schema\">10.0.0.9</IPv4Address>")) >= 0,
          QStringLiteral("an NTP server is written with SetNTP"));
    Reply info = call(QStringLiteral("GET"), QStringLiteral("/ISAPI/System/deviceInfo"));
    check(info.ok && info.body.contains(QLatin1String("<model>CJ3000</model>")) && info.body.contains(QLatin1String("<serialNumber>SN123</serialNumber>")),
          QStringLiteral("device info read over ONVIF"));
    check(call(QStringLiteral("GET"), QStringLiteral("/ISAPI/System/Network/interfaces/1/ipAddress")).status == 404, QStringLiteral("pages ONVIF cannot serve answer 404"));
}


// ---- ONVIF dual-spectrum: one video source per sensor -----------------------------------

static void testOnvifDual()
{
    FakeCamera cam;
    const QString base = cam.url();
    // Deliberately awkward: the camera lists the small profiles first and only the third carries PTZ.
    struct Prof { const char *token, *source, *enc; int w, h; bool ptz; };
    static const Prof profs[] = {
        {"P_vis_sub", "VideoSource_1", "VEC_a", 704, 576, false},
        {"P_th_main", "VideoSource_2", "VEC_c", 640, 512, false},
        {"P_vis_main", "VideoSource_1", "VEC_b", 1920, 1080, true},
        {"P_th_sub", "VideoSource_2", "VEC_d", 320, 256, false},
    };
    cam.handler = [base](const Req &r, int &status, QByteArray &reply) {
        status = 200;
        const QString b = QString::fromUtf8(r.body);
        auto has = [&b](const char *s) { return b.contains(QLatin1String(s)); };
        reply = "<s:Envelope><s:Body><ok/></s:Body></s:Envelope>";
        if (has("<GetCapabilities")) {
            reply = QStringLiteral("<s:Envelope><s:Body><tds:Capabilities>"
                                   "<tt:Media><tt:XAddr>%1/onvif/media</tt:XAddr></tt:Media>"
                                   "<tt:PTZ><tt:XAddr>%1/onvif/ptz</tt:XAddr></tt:PTZ>"
                                   "<tt:Imaging><tt:XAddr>%1/onvif/imaging</tt:XAddr></tt:Imaging>"
                                   "</tds:Capabilities></s:Body></s:Envelope>").arg(base).toUtf8();
        } else if (has("<GetProfiles")) {
            QString x = QStringLiteral("<s:Envelope><s:Body><trt:GetProfilesResponse>");
            for (const Prof &p : profs)
                x += QStringLiteral("<trt:Profiles fixed=\"true\" token=\"%1\"><tt:Name>%1</tt:Name>"
                                    "<tt:VideoSourceConfiguration token=\"VSC\"><tt:SourceToken>%2</tt:SourceToken></tt:VideoSourceConfiguration>"
                                    "<tt:VideoEncoderConfiguration token=\"%3\"><tt:Encoding>H264</tt:Encoding>"
                                    "<tt:Resolution><tt:Width>%4</tt:Width><tt:Height>%5</tt:Height></tt:Resolution></tt:VideoEncoderConfiguration>%6"
                                    "</trt:Profiles>").arg(QString::fromLatin1(p.token), QString::fromLatin1(p.source), QString::fromLatin1(p.enc))
                         .arg(p.w).arg(p.h).arg(p.ptz ? QStringLiteral("<tt:PTZConfiguration token=\"PTZ\"/>") : QString());
            x += QStringLiteral("</trt:GetProfilesResponse></s:Body></s:Envelope>");
            reply = x.toUtf8();
        } else if (has("<GetStreamUri")) {
            for (const Prof &p : profs)
                if (b.contains(QStringLiteral("<ProfileToken>%1</ProfileToken>").arg(QString::fromLatin1(p.token))))
                    reply = QStringLiteral("<r><trt:MediaUri><tt:Uri>rtsp://127.0.0.1:554/%1</tt:Uri></trt:MediaUri></r>").arg(QString::fromLatin1(p.token)).toUtf8();
        }
    };

    CameraConfig cfg;
    cfg.id = QStringLiteral("dual");
    cfg.proto = QStringLiteral("ONVIF");
    cfg.ip = QStringLiteral("127.0.0.1");
    cfg.httpPort = cam.port();
    // no username, no password: the camera has ONVIF authentication off
    OnvifDriver d(cfg);

    check(waitFor([&] { return d.streamUrl(true, 1).contains(QLatin1String("P_vis_main")) && d.streamUrl(false, 2).contains(QLatin1String("P_th_sub")); }),
          QStringLiteral("dual camera: both sensors' stream addresses are read"));
    check(d.streamUrl(true, 1).endsWith(QLatin1String("/P_vis_main")) && d.streamUrl(false, 1).endsWith(QLatin1String("/P_vis_sub")),
          QStringLiteral("visible (first source): biggest profile is main, next is sub, whatever order the camera lists them (%1 / %2)")
              .arg(d.streamUrl(true, 1), d.streamUrl(false, 1)));
    check(d.streamUrl(true, 2).endsWith(QLatin1String("/P_th_main")) && d.streamUrl(false, 2).endsWith(QLatin1String("/P_th_sub")),
          QStringLiteral("thermal (second source): main and sub grouped separately (%1 / %2)").arg(d.streamUrl(true, 2), d.streamUrl(false, 2)));
    check(d.hasChannel2(), QStringLiteral("two sensors: the Thermal option is offered"));

    QList<Done> done;
    QObject::connect(&d, &CameraDriver::commandDone, [&](const QString &label, bool ok, int, const QString &err) { done.append({label, ok, err}); });
    d.continuousMove(30, 0, 0, QStringLiteral("move"));
    waitFor([&] { return !done.isEmpty(); });
    check(cam.indexOf(QStringLiteral("/onvif/ptz"), QStringLiteral("<ProfileToken>P_vis_main</ProfileToken>")) >= 0,
          QStringLiteral("PTZ goes to the profile that has the PTZ configuration"));
    check(!cam.log.isEmpty() && !cam.log.first().body.contains("UsernameToken"),
          QStringLiteral("blank credentials: no security header is sent"));

    // the planner on its own, including the single-sensor and missing-resolution cases
    QList<OnvifProfile> one{{QStringLiteral("a"), QStringLiteral("S1"), QString(), 0, 0, true}};
    const auto p1 = OnvifDriver::planStreams(one);
    check(p1.size() == 1 && p1[0].mainIndex == 0 && p1[0].subIndex == 0, QStringLiteral("one sensor, one profile: main and sub are the same (no thermal option)"));
    QList<OnvifProfile> two{{QStringLiteral("m"), QStringLiteral("S1"), QString(), 0, 0, false}, {QStringLiteral("s"), QStringLiteral("S1"), QString(), 0, 0, false}};
    const auto p2 = OnvifDriver::planStreams(two);
    check(p2.size() == 1 && p2[0].mainIndex == 0 && p2[0].subIndex == 1, QStringLiteral("profiles without resolutions keep the camera's order"));
}


// ---- optional credentials, HTTPS pinning ------------------------------------------------

static void testCredentialsAndTls()
{
    CameraConfig blank = CameraConfig::fromMap({{QStringLiteral("proto"), QStringLiteral("ONVIF")}, {QStringLiteral("ip"), QStringLiteral("10.0.0.5")}});
    check(blank.noCredentials() && blank.toMap(false).value(QStringLiteral("authOff")).toBool(),
          QStringLiteral("a camera saved with no username and no password is flagged 'authentication off'"));
    CameraConfig with = CameraConfig::fromMap({{QStringLiteral("proto"), QStringLiteral("ONVIF")}, {QStringLiteral("ip"), QStringLiteral("10.0.0.5")},
                                               {QStringLiteral("user"), QStringLiteral("admin")}, {QStringLiteral("pass"), QStringLiteral("x")}});
    check(!with.noCredentials() && !with.toMap(false).value(QStringLiteral("authOff")).toBool() && !with.toMap(false).contains(QStringLiteral("pass")),
          QStringLiteral("with credentials it is not flagged, and the password is not part of the saved map"));
    CameraConfig demo = CameraConfig::fromMap({{QStringLiteral("proto"), QStringLiteral("Demo")}});
    check(!demo.toMap(false).value(QStringLiteral("authOff")).toBool(), QStringLiteral("the demo camera is never flagged"));

    check(TlsPin::decide(QString(), QStringLiteral("AA:BB")) == TlsPin::Decision::NeedsApproval, QStringLiteral("nothing pinned: the user is asked"));
    check(TlsPin::decide(QStringLiteral("aa:bb"), QStringLiteral("AA:BB")) == TlsPin::Decision::Trusted, QStringLiteral("the pinned certificate is accepted (case-insensitive)"));
    check(TlsPin::decide(QStringLiteral("AA:BB"), QStringLiteral("CC:DD")) == TlsPin::Decision::Changed, QStringLiteral("a different certificate is refused and reported as changed"));
    // Following an HTTP-to-HTTPS redirect and the TLS handshake itself need a real HTTPS camera; they are not tested here.
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CorinthTest"));
    QCoreApplication::setApplicationName(QStringLiteral("driver_test"));
    QStandardPaths::setTestModeEnabled(true);   // keep profile files out of the real AppData
    testIsapi();
    testOnvif();
    testOnvifDual();
    testCredentialsAndTls();
    qInfo().noquote() << "DRIVER TEST failures:" << g_fail;
    return g_fail;
}
