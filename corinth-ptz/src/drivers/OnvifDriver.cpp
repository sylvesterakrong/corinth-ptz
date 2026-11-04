#include "OnvifDriver.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace {
const char *kPtzNs = "http://www.onvif.org/ver20/ptz/wsdl";
const char *kMediaNs = "http://www.onvif.org/ver10/media/wsdl";
const char *kDeviceNs = "http://www.onvif.org/ver10/device/wsdl";
const char *kImagingNs = "http://www.onvif.org/ver20/imaging/wsdl";
const char *kSchemaNs = "http://www.onvif.org/ver10/schema";

QString unescapeXml(QString s)
{
    s.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    s.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    s.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    s.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    return s;
}

QString xaddrFor(const QString &xml, const QString &service)
{
    const QRegularExpression re(
        QStringLiteral("<(?:\\w+:)?%1>\\s*<(?:\\w+:)?XAddr>([^<]+)<").arg(service),
        QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(xml);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

QString soapFault(const QByteArray &data)
{
    const QString xml = QString::fromUtf8(data);
    QString reason = Xml::tagText(xml, QStringLiteral("Text"));
    if (reason.isEmpty())
        reason = xml.left(120);
    return reason;
}

// <section> ... <tag>value</tag> ... </section>
QString sectionText(const QString &xml, const QString &section, const QString &tag)
{
    const QRegularExpression re(QStringLiteral("<(?:\\w+:)?%1\\b[^>]*>(.*?)</(?:\\w+:)?%1>").arg(section),
                                QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(xml);
    return m.hasMatch() ? Xml::tagText(m.captured(1), tag) : QString();
}

// ONVIF image values are floats; the Settings pages work in whole numbers.
QString wholeNumber(const QString &text)
{
    bool ok = false;
    const double d = text.toDouble(&ok);
    return ok ? QString::number(qRound(d)) : QString();
}

QString tagIf(const QString &tag, const QString &value)
{
    return value.isEmpty() ? QString() : QStringLiteral("<%1>%2</%1>").arg(tag, value);
}

// Every <tag>value</tag> in the text (with or without a namespace prefix), in order.
QStringList allTags(const QString &xml, const QString &tag)
{
    QStringList out;
    const QRegularExpression re(QStringLiteral("<(?:\\w+:)?%1\\b[^>]*>([^<]*)</(?:\\w+:)?%1>").arg(tag));
    auto it = re.globalMatch(xml);
    while (it.hasNext()) out << it.next().captured(1).trimmed();
    return out;
}

// The inside of the first <section> ... </section>.
QString sectionXml(const QString &xml, const QString &section)
{
    const QRegularExpression re(QStringLiteral("<(?:\\w+:)?%1\\b[^>]*>(.*?)</(?:\\w+:)?%1>").arg(section),
                                QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(xml);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString optList(const QStringList &values) { return values.join(QLatin1Char(',')); }

// Replace the text of the first <tag>..</tag>; false if there is none.
bool setTagText(QString &xml, const QString &tag, const QString &value)
{
    const QRegularExpression re(QStringLiteral("(<(?:\\w+:)?%1\\b[^>]*>)[^<]*(</(?:\\w+:)?%1>)").arg(tag));
    const auto m = re.match(xml);
    if (!m.hasMatch()) return false;
    xml.replace(m.capturedStart(0), m.capturedLength(0), m.captured(1) + Xml::escape(value) + m.captured(2));
    return true;
}

// ONVIF GetImagingSettings answer -> the ISAPI-shaped XML the Settings pages expect.
QString imagingToIsapi(const QString &kind, const QString &x)
{
    if (kind == QLatin1String("color"))
        return QStringLiteral("<Color>%1%2%3</Color>")
            .arg(tagIf(QStringLiteral("brightnessLevel"), wholeNumber(Xml::tagText(x, QStringLiteral("Brightness")))),
                 tagIf(QStringLiteral("contrastLevel"), wholeNumber(Xml::tagText(x, QStringLiteral("Contrast")))),
                 tagIf(QStringLiteral("saturationLevel"), wholeNumber(Xml::tagText(x, QStringLiteral("ColorSaturation")))));
    if (kind == QLatin1String("sharpness"))
        return QStringLiteral("<Sharpness>%1</Sharpness>")
            .arg(tagIf(QStringLiteral("SharpnessLevel"), wholeNumber(Xml::tagText(x, QStringLiteral("Sharpness")))));
    if (kind == QLatin1String("IrcutFilter")) {
        const QString v = Xml::tagText(x, QStringLiteral("IrCutFilter")).toUpper();
        const QString t = v == QLatin1String("ON") ? QStringLiteral("day") : v == QLatin1String("OFF") ? QStringLiteral("night")
                          : v == QLatin1String("AUTO") ? QStringLiteral("auto") : QString();
        return QStringLiteral("<IrcutFilter>%1</IrcutFilter>").arg(tagIf(QStringLiteral("IrcutFilterType"), t));
    }
    if (kind == QLatin1String("WDR")) {
        const QString v = sectionText(x, QStringLiteral("WideDynamicRange"), QStringLiteral("Mode")).toUpper();
        const QString t = v == QLatin1String("ON") ? QStringLiteral("open") : v == QLatin1String("OFF") ? QStringLiteral("close") : QString();
        return QStringLiteral("<WDR>%1</WDR>").arg(tagIf(QStringLiteral("mode"), t));
    }
    if (kind == QLatin1String("BLC")) {
        const QString v = sectionText(x, QStringLiteral("BacklightCompensation"), QStringLiteral("Mode")).toUpper();
        const QString t = v == QLatin1String("ON") ? QStringLiteral("true") : v == QLatin1String("OFF") ? QStringLiteral("false") : QString();
        return QStringLiteral("<BLC>%1</BLC>").arg(tagIf(QStringLiteral("enabled"), t));
    }
    if (kind == QLatin1String("exposure")) {
        const QString v = sectionText(x, QStringLiteral("Exposure"), QStringLiteral("Mode")).toUpper();
        const QString t = v == QLatin1String("AUTO") ? QStringLiteral("auto") : v == QLatin1String("MANUAL") ? QStringLiteral("manual") : QString();
        return QStringLiteral("<Exposure>%1</Exposure>").arg(tagIf(QStringLiteral("ExposureType"), t));
    }
    if (kind == QLatin1String("focusMode")) {
        const QString v = sectionText(x, QStringLiteral("Focus"), QStringLiteral("AutoFocusMode")).toUpper();
        return QStringLiteral("<FocusConfiguration>%1</FocusConfiguration>").arg(tagIf(QStringLiteral("focusStyle"), v));
    }
    // whiteBalance
    const QString v = sectionText(x, QStringLiteral("WhiteBalance"), QStringLiteral("Mode")).toUpper();
    const QString t = v == QLatin1String("AUTO") ? QStringLiteral("auto") : v == QLatin1String("MANUAL") ? QStringLiteral("manual") : QString();
    return QStringLiteral("<WhiteBalance>%1</WhiteBalance>").arg(tagIf(QStringLiteral("WhiteBalanceStyle"), t));
}
} // namespace

OnvifDriver::OnvifDriver(const CameraConfig &cfg, QObject *parent)
    : CameraDriver(cfg, parent)
{
    init();
}

QString OnvifDriver::deviceUrl() const
{
    return baseUrl() + QStringLiteral("/onvif/device_service");
}

QString OnvifDriver::securityHeader() const
{
    if (m_cfg.user.isEmpty())
        return {};
    QByteArray nonce(16, Qt::Uninitialized);
    for (int i = 0; i < nonce.size(); ++i)
        nonce[i] = char(QRandomGenerator::global()->bounded(256));
    const QString created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    // PasswordDigest = Base64( SHA1( nonce + created + password ) )
    const QByteArray digest = QCryptographicHash::hash(
        nonce + created.toUtf8() + m_cfg.pass.toUtf8(), QCryptographicHash::Sha1).toBase64();

    return QStringLiteral(
        "<s:Header><Security s:mustUnderstand=\"1\" "
        "xmlns=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
        "<UsernameToken><Username>%1</Username>"
        "<Password Type=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest\">%2</Password>"
        "<Nonce EncodingType=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-soap-message-security-1.0#Base64Binary\">%3</Nonce>"
        "<Created xmlns=\"http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd\">%4</Created>"
        "</UsernameToken></Security></s:Header>")
        .arg(Xml::escape(m_cfg.user), QString::fromLatin1(digest),
             QString::fromLatin1(nonce.toBase64()), created);
}

void OnvifDriver::soap(const QString &endpoint, const QString &body, bool auth, HttpCallback cb)
{
    const QString env = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">%1<s:Body>%2</s:Body></s:Envelope>")
        .arg(auth ? securityHeader() : QString(), body);
    httpUrl(QStringLiteral("POST"), QUrl(endpoint), env.toUtf8(),
            QByteArrayLiteral("application/soap+xml; charset=utf-8"), std::move(cb), 4000);
}

void OnvifDriver::init()
{
    if (m_initRunning)
        return;
    m_initRunning = true;

    const QString caps = QStringLiteral("<GetCapabilities xmlns=\"%1\"><Category>All</Category></GetCapabilities>")
                             .arg(QLatin1String(kDeviceNs));
    soap(deviceUrl(), caps, true, [this](bool ok, int, const QByteArray &data, int) {
        if (!ok) { m_initRunning = false; return; }
        const QString xml = QString::fromUtf8(data);
        m_mediaUrl = xaddrFor(xml, QStringLiteral("Media"));
        m_ptzUrl = xaddrFor(xml, QStringLiteral("PTZ"));
        m_imagingUrl = xaddrFor(xml, QStringLiteral("Imaging"));
        if (m_mediaUrl.isEmpty()) { m_initRunning = false; return; }

        const QString gp = QStringLiteral("<GetProfiles xmlns=\"%1\"/>").arg(QLatin1String(kMediaNs));
        soap(m_mediaUrl, gp, true, [this](bool ok2, int, const QByteArray &data2, int) {
            if (!ok2) { m_initRunning = false; return; }
            const QString xml2 = QString::fromUtf8(data2);
            m_plist.clear();
            m_plan.clear();
            m_uris.clear();
            m_sourceTokens.clear();
            const QRegularExpression block(QStringLiteral("<(?:\\w+:)?Profiles\\b([^>]*)>(.*?)</(?:\\w+:)?Profiles>"),
                                           QRegularExpression::DotMatchesEverythingOption);
            const QRegularExpression tokenRe(QStringLiteral("\\btoken=\"([^\"]+)\""));
            const QRegularExpression encRe(QStringLiteral("<(?:\\w+:)?VideoEncoderConfiguration\\b([^>]*)>(.*?)</(?:\\w+:)?VideoEncoderConfiguration>"),
                                           QRegularExpression::DotMatchesEverythingOption);
            const QRegularExpression srcRe(QStringLiteral("<(?:\\w+:)?SourceToken>([^<]+)<"));
            const QRegularExpression ptzRe(QStringLiteral("<(?:\\w+:)?PTZConfiguration\\b"));
            auto it = block.globalMatch(xml2);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                const QRegularExpressionMatch tok = tokenRe.match(m.captured(1));
                if (!tok.hasMatch())
                    continue;
                OnvifProfile p;
                p.token = tok.captured(1);
                const QString inner = m.captured(2);
                const QRegularExpressionMatch enc = encRe.match(inner);
                if (enc.hasMatch()) {
                    const QRegularExpressionMatch et = tokenRe.match(enc.captured(1));
                    if (et.hasMatch()) p.encoderToken = et.captured(1);
                    p.width = Xml::tagText(enc.captured(2), QStringLiteral("Width")).toInt();
                    p.height = Xml::tagText(enc.captured(2), QStringLiteral("Height")).toInt();
                }
                const QRegularExpressionMatch src = srcRe.match(inner);
                if (src.hasMatch()) p.source = src.captured(1).trimmed();
                p.ptz = ptzRe.match(inner).hasMatch();
                m_plist << p;
            }
            m_plan = planStreams(m_plist);
            for (const OnvifStreamPlan &sp : std::as_const(m_plan))
                m_sourceTokens << sp.source;
            m_initRunning = false;

            // Stream addresses: the main and sub profile of each sensor (visible, then thermal).
            QSet<int> wanted;
            for (const OnvifStreamPlan &sp : std::as_const(m_plan)) {
                if (sp.mainIndex >= 0) wanted.insert(sp.mainIndex);
                if (sp.subIndex >= 0) wanted.insert(sp.subIndex);
            }
            for (int idx : std::as_const(wanted)) {
                const QString token = m_plist.at(idx).token;
                const QString gs = QStringLiteral(
                    "<GetStreamUri xmlns=\"%1\"><StreamSetup>"
                    "<Stream xmlns=\"%2\">RTP-Unicast</Stream>"
                    "<Transport xmlns=\"%2\"><Protocol>RTSP</Protocol></Transport>"
                    "</StreamSetup><ProfileToken>%3</ProfileToken></GetStreamUri>")
                    .arg(QLatin1String(kMediaNs), QLatin1String(kSchemaNs), token);
                soap(m_mediaUrl, gs, true, [this, token](bool ok3, int, const QByteArray &d3, int) {
                    if (!ok3) return;
                    const QString uri = unescapeXml(Xml::tagText(QString::fromUtf8(d3), QStringLiteral("Uri")));
                    if (uri.isEmpty()) return;
                    m_uris.insert(token, uri);
                    emit streamUrlsChanged();
                });
            }
        });
    });
}

QList<OnvifStreamPlan> OnvifDriver::planStreams(const QList<OnvifProfile> &profiles)
{
    // Sensors in order of first appearance.
    QStringList sources;
    for (const OnvifProfile &p : profiles)
        if (!sources.contains(p.source)) sources << p.source;

    QList<OnvifStreamPlan> plan;
    for (const QString &src : std::as_const(sources)) {
        QList<int> idx;
        for (int i = 0; i < profiles.size(); ++i)
            if (profiles.at(i).source == src) idx << i;
        // Biggest picture first; equal sizes keep the camera's own order.
        std::stable_sort(idx.begin(), idx.end(), [&profiles](int a, int b) {
            return qint64(profiles.at(a).width) * profiles.at(a).height > qint64(profiles.at(b).width) * profiles.at(b).height;
        });
        OnvifStreamPlan sp;
        sp.source = src;
        sp.mainIndex = idx.value(0, -1);
        sp.subIndex = idx.size() > 1 ? idx.at(1) : sp.mainIndex;
        sp.thirdIndex = idx.size() > 2 ? idx.at(2) : sp.subIndex;
        plan << sp;
    }
    return plan;
}

int OnvifDriver::ptzProfileIndex(const QList<OnvifProfile> &profiles)
{
    for (int i = 0; i < profiles.size(); ++i)
        if (profiles.at(i).ptz) return i;
    return profiles.isEmpty() ? -1 : 0;
}

QString OnvifDriver::ptzToken() const
{
    const int i = ptzProfileIndex(m_plist);
    return i >= 0 ? m_plist.at(i).token : QString();
}

void OnvifDriver::ptzCommand(const QString &body, const QString &label, bool changesPresets)
{
    if (m_ptzUrl.isEmpty() || m_plist.isEmpty()) {
        init();
        emit commandDone(label, false, 0, QStringLiteral("ONVIF not ready (no PTZ service or profile yet)"));
        return;
    }
    soap(m_ptzUrl, body, true, [this, label, changesPresets](bool ok, int status, const QByteArray &data, int ms) {
        emit commandDone(label, ok, ms, ok ? QString() : QStringLiteral("HTTP %1 %2").arg(status).arg(soapFault(data)));
        if (ok && changesPresets)
            emit presetsDirty();
    });
}

void OnvifDriver::imagingCommand(const QString &body, const QString &label)
{
    if (m_imagingUrl.isEmpty() || m_sourceTokens.isEmpty()) {
        init();
        emit commandDone(label, false, 0, QStringLiteral("ONVIF imaging service not ready (no Imaging address or video source yet)"));
        return;
    }
    soap(m_imagingUrl, body, true, [this, label](bool ok, int status, const QByteArray &data, int ms) {
        emit commandDone(label, ok, ms, ok ? QString() : QStringLiteral("HTTP %1 %2").arg(status).arg(soapFault(data)));
    });
}

void OnvifDriver::continuousMove(int pan, int tilt, int zoom, const QString &label)
{
    const QString tok = ptzToken();
    if (pan == 0 && tilt == 0 && zoom == 0) {
        ptzCommand(QStringLiteral("<Stop xmlns=\"%1\"><ProfileToken>%2</ProfileToken>"
                                  "<PanTilt>true</PanTilt><Zoom>true</Zoom></Stop>")
                       .arg(QLatin1String(kPtzNs), tok), label);
        return;
    }
    ptzCommand(QStringLiteral(
                   "<ContinuousMove xmlns=\"%1\"><ProfileToken>%2</ProfileToken><Velocity>"
                   "<PanTilt xmlns=\"%3\" x=\"%4\" y=\"%5\"/><Zoom xmlns=\"%3\" x=\"%6\"/>"
                   "</Velocity></ContinuousMove>")
                   .arg(QLatin1String(kPtzNs), tok, QLatin1String(kSchemaNs))
                   .arg(pan / 100.0, 0, 'f', 2).arg(tilt / 100.0, 0, 'f', 2).arg(zoom / 100.0, 0, 'f', 2),
               label);
}

QString OnvifDriver::presetToken(int id) const
{
    return m_presetTokens.value(id, QString::number(id));
}

void OnvifDriver::gotoPreset(int id, const QString &label)
{
    ptzCommand(QStringLiteral("<GotoPreset xmlns=\"%1\"><ProfileToken>%2</ProfileToken>"
                              "<PresetToken>%3</PresetToken></GotoPreset>")
                   .arg(QLatin1String(kPtzNs), ptzToken(), presetToken(id)), label);
}

void OnvifDriver::setPreset(int id, const QString &name, const QString &label)
{
    // A known slot is overwritten (token given); a new slot lets the camera
    // choose its token, which the next preset listing picks up.
    const QString token = m_presetTokens.contains(id)
        ? QStringLiteral("<PresetToken>%1</PresetToken>").arg(m_presetTokens.value(id)) : QString();
    ptzCommand(QStringLiteral("<SetPreset xmlns=\"%1\"><ProfileToken>%2</ProfileToken>"
                              "<PresetName>%3</PresetName>%4</SetPreset>")
                   .arg(QLatin1String(kPtzNs), ptzToken(), Xml::escape(name), token), label, true);
}

void OnvifDriver::gotoHome(const QString &label)
{
    ptzCommand(QStringLiteral("<GotoHomePosition xmlns=\"%1\"><ProfileToken>%2</ProfileToken></GotoHomePosition>")
                   .arg(QLatin1String(kPtzNs), ptzToken()), label);
}

void OnvifDriver::setHome(const QString &label)
{
    ptzCommand(QStringLiteral("<SetHomePosition xmlns=\"%1\"><ProfileToken>%2</ProfileToken></SetHomePosition>")
                   .arg(QLatin1String(kPtzNs), ptzToken()), label);
}

void OnvifDriver::focus(int dir, const QString &label)
{
    // Imaging service. Positive speed = towards far/infinity (per the ONVIF
    // Imaging spec); confirm on the camera, a swapped Near/Far is easy to spot.
    const QString src = m_sourceTokens.value(0);
    if (dir == 0) {
        imagingCommand(QStringLiteral("<Stop xmlns=\"%1\"><VideoSourceToken>%2</VideoSourceToken></Stop>")
                           .arg(QLatin1String(kImagingNs), src), label);
        return;
    }
    imagingCommand(QStringLiteral("<Move xmlns=\"%1\"><VideoSourceToken>%2</VideoSourceToken><Focus>"
                                  "<Continuous xmlns=\"%3\"><Speed>%4</Speed></Continuous></Focus></Move>")
                       .arg(QLatin1String(kImagingNs), src, QLatin1String(kSchemaNs))
                       .arg(dir > 0 ? 0.5 : -0.5, 0, 'f', 2), label);
}

void OnvifDriver::iris(int, const QString &label)
{
    emit commandDone(label, false, 0, QStringLiteral("ONVIF has no continuous iris control; use ISAPI"));
}

void OnvifDriver::onePushFocus(const QString &label)
{
    emit commandDone(label, false, 0, QStringLiteral("ONVIF has no one-push focus; use ISAPI"));
}

void OnvifDriver::setAux(const QString &name, bool on, const QString &label)
{
    QString cmd;
    if (name == QLatin1String("wiper")) cmd = QStringLiteral("tt:Wiper|");
    else if (name == QLatin1String("ir")) cmd = QStringLiteral("tt:IRLamp|");
    if (cmd.isEmpty()) {
        emit commandDone(label, false, 0, QStringLiteral("No standard ONVIF auxiliary command for this"));
        return;
    }
    cmd += on ? QStringLiteral("On") : QStringLiteral("Off");
    ptzCommand(QStringLiteral("<SendAuxiliaryCommand xmlns=\"%1\"><ProfileToken>%2</ProfileToken>"
                              "<AuxiliaryData>%3</AuxiliaryData></SendAuxiliaryCommand>")
                   .arg(QLatin1String(kPtzNs), ptzToken(), cmd), label);
}

QVariantList OnvifDriver::auxItems() const
{
    struct Item { const char *id; const char *label; bool supported; };
    static const Item items[] = {
        {"stab", "Stabilisation", false}, {"lock", "Heading lock", false}, {"wiper", "Wiper", true},
        {"heater", "Heater / defog", false}, {"ir", "IR light", true}};
    QVariantList out;
    for (const Item &i : items) {
        QVariantMap m;
        m[QStringLiteral("id")] = QString::fromLatin1(i.id);
        m[QStringLiteral("label")] = QString::fromLatin1(i.label);
        m[QStringLiteral("supported")] = i.supported;
        m[QStringLiteral("readable")] = false;   // ONVIF cannot read these back
        out << m;
    }
    return out;
}

// ONVIF positions zoom as 0..1 over the lens's range, not as a magnification. The CJ3000's lens is a
// 40x optical zoom (6.6-264 mm); the app assumes the position is linear over that range, 0 = 1x and
// 1 = 40x. If the readout on the real camera disagrees, change kMaxZoomTimes or the mapping here.
namespace { constexpr double kMaxZoomTimes = 40.0; }

void OnvifDriver::setZoomLevel(double times, const QString &label)
{
    const double x = qBound(0.0, (times - 1.0) / (kMaxZoomTimes - 1.0), 1.0);
    ptzCommand(QStringLiteral("<AbsoluteMove xmlns=\"%1\"><ProfileToken>%2</ProfileToken><Position>"
                              "<Zoom xmlns=\"%3\" x=\"%4\"/></Position></AbsoluteMove>")
                   .arg(QLatin1String(kPtzNs), ptzToken(), QLatin1String(kSchemaNs)).arg(x, 0, 'f', 4), label);
}

void OnvifDriver::pointAt(double nx, double ny, const QString &label)
{
    // Translation in field-of-view units: 1.0 = one full picture width/height.
    // Needs a camera that supports TranslationSpaceFov; others answer with a fault.
    ptzCommand(QStringLiteral(
                   "<RelativeMove xmlns=\"%1\"><ProfileToken>%2</ProfileToken><Translation>"
                   "<PanTilt xmlns=\"%3\" x=\"%4\" y=\"%5\" "
                   "space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationSpaceFov\"/>"
                   "</Translation></RelativeMove>")
                   .arg(QLatin1String(kPtzNs), ptzToken(), QLatin1String(kSchemaNs))
                   .arg(nx - 0.5, 0, 'f', 3).arg(0.5 - ny, 0, 'f', 3),
               label);
}

void OnvifDriver::queryPresets()
{
    if (m_ptzUrl.isEmpty() || m_plist.isEmpty())
        return;
    soap(m_ptzUrl, QStringLiteral("<GetPresets xmlns=\"%1\"><ProfileToken>%2</ProfileToken></GetPresets>")
                       .arg(QLatin1String(kPtzNs), ptzToken()), true,
         [this](bool ok, int status, const QByteArray &data, int) {
             if (!ok) {
                 emit presetsListed(false, {}, QStringLiteral("HTTP %1 %2").arg(status).arg(soapFault(data)));
                 return;
             }
             const QString xml = QString::fromUtf8(data);
             const QRegularExpression block(QStringLiteral("<(?:\\w+:)?Preset\\b([^>]*)>(.*?)</(?:\\w+:)?Preset>"),
                                            QRegularExpression::DotMatchesEverythingOption);
             const QRegularExpression tokenRe(QStringLiteral("\\btoken=\"([^\"]+)\""));
             struct Raw { QString token, name; };
             QList<Raw> raws;
             auto it = block.globalMatch(xml);
             while (it.hasNext()) {
                 const QRegularExpressionMatch m = it.next();
                 const QRegularExpressionMatch t = tokenRe.match(m.captured(1));
                 if (!t.hasMatch())
                     continue;
                 raws.append({t.captured(1), Xml::tagText(m.captured(2), QStringLiteral("Name"))});
             }
             // Numeric tokens keep their number as the preset id; others get the
             // lowest free number so the UI can still address them.
             QSet<int> used;
             for (const Raw &r : raws) {
                 bool numeric = false;
                 const int n = r.token.toInt(&numeric);
                 if (numeric && n > 0) used.insert(n);
             }
             m_presetTokens.clear();
             QVariantList list;
             int next = 1;
             for (const Raw &r : raws) {
                 bool numeric = false;
                 int id = r.token.toInt(&numeric);
                 if (!numeric || id <= 0) {
                     while (used.contains(next)) ++next;
                     id = next;
                     used.insert(id);
                 }
                 m_presetTokens.insert(id, r.token);
                 QVariantMap m;
                 m[QStringLiteral("id")] = id;
                 m[QStringLiteral("name")] = r.name.isEmpty() ? QStringLiteral("Preset %1").arg(id) : r.name;
                 list << m;
             }
             emit presetsListed(true, list, QString());
         });
}

void OnvifDriver::queryStatus()
{
    if (m_ptzUrl.isEmpty() || m_plist.isEmpty() || m_statusInFlight)
        return;
    m_statusInFlight = true;
    soap(m_ptzUrl, QStringLiteral("<GetStatus xmlns=\"%1\"><ProfileToken>%2</ProfileToken></GetStatus>")
                       .arg(QLatin1String(kPtzNs), ptzToken()), true,
         [this](bool ok, int, const QByteArray &data, int) {
             m_statusInFlight = false;
             if (!ok) return;
             const QString xml = QString::fromUtf8(data);
             const QRegularExpression pt(QStringLiteral("PanTilt[^>]*\\sx=\"([-0-9.eE]+)\"[^>]*\\sy=\"([-0-9.eE]+)\""));
             const auto m = pt.match(xml);
             if (!m.hasMatch()) return;
             // ONVIF reports a normalised space (-1..1). Converting to degrees
             // needs the camera's space limits, so these are approximate.
             const double x = m.captured(1).toDouble(), y = m.captured(2).toDouble();
             // Zoom position, when the camera reports one, back to a magnification (see setZoomLevel).
             double zoomTimes = -1.0;
             const QRegularExpression zr(QStringLiteral("<(?:\\w+:)?Position>.*?<(?:\\w+:)?Zoom[^>]*\\sx=\"([-0-9.eE]+)\""),
                                         QRegularExpression::DotMatchesEverythingOption);
             const auto zm = zr.match(xml);
             if (zm.hasMatch())
                 zoomTimes = 1.0 + (kMaxZoomTimes - 1.0) * qBound(0.0, zm.captured(1).toDouble(), 1.0);
             emit statusUpdated((x + 1.0) * 180.0, y * 90.0, zoomTimes);
         });
}

void OnvifDriver::ping()
{
    // GetSystemDateAndTime is allowed without authentication: a clean RTT probe.
    soap(deviceUrl(), QStringLiteral("<GetSystemDateAndTime xmlns=\"%1\"/>").arg(QLatin1String(kDeviceNs)), false,
         [this](bool ok, int, const QByteArray &, int ms) {
             emit pingResult(ok, ms);
             if (ok && m_plist.isEmpty())
                 init();
         });
}

QString OnvifDriver::streamUrl(bool mainStream, int channel) const
{
    if (channel == 2) {
        // An address typed in by the user wins; otherwise the camera's second sensor.
        const QString t = thermalOverride(mainStream).isEmpty() ? thermalOverride(true) : thermalOverride(mainStream);
        if (!t.isEmpty())
            return rtspWithCredentials(t);
    }
    const QString custom = mainStream ? m_cfg.mainUrl : m_cfg.subUrl;
    if (channel != 2 && !custom.isEmpty())
        return rtspWithCredentials(custom);
    // Channel 1 is the first sensor (visible), channel 2 the second (thermal); a camera with
    // one sensor only has channel 1.
    const int sensor = qBound(0, channel - 1, int(m_plan.size()) - 1);
    if (sensor < 0 || sensor >= m_plan.size())
        return {};
    const OnvifStreamPlan &sp = m_plan.at(sensor);
    auto uriOf = [this](int index) { return index >= 0 && index < m_plist.size() ? m_uris.value(m_plist.at(index).token) : QString(); };
    QString uri = uriOf(mainStream ? sp.mainIndex : sp.subIndex);
    if (uri.isEmpty()) uri = uriOf(sp.mainIndex);
    return rtspWithCredentials(uri);
}

// ---- Settings pages over ONVIF ----------------------------------------------

namespace {
const QRegularExpression &imageRe()
{
    static const QRegularExpression re(QStringLiteral("^/ISAPI/Image/channels/(\\d+)/(color|sharpness|IrcutFilter|WDR|whiteBalance|BLC|exposure|focusMode)(/capabilities)?$"));
    return re;
}
const QRegularExpression &timeRe()
{
    static const QRegularExpression re(QStringLiteral("^/ISAPI/System/time(/ntpServers/1)?$"));
    return re;
}
const QRegularExpression &streamRe()
{
    static const QRegularExpression re(QStringLiteral("^/ISAPI/Streaming/channels/(\\d+)(/capabilities)?$"));
    return re;
}
}

bool OnvifDriver::supportsSettingsEndpoint(const QString &path) const
{
    const QString p = path.section(QLatin1Char('?'), 0, 0);
    return p == QLatin1String("/ISAPI/System/deviceInfo") || p == QLatin1String("/ISAPI/System/reboot")
           || imageRe().match(p).hasMatch() || streamRe().match(p).hasMatch() || timeRe().match(p).hasMatch();
}

void OnvifDriver::request(const QString &method, const QString &path, const QByteArray &body, HttpCallback cb)
{
    const QString p = path.section(QLatin1Char('?'), 0, 0);
    const bool isGet = method.compare(QLatin1String("GET"), Qt::CaseInsensitive) == 0;
    auto fail = [cb](int status, const QString &msg) { if (cb) cb(false, status, msg.toUtf8(), 0); };

    if (p == QLatin1String("/ISAPI/System/deviceInfo") && isGet) {
        soap(deviceUrl(), QStringLiteral("<GetDeviceInformation xmlns=\"%1\"/>").arg(QLatin1String(kDeviceNs)), true,
             [cb](bool ok, int status, const QByteArray &data, int ms) {
                 if (!ok) { if (cb) cb(false, status, soapFault(data).toUtf8(), ms); return; }
                 const QString x = QString::fromUtf8(data);
                 const QString model = Xml::tagText(x, QStringLiteral("Model"));
                 const QString out = QStringLiteral("<DeviceInfo><deviceName>%1</deviceName><model>%1</model>"
                                                    "<serialNumber>%2</serialNumber><firmwareVersion>%3</firmwareVersion></DeviceInfo>")
                                         .arg(Xml::escape(model), Xml::escape(Xml::tagText(x, QStringLiteral("SerialNumber"))),
                                              Xml::escape(Xml::tagText(x, QStringLiteral("FirmwareVersion"))));
                 if (cb) cb(true, 200, out.toUtf8(), ms);
             });
        return;
    }
    if (p == QLatin1String("/ISAPI/System/reboot") && !isGet) {
        soap(deviceUrl(), QStringLiteral("<SystemReboot xmlns=\"%1\"/>").arg(QLatin1String(kDeviceNs)), true,
             [cb](bool ok, int status, const QByteArray &data, int ms) {
                 if (cb) cb(ok, status, ok ? QByteArray("<ResponseStatus><statusString>OK</statusString></ResponseStatus>")
                                           : soapFault(data).toUtf8(), ms);
             });
        return;
    }

    const QRegularExpressionMatch im = imageRe().match(p);
    if (im.hasMatch()) {
        if (!im.captured(3).isEmpty()) imagingCaps(im.captured(1).toInt(), im.captured(2), cb);
        else if (isGet) imagingGet(im.captured(1).toInt(), im.captured(2), cb);
        else imagingPut(im.captured(1).toInt(), im.captured(2), body, cb);
        return;
    }
    const QRegularExpressionMatch tm = timeRe().match(p);
    if (tm.hasMatch()) {
        if (isGet) timeGet(!tm.captured(1).isEmpty(), cb);
        else timePut(!tm.captured(1).isEmpty(), body, cb);
        return;
    }

    const QRegularExpressionMatch sm = streamRe().match(p);
    if (sm.hasMatch()) {
        if (!sm.captured(2).isEmpty()) streamCaps(sm.captured(1).toInt(), cb);
        else if (isGet) streamGet(sm.captured(1).toInt(), cb);
        else streamPut(sm.captured(1).toInt(), body, cb);
        return;
    }

    fail(404, QStringLiteral("Not available over ONVIF. This setting needs an ISAPI camera entry."));
}

void OnvifDriver::imagingGet(int channel, const QString &kind, HttpCallback cb)
{
    const QString src = m_sourceTokens.value(channel - 1, m_sourceTokens.value(0));
    if (m_imagingUrl.isEmpty() || src.isEmpty()) {
        init();
        if (cb) cb(false, 503, QByteArrayLiteral("ONVIF imaging service not ready yet; try again in a moment"), 0);
        return;
    }
    soap(m_imagingUrl, QStringLiteral("<GetImagingSettings xmlns=\"%1\"><VideoSourceToken>%2</VideoSourceToken></GetImagingSettings>")
                           .arg(QLatin1String(kImagingNs), src), true,
         [kind, cb](bool ok, int status, const QByteArray &data, int ms) {
             if (!ok) { if (cb) cb(false, status, soapFault(data).toUtf8(), ms); return; }
             if (cb) cb(true, 200, imagingToIsapi(kind, QString::fromUtf8(data)).toUtf8(), ms);
         });
}

void OnvifDriver::imagingPut(int channel, const QString &kind, const QByteArray &body, HttpCallback cb)
{
    const QString src = m_sourceTokens.value(channel - 1, m_sourceTokens.value(0));
    if (m_imagingUrl.isEmpty() || src.isEmpty()) {
        init();
        if (cb) cb(false, 503, QByteArrayLiteral("ONVIF imaging service not ready yet; try again in a moment"), 0);
        return;
    }
    const QString x = QString::fromUtf8(body);
    auto el = [](const QString &name, const QString &value) {
        return value.isEmpty() ? QString() : QStringLiteral("<%1 xmlns=\"%2\">%3</%1>").arg(name, QLatin1String(kSchemaNs), value);
    };
    // ImagingSettings20 children must follow the schema order.
    QString settings;
    if (kind == QLatin1String("color")) {
        settings = el(QStringLiteral("Brightness"), Xml::tagText(x, QStringLiteral("brightnessLevel")))
                 + el(QStringLiteral("ColorSaturation"), Xml::tagText(x, QStringLiteral("saturationLevel")))
                 + el(QStringLiteral("Contrast"), Xml::tagText(x, QStringLiteral("contrastLevel")));
    } else if (kind == QLatin1String("sharpness")) {
        settings = el(QStringLiteral("Sharpness"), Xml::tagText(x, QStringLiteral("SharpnessLevel")));
    } else if (kind == QLatin1String("IrcutFilter")) {
        const QString t = Xml::tagText(x, QStringLiteral("IrcutFilterType")).toLower();
        const QString v = t == QLatin1String("day") ? QStringLiteral("ON") : t == QLatin1String("night") ? QStringLiteral("OFF")
                          : t.isEmpty() ? QString() : QStringLiteral("AUTO");
        settings = el(QStringLiteral("IrCutFilter"), v);
    } else if (kind == QLatin1String("WDR")) {
        const QString t = Xml::tagText(x, QStringLiteral("mode")).toLower();
        if (!t.isEmpty())
            settings = QStringLiteral("<WideDynamicRange xmlns=\"%1\"><Mode>%2</Mode></WideDynamicRange>")
                           .arg(QLatin1String(kSchemaNs), t == QLatin1String("open") ? QStringLiteral("ON") : QStringLiteral("OFF"));
    } else if (kind == QLatin1String("BLC")) {
        const QString t = Xml::tagText(x, QStringLiteral("enabled")).toLower();
        if (!t.isEmpty())
            settings = QStringLiteral("<BacklightCompensation xmlns=\"%1\"><Mode>%2</Mode></BacklightCompensation>")
                           .arg(QLatin1String(kSchemaNs), t == QLatin1String("true") ? QStringLiteral("ON") : QStringLiteral("OFF"));
    } else if (kind == QLatin1String("exposure")) {
        const QString t = Xml::tagText(x, QStringLiteral("ExposureType")).toLower();
        if (!t.isEmpty())
            settings = QStringLiteral("<Exposure xmlns=\"%1\"><Mode>%2</Mode></Exposure>")
                           .arg(QLatin1String(kSchemaNs), t == QLatin1String("auto") ? QStringLiteral("AUTO") : QStringLiteral("MANUAL"));
    } else if (kind == QLatin1String("focusMode")) {
        const QString t = Xml::tagText(x, QStringLiteral("focusStyle")).toUpper();
        if (!t.isEmpty())
            settings = QStringLiteral("<Focus xmlns=\"%1\"><AutoFocusMode>%2</AutoFocusMode></Focus>")
                           .arg(QLatin1String(kSchemaNs), t == QLatin1String("AUTO") ? QStringLiteral("AUTO") : QStringLiteral("MANUAL"));
    } else {   // whiteBalance
        const QString t = Xml::tagText(x, QStringLiteral("WhiteBalanceStyle")).toLower();
        if (!t.isEmpty())
            settings = QStringLiteral("<WhiteBalance xmlns=\"%1\"><Mode>%2</Mode></WhiteBalance>")
                           .arg(QLatin1String(kSchemaNs), t == QLatin1String("manual") ? QStringLiteral("MANUAL") : QStringLiteral("AUTO"));
    }
    if (settings.isEmpty()) {
        if (cb) cb(false, 400, QByteArrayLiteral("Nothing to send for this setting over ONVIF"), 0);
        return;
    }
    soap(m_imagingUrl, QStringLiteral("<SetImagingSettings xmlns=\"%1\"><VideoSourceToken>%2</VideoSourceToken>"
                                      "<ImagingSettings>%3</ImagingSettings><ForcePersistence>true</ForcePersistence></SetImagingSettings>")
                           .arg(QLatin1String(kImagingNs), src, settings), true,
         [cb](bool ok, int status, const QByteArray &data, int ms) {
             if (cb) cb(ok, status, ok ? QByteArray("<ResponseStatus><statusString>OK</statusString></ResponseStatus>")
                                       : soapFault(data).toUtf8(), ms);
         });
}

void OnvifDriver::streamGet(int channelId, HttpCallback cb)
{
    // ISAPI 101/102/103 = first sensor main/sub/third, 201/202/203 = second sensor.
    const QString token = encoderTokenFor(channelId);
    if (m_mediaUrl.isEmpty() || token.isEmpty()) {
        init();
        if (cb) cb(false, 404, QByteArrayLiteral("No ONVIF video encoder configuration for this stream"), 0);
        return;
    }
    soap(m_mediaUrl, QStringLiteral("<GetVideoEncoderConfiguration xmlns=\"%1\"><ConfigurationToken>%2</ConfigurationToken></GetVideoEncoderConfiguration>")
                         .arg(QLatin1String(kMediaNs), token), true,
         [channelId, cb](bool ok, int status, const QByteArray &data, int ms) {
             if (!ok) { if (cb) cb(false, status, soapFault(data).toUtf8(), ms); return; }
             const QString x = QString::fromUtf8(data);
             QString codec = Xml::tagText(x, QStringLiteral("Encoding"));
             if (codec == QLatin1String("H264")) codec = QStringLiteral("H.264");
             else if (codec == QLatin1String("H265")) codec = QStringLiteral("H.265");
             else if (codec == QLatin1String("JPEG")) codec = QStringLiteral("MJPEG");
             bool okFps = false;
             const double fps = Xml::tagText(x, QStringLiteral("FrameRateLimit")).toDouble(&okFps);
             const QString out = QStringLiteral("<StreamingChannel><id>%1</id><Video>%2%3%4<videoQualityControlType>VBR</videoQualityControlType>%5%6%7</Video></StreamingChannel>")
                 .arg(channelId)
                 .arg(tagIf(QStringLiteral("videoCodecType"), codec),
                      tagIf(QStringLiteral("videoResolutionWidth"), Xml::tagText(x, QStringLiteral("Width"))),
                      tagIf(QStringLiteral("videoResolutionHeight"), Xml::tagText(x, QStringLiteral("Height"))),
                      tagIf(QStringLiteral("vbrUpperCap"), Xml::tagText(x, QStringLiteral("BitrateLimit"))),
                      tagIf(QStringLiteral("maxFrameRate"), okFps ? QString::number(qRound(fps * 100)) : QString()),
                      tagIf(QStringLiteral("GovLength"), Xml::tagText(x, QStringLiteral("GovLength"))));
             if (cb) cb(true, 200, out.toUtf8(), ms);
         });
}

QString OnvifDriver::encoderTokenFor(int channelId) const
{
    // ISAPI 101/102 = first sensor main/sub, 201/202 = second sensor main/sub.
    const int sensor = channelId / 100 - 1;
    const int which = channelId % 100;   // 1 main, 2 sub, 3 third
    if (sensor >= 0 && sensor < m_plan.size()) {
        const OnvifStreamPlan &sp = m_plan.at(sensor);
        const int i = which == 1 ? sp.mainIndex : which == 3 ? sp.thirdIndex : sp.subIndex;
        if (i >= 0 && i < m_plist.size()) return m_plist.at(i).encoderToken;
    }
    return {};
}

// Change the encoder settings: read the camera's own configuration, change only the values the
// page sent, write the whole thing back (ONVIF replaces a configuration, it does not patch it).
void OnvifDriver::streamPut(int channelId, const QByteArray &body, HttpCallback cb)
{
    const QString token = encoderTokenFor(channelId);
    if (m_mediaUrl.isEmpty() || token.isEmpty()) {
        init();
        if (cb) cb(false, 404, QByteArrayLiteral("No ONVIF video encoder configuration for this stream"), 0);
        return;
    }
    const QString isapi = QString::fromUtf8(body);
    soap(m_mediaUrl, QStringLiteral("<GetVideoEncoderConfiguration xmlns=\"%1\"><ConfigurationToken>%2</ConfigurationToken></GetVideoEncoderConfiguration>")
                         .arg(QLatin1String(kMediaNs), token), true,
         [this, token, isapi, cb](bool ok, int status, const QByteArray &data, int ms) {
             auto fail = [&](int st, const QString &msg) { if (cb) cb(false, st, msg.toUtf8(), ms); };
             if (!ok) { fail(status, soapFault(data)); return; }
             const QString x = QString::fromUtf8(data);
             const QRegularExpression cfgRe(QStringLiteral("<(?:\\w+:)?Configuration\\b([^>]*)>(.*)</(?:\\w+:)?Configuration>"),
                                            QRegularExpression::DotMatchesEverythingOption);
             const auto m = cfgRe.match(x);
             if (!m.hasMatch()) { fail(502, QStringLiteral("The camera's encoder configuration could not be read back")); return; }
             QString attrs = m.captured(1);
             QString inner = m.captured(2);

             QStringList changed;
             auto change = [&](const QString &onvifTag, const QString &value, const QString &what) {
                 if (value.isEmpty()) return;
                 if (setTagText(inner, onvifTag, value)) changed << what;
             };
             QString codec = Xml::tagText(isapi, QStringLiteral("videoCodecType"));
             if (!codec.isEmpty()) {
                 const QString enc = codec == QLatin1String("H.264") ? QStringLiteral("H264") : codec == QLatin1String("H.265") ? QStringLiteral("H265")
                                     : codec == QLatin1String("MJPEG") ? QStringLiteral("JPEG") : codec;
                 const QString was = Xml::tagText(inner, QStringLiteral("Encoding"));
                 if (!was.isEmpty() && was != enc) {
                     setTagText(inner, QStringLiteral("Encoding"), enc);
                     // the codec-specific block (GOP length, profile) carries the codec's name
                     if ((was == QLatin1String("H264") || was == QLatin1String("H265")) && (enc == QLatin1String("H264") || enc == QLatin1String("H265")))
                         inner.replace(QRegularExpression(QStringLiteral("(</?(?:\\w+:)?)%1\\b").arg(was)), QStringLiteral("\\1") + enc);
                     changed << QStringLiteral("codec");
                 }
             }
             change(QStringLiteral("Width"), Xml::tagText(isapi, QStringLiteral("videoResolutionWidth")), QStringLiteral("resolution"));
             change(QStringLiteral("Height"), Xml::tagText(isapi, QStringLiteral("videoResolutionHeight")), QStringLiteral("resolution"));
             bool okFps = false;
             const int fps = qRound(Xml::tagText(isapi, QStringLiteral("maxFrameRate")).toDouble(&okFps) / 100.0);
             if (okFps) change(QStringLiteral("FrameRateLimit"), QString::number(fps), QStringLiteral("frame rate"));
             change(QStringLiteral("BitrateLimit"), Xml::tagText(isapi, QStringLiteral("vbrUpperCap")), QStringLiteral("bitrate"));
             change(QStringLiteral("GovLength"), Xml::tagText(isapi, QStringLiteral("GovLength")), QStringLiteral("I-frame interval"));
             if (changed.isEmpty()) { fail(400, QStringLiteral("Nothing in this change can be sent over ONVIF")); return; }

             // The answer's children use a namespace prefix declared higher up; declare it here.
             const QRegularExpressionMatch pf = QRegularExpression(QStringLiteral("<(\\w+):Encoding>")).match(inner);
             if (pf.hasMatch() && !attrs.contains(QStringLiteral("xmlns:") + pf.captured(1)))
                 attrs += QStringLiteral(" xmlns:%1=\"%2\"").arg(pf.captured(1), QLatin1String(kSchemaNs));
             soap(m_mediaUrl, QStringLiteral("<SetVideoEncoderConfiguration xmlns=\"%1\"><Configuration%2>%3</Configuration>"
                                             "<ForcePersistence>true</ForcePersistence></SetVideoEncoderConfiguration>")
                                  .arg(QLatin1String(kMediaNs), attrs, inner), true,
                  [cb](bool ok2, int st2, const QByteArray &d2, int ms2) {
                      if (cb) cb(ok2, st2, ok2 ? QByteArray("<ResponseStatus><statusString>OK</statusString></ResponseStatus>") : soapFault(d2).toUtf8(), ms2);
                  });
         });
}

// What the camera will accept for this stream, in the same <tag opt="a,b"/> / min,max shape
// the Hikvision capabilities documents use, so the Settings page lists only valid values.
void OnvifDriver::streamCaps(int channelId, HttpCallback cb)
{
    const QString token = encoderTokenFor(channelId);
    if (m_mediaUrl.isEmpty() || token.isEmpty()) {
        if (cb) cb(false, 404, QByteArrayLiteral("No ONVIF video encoder configuration for this stream"), 0);
        return;
    }
    soap(m_mediaUrl, QStringLiteral("<GetVideoEncoderConfigurationOptions xmlns=\"%1\"><ConfigurationToken>%2</ConfigurationToken></GetVideoEncoderConfigurationOptions>")
                         .arg(QLatin1String(kMediaNs), token), true,
         [cb](bool ok, int status, const QByteArray &data, int ms) {
             if (!ok) { if (cb) cb(false, status, soapFault(data).toUtf8(), ms); return; }
             const QString x = QString::fromUtf8(data);
             QStringList codecs, resolutions;
             if (!sectionXml(x, QStringLiteral("H265")).isEmpty()) codecs << QStringLiteral("H.265");
             if (!sectionXml(x, QStringLiteral("H264")).isEmpty()) codecs << QStringLiteral("H.264");
             if (!sectionXml(x, QStringLiteral("JPEG")).isEmpty()) codecs << QStringLiteral("MJPEG");
             // every resolution any codec offers, biggest first
             QList<QPair<qint64, QString>> sized;
             const QRegularExpression res(QStringLiteral("<(?:\\w+:)?ResolutionsAvailable>\\s*<(?:\\w+:)?Width>(\\d+)</(?:\\w+:)?Width>\\s*<(?:\\w+:)?Height>(\\d+)</(?:\\w+:)?Height>"));
             auto it = res.globalMatch(x);
             while (it.hasNext()) {
                 const auto m = it.next();
                 const QString label = m.captured(1) + QStringLiteral("×") + m.captured(2);
                 const qint64 area = m.captured(1).toLongLong() * m.captured(2).toLongLong();
                 bool dup = false;
                 for (const auto &p : sized) if (p.second == label) dup = true;
                 if (!dup) sized.append({area, label});
             }
             std::sort(sized.begin(), sized.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
             for (const auto &p : sized) resolutions << p.second;

             // frame rates: the usual ones inside the camera's range, in hundredths like the ISAPI value
             QStringList fps;
             const QString fr = sectionXml(x, QStringLiteral("FrameRateRange"));
             const int fmin = Xml::tagText(fr, QStringLiteral("Min")).toInt(), fmax = Xml::tagText(fr, QStringLiteral("Max")).toInt();
             if (fmax > 0)
                 for (int f : {60, 50, 30, 25, 20, 15, 12, 10, 8, 5, 1})
                     if (f >= fmin && f <= fmax) fps << QString::number(f * 100);
             const QString gov = sectionXml(x, QStringLiteral("GovLengthRange"));
             const QString br = sectionXml(x, QStringLiteral("BitrateRange"));

             QString out = QStringLiteral("<StreamingChannel><Video>");
             if (!codecs.isEmpty()) out += QStringLiteral("<videoCodecType opt=\"%1\"/>").arg(optList(codecs));
             if (!resolutions.isEmpty()) out += QStringLiteral("<videoResolution opt=\"%1\"/>").arg(optList(resolutions));
             if (!fps.isEmpty()) out += QStringLiteral("<maxFrameRate opt=\"%1\"/>").arg(optList(fps));
             if (!gov.isEmpty()) out += QStringLiteral("<GovLength min=\"%1\" max=\"%2\"/>").arg(Xml::tagText(gov, QStringLiteral("Min")), Xml::tagText(gov, QStringLiteral("Max")));
             if (!br.isEmpty()) out += QStringLiteral("<vbrUpperCap min=\"%1\" max=\"%2\"/>").arg(Xml::tagText(br, QStringLiteral("Min")), Xml::tagText(br, QStringLiteral("Max")));
             out += QStringLiteral("</Video></StreamingChannel>");
             if (cb) cb(true, 200, out.toUtf8(), ms);
         });
}

void OnvifDriver::imagingCaps(int channel, const QString &kind, HttpCallback cb)
{
    const QString src = m_sourceTokens.value(channel - 1, m_sourceTokens.value(0));
    if (m_imagingUrl.isEmpty() || src.isEmpty()) {
        if (cb) cb(false, 503, QByteArrayLiteral("ONVIF imaging service not ready yet"), 0);
        return;
    }
    soap(m_imagingUrl, QStringLiteral("<GetOptions xmlns=\"%1\"><VideoSourceToken>%2</VideoSourceToken></GetOptions>")
                           .arg(QLatin1String(kImagingNs), src), true,
         [kind, cb](bool ok, int status, const QByteArray &data, int ms) {
             if (!ok) { if (cb) cb(false, status, soapFault(data).toUtf8(), ms); return; }
             const QString x = QString::fromUtf8(data);
             auto map = [](const QStringList &in, const QHash<QString, QString> &to) {
                 QStringList out;
                 for (const QString &s : in) if (to.contains(s.toUpper())) out << to.value(s.toUpper());
                 return out;
             };
             auto range = [&](const QString &section, const QString &tag) {
                 const QString r = sectionXml(x, section);
                 return QStringLiteral("<%1 min=\"%2\" max=\"%3\"/>").arg(tag, wholeNumber(Xml::tagText(r, QStringLiteral("Min"))), wholeNumber(Xml::tagText(r, QStringLiteral("Max"))));
             };
             QString out;
             if (kind == QLatin1String("color"))
                 out = QStringLiteral("<Color>%1%2%3</Color>").arg(range(QStringLiteral("Brightness"), QStringLiteral("brightnessLevel")),
                                                                     range(QStringLiteral("Contrast"), QStringLiteral("contrastLevel")),
                                                                     range(QStringLiteral("ColorSaturation"), QStringLiteral("saturationLevel")));
             else if (kind == QLatin1String("sharpness"))
                 out = QStringLiteral("<Sharpness>%1</Sharpness>").arg(range(QStringLiteral("Sharpness"), QStringLiteral("SharpnessLevel")));
             else if (kind == QLatin1String("IrcutFilter"))
                 out = QStringLiteral("<IrcutFilter><IrcutFilterType opt=\"%1\"/></IrcutFilter>")
                           .arg(optList(map(allTags(x, QStringLiteral("IrCutFilterModes")), {{"ON", "day"}, {"OFF", "night"}, {"AUTO", "auto"}})));
             else if (kind == QLatin1String("WDR"))
                 out = QStringLiteral("<WDR><mode opt=\"%1\"/></WDR>")
                           .arg(optList(map(allTags(sectionXml(x, QStringLiteral("WideDynamicRange")), QStringLiteral("Mode")), {{"ON", "open"}, {"OFF", "close"}})));
             else if (kind == QLatin1String("whiteBalance"))
                 out = QStringLiteral("<WhiteBalance><WhiteBalanceStyle opt=\"%1\"/></WhiteBalance>")
                           .arg(optList(map(allTags(sectionXml(x, QStringLiteral("WhiteBalance")), QStringLiteral("Mode")), {{"AUTO", "auto"}, {"MANUAL", "manual"}})));
             else if (kind == QLatin1String("BLC"))
                 out = QStringLiteral("<BLC><enabled opt=\"%1\"/></BLC>")
                           .arg(optList(map(allTags(sectionXml(x, QStringLiteral("BacklightCompensation")), QStringLiteral("Mode")), {{"ON", "true"}, {"OFF", "false"}})));
             else if (kind == QLatin1String("exposure"))
                 out = QStringLiteral("<Exposure><ExposureType opt=\"%1\"/></Exposure>")
                           .arg(optList(map(allTags(sectionXml(x, QStringLiteral("Exposure")), QStringLiteral("Mode")), {{"AUTO", "auto"}, {"MANUAL", "manual"}})));
             else   // focusMode
                 out = QStringLiteral("<FocusConfiguration><focusStyle opt=\"%1\"/></FocusConfiguration>")
                           .arg(optList(map(allTags(sectionXml(x, QStringLiteral("Focus")), QStringLiteral("AutoFocusModes")), {{"AUTO", "AUTO"}, {"MANUAL", "MANUAL"}})));
             if (cb) cb(true, 200, out.toUtf8(), ms);
         });
}

// Clock: time mode (NTP or manual) and the NTP server, through the Device service.
void OnvifDriver::timeGet(bool ntp, HttpCallback cb)
{
    if (ntp) {
        soap(deviceUrl(), QStringLiteral("<GetNTP xmlns=\"%1\"/>").arg(QLatin1String(kDeviceNs)), true,
             [cb](bool ok, int status, const QByteArray &data, int ms) {
                 if (!ok) { if (cb) cb(false, status, soapFault(data).toUtf8(), ms); return; }
                 const QString x = QString::fromUtf8(data);
                 const QString manual = sectionXml(x, QStringLiteral("NTPManual"));
                 QString host = Xml::tagText(manual, QStringLiteral("IPv4Address"));
                 if (host.isEmpty()) host = Xml::tagText(manual, QStringLiteral("DNSname"));
                 if (cb) cb(true, 200, QStringLiteral("<NTPServer>%1</NTPServer>").arg(tagIf(QStringLiteral("ipAddress"), Xml::escape(host))).toUtf8(), ms);
             });
        return;
    }
    soap(deviceUrl(), QStringLiteral("<GetSystemDateAndTime xmlns=\"%1\"/>").arg(QLatin1String(kDeviceNs)), true,
         [cb](bool ok, int status, const QByteArray &data, int ms) {
             if (!ok) { if (cb) cb(false, status, soapFault(data).toUtf8(), ms); return; }
             const QString type = Xml::tagText(QString::fromUtf8(data), QStringLiteral("DateTimeType"));
             const QString mode = type.compare(QLatin1String("NTP"), Qt::CaseInsensitive) == 0 ? QStringLiteral("NTP") : QStringLiteral("manual");
             if (cb) cb(true, 200, QStringLiteral("<Time>%1</Time>").arg(tagIf(QStringLiteral("timeMode"), type.isEmpty() ? QString() : mode)).toUtf8(), ms);
         });
}

void OnvifDriver::timePut(bool ntp, const QByteArray &body, HttpCallback cb)
{
    const QString x = QString::fromUtf8(body);
    if (ntp) {
        const QString host = Xml::tagText(x, QStringLiteral("ipAddress"));
        if (host.isEmpty()) { if (cb) cb(false, 400, QByteArrayLiteral("No NTP server in this change"), 0); return; }
        const bool isIp = !QHostAddress(host).isNull();
        const QString addr = isIp ? QStringLiteral("<Type xmlns=\"%1\">IPv4</Type><IPv4Address xmlns=\"%1\">%2</IPv4Address>").arg(QLatin1String(kSchemaNs), Xml::escape(host))
                                  : QStringLiteral("<Type xmlns=\"%1\">DNS</Type><DNSname xmlns=\"%1\">%2</DNSname>").arg(QLatin1String(kSchemaNs), Xml::escape(host));
        soap(deviceUrl(), QStringLiteral("<SetNTP xmlns=\"%1\"><FromDHCP>false</FromDHCP><NTPManual>%2</NTPManual></SetNTP>").arg(QLatin1String(kDeviceNs), addr), true,
             [cb](bool ok, int status, const QByteArray &data, int ms) {
                 if (cb) cb(ok, status, ok ? QByteArray("<ResponseStatus><statusString>OK</statusString></ResponseStatus>") : soapFault(data).toUtf8(), ms);
             });
        return;
    }
    const bool manual = Xml::tagText(x, QStringLiteral("timeMode")).compare(QLatin1String("manual"), Qt::CaseInsensitive) == 0;
    QString utc;
    if (manual) {   // manual: set the camera to this laptop's clock
        const QDateTime now = QDateTime::currentDateTimeUtc();
        utc = QStringLiteral("<UTCDateTime><Time xmlns=\"%1\"><Hour>%2</Hour><Minute>%3</Minute><Second>%4</Second></Time>"
                             "<Date xmlns=\"%1\"><Year>%5</Year><Month>%6</Month><Day>%7</Day></Date></UTCDateTime>")
                  .arg(QLatin1String(kSchemaNs)).arg(now.time().hour()).arg(now.time().minute()).arg(now.time().second())
                  .arg(now.date().year()).arg(now.date().month()).arg(now.date().day());
    }
    soap(deviceUrl(), QStringLiteral("<SetSystemDateAndTime xmlns=\"%1\"><DateTimeType>%2</DateTimeType><DaylightSavings>false</DaylightSavings>%3</SetSystemDateAndTime>")
                          .arg(QLatin1String(kDeviceNs), manual ? QStringLiteral("Manual") : QStringLiteral("NTP"), utc), true,
         [cb](bool ok, int status, const QByteArray &data, int ms) {
             if (cb) cb(ok, status, ok ? QByteArray("<ResponseStatus><statusString>OK</statusString></ResponseStatus>") : soapFault(data).toUtf8(), ms);
         });
}
