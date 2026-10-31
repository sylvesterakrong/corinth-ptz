#include "OnvifDriver.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>

namespace {
const char *kPtzNs = "http://www.onvif.org/ver20/ptz/wsdl";
const char *kMediaNs = "http://www.onvif.org/ver10/media/wsdl";
const char *kDeviceNs = "http://www.onvif.org/ver10/device/wsdl";
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
        if (m_mediaUrl.isEmpty()) { m_initRunning = false; return; }

        const QString gp = QStringLiteral("<GetProfiles xmlns=\"%1\"/>").arg(QLatin1String(kMediaNs));
        soap(m_mediaUrl, gp, true, [this](bool ok2, int, const QByteArray &data2, int) {
            if (!ok2) { m_initRunning = false; return; }
            const QString xml2 = QString::fromUtf8(data2);
            m_profiles.clear();
            const QRegularExpression re(QStringLiteral("<(?:\\w+:)?Profiles[^>]*\\stoken=\"([^\"]+)\""));
            auto it = re.globalMatch(xml2);
            while (it.hasNext())
                m_profiles << it.next().captured(1);
            m_initRunning = false;

            // Stream URIs: profile 0 = main, profile 1 = sub (vendor convention).
            for (int i = 0; i < qMin(2, int(m_profiles.size())); ++i) {
                const QString gs = QStringLiteral(
                    "<GetStreamUri xmlns=\"%1\"><StreamSetup>"
                    "<Stream xmlns=\"%2\">RTP-Unicast</Stream>"
                    "<Transport xmlns=\"%2\"><Protocol>RTSP</Protocol></Transport>"
                    "</StreamSetup><ProfileToken>%3</ProfileToken></GetStreamUri>")
                    .arg(QLatin1String(kMediaNs), QLatin1String(kSchemaNs), m_profiles.at(i));
                soap(m_mediaUrl, gs, true, [this, i](bool ok3, int, const QByteArray &d3, int) {
                    if (!ok3) return;
                    const QString uri = unescapeXml(Xml::tagText(QString::fromUtf8(d3), QStringLiteral("Uri")));
                    if (i == 0) m_mainUri = uri; else m_subUri = uri;
                    emit streamUrlsChanged();
                });
            }
        });
    });
}

void OnvifDriver::ptzCommand(const QString &body, const QString &label)
{
    if (m_ptzUrl.isEmpty() || m_profiles.isEmpty()) {
        init();
        emit commandDone(label, false, 0, QStringLiteral("ONVIF not ready (no PTZ service or profile yet)"));
        return;
    }
    soap(m_ptzUrl, body, true, [this, label](bool ok, int status, const QByteArray &data, int ms) {
        emit commandDone(label, ok, ms, ok ? QString() : QStringLiteral("HTTP %1 %2").arg(status).arg(soapFault(data)));
    });
}

void OnvifDriver::continuousMove(int pan, int tilt, int zoom, const QString &label)
{
    const QString tok = m_profiles.value(0);
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

void OnvifDriver::gotoPreset(int id, const QString &label)
{
    ptzCommand(QStringLiteral("<GotoPreset xmlns=\"%1\"><ProfileToken>%2</ProfileToken>"
                              "<PresetToken>%3</PresetToken></GotoPreset>")
                   .arg(QLatin1String(kPtzNs), m_profiles.value(0)).arg(id), label);
}

void OnvifDriver::setPreset(int id, const QString &name, const QString &label)
{
    ptzCommand(QStringLiteral("<SetPreset xmlns=\"%1\"><ProfileToken>%2</ProfileToken>"
                              "<PresetName>%3</PresetName><PresetToken>%4</PresetToken></SetPreset>")
                   .arg(QLatin1String(kPtzNs), m_profiles.value(0), Xml::escape(name)).arg(id), label);
}

void OnvifDriver::gotoHome(const QString &label)
{
    ptzCommand(QStringLiteral("<GotoHomePosition xmlns=\"%1\"><ProfileToken>%2</ProfileToken></GotoHomePosition>")
                   .arg(QLatin1String(kPtzNs), m_profiles.value(0)), label);
}

void OnvifDriver::setHome(const QString &label)
{
    ptzCommand(QStringLiteral("<SetHomePosition xmlns=\"%1\"><ProfileToken>%2</ProfileToken></SetHomePosition>")
                   .arg(QLatin1String(kPtzNs), m_profiles.value(0)), label);
}

void OnvifDriver::focus(int, const QString &label)
{
    emit commandDone(label, false, 0, QStringLiteral("Focus over ONVIF Imaging is not in this build; use ISAPI"));
}

void OnvifDriver::iris(int, const QString &label)
{
    emit commandDone(label, false, 0, QStringLiteral("Iris over ONVIF is not in this build; use ISAPI"));
}

void OnvifDriver::onePushFocus(const QString &label)
{
    emit commandDone(label, false, 0, QStringLiteral("One-push focus over ONVIF is not in this build; use ISAPI"));
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
                   .arg(QLatin1String(kPtzNs), m_profiles.value(0), cmd), label);
}

void OnvifDriver::queryStatus()
{
    if (m_ptzUrl.isEmpty() || m_profiles.isEmpty() || m_statusInFlight)
        return;
    m_statusInFlight = true;
    soap(m_ptzUrl, QStringLiteral("<GetStatus xmlns=\"%1\"><ProfileToken>%2</ProfileToken></GetStatus>")
                       .arg(QLatin1String(kPtzNs), m_profiles.value(0)), true,
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
             emit statusUpdated((x + 1.0) * 180.0, y * 90.0, -1.0);
         });
}

void OnvifDriver::ping()
{
    // GetSystemDateAndTime is allowed without authentication: a clean RTT probe.
    soap(deviceUrl(), QStringLiteral("<GetSystemDateAndTime xmlns=\"%1\"/>").arg(QLatin1String(kDeviceNs)), false,
         [this](bool ok, int, const QByteArray &, int ms) {
             emit pingResult(ok, ms);
             if (ok && m_profiles.isEmpty())
                 init();
         });
}

QString OnvifDriver::streamUrl(bool mainStream, int) const
{
    const QString custom = mainStream ? m_cfg.mainUrl : m_cfg.subUrl;
    if (!custom.isEmpty())
        return rtspWithCredentials(custom);
    const QString uri = mainStream ? m_mainUri : (m_subUri.isEmpty() ? m_mainUri : m_subUri);
    return rtspWithCredentials(uri);
}
