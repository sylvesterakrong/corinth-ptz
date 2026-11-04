// Team-control coordinator: hand-over, request/grant/deny, override, a holder that
// goes quiet, forged and replayed messages, and one token per camera. Real UDP on
// 127.0.0.1 with short timers; run with: ctest --test-dir build --output-on-failure
#include "ControlCoordinator.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QNetworkDatagram>
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

static bool waitFor(const std::function<bool()> &cond, int timeoutMs = 4000)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

static void settle(int ms)
{
    waitFor([] { return false; }, ms);
}

static ControlCoordinator::Config cfg(const QString &name, quint16 port, const QList<quint16> &peers,
                                      const QString &key = QStringLiteral("team-key"))
{
    ControlCoordinator::Config c;
    c.enabled = true;
    c.key = key;
    c.port = port;
    c.operatorName = name;
    c.host = name + QStringLiteral("-pc");
    c.instanceId = name.toLower() + QStringLiteral("-inst");
    for (quint16 p : peers) c.peers << QStringLiteral("127.0.0.1:%1").arg(p);
    c.multicast = false;
    c.heartbeatMs = 100;
    c.staleMs = 1500;
    c.requestMs = 1500;
    c.listenMs = 400;
    return c;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const quint16 pa = 47701, pb = 47702, pr = 47710;
    ControlCoordinator a(cfg(QStringLiteral("Ann"), pa, {pb, pr}));
    ControlCoordinator b(cfg(QStringLiteral("Ben"), pb, {pa}));

    // A raw socket that records everything Ann sends, for the replay test.
    QUdpSocket spy;
    spy.bind(QHostAddress::LocalHost, pr);

    int aLost = 0, bLost = 0;
    QObject::connect(&a, &ControlCoordinator::lostControl, [&] { ++aLost; });
    QObject::connect(&b, &ControlCoordinator::lostControl, [&] { ++bLost; });

    // ---- 1. two laptops start together: exactly one ends up in control ----------
    a.setCamera(QStringLiteral("cam1"));
    b.setCamera(QStringLiteral("cam1"));
    check(a.state() == QLatin1String("listening") && !a.isMine(), QStringLiteral("starts by listening, not holding control"));
    const bool settled = waitFor([&] {
        return (a.state() == QLatin1String("mine") && b.state() == QLatin1String("other"))
            || (b.state() == QLatin1String("mine") && a.state() == QLatin1String("other"));
    });
    check(settled, QStringLiteral("simultaneous start resolves to one holder (A=%1 B=%2)").arg(a.state(), b.state()));
    ControlCoordinator *holder = a.isMine() ? &a : &b;
    ControlCoordinator *viewer = a.isMine() ? &b : &a;
    check(viewer->holderName() == (holder == &a ? QLatin1String("Ann") : QLatin1String("Ben")),
          QStringLiteral("the viewer sees who is in control (%1)").arg(viewer->holderName()));
    check(holder->operators().size() == 2, QStringLiteral("operator list shows both laptops"));

    // ---- 2. request and grant ------------------------------------------------------
    viewer->claim();
    check(viewer->state() == QLatin1String("requesting"), QStringLiteral("claiming while another holds it sends a request"));
    const bool told = waitFor([&] { return !holder->requesterName().isEmpty(); });
    check(told, QStringLiteral("the holder is told who is asking (%1)").arg(holder->requesterName()));
    holder->grant();
    check(waitFor([&] { return viewer->isMine() && holder->state() == QLatin1String("other"); }),
          QStringLiteral("granting hands control over"));
    check((holder == &a ? aLost : bLost) >= 1, QStringLiteral("the old holder is told it lost control (so it can stop the camera)"));
    std::swap(holder, viewer);

    // ---- 3. request and deny -------------------------------------------------------
    viewer->claim();
    waitFor([&] { return !holder->requesterName().isEmpty(); });
    holder->deny();
    check(waitFor([&] { return viewer->state() == QLatin1String("other"); }) && holder->isMine(),
          QStringLiteral("denying keeps control with the holder"));

    // ---- 4. an unanswered request times out, then the override works ---------------
    viewer->claim();
    check(waitFor([&] { return viewer->requestTimedOut(); }, 3000), QStringLiteral("an unanswered request times out"));
    holder->setCamera(QStringLiteral("cam1"));   // same camera: must not disturb anything
    viewer->takeOver();
    check(viewer->isMine(), QStringLiteral("take over succeeds without the holder's consent"));
    check(waitFor([&] { return holder->state() == QLatin1String("other"); }),
          QStringLiteral("the displaced holder yields (higher epoch wins)"));
    std::swap(holder, viewer);
    settle(300);
    check(holder->isMine() && !viewer->isMine(), QStringLiteral("still exactly one holder after the override"));

    // ---- 5. a holder that goes quiet is treated as gone ------------------------------
    holder->pauseNetwork(true);
    const bool expired = waitFor([&] { return viewer->state() == QLatin1String("free"); });
    check(expired, QStringLiteral("a silent holder expires after the stale time (viewer: %1)").arg(viewer->state()));
    viewer->claim();
    check(viewer->isMine(), QStringLiteral("a free camera is taken immediately"));
    holder->pauseNetwork(false);
    check(waitFor([&] { return holder->state() == QLatin1String("other"); }) && viewer->isMine(),
          QStringLiteral("the returning old holder yields to the newer one"));
    std::swap(holder, viewer);

    // ---- 6. forged messages are ignored ---------------------------------------------
    {
        QUdpSocket forger;
        const quint16 holderPort = holder == &a ? pa : pb;
        QJsonObject o{{"v", 1}, {"i", "mallory"}, {"o", "Mallory"}, {"h", "evil"}, {"q", double(QDateTime::currentMSecsSinceEpoch() + 1000)},
                      {"t", "hb"}, {"c", "cam1"}, {"m", 1}, {"e", 9999.0}};
        const QByteArray json = QJsonDocument(o).toJson(QJsonDocument::Compact);
        // wrong key
        const QByteArray bad = QMessageAuthenticationCode::hash(json, "not-the-key", QCryptographicHash::Sha256).toHex();
        forger.writeDatagram(json + '\n' + bad, QHostAddress::LocalHost, holderPort);
        // no signature at all
        forger.writeDatagram(json, QHostAddress::LocalHost, holderPort);
        // garbage signature of the right length
        forger.writeDatagram(json + '\n' + QByteArray(64, 'a'), QHostAddress::LocalHost, holderPort);
        settle(400);
        check(holder->isMine() && holder->operators().size() == 2,
              QStringLiteral("unsigned, wrongly signed and garbage messages change nothing"));
    }

    // ---- 7. a replayed old heartbeat does not resurrect a former holder --------------
    // `spy` listens as an extra peer of Ann, so it holds genuine signed heartbeats.
    ControlCoordinator *annIsHolder = &a;
    if (!a.isMine()) {
        a.takeOver();
        waitFor([&] { return b.state() == QLatin1String("other"); });
    }
    while (spy.hasPendingDatagrams()) spy.receiveDatagram();
    settle(300);
    QByteArray captured;
    while (spy.hasPendingDatagrams()) {
        const QByteArray d = spy.receiveDatagram().data();
        if (d.contains("\"m\":1")) captured = d;
    }
    check(!captured.isEmpty(), QStringLiteral("captured a genuine signed heartbeat from the holder"));
    annIsHolder->release();
    check(waitFor([&] { return b.state() == QLatin1String("free"); }), QStringLiteral("released camera shows as free"));
    {
        QUdpSocket replayer;
        replayer.writeDatagram(captured, QHostAddress::LocalHost, pb);
        settle(300);
        check(b.state() == QLatin1String("free"), QStringLiteral("a replayed heartbeat is dropped (viewer stays %1)").arg(b.state()));
    }

    // ---- 8. control is per camera -------------------------------------------------------
    a.claim();
    waitFor([&] { return a.isMine(); });
    b.setCamera(QStringLiteral("cam2"));
    waitFor([&] { return b.isMine(); }, 3000);
    check(a.isMine() && b.isMine(), QStringLiteral("two laptops on different cameras each hold their own"));
    b.setCamera(QStringLiteral("cam1"));
    const bool viewOnly = waitFor([&] { return b.state() == QLatin1String("other"); }, 3000);
    check(viewOnly, QStringLiteral("moving to a camera someone holds ends view-only (%1)").arg(b.state()));

    // ---- 9. another team's key never mixes in ----------------------------------------
    ControlCoordinator c(cfg(QStringLiteral("Cy"), 47703, {pa, pb}, QStringLiteral("other-team")));
    c.setCamera(QStringLiteral("cam1"));
    check(waitFor([&] { return c.isMine(); }, 3000) && a.isMine() && b.state() == QLatin1String("other"),
          QStringLiteral("a laptop with a different team key neither sees nor disturbs the others"));

    qInfo().noquote() << "CTL TEST failures:" << g_fail;
    return g_fail;
}
