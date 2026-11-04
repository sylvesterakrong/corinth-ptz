#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QStringList>
#include <QVariantList>

class QJsonObject;
class QTimer;
class QUdpSocket;

// Shares the "who is in control of this camera" token between laptops, with no
// server: every Corinth PTZ instance announces itself over UDP once a second
// (LAN multicast, plus unicast to any listed peers, which is what works over
// Tailscale) and they agree on one holder per camera.
//
//   listening  just started; waiting a moment to hear who is already in control
//   free       nobody is in control
//   mine       this laptop is in control
//   other      another laptop is in control (this one is view-only)
//   requesting asked the holder for control and waiting for the answer
//   local      not configured: single-laptop behaviour, no network traffic
//
// Messages are JSON signed with HMAC-SHA256 using the shared team key; anything
// unsigned, wrongly signed or replayed is ignored. Hand-over is by request and
// grant; "take over" is the explicit override for an unresponsive holder and
// always wins (higher epoch). A holder that stops announcing for staleMs is
// treated as gone.
class ControlCoordinator : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY configChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool mine READ isMine NOTIFY stateChanged)
    Q_PROPERTY(QString holderName READ holderName NOTIFY stateChanged)
    Q_PROPERTY(QString holderHost READ holderHost NOTIFY stateChanged)
    Q_PROPERTY(bool canTakeOver READ canTakeOver NOTIFY stateChanged)
    Q_PROPERTY(bool requestTimedOut READ requestTimedOut NOTIFY stateChanged)
    Q_PROPERTY(QString requesterName READ requesterName NOTIFY requestChanged)
    Q_PROPERTY(QString requesterHost READ requesterHost NOTIFY requestChanged)
    Q_PROPERTY(QVariantList operators READ operators NOTIFY peersChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
public:
    struct Config
    {
        bool enabled = false;
        QString key;
        QStringList peers;               // "host" or "host:port"
        quint16 port = 47620;
        QString instanceId;
        QString operatorName;
        QString host;
        bool multicast = true;
        int heartbeatMs = 1000;
        int staleMs = 5000;
        int requestMs = 15000;           // after this the requester may take over
        int listenMs = 2500;
    };

    explicit ControlCoordinator(QObject *parent = nullptr);
    explicit ControlCoordinator(const Config &cfg, QObject *parent = nullptr);
    ~ControlCoordinator() override;

    // Reads local/ctlKey, ctlPeers, ctlPort from QSettings (and the
    // CORINTH_CONTROL_* environment overrides used for testing).
    static Config configFromSettings();
    void reload();
    void applyConfig(const Config &cfg);
    void setOperatorName(const QString &name);
    // The camera this laptop is looking at; control is per camera.
    void setCamera(const QString &key);
    // Simulates a cut link (nothing sent, nothing heard) for tests and diagnostics.
    void pauseNetwork(bool paused) { m_paused = paused; }

    bool enabled() const { return m_cfg.enabled && !m_cfg.key.isEmpty(); }
    QString state() const;
    bool isMine() const { return m_mode == Mode::Mine; }
    QString holderName() const;
    QString holderHost() const;
    bool canTakeOver() const { return enabled() && (m_mode == Mode::Other || m_mode == Mode::Requesting); }
    bool requestTimedOut() const;
    QString requesterName() const { return m_incoming.op; }
    QString requesterHost() const { return m_incoming.host; }
    QVariantList operators() const;
    QString status() const;

    Q_INVOKABLE void claim();           // take it if free, otherwise ask the holder
    Q_INVOKABLE void cancelRequest();
    Q_INVOKABLE void release();
    Q_INVOKABLE void takeOver();        // override: no questions asked
    Q_INVOKABLE void grant();           // holder says yes to the pending request
    Q_INVOKABLE void deny();

signals:
    void configChanged();
    void stateChanged();
    void peersChanged();
    void requestChanged();
    // This laptop just stopped being the holder (handed over, overridden, conflict).
    void lostControl(const QString &toName);
    void notice(const QString &text, const QString &kind);   // for the audit log and a toast

private:
    enum class Mode { Local, Listening, Free, Mine, Other, Requesting };
    struct Peer
    {
        QString op, host, cam;
        bool mine = false;
        qint64 epoch = 0;
        qint64 seen = 0;
        qint64 seq = 0;
    };
    struct Incoming
    {
        QString inst, op, host;
    };

    qint64 now() const { return m_clock.elapsed(); }
    void setMode(Mode m);
    void becomeMine();
    bool liveHolder(QString *inst, Peer *out) const;
    bool peerWins(const QString &inst, const Peer &p) const;
    void tick();
    void evaluate();
    void sendHeartbeat();
    void send(const QJsonObject &msg);
    void onReadyRead();
    void handle(const QString &inst, const QJsonObject &obj);
    QByteArray sign(const QByteArray &json) const;
    void openSocket();
    void closeSocket();
    void resolvePeers();

    Config m_cfg;
    QElapsedTimer m_clock;
    QUdpSocket *m_sock = nullptr;
    QTimer *m_hb = nullptr;
    QTimer *m_tick = nullptr;
    QList<QHostAddress> m_peerAddrs;
    QList<quint16> m_peerPorts;
    QString m_error;
    bool m_paused = false;

    QString m_cam;
    Mode m_mode = Mode::Local;
    qint64 m_epoch = 0;
    qint64 m_seq = 0;
    qint64 m_listenEnd = 0;
    qint64 m_reqStart = 0;
    QString m_reqTo;
    bool m_timedOutShown = false;
    QString m_lastHolder;
    QHash<QString, Peer> m_peers;
    Incoming m_incoming;
};
