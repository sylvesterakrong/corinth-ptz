#include "ControlCoordinator.h"
#include "NetUtil.h"

#include <QDateTime>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUdpSocket>
#include <QUuid>

namespace {
const char *kGroup = "239.255.77.17";

bool constantTimeEqual(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    int diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= a.at(i) ^ b.at(i);
    return diff == 0;
}
}

ControlCoordinator::ControlCoordinator(QObject *parent) : ControlCoordinator(Config(), parent) {}

ControlCoordinator::ControlCoordinator(const Config &cfg, QObject *parent) : QObject(parent)
{
    m_clock.start();
    m_hb = new QTimer(this);
    m_tick = new QTimer(this);
    connect(m_hb, &QTimer::timeout, this, &ControlCoordinator::sendHeartbeat);
    connect(m_tick, &QTimer::timeout, this, &ControlCoordinator::tick);
    Config c = cfg;
    if (c.instanceId.isEmpty())
        c.instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);
    if (c.host.isEmpty())
        c.host = QHostInfo::localHostName();
    if (c.operatorName.isEmpty())
        c.operatorName = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER", QStringLiteral("Operator")));
    m_cfg = c;
    if (c.enabled)
        applyConfig(c);
}

ControlCoordinator::~ControlCoordinator()
{
    // Tell the others the token is free; no signals while being destroyed.
    if (m_mode == Mode::Mine)
        send({{QStringLiteral("t"), QStringLiteral("rel")}, {QStringLiteral("c"), m_cam}});
}

ControlCoordinator::Config ControlCoordinator::configFromSettings()
{
    Config c;
    QSettings s;
    c.key = qEnvironmentVariable("CORINTH_CONTROL_KEY", s.value(QStringLiteral("local/ctlKey")).toString());
    const QString peers = qEnvironmentVariable("CORINTH_CONTROL_PEERS", s.value(QStringLiteral("local/ctlPeers")).toString());
    c.peers = peers.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
    bool ok = false;
    const int port = qEnvironmentVariable("CORINTH_CONTROL_PORT", s.value(QStringLiteral("local/ctlPort"), 47620).toString()).toInt(&ok);
    c.port = (ok && port > 0 && port < 65536) ? quint16(port) : quint16(47620);
    c.enabled = !c.key.isEmpty();
    return c;
}

void ControlCoordinator::reload()
{
    Config c = configFromSettings();
    c.instanceId = m_cfg.instanceId;
    c.operatorName = m_cfg.operatorName;
    c.host = m_cfg.host;
    applyConfig(c);
}

void ControlCoordinator::applyConfig(const Config &cfg)
{
    const bool wasMine = m_mode == Mode::Mine;
    if (wasMine)
        release();
    closeSocket();
    m_peers.clear();
    m_incoming = {};
    m_reqTo.clear();
    m_lastHolder.clear();
    m_error.clear();
    m_cfg = cfg;
    if (m_cfg.instanceId.isEmpty())
        m_cfg.instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces).left(12);

    if (enabled()) {
        openSocket();
        resolvePeers();
        m_hb->start(m_cfg.heartbeatMs);
        m_tick->start(qMax(50, qMin(200, m_cfg.heartbeatMs / 2)));
        m_listenEnd = now() + m_cfg.listenMs + qint64(QRandomGenerator::global()->bounded(qMax(1, m_cfg.listenMs / 4)));
        m_mode = m_cam.isEmpty() ? Mode::Free : Mode::Listening;
    } else {
        m_hb->stop();
        m_tick->stop();
        m_mode = Mode::Local;
    }
    emit configChanged();
    emit stateChanged();
    emit peersChanged();
    emit requestChanged();
    if (wasMine)
        emit lostControl(QString());
}

void ControlCoordinator::setOperatorName(const QString &name)
{
    if (name.trimmed().isEmpty() || name == m_cfg.operatorName)
        return;
    m_cfg.operatorName = name.trimmed();
    if (enabled())
        sendHeartbeat();
}

