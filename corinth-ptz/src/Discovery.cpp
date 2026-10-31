#include "Discovery.h"

#include <QNetworkDatagram>
#include <QRegularExpression>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>
#include <QUuid>

Discovery::Discovery(QObject *parent)
    : QObject(parent), m_sock(new QUdpSocket(this)), m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Discovery::finish);
    connect(m_sock, &QUdpSocket::readyRead, this, &Discovery::onReadyRead);
}

void Discovery::scan()
{
    if (m_scanning) return;
    m_results.clear();
    emit resultsChanged();

    if (m_sock->state() != QAbstractSocket::BoundState)
        m_sock->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

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
    emit scanningChanged();
    // Send twice: UDP multicast is unreliable.
    m_sock->writeDatagram(probe, QHostAddress(QStringLiteral("239.255.255.250")), 3702);
    QTimer::singleShot(400, this, [this, probe]() {
        m_sock->writeDatagram(probe, QHostAddress(QStringLiteral("239.255.255.250")), 3702);
    });
    m_timer->start(3000);
}

void Discovery::onReadyRead()
{
    while (m_sock->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_sock->receiveDatagram();
        const QString xml = QString::fromUtf8(dg.data());
        if (!xml.contains(QLatin1String("ProbeMatch")))
            continue;

        const QRegularExpression xaddrRe(QStringLiteral("<(?:\\w+:)?XAddrs>([^<]+)<"));
        const QString xaddrs = xaddrRe.match(xml).captured(1).trimmed();
        const QString first = xaddrs.split(QLatin1Char(' '), Qt::SkipEmptyParts).value(0);
        QString ip = QUrl(first).host();
        if (ip.isEmpty())
            ip = dg.senderAddress().toString().remove(QStringLiteral("::ffff:"));

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
        r[QStringLiteral("xaddr")] = first;
        r[QStringLiteral("model")] = hardware.isEmpty() ? QStringLiteral("ONVIF camera") : hardware;
        r[QStringLiteral("vendor")] = name.isEmpty() ? QStringLiteral("ONVIF device") : name;
        r[QStringLiteral("protos")] = QStringLiteral("ONVIF");
        m_results.append(r);
        emit resultsChanged();
    }
}

void Discovery::finish()
{
    m_scanning = false;
    m_hasScanned = true;
    emit scanningChanged();
    emit resultsChanged();
    emit finished(int(m_results.size()));
}
