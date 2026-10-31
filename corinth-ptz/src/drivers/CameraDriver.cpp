#include "CameraDriver.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QAuthenticator>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QUrl>

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
    m[QStringLiteral("mainUrl")] = mainUrl;
    m[QStringLiteral("subUrl")] = subUrl;
    m[QStringLiteral("shown")] = shown;
    return m;
}

CameraDriver::CameraDriver(const CameraConfig &cfg, QObject *parent)
    : QObject(parent), m_cfg(cfg), m_nam(new QNetworkAccessManager(this))
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
}

CameraDriver::~CameraDriver() = default;

QString CameraDriver::baseUrl() const
{
    return QStringLiteral("http://%1:%2").arg(m_cfg.ip).arg(m_cfg.httpPort);
}

void CameraDriver::request(const QString &, const QString &, const QByteArray &, HttpCallback cb)
{
    if (cb) cb(false, 0, QByteArrayLiteral("This camera protocol does not support raw ISAPI requests"), 0);
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

    connect(reply, &QNetworkReply::finished, this, [reply, timer, cb]() {
        const int ms = int(timer->elapsed());
        delete timer;
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
