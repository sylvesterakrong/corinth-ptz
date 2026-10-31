#pragma once

#include <QObject>
#include <QVariantList>
#include <QHostAddress>

class QUdpSocket;
class QTimer;

// ONVIF WS-Discovery: multicast Probe to 239.255.255.250:3702 and collect
// ProbeMatches for 3 seconds. Finds any ONVIF camera on the same network
// segment. Multicast does not cross Tailscale or routers, so when the laptop is
// remote the scan has to run on the Jetson (see README).
class Discovery : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(bool hasScanned READ hasScanned NOTIFY resultsChanged)
public:
    explicit Discovery(QObject *parent = nullptr);

    bool scanning() const { return m_scanning; }
    QVariantList results() const { return m_results; }
    bool hasScanned() const { return m_hasScanned; }

    Q_INVOKABLE void scan();

signals:
    void scanningChanged();
    void resultsChanged();
    void finished(int count);

private:
    void onReadyRead();
    void finish();

    QUdpSocket *m_sock = nullptr;
    QTimer *m_timer = nullptr;
    bool m_scanning = false;
    bool m_hasScanned = false;
    QVariantList m_results;
};