void ControlCoordinator::setCamera(const QString &key)
{
    if (key == m_cam)
        return;
    if (m_mode == Mode::Mine) {
        QJsonObject rel{{QStringLiteral("t"), QStringLiteral("rel")}, {QStringLiteral("c"), m_cam}};
        send(rel);
    }
    m_cam = key;
    m_incoming = {};
    m_reqTo.clear();
    m_lastHolder.clear();
    if (enabled()) {
        m_listenEnd = now() + m_cfg.listenMs + qint64(QRandomGenerator::global()->bounded(qMax(1, m_cfg.listenMs / 4)));
        m_mode = Mode::Listening;
        sendHeartbeat();
    } else {
        m_mode = Mode::Local;
    }
    emit stateChanged();
    emit peersChanged();
    emit requestChanged();
}

// ---- state --------------------------------------------------------------------

QString ControlCoordinator::state() const
{
    switch (m_mode) {
    case Mode::Local: return QStringLiteral("local");
    case Mode::Listening: return QStringLiteral("listening");
    case Mode::Free: return QStringLiteral("free");
    case Mode::Mine: return QStringLiteral("mine");
    case Mode::Other: return QStringLiteral("other");
    case Mode::Requesting: return QStringLiteral("requesting");
    }
    return QStringLiteral("local");
}

void ControlCoordinator::setMode(Mode m)
{
    if (m == m_mode)
        return;
    m_mode = m;
    emit stateChanged();
}

bool ControlCoordinator::liveHolder(QString *inst, Peer *out) const
{
    bool found = false;
    QString bestInst;
    Peer best;
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        const Peer &p = it.value();
        if (p.cam != m_cam || !p.mine || now() - p.seen >= m_cfg.staleMs)
            continue;
        if (!found || p.epoch > best.epoch || (p.epoch == best.epoch && it.key() < bestInst)) {
            found = true;
            best = p;
            bestInst = it.key();
        }
    }
    if (found) {
        if (inst) *inst = bestInst;
        if (out) *out = best;
    }
    return found;
}

QString ControlCoordinator::holderName() const
{
    if (m_mode == Mode::Mine)
        return m_cfg.operatorName;
    Peer p;
    return liveHolder(nullptr, &p) ? p.op : QString();
}

QString ControlCoordinator::holderHost() const
{
    if (m_mode == Mode::Mine)
        return m_cfg.host;
    Peer p;
    return liveHolder(nullptr, &p) ? p.host : QString();
}

bool ControlCoordinator::requestTimedOut() const
{
    return m_mode == Mode::Requesting && now() - m_reqStart >= m_cfg.requestMs;
}

QVariantList ControlCoordinator::operators() const
{
    QVariantList out;
    auto add = [&out](const QString &name, const QString &host, bool holder, bool me) {
        QVariantMap m;
        m[QStringLiteral("name")] = name;
        m[QStringLiteral("host")] = host;
        m[QStringLiteral("role")] = holder ? QStringLiteral("in control") : QStringLiteral("viewing");
        m[QStringLiteral("me")] = me;
        out << m;
    };
    if (enabled())
        add(m_cfg.operatorName, m_cfg.host, m_mode == Mode::Mine, true);
    for (auto it = m_peers.begin(); it != m_peers.end(); ++it) {
        const Peer &p = it.value();
        if (p.cam == m_cam && now() - p.seen < m_cfg.staleMs)
            add(p.op, p.host, p.mine, false);
    }
    return out;
}

QString ControlCoordinator::status() const
{
    if (!enabled())
        return QStringLiteral("Off. Enter a team key to share control between laptops.");
    if (!m_error.isEmpty())
        return m_error;
    return QStringLiteral("On · UDP %1 · %2 on this camera").arg(m_cfg.port).arg(operators().size());
}

// ---- actions ------------------------------------------------------------------

void ControlCoordinator::becomeMine()
{
    qint64 maxEpoch = m_epoch;
    for (const Peer &p : std::as_const(m_peers))
        if (p.cam == m_cam)
            maxEpoch = qMax(maxEpoch, p.epoch);
    m_epoch = maxEpoch + 1;
    m_reqTo.clear();
    m_incoming = {};
    setMode(Mode::Mine);
    sendHeartbeat();
    emit requestChanged();
    emit peersChanged();
    emit notice(QStringLiteral("%1 took control").arg(m_cfg.operatorName), QStringLiteral("ok"));
}

