#include "Discovery.h"
#include "NetUtil.h"

#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QUuid>

Discovery::Discovery(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_timer(new QTimer(this)), m_cap(new QTimer(this))
{
    m_timer->setSingleShot(true);
    m_cap->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this]() { m_collecting = false; maybeFinish(); });
    connect(m_cap, &QTimer::timeout, this, &Discovery::finish);
}

void Discovery::scan()
{
    if (m_scanning) return;
    m_results.clear();
    m_adapters.clear();
    qDeleteAll(m_socks);
    m_socks.clear();
    emit resultsChanged();

    // One socket per LAN adapter, bound to that adapter's address, so the probe
    // really leaves through it. Falls back to the default route if none qualify.
    auto addSocket = [this](const QHostAddress &bindTo, const QNetworkInterface &iface) {
        auto *s = new QUdpSocket(this);
        if (!s->bind(bindTo, 0)) {
            delete s;
            return false;
        }
        if (iface.isValid())
            s->setMulticastInterface(iface);
        connect(s, &QUdpSocket::readyRead, this, [this, s]() { onReadyRead(s); });
        m_socks << s;
        return true;
    };
    const QList<NetUtil::Adapter> adapters = NetUtil::lanAdapters();
    for (const NetUtil::Adapter &a : adapters)
        if (addSocket(a.addr, a.iface))
            m_adapters << a.label();
    if (m_socks.isEmpty() && addSocket(QHostAddress::AnyIPv4, QNetworkInterface()))
        m_adapters << QStringLiteral("default network route");

    const QString msgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QByteArray probe = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<e:Envelope xmlns:e=\"http://www.w3.org/2003/05/soap-envelope\" "
        "xmlns:w=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
        "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" "
        "xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
        "<e:Header><w:MessageID>uuid:%1</w:MessageID>"
        "<w:To e:mustUnderstand=\"true\">urn:schemas-xmlsoap-org:ws:2005:04:discovery</w:To>"
        "<w:Action e:mustUnderstand=\"true\">http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</w:Action>"
        "</e:Header><e:Body><d:Probe><d:Types>dn:NetworkVideoTransmitter</d:Types></d:Probe></e:Body></e:Envelope>")
        .arg(msgId).toUtf8();

    m_scanning = true;
    m_collecting = true;
    m_pending = 0;
    ++m_scanId;
    emit scanningChanged();
    // Send twice: UDP multicast is unreliable.
    const QHostAddress group(QStringLiteral("239.255.255.250"));
    for (QUdpSocket *s : std::as_const(m_socks))
        s->writeDatagram(probe, group, 3702);
    QTimer::singleShot(400, this, [this, probe, group]() {
        for (QUdpSocket *s : std::as_const(m_socks))
            s->writeDatagram(probe, group, 3702);
    });
    m_timer->start(3000);
    m_cap->start(6500);
    emit resultsChanged();   // publishes the adapter list
}

void Discovery::onReadyRead(QUdpSocket *sock)
{
    while (sock->hasPendingDatagrams()) {
        const QNetworkDatagram dg = sock->receiveDatagram();
        const QString xml = QString::fromUtf8(dg.data());
        if (!xml.contains(QLatin1String("ProbeMatch")))
            continue;

        const QRegularExpression xaddrRe(QStringLiteral("<(?:\\w+:)?XAddrs>([^<]+)<"));
        const QString xaddrs = xaddrRe.match(xml).captured(1).trimmed();
        const QString first = xaddrs.split(QLatin1Char(' '), Qt::SkipEmptyParts).value(0);
        QString ip = QUrl(first).host();
        if (ip.isEmpty())
            ip = dg.senderAddress().toString().remove(QStringLiteral("::ffff:"));
        const int port = QUrl(first).port(80);

        const QRegularExpression scopesRe(QStringLiteral("<(?:\\w+:)?Scopes[^>]*>([^<]+)<"));
        const QString scopes = scopesRe.match(xml).captured(1);
        auto scopeValue = [&scopes](const QString &key) {
            const QRegularExpression re(QStringLiteral("onvif://www\\.onvif\\.org/%1/(\\S+)").arg(key));
            return QUrl::fromPercentEncoding(re.match(scopes).captured(1).toUtf8()).replace(QLatin1Char('_'), QLatin1Char(' '));
        };
        const QString name = scopeValue(QStringLiteral("name"));
        const QString hardware = scopeValue(QStringLiteral("hardware"));

        bool dup = false;
        for (const QVariant &v : m_results)
            if (v.toMap().value(QStringLiteral("ip")).toString() == ip) { dup = true; break; }
        if (dup || ip.isEmpty()) continue;

        QVariantMap r;
        r[QStringLiteral("id")] = QStringLiteral("found-") + ip;
        r[QStringLiteral("ip")] = ip;
        r[QStringLiteral("port")] = port;
        r[QStringLiteral("xaddr")] = first;
        r[QStringLiteral("model")] = hardware.isEmpty() ? QStringLiteral("ONVIF camera") : hardware;
        r[QStringLiteral("vendor")] = name.isEmpty() ? QStringLiteral("ONVIF device") : name;
        r[QStringLiteral("protos")] = QStringLiteral("ONVIF");
        m_results.append(r);
        const int index = int(m_results.size()) - 1;
        emit resultsChanged();

        const QString vendorText = (name + QLatin1Char(' ') + hardware).toLower();
        const bool vendorHint = vendorText.contains(QLatin1String("hikvision")) || vendorText.contains(QLatin1String("hik"))
                                || hardware.startsWith(QLatin1String("DS-"), Qt::CaseInsensitive);
        probeIsapi(ip, port, index, vendorHint);
    }
}

// Hikvision-family cameras answer the ISAPI device-info path without a login
// with 401 + a "Server: App-webs/" style header (or 200 when open). Some other
// vendors answer 401 to every path, so a bare 401 only counts when the Server
// header or the ONVIF scopes also point at Hikvision.
void Discovery::probeIsapi(const QString &ip, int httpPort, int resultIndex, bool vendorHint)
{
    QNetworkRequest req(QUrl(QStringLiteral("http://%1:%2/ISAPI/System/deviceInfo").arg(ip).arg(httpPort)));
    req.setTransferTimeout(2000);
    ++m_pending;
    const int scanId = m_scanId;
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, resultIndex, vendorHint, scanId]() {
        if (scanId != m_scanId) {   // answer to an earlier scan
            reply->deleteLater();
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString server = QString::fromLatin1(reply->rawHeader("Server")).toLower();
        reply->deleteLater();
        const bool serverHint = server.contains(QLatin1String("app-webs")) || server.contains(QLatin1String("hikvision"))
                                || server.contains(QLatin1String("dnvrs"));
        const bool isapi = status == 200 || (status == 401 && (serverHint || vendorHint));
        if (isapi && resultIndex < m_results.size()) {
            QVariantMap r = m_results.at(resultIndex).toMap();
            r[QStringLiteral("protos")] = QStringLiteral("ONVIF, ISAPI");
            m_results[resultIndex] = r;
            emit resultsChanged();
        }
        --m_pending;
        maybeFinish();
    });
}

void Discovery::maybeFinish()
{
    if (m_scanning && !m_collecting && m_pending <= 0)
        finish();
}

void Discovery::finish()
{
    if (!m_scanning) return;
    m_timer->stop();
    m_cap->stop();
    m_collecting = false;
    m_scanning = false;
    m_hasScanned = true;
    emit scanningChanged();
    emit resultsChanged();
    emit finished(int(m_results.size()));
}
