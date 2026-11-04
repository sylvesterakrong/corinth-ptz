#include "NetUtil.h"

#include <QNetworkAddressEntry>

namespace NetUtil {

bool excludedAdapterName(const QString &name)
{
    static const char *const skip[] = {
        "tailscale", "zerotier", "vmware", "virtualbox", "vethernet", "hyper-v", "wsl",
        "docker", "bluetooth", "loopback", "pseudo", "vpn", "wireguard", "npcap"};
    const QString n = name.toLower();
    for (const char *s : skip)
        if (n.contains(QLatin1String(s)))
            return true;
    return false;
}

QList<Adapter> lanAdapters()
{
    QList<Adapter> out;
    const auto all = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &i : all) {
        const auto f = i.flags();
        if (!(f & QNetworkInterface::IsUp) || !(f & QNetworkInterface::IsRunning)
            || !(f & QNetworkInterface::CanMulticast) || (f & QNetworkInterface::IsLoopBack))
            continue;
        if (excludedAdapterName(i.humanReadableName()) || excludedAdapterName(i.name()))
            continue;
        for (const QNetworkAddressEntry &e : i.addressEntries()) {
            const QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            if (a.isInSubnet(QHostAddress(QStringLiteral("100.64.0.0")), 10))
                continue;   // Tailscale / carrier-grade NAT range
            out.append({i, a});
            break;
        }
    }
    return out;
}

}