void ControlCoordinator::claim()
{
    if (!enabled() || m_mode == Mode::Mine || m_mode == Mode::Requesting)
        return;
    QString holder;
    Peer p;
    if (!liveHolder(&holder, &p)) {
        becomeMine();
        return;
    }
    m_reqTo = holder;
    m_reqStart = now();
    m_timedOutShown = false;
    setMode(Mode::Requesting);
    send({{QStringLiteral("t"), QStringLiteral("req")}, {QStringLiteral("to"), holder}, {QStringLiteral("c"), m_cam}});
    emit notice(QStringLiteral("Asked %1 for control").arg(p.op), QStringLiteral("ok"));
}

void ControlCoordinator::cancelRequest()
{
    if (m_mode != Mode::Requesting)
        return;
    send({{QStringLiteral("t"), QStringLiteral("cancel")}, {QStringLiteral("to"), m_reqTo}, {QStringLiteral("c"), m_cam}});
    m_reqTo.clear();
    setMode(Mode::Other);
}

void ControlCoordinator::release()
{
    if (m_mode != Mode::Mine)
        return;
    setMode(Mode::Free);
    send({{QStringLiteral("t"), QStringLiteral("rel")}, {QStringLiteral("c"), m_cam}});
    m_incoming = {};
    emit requestChanged();
    emit notice(QStringLiteral("%1 released control").arg(m_cfg.operatorName), QStringLiteral("ok"));
}

void ControlCoordinator::takeOver()
{
    if (!enabled() || m_mode == Mode::Mine)
        return;
    Peer p;
    const bool had = liveHolder(nullptr, &p);
    becomeMine();
    if (had)
        emit notice(QStringLiteral("%1 took control over from %2").arg(m_cfg.operatorName, p.op), QStringLiteral("warn"));
}

void ControlCoordinator::grant()
{
    if (m_mode != Mode::Mine || m_incoming.inst.isEmpty())
        return;
    const Incoming to = m_incoming;
    m_incoming = {};
    // Mine ends first, so the camera can be stopped before the new holder starts.
    setMode(Mode::Other);
    emit lostControl(to.op);
    send({{QStringLiteral("t"), QStringLiteral("grant")}, {QStringLiteral("to"), to.inst}, {QStringLiteral("c"), m_cam}});
    emit requestChanged();
    emit notice(QStringLiteral("%1 handed control to %2").arg(m_cfg.operatorName, to.op), QStringLiteral("ok"));
}

void ControlCoordinator::deny()
{
    if (m_incoming.inst.isEmpty())
        return;
    const Incoming to = m_incoming;
    m_incoming = {};
    send({{QStringLiteral("t"), QStringLiteral("deny")}, {QStringLiteral("to"), to.inst}, {QStringLiteral("c"), m_cam}});
    emit requestChanged();
    emit notice(QStringLiteral("%1 kept control; declined %2").arg(m_cfg.operatorName, to.op), QStringLiteral("ok"));
}

// ---- network ------------------------------------------------------------------

QByteArray ControlCoordinator::sign(const QByteArray &json) const
{
    return QMessageAuthenticationCode::hash(json, m_cfg.key.toUtf8(), QCryptographicHash::Sha256).toHex();
}

