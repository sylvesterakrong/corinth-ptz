#include "CameraDriver.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QAuthenticator>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSslError>
#include <QUrl>
#include "TlsPin.h"

CameraConfig CameraConfig::fromMap(const QVariantMap &m)
{
    CameraConfig c;
    c.id = m.value(QStringLiteral("id")).toString();
    c.name = m.value(QStringLiteral("name")).toString();
    c.ip = m.value(QStringLiteral("ip")).toString().trimmed();
    c.proto = m.value(QStringLiteral("proto"), QStringLiteral("ISAPI")).toString();
    c.httpPort = m.value(QStringLiteral("httpPort"), 80).toInt();
    c.rtspPort = m.value(QStringLiteral("rtspPort"), 554).toInt();
    c.user = m.value(QStringLiteral("user")).toString();
    c.pass = m.value(QStringLiteral("pass")).toString();
    c.mainUrl = m.value(QStringLiteral("mainUrl")).toString().trimmed();
    c.subUrl = m.value(QStringLiteral("subUrl")).toString().trimmed();
    c.flvUrl = m.value(QStringLiteral("flvUrl")).toString().trimmed();
    c.thermalMainUrl = m.value(QStringLiteral("thermalMainUrl")).toString().trimmed();
    c.thermalSubUrl = m.value(QStringLiteral("thermalSubUrl")).toString().trimmed();
    c.shown = m.value(QStringLiteral("shown"), true).toBool();
    if (c.httpPort <= 0) c.httpPort = 80;
    if (c.rtspPort <= 0) c.rtspPort = 554;
    return c;
}

QVariantMap CameraConfig::toMap(bool includePassword) const
{
    QVariantMap m;
    m[QStringLiteral("id")] = id;
    m[QStringLiteral("name")] = name;
    m[QStringLiteral("ip")] = ip;
    m[QStringLiteral("proto")] = proto;
    m[QStringLiteral("httpPort")] = httpPort;
    m[QStringLiteral("rtspPort")] = rtspPort;
    m[QStringLiteral("user")] = user;
    if (includePassword)
        m[QStringLiteral("pass")] = pass;
    m[QStringLiteral("hasPassword")] = !pass.isEmpty();
    m[QStringLiteral("authOff")] = noCredentials() && proto != QLatin1String("Demo");
    m[QStringLiteral("mainUrl")] = mainUrl;
    m[QStringLiteral("subUrl")] = subUrl;
    m[QStringLiteral("flvUrl")] = flvUrl;
    m[QStringLiteral("thermalMainUrl")] = thermalMainUrl;
    m[QStringLiteral("thermalSubUrl")] = thermalSubUrl;
    m[QStringLiteral("shown")] = shown;
    return m;
}

CameraDriver::CameraDriver(const CameraConfig &cfg, QObject *parent)
    : QObject(parent), m_cfg(cfg),
      m_profile(CameraProfile::load(cfg.id, cfg.proto == QLatin1String("ISAPI") || cfg.proto == QLatin1String("Demo"))),
      m_nam(new QNetworkAccessManager(this))
{
    // Qt answers both Digest and Basic challenges through this signal.
    connect(m_nam, &QNetworkAccessManager::authenticationRequired, this,
            [this](QNetworkReply *reply, QAuthenticator *auth) {
                // Only try the stored credentials once per request, so a wrong
                // password fails fast instead of looping (and tripping the
                // camera's lockout).
                if (reply->property("corinthAuthTried").toBool())
                    return;
                reply->setProperty("corinthAuthTried", true);
                auth->setUser(m_cfg.user);
                auth->setPassword(m_cfg.pass);
            });

    // A camera's own certificate is not signed by anyone the PC knows. It is accepted only if it
    // is the one pinned for this camera; anything else is refused and the user is asked or warned.
    connect(m_nam, &QNetworkAccessManager::sslErrors, this, [this](QNetworkReply *reply, const QList<QSslError> &) {
        const QString fp = TlsPin::fingerprint(reply->sslConfiguration().peerCertificate());
        if (fp.isEmpty())
            return;
        switch (TlsPin::decide(TlsPin::pinned(pinKey()), fp)) {
        case TlsPin::Decision::Trusted: reply->ignoreSslErrors(); break;
        case TlsPin::Decision::NeedsApproval: emit certificateUntrusted(fp, false); break;
        case TlsPin::Decision::Changed: emit certificateUntrusted(fp, true); break;
        }
    });
}

QString CameraDriver::pinKey() const
{
    return m_cfg.id.isEmpty() ? QStringLiteral("%1:%2").arg(m_cfg.ip).arg(m_cfg.httpPort) : m_cfg.id;
}

CameraDriver::~CameraDriver() = default;

QString CameraDriver::baseUrl() const
{
    return m_useHttps ? QStringLiteral("https://%1:%2").arg(m_cfg.ip).arg(m_httpsPort)
                      : QStringLiteral("http://%1:%2").arg(m_cfg.ip).arg(m_cfg.httpPort);
}

void CameraDriver::request(const QString &, const QString &, const QByteArray &, HttpCallback cb)
{
    if (cb) cb(false, 0, QByteArrayLiteral("This camera protocol does not support raw ISAPI requests"), 0);
}

