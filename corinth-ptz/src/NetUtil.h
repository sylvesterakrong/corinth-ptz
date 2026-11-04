#pragma once

#include <QHostAddress>
#include <QList>
#include <QNetworkInterface>
#include <QString>

namespace NetUtil {

struct Adapter
{
    QNetworkInterface iface;
    QHostAddress addr;
    QString label() const { return iface.humanReadableName() + QLatin1Char(' ') + addr.toString(); }
};

// Adapters worth multicasting on: up, multicast-capable, IPv4, and not loopback,
// VPN or virtual (Tailscale, VMware, Hyper-V ...). Multicast cannot cross those
// anyway, and leaving the choice to the OS can send probes into the wrong one.
QList<Adapter> lanAdapters();
bool excludedAdapterName(const QString &name);

}