void ControlCoordinator::openSocket()
{
    m_sock = new QUdpSocket(this);
    if (!m_sock->bind(QHostAddress::AnyIPv4, m_cfg.port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        m_error = QStringLiteral("Cannot open UDP port %1: %2").arg(m_cfg.port).arg(m_sock->errorString());
        return;
    }
    connect(m_sock, &QUdpSocket::readyRead, this, &ControlCoordinator::onReadyRead);
    if (m_cfg.multicast) {
        const QHostAddress group = QHostAddress(QString::fromLatin1(kGroup));
        for (const NetUtil::Adapter &a : NetUtil::lanAdapters())
            m_sock->joinMulticastGroup(group, a.iface);   // best effort
    }
}

void ControlCoordinator::closeSocket()
{
    if (m_sock) {
        m_sock->close();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    m_peerAddrs.clear();
    m_peerPorts.clear();
}

void ControlCoordinator::resolvePeers()
{
    for (const QString &entry : std::as_const(m_cfg.peers)) {
        QString host = entry;
        quint16 port = m_cfg.port;
        const int colon = entry.lastIndexOf(QLatin1Char(':'));
        if (colon > 0 && entry.count(QLatin1Char(':')) == 1) {
            host = entry.left(colon);
            bool ok = false;
            const int p = entry.mid(colon + 1).toInt(&ok);
            if (ok && p > 0 && p < 65536) port = quint16(p);
        }
        if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
            host = QStringLiteral("127.0.0.1");
        const QHostAddress ip(host);
        if (!ip.isNull()) {
            m_peerAddrs << ip;
            m_peerPorts << port;
        } else {
            QHostInfo::lookupHost(host, this, [this, port](const QHostInfo &info) {
                if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) return;
                m_peerAddrs << info.addresses().first();
                m_peerPorts << port;
            });
        }
    }
}

void ControlCoordinator::send(const QJsonObject &msg)
{
    if (!m_sock || m_paused || !enabled() || m_cam.isEmpty())
        return;
    QJsonObject o = msg;
    o[QStringLiteral("v")] = 1;
    o[QStringLiteral("i")] = m_cfg.instanceId;
    o[QStringLiteral("o")] = m_cfg.operatorName;
    o[QStringLiteral("h")] = m_cfg.host;
    m_seq = qMax(m_seq + 1, QDateTime::currentMSecsSinceEpoch());
    o[QStringLiteral("q")] = double(m_seq);
    const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
    const QByteArray datagram = json + '\n' + sign(json);

    for (int i = 0; i < m_peerAddrs.size(); ++i)
        m_sock->writeDatagram(datagram, m_peerAddrs.at(i), m_peerPorts.at(i));
    if (m_cfg.multicast) {
        const QHostAddress group = QHostAddress(QString::fromLatin1(kGroup));
        const auto adapters = NetUtil::lanAdapters();
        if (adapters.isEmpty()) {
            m_sock->writeDatagram(datagram, group, m_cfg.port);
        } else {
            for (const NetUtil::Adapter &a : adapters) {
                m_sock->setMulticastInterface(a.iface);
                m_sock->writeDatagram(datagram, group, m_cfg.port);
            }
        }
    }
}

void ControlCoordinator::sendHeartbeat()
{
    if (m_cam.isEmpty())
        return;
    send({{QStringLiteral("t"), QStringLiteral("hb")}, {QStringLiteral("c"), m_cam},
          {QStringLiteral("m"), m_mode == Mode::Mine ? 1 : 0}, {QStringLiteral("e"), double(m_epoch)}});
}

void ControlCoordinator::onReadyRead()
{
    while (m_sock && m_sock->hasPendingDatagrams()) {
        const QByteArray data = m_sock->receiveDatagram().data();
        const int nl = data.lastIndexOf('\n');
        if (nl <= 0 || m_paused || !enabled())
            continue;
        const QByteArray json = data.left(nl);
        if (!constantTimeEqual(data.mid(nl + 1).trimmed(), sign(json)))
            continue;   // not signed with our team key
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        const QJsonObject obj = doc.object();
        const QString inst = obj.value(QStringLiteral("i")).toString();
        if (obj.value(QStringLiteral("v")).toInt() != 1 || inst.isEmpty() || inst == m_cfg.instanceId)
            continue;
        handle(inst, obj);
    }
}

bool ControlCoordinator::peerWins(const QString &inst, const Peer &p) const
{
    return p.epoch > m_epoch || (p.epoch == m_epoch && inst < m_cfg.instanceId);
}

void ControlCoordinator::handle(const QString &inst, const QJsonObject &obj)
{
    const qint64 seq = qint64(obj.value(QStringLiteral("q")).toDouble());
    Peer &p = m_peers[inst];
    // Replays and reordered datagrams are dropped; a sequence far in the past
    // means the other laptop restarted or changed its clock.
    if (seq <= p.seq && p.seq - seq < 600000)
        return;
    p.seq = seq;
    p.seen = now();
    p.op = obj.value(QStringLiteral("o")).toString();
    p.host = obj.value(QStringLiteral("h")).toString();

    const QString t = obj.value(QStringLiteral("t")).toString();
    const QString cam = obj.value(QStringLiteral("c")).toString();
    const QString to = obj.value(QStringLiteral("to")).toString();

    if (t == QLatin1String("hb")) {
        p.cam = cam;
        p.mine = obj.value(QStringLiteral("m")).toInt() == 1;
        p.epoch = qint64(obj.value(QStringLiteral("e")).toDouble());
        if (p.mine && cam == m_cam && m_mode == Mode::Mine && peerWins(inst, p)) {
            // Two holders (simultaneous claim, or an override): the higher epoch wins.
            m_incoming = {};
            setMode(Mode::Other);
            emit lostControl(p.op);
            emit requestChanged();
            emit notice(QStringLiteral("Control moved to %1").arg(p.op), QStringLiteral("warn"));
        }
    } else if (t == QLatin1String("rel")) {
        p.cam = cam;
        p.mine = false;
    } else if (t == QLatin1String("req")) {
        if (to == m_cfg.instanceId && cam == m_cam && m_mode == Mode::Mine) {
            m_incoming = {inst, p.op, p.host};
            emit requestChanged();
            emit notice(QStringLiteral("%1 is asking for control").arg(p.op), QStringLiteral("warn"));
        }
    } else if (t == QLatin1String("cancel")) {
        if (to == m_cfg.instanceId && m_incoming.inst == inst) {
            m_incoming = {};
            emit requestChanged();
        }
    } else if (t == QLatin1String("grant")) {
        if (to == m_cfg.instanceId && cam == m_cam && m_mode == Mode::Requesting && inst == m_reqTo)
            becomeMine();
    } else if (t == QLatin1String("deny")) {
        if (to == m_cfg.instanceId && cam == m_cam && m_mode == Mode::Requesting && inst == m_reqTo) {
            m_reqTo.clear();
            setMode(Mode::Other);
            emit notice(QStringLiteral("%1 kept control").arg(p.op), QStringLiteral("warn"));
        }
    }
    evaluate();
    emit peersChanged();
}

void ControlCoordinator::tick()
{
    evaluate();
}

void ControlCoordinator::evaluate()
{
    if (!enabled() || m_cam.isEmpty())
        return;
    QString holder;
    Peer hp;
    const bool has = liveHolder(&holder, &hp);

    switch (m_mode) {
    case Mode::Listening:
        if (now() >= m_listenEnd) {
            if (!has) becomeMine();
            else setMode(Mode::Other);
        }
        break;
    case Mode::Free:
        if (has) setMode(Mode::Other);
        break;
    case Mode::Other:
        if (!has) setMode(Mode::Free);
        break;
    case Mode::Requesting:
        if (!has) {
            becomeMine();                       // the holder released or went quiet
        } else if (holder != m_reqTo) {
            m_reqTo = holder;                   // someone else holds it now
        } else if (requestTimedOut() && !m_timedOutShown) {
            m_timedOutShown = true;
            emit stateChanged();                // lets the UI offer "take over"
        }
        break;
    case Mode::Mine:
    case Mode::Local:
        break;
    }

    // A request is only meaningful while we hold control and the asker is alive.
    if (!m_incoming.inst.isEmpty()) {
        const Peer &ask = m_peers.value(m_incoming.inst);
        if (m_mode != Mode::Mine || now() - ask.seen >= m_cfg.staleMs) {
            m_incoming = {};
            emit requestChanged();
        }
    }

    if (holder != m_lastHolder) {
        m_lastHolder = holder;
        emit stateChanged();
    }
}