void CameraDriver::unsupported(const QString &label, const QString &what)
{
    emit commandDone(label, false, 0, what);
}

void CameraDriver::pointAt(double, double, const QString &label)
{
    unsupported(label, QStringLiteral("Click-to-point is not supported by this camera protocol"));
}

void CameraDriver::boxZoom(double, double, double, double, const QString &label)
{
    unsupported(label, QStringLiteral("Box zoom is not supported by this camera protocol"));
}

void CameraDriver::setZoomLevel(double, const QString &label)
{
    unsupported(label, QStringLiteral("Setting the zoom level directly is not supported by this camera protocol"));
}

void CameraDriver::renamePreset(int, const QString &, const QString &label)
{
    unsupported(label, QStringLiteral("Renaming on the camera is not supported by this camera protocol"));
}

void CameraDriver::reloadProfile()
{
    m_profile = CameraProfile::load(m_cfg.id, m_cfg.proto == QLatin1String("ISAPI") || m_cfg.proto == QLatin1String("Demo"));
    emit profileChanged();
}

QString CameraDriver::thermalOverride(bool mainStream) const
{
    return mainStream ? m_cfg.thermalMainUrl : m_cfg.thermalSubUrl;
}

QVariantList CameraDriver::profileAuxItems() const
{
    QVariantList out;
    const QVariantList items = m_profile.list(QStringLiteral("aux"));
    for (const QVariant &v : items) {
        const QString id = v.toMap().value(QStringLiteral("id")).toString();
        QVariantMap m;
        m[QStringLiteral("id")] = id;
        m[QStringLiteral("label")] = v.toMap().value(QStringLiteral("label"), id).toString();
        m[QStringLiteral("supported")] = m_profile.has(QStringLiteral("aux.%1.set").arg(id));
        m[QStringLiteral("readable")] = m_profile.has(QStringLiteral("aux.%1.get").arg(id));
        out << m;
    }
    return out;
}

void CameraDriver::http(const QString &method, const QString &path, const QByteArray &body,
                        const QByteArray &contentType, HttpCallback cb, int timeoutMs)
{
    httpUrl(method, QUrl(baseUrl() + path), body, contentType, std::move(cb), timeoutMs);
}

void CameraDriver::httpUrl(const QString &method, const QUrl &url, const QByteArray &body,
                           const QByteArray &contentType, HttpCallback cb, int timeoutMs)
{
    QNetworkRequest req(url);
    req.setTransferTimeout(timeoutMs);
    // Plain HTTP stays the default; if the camera answers with a redirect to HTTPS, follow it.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!contentType.isEmpty())
        req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);

    auto *timer = new QElapsedTimer();
    timer->start();

    QNetworkReply *reply = nullptr;
    const QByteArray verb = method.toUpper().toLatin1();
    if (verb == "GET")
        reply = m_nam->get(req);
    else if (verb == "PUT")
        reply = m_nam->put(req, body);
    else if (verb == "POST")
        reply = m_nam->post(req, body);
    else if (verb == "DELETE")
        reply = m_nam->deleteResource(req);
    else
        reply = m_nam->sendCustomRequest(req, verb, body);

    connect(reply, &QNetworkReply::finished, this, [this, reply, timer, cb, url]() {
        const int ms = int(timer->elapsed());
        delete timer;
        if (url.scheme() == QLatin1String("http") && reply->url().scheme() == QLatin1String("https") && !m_useHttps) {
            m_useHttps = true;                       // every later request goes straight to HTTPS
            m_httpsPort = reply->url().port(443);
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        QByteArray payload = data;
        if (!ok && payload.isEmpty())
            payload = reply->errorString().toUtf8();
        reply->deleteLater();
        if (cb) cb(ok, status, payload, ms);
    });
}

QString CameraDriver::rtspWithCredentials(const QString &url) const
{
    if (url.isEmpty())
        return url;
    QUrl u(url);
    if (u.userName().isEmpty() && !m_cfg.user.isEmpty()) {
        u.setUserName(m_cfg.user);
        u.setPassword(m_cfg.pass);
    }
    return u.toString(QUrl::FullyEncoded);
}

namespace Xml {

QString tagText(const QString &xml, const QString &tag)
{
    // Matches <tag>value</tag> or <ns:tag ...>value</ns:tag>, first occurrence.
    const QRegularExpression re(
        QStringLiteral("<(?:[A-Za-z0-9_]+:)?%1(?:\\s[^>]*)?>([^<]*)</(?:[A-Za-z0-9_]+:)?%1>")
            .arg(QRegularExpression::escape(tag)));
    const auto m = re.match(xml);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

QString attr(const QString &xml, const QString &tag, const QString &attribute)
{
    const QRegularExpression re(
        QStringLiteral("<(?:[A-Za-z0-9_]+:)?%1\\s[^>]*%2=\"([^\"]*)\"")
            .arg(QRegularExpression::escape(tag), QRegularExpression::escape(attribute)));
    const auto m = re.match(xml);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString escape(const QString &s)
{
    QString r = s;
    r.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    r.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    r.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    r.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return r;
}

} // namespace Xml
