// Camera discovery against fake devices: a WS-Discovery responder (on the real LAN
// adapter the scan uses) and HTTP servers that behave like a Hikvision camera, a
// vendor that answers 401 to everything, and a device without ISAPI.
// Skips (passes) on a machine with no usable LAN adapter.
#include "Discovery.h"
#include "NetUtil.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkDatagram>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUdpSocket>
#include <functional>
#include <memory>

static int g_fail = 0;

static void check(bool ok, const QString &what)
{
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << what;
    if (!ok) ++g_fail;
}

static bool waitFor(const std::function<bool()> &cond, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

// Answers every HTTP request with a fixed status and optional extra header line.
class FakeHttp : public QObject
{
public:
    FakeHttp(const QString &ip, quint16 port, int status, const QByteArray &headers)
        : m_status(status), m_headers(headers)
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *s = m_server.nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, this, [this, s]() {
                    s->readAll();
                    s->write("HTTP/1.1 " + QByteArray::number(m_status) + " X\r\n" + m_headers
                             + "Content-Length: 0\r\nConnection: close\r\n\r\n");
                    s->disconnectFromHost();
                });
        });
        m_ok = m_server.listen(QHostAddress(ip), port);
    }
    bool ok() const { return m_ok; }
private:
    QTcpServer m_server;
    int m_status;
    QByteArray m_headers;
    bool m_ok = false;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const auto adapters = NetUtil::lanAdapters();
    if (adapters.isEmpty()) {
        qInfo().noquote() << "SKIP no usable LAN adapter on this machine";
        return 0;
    }
    for (const auto &a : adapters) qInfo().noquote() << "adapter:" << a.label();
    check(true, QStringLiteral("scan uses %1 LAN adapter(s)").arg(adapters.size()));
    for (const auto &a : adapters)
        check(!NetUtil::excludedAdapterName(a.iface.humanReadableName()), QStringLiteral("adapter %1 is not a VPN or virtual one").arg(a.iface.humanReadableName()));

    const quint16 port = 18080;
    FakeHttp hik(QStringLiteral("127.0.0.1"), port, 401, "Server: App-webs/\r\nWWW-Authenticate: Digest realm=\"DS-2DE\"\r\n");
    FakeHttp generic(QStringLiteral("127.0.0.2"), port, 401, "WWW-Authenticate: Basic realm=\"x\"\r\n");
    FakeHttp none(QStringLiteral("127.0.0.3"), port, 404, "");
    FakeHttp named(QStringLiteral("127.0.0.4"), port, 401, "WWW-Authenticate: Basic realm=\"x\"\r\n");
    check(hik.ok() && generic.ok() && none.ok() && named.ok(), QStringLiteral("fake devices are listening"));

    // The WS-Discovery responder: joins the group on every adapter and answers unicast.
    QUdpSocket responder;
    responder.bind(QHostAddress::AnyIPv4, 3702, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    for (const auto &a : adapters)
        responder.joinMulticastGroup(QHostAddress(QStringLiteral("239.255.255.250")), a.iface);
    int probes = 0;
    QObject::connect(&responder, &QUdpSocket::readyRead, [&]() {
        while (responder.hasPendingDatagrams()) {
            const QNetworkDatagram dg = responder.receiveDatagram();
            if (!QString::fromUtf8(dg.data()).contains(QLatin1String("Probe"))) continue;
            ++probes;
            struct Dev { const char *ip, *scopes; };
            const Dev devs[] = {
                {"127.0.0.1", "onvif://www.onvif.org/name/ACME onvif://www.onvif.org/hardware/CJ3000"},
                {"127.0.0.2", "onvif://www.onvif.org/name/Other onvif://www.onvif.org/hardware/Box1"},
                {"127.0.0.3", "onvif://www.onvif.org/name/Plain onvif://www.onvif.org/hardware/Cam3"},
                {"127.0.0.4", "onvif://www.onvif.org/name/Hikvision onvif://www.onvif.org/hardware/DS-2DE7"}};
            for (const Dev &d : devs) {
                const QByteArray xml = QStringLiteral(
                    "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
                    "xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\"><s:Body><d:ProbeMatches><d:ProbeMatch>"
                    "<d:Scopes>%1</d:Scopes><d:XAddrs>http://%2:%3/onvif/device_service</d:XAddrs>"
                    "</d:ProbeMatch></d:ProbeMatches></s:Body></s:Envelope>").arg(QString::fromLatin1(d.scopes), QString::fromLatin1(d.ip)).arg(port).toUtf8();
                responder.writeDatagram(xml, dg.senderAddress(), quint16(dg.senderPort()));
            }
        }
    });

    Discovery disc;
    int finishedCount = -1;
    QObject::connect(&disc, &Discovery::finished, [&](int n) { finishedCount = n; });
    disc.scan();
    check(disc.scanning(), QStringLiteral("scan starts"));
    const bool done = waitFor([&] { return finishedCount >= 0; }, 15000);
    check(done, QStringLiteral("scan finishes (%1 found, %2 probes heard)").arg(finishedCount).arg(probes));
    check(probes >= adapters.size(), QStringLiteral("a probe went out through each adapter (%1 probes for %2 adapters)").arg(probes).arg(adapters.size()));
    check(!disc.adapters().isEmpty() && !disc.adapters().join(QLatin1Char(' ')).toLower().contains(QLatin1String("tailscale")),
          QStringLiteral("the scan reports the adapters it used: %1").arg(disc.adapters().join(QStringLiteral(" | "))));

    auto protos = [&](const QString &ip) {
        for (const QVariant &v : disc.results())
            if (v.toMap().value(QStringLiteral("ip")).toString() == ip) return v.toMap().value(QStringLiteral("protos")).toString();
        return QStringLiteral("(not found)");
    };
    check(disc.results().size() == 4, QStringLiteral("all four devices found, once each (%1)").arg(disc.results().size()));
    check(protos(QStringLiteral("127.0.0.1")) == QLatin1String("ONVIF, ISAPI"), QStringLiteral("Hikvision-style 401 with its Server header is ISAPI (%1)").arg(protos(QStringLiteral("127.0.0.1"))));
    check(protos(QStringLiteral("127.0.0.2")) == QLatin1String("ONVIF"), QStringLiteral("a vendor that answers 401 to everything stays ONVIF (%1)").arg(protos(QStringLiteral("127.0.0.2"))));
    check(protos(QStringLiteral("127.0.0.3")) == QLatin1String("ONVIF"), QStringLiteral("404 on the ISAPI path stays ONVIF (%1)").arg(protos(QStringLiteral("127.0.0.3"))));
    check(protos(QStringLiteral("127.0.0.4")) == QLatin1String("ONVIF, ISAPI"), QStringLiteral("401 plus Hikvision in the ONVIF scopes is ISAPI (%1)").arg(protos(QStringLiteral("127.0.0.4"))));
    int port0 = 0;
    for (const QVariant &v : disc.results()) port0 = v.toMap().value(QStringLiteral("port")).toInt();
    check(port0 == port, QStringLiteral("the HTTP port from the camera's address is kept (%1)").arg(port0));

    qInfo().noquote() << "DISCOVERY TEST failures:" << g_fail;
    return g_fail;
}
