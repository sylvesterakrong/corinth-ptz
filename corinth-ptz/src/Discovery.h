#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QHostAddress>

class QNetworkAccessManager;
class QUdpSocket;
class QTimer;

// ONVIF WS-Discovery: a multicast Probe to 239.255.255.250:3702 sent out of each
// real LAN adapter in turn (not Tailscale or virtual adapters), collecting
// ProbeMatches for 3 seconds. Each camera found is then asked once for the
// Hikvision ISAPI device-info path to tell "ONVIF only" from "ISAPI too".
// Multicast does not cross Tailscale or routers, so when the laptop is remote
// the scan has to run on the Jetson (see README).
class Discovery : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool hasScanned READ hasScanned NOTIFY resultsChanged)
    Q_PROPERTY(QStringList adapters READ adapters NOTIFY resultsChanged)   // what the last scan used
public:
    explicit Discovery(QObject *parent = nullptr);

    bool scanning() const { return m_scanning; }
    QVariantList results() const { return m_results; }
    bool hasScanned() const { return m_hasScanned; }
    QStringList adapters() const { return m_adapters; }

    Q_INVOKABLE void scan();

signals:
    void scanningChanged();
    void resultsChanged();
    void finished(int count);

private:
    void onReadyRead(QUdpSocket *sock);
    void probeIsapi(const QString &ip, int httpPort, int resultIndex, bool vendorHint);
    void maybeFinish();
    void finish();

    QNetworkAccessManager *m_nam = nullptr;
    QList<QUdpSocket *> m_socks;
    QTimer *m_timer = nullptr;      // end of the listening window
    QTimer *m_cap = nullptr;        // hard stop, even if a probe hangs
    QStringList m_adapters;
    bool m_scanning = false;
    bool m_collecting = false;
    bool m_hasScanned = false;
    int m_pending = 0;
    int m_scanId = 0;
    QVariantList m_results;
};
