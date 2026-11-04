#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QDir>
#include <QFile>
#include <QJSValue>
#include <QTimer>
#include <cmath>
#include <functional>
#include <memory>

#include "AppController.h"
#include "AuditLog.h"
#include "PtzController.h"
#include "LinkMonitor.h"
#include "CaptureService.h"
#include "Discovery.h"
#include "JoystickManager.h"
#include "HealthMonitor.h"
#include "ControlCoordinator.h"
#include "drivers/CameraProfile.h"
#include "drivers/OnvifDriver.h"
#include <QSettings>

// Developer aid: CORINTH_SHOTS=<folder> walks through the screens and themes,
// saves a PNG of each, then quits. Useful for design review and CI.
static void runScreenshotTour(QQmlApplicationEngine &engine, AppController &ctrl, const QString &dir)
{
    QDir().mkpath(dir);
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!win) return;
    QObject *settings = win->findChild<QObject *>(QStringLiteral("settingsPage"));
    QObject *live = win->findChild<QObject *>(QStringLiteral("liveView"));
    QObject *tester = win->findChild<QObject *>(QStringLiteral("endpointTester"));

    struct Step { QString name; std::function<void()> apply; };
    auto steps = std::make_shared<QList<Step>>();
    *steps << Step{QStringLiteral("01-live-dark"), [win, &ctrl]() { win->setProperty("page", 0); ctrl.setTheme(QStringLiteral("dark")); }}
           << Step{QStringLiteral("02-live-day"), [win, &ctrl]() { ctrl.setTheme(QStringLiteral("light")); }}
           << Step{QStringLiteral("03-live-night"), [win, &ctrl]() { ctrl.setTheme(QStringLiteral("night")); }}
           << Step{QStringLiteral("04-live-degraded"), [win, &ctrl]() { ctrl.setTheme(QStringLiteral("dark")); ctrl.link()->setSimulate(QStringLiteral("degraded")); }}
           << Step{QStringLiteral("05-live-lost"), [win, &ctrl]() { ctrl.link()->setSimulate(QStringLiteral("lost")); }}
           << Step{QStringLiteral("06-live-viewonly"), [win, &ctrl]() { ctrl.link()->setSimulate(QString()); ctrl.ptz()->releaseControl(); }}
           << Step{QStringLiteral("07-settings-cameras"), [win, &ctrl]() { ctrl.ptz()->takeControl(); win->setProperty("page", 1); }}
           << Step{QStringLiteral("08-settings-image"), [win, settings, &ctrl]() { if (settings) settings->setProperty("pageId", QStringLiteral("display")); }}
           << Step{QStringLiteral("09-settings-main-stream-day"), [win, settings, &ctrl]() { ctrl.setTheme(QStringLiteral("light")); if (settings) settings->setProperty("pageId", QStringLiteral("main")); }}
           << Step{QStringLiteral("10-input-dark"), [win, &ctrl]() { ctrl.setTheme(QStringLiteral("dark")); win->setProperty("page", 2); }}
           << Step{QStringLiteral("11-input-flight-day"), [win, &ctrl]() { ctrl.setTheme(QStringLiteral("light")); ctrl.joystick()->setDeviceType(QStringLiteral("flight")); }}
           << Step{QStringLiteral("12-input-kb-night"), [win, &ctrl]() { ctrl.setTheme(QStringLiteral("night")); ctrl.joystick()->setDeviceType(QStringLiteral("kb")); }}
           << Step{QStringLiteral("13-live-presets"), [win, live, &ctrl]() { ctrl.setTheme(QStringLiteral("dark")); win->setProperty("page", 0); if (live) live->setProperty("tab", QStringLiteral("presets")); }}
           << Step{QStringLiteral("14-live-aux"), [live]() { if (live) live->setProperty("tab", QStringLiteral("aux")); }}
           << Step{QStringLiteral("15-live-health"), [live]() { if (live) live->setProperty("tab", QStringLiteral("health")); }}
           << Step{QStringLiteral("16-live-point"), [live]() { if (live) { live->setProperty("tab", QStringLiteral("ptz")); live->setProperty("pointMode", true); } }}
           << Step{QStringLiteral("17-settings-tester"), [win, settings, tester, live]() {
                   if (live) live->setProperty("pointMode", false);
                   win->setProperty("page", 1);
                   if (settings) settings->setProperty("pageId", QStringLiteral("tester"));
                   if (tester) QMetaObject::invokeMethod(tester, "send");
               }}
           << Step{QStringLiteral("18-settings-tester-probe"), [tester]() { if (tester) QMetaObject::invokeMethod(tester, "probeAll"); }}
           << Step{QStringLiteral("18b-settings-tester-wait"), []() {}}
           << Step{QStringLiteral("18c-settings-tester-probe-done"), []() {}}
           << Step{QStringLiteral("19-settings-teamctl"), [settings]() { if (settings) settings->setProperty("pageId", QStringLiteral("teamctl")); }}
           << Step{QStringLiteral("20-live-pip"), [win, &ctrl]() {
                   // Saves a camera, so only in an isolated settings store (never the real one).
                   if (QCoreApplication::applicationName() == QLatin1String("Corinth PTZ")) return;
                   QVariantMap cam = ctrl.activeCamera();
                   cam[QStringLiteral("thermalMainUrl")] = QStringLiteral("rtsp://127.0.0.1:9/thermal");
                   ctrl.saveCamera(cam);
                   ctrl.setPipEnabled(true);
                   win->setProperty("page", 0);
               }}
           << Step{QStringLiteral("21-live-thermal"), [&ctrl]() {
                   if (QCoreApplication::applicationName() == QLatin1String("Corinth PTZ")) return;
                   ctrl.setChannel(2);
               }}
           << Step{QStringLiteral("22-live-controls-hidden"), [win, live, &ctrl]() {
                   ctrl.setChannel(1);
                   win->setProperty("page", 0);
                   if (live) live->setProperty("overlays", false);
               }}
           << Step{QStringLiteral("23-live-controls-back"), [live]() { if (live) live->setProperty("overlays", true); }}
           << Step{QStringLiteral("23b-live-fullscreen"), [win, live]() {
                   win->setProperty("page", 0);
                   if (live) live->setProperty("isFullScreen", true);
               }}
           << Step{QStringLiteral("23c-live-fullscreen-exit-x"), [live]() { if (live) live->setProperty("exitChipShown", true); }}
           << Step{QStringLiteral("23d-live-fullscreen-left"), [live]() { if (live) { live->setProperty("exitChipShown", false); live->setProperty("isFullScreen", false); } }}
           << Step{QStringLiteral("24-settings-onvif"), [win, settings, &ctrl]() {
                   if (QCoreApplication::applicationName() == QLatin1String("Corinth PTZ")) return;
                   QVariantMap cam{{QStringLiteral("name"), QStringLiteral("ONVIF camera")}, {QStringLiteral("ip"), QStringLiteral("127.0.0.1")},
                                   {QStringLiteral("httpPort"), 9}, {QStringLiteral("proto"), QStringLiteral("ONVIF")}};
                   ctrl.setActive(ctrl.saveCamera(cam));
                   win->setProperty("page", 1);
                   if (settings) settings->setProperty("pageId", QStringLiteral("display"));
               }};

    auto index = std::make_shared<int>(0);
    auto timer = new QTimer(win);
    timer->setInterval(1800);
    QObject::connect(timer, &QTimer::timeout, win, [=, &ctrl]() mutable {
        if (*index > 0) {
            const QImage img = win->grabWindow();
            img.save(dir + QLatin1Char('/') + steps->at(*index - 1).name + QStringLiteral(".png"));
        }
        if (*index >= steps->size()) {
            ctrl.joystick()->setDeviceType(QStringLiteral("xbox"));
            ctrl.setTheme(QStringLiteral("dark"));
            QGuiApplication::quit();
            return;
        }
        steps->at(*index).apply();
        ++*index;
    });
    QTimer::singleShot(2500, timer, [timer]() { timer->start(); });
}

// Developer aid: CORINTH_SELFTEST=1 runs the core behaviours against the demo
// camera (presets, settings write-back, joystick bindings, link safety rules)
// and exits with the number of failures.
static void runSelfTest(QQmlApplicationEngine &engine, AppController &ctrl)
{
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    QObject *settings = win ? win->findChild<QObject *>(QStringLiteral("settingsPage")) : nullptr;
    auto fails = std::make_shared<int>(0);
    auto check = [fails](bool ok, const QString &what) {
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << what;
        if (!ok) ++*fails;
    };

    ctrl.setActive(QStringLiteral("demo"));
    if (settings) settings->setProperty("pageId", QStringLiteral("display"));

    QTimer::singleShot(1500, &ctrl, [&ctrl, check]() {
        check(ctrl.driverKind() == QLatin1String("Demo"), QStringLiteral("demo driver active"));
        ctrl.ptz()->gotoPreset(3, QStringLiteral("Starboard beam"));
    });
    QTimer::singleShot(2500, &ctrl, [settings]() {
        if (!settings) return;
        QMetaObject::invokeMethod(settings, "setVal", Q_ARG(QVariant, QStringLiteral("bright")), Q_ARG(QVariant, 70));
        QMetaObject::invokeMethod(settings, "apply");
    });
    QTimer::singleShot(5000, &ctrl, [&ctrl, settings, check]() {
        check(qAbs(ctrl.ptz()->az() - 90.0) < 1.0, QStringLiteral("preset 3 moves camera to az 90 (got %1)").arg(ctrl.ptz()->az()));
        check(ctrl.ptz()->lastState() == QLatin1String("ack"), QStringLiteral("preset command acknowledged (%1)").arg(ctrl.ptz()->lastState()));
        if (settings) {
            const QVariantMap saved = settings->property("saved").value<QJSValue>().toVariant().toMap();
            check(saved.value(QStringLiteral("bright")).toInt() == 70, QStringLiteral("brightness written and re-read from camera (got %1)").arg(saved.value(QStringLiteral("bright")).toString()));
        } else check(false, QStringLiteral("settings page found"));

        auto *joy = ctrl.joystick();
        joy->setDeviceType(QStringLiteral("xbox"));
        joy->restoreDefaults();
        const QString n1 = joy->setBinding(QStringLiteral("snapshot"), QStringLiteral("y"));
        check(n1.startsWith(QLatin1Char('!')) && joy->bindings().value(QStringLiteral("snapshot")).toString() == QLatin1String("y")
                  && joy->bindings().value(QStringLiteral("channel")).toString().isEmpty(),
              QStringLiteral("rebinding moves button and unassigns old action (%1)").arg(n1));
        const QString n2 = joy->setBinding(QStringLiteral("snapshot"), QStringLiteral("b"));
        check(n2.startsWith(QLatin1Char('?')), QStringLiteral("Stop all button is protected (%1)").arg(n2));
        const QString n3 = joy->setBinding(QStringLiteral("zoom"), QStringLiteral("a"));
        check(n3.startsWith(QLatin1Char('?')), QStringLiteral("axis action rejects a button (%1)").arg(n3));
        joy->restoreDefaults();

        ctrl.link()->setSimulate(QStringLiteral("degraded"));
        check(ctrl.ptz()->effectiveSpeed() == 30 && ctrl.streamForced() && ctrl.effectiveStream() == QLatin1String("sub"),
              QStringLiteral("degraded link caps speed at 30 and forces sub stream"));
        ctrl.link()->setSimulate(QStringLiteral("lost"));
        check(ctrl.ptz()->locked(), QStringLiteral("lost link locks controls"));
        ctrl.link()->setSimulate(QString());
        check(!ctrl.ptz()->locked() && ctrl.ptz()->effectiveSpeed() == 40, QStringLiteral("restored link unlocks and uncaps"));

        ctrl.ptz()->releaseControl();
        ctrl.ptz()->gotoPreset(1, QStringLiteral("Bow"));
        check(ctrl.ptz()->lastState() == QLatin1String("blocked"), QStringLiteral("view-only blocks commands"));
        ctrl.ptz()->takeControl();

        const QVariantMap snap = ctrl.capture()->snapshot({{QStringLiteral("camera"), QStringLiteral("Demo")}, {QStringLiteral("operator"), QStringLiteral("test")}});
        check(!snap.value(QStringLiteral("ok")).toBool(), QStringLiteral("snapshot without video reports failure cleanly"));

        check(ctrl.log()->count() > 5, QStringLiteral("audit log records events (%1)").arg(ctrl.log()->count()));
        const QString csv = ctrl.log()->exportCsv();
        check(!csv.isEmpty() && QFile::exists(csv), QStringLiteral("audit log exports CSV"));
    });

    // Camera presets, aux switches, click-to-point, health and the camera profile.
    auto azBefore = std::make_shared<double>(0);
    QTimer::singleShot(6000, &ctrl, [&ctrl, check, azBefore]() {
        check(ctrl.presetsFromCamera() && ctrl.presets().size() == 5,
              QStringLiteral("preset list read from the camera (%1 presets)").arg(ctrl.presets().size()));
        check(ctrl.canRenamePreset(), QStringLiteral("demo camera can rename presets"));
        ctrl.renamePreset(2, QStringLiteral("Port side"));
        ctrl.ptz()->aux(QStringLiteral("wiper"), true, QStringLiteral("Wiper"));
        ctrl.ptz()->aux(QStringLiteral("stab"), true, QStringLiteral("Stabilisation"));
        *azBefore = ctrl.ptz()->az();
        ctrl.ptz()->pointAt(0.9, 0.5);

        check(CameraProfile::expand(QStringLiteral("/x/{ch}/{id}/{nope}"), {{QStringLiteral("ch"), 1}, {QStringLiteral("id"), 7}}, false)
                  == QLatin1String("/x/1/7/{nope}"), QStringLiteral("profile placeholders fill in, unknown ones are kept"));
        check(CameraProfile::expand(QStringLiteral("<n>{name}</n>"), {{QStringLiteral("name"), QStringLiteral("A<&>")}}, true)
                  == QLatin1String("<n>A&lt;&amp;&gt;</n>"), QStringLiteral("profile body values are XML-escaped"));
        const QVariantMap cpu{{QStringLiteral("warnAbove"), 85}, {QStringLiteral("critAbove"), 95}};
        check(HealthMonitor::levelFor(cpu, QStringLiteral("60")) == QLatin1String("ok")
                  && HealthMonitor::levelFor(cpu, QStringLiteral("90")) == QLatin1String("warn")
                  && HealthMonitor::levelFor(cpu, QStringLiteral("97")) == QLatin1String("crit"),
              QStringLiteral("health thresholds grade ok / warn / crit"));
        const QVariantMap sd{{QStringLiteral("okValues"), QStringList{QStringLiteral("ok")}}, {QStringLiteral("badLevel"), QStringLiteral("crit")}};
        check(HealthMonitor::levelFor(sd, QStringLiteral("OK")) == QLatin1String("ok")
                  && HealthMonitor::levelFor(sd, QStringLiteral("error")) == QLatin1String("crit"),
              QStringLiteral("health value lists grade by match"));

        // ---- optional credentials: a camera with no username and no password
        CameraConfig blank = CameraConfig::fromMap({{QStringLiteral("proto"), QStringLiteral("ONVIF")}, {QStringLiteral("ip"), QStringLiteral("10.0.0.5")}});
        check(blank.noCredentials() && blank.toMap(false).value(QStringLiteral("authOff")).toBool(),
              QStringLiteral("blank username and password: ONVIF camera config is valid and flagged 'authentication off'"));

        // ---- passwords go to the credential store, not the settings
        QVariantMap pw{{QStringLiteral("name"), QStringLiteral("Password test")}, {QStringLiteral("ip"), QStringLiteral("10.9.9.9")},
                       {QStringLiteral("proto"), QStringLiteral("ONVIF")}, {QStringLiteral("user"), QStringLiteral("operator")},
                       {QStringLiteral("pass"), QStringLiteral("s3cret-Pass-9")}};
        const QString pwId = ctrl.saveCamera(pw);
        const QByteArray stored = QSettings().value(QStringLiteral("cameras/list")).toByteArray();
        bool has = false;
        for (const QVariant &v : ctrl.cameras()) if (v.toMap().value(QStringLiteral("id")).toString() == pwId) has = v.toMap().value(QStringLiteral("hasPassword")).toBool();
        check(!stored.contains("s3cret-Pass-9") && has, QStringLiteral("a saved camera password is not in the settings store, yet the camera still has it"));
        ctrl.removeCamera(pwId);

        // ---- two-sensor camera: which profile is visible / thermal, main / sub
        QList<OnvifProfile> mock{
            {QStringLiteral("vis_sub"), QStringLiteral("VS1"), QString(), 704, 576, false},
            {QStringLiteral("th_main"), QStringLiteral("VS2"), QString(), 640, 512, false},
            {QStringLiteral("vis_main"), QStringLiteral("VS1"), QString(), 1920, 1080, true},
            {QStringLiteral("th_sub"), QStringLiteral("VS2"), QString(), 320, 256, false}};
        const auto plan = OnvifDriver::planStreams(mock);
        check(plan.size() == 2 && plan[0].source == QLatin1String("VS1") && mock[plan[0].mainIndex].token == QLatin1String("vis_main")
                  && mock[plan[0].subIndex].token == QLatin1String("vis_sub") && mock[plan[1].mainIndex].token == QLatin1String("th_main")
                  && mock[plan[1].subIndex].token == QLatin1String("th_sub") && OnvifDriver::ptzProfileIndex(mock) == 2,
              QStringLiteral("channel grouping: source 1 = visible, 2 = thermal, biggest = main, PTZ on the PTZ profile"));
    });
    QTimer::singleShot(7800, &ctrl, [&ctrl, check, azBefore]() {
        QString name;
        for (const QVariant &v : ctrl.presets())
            if (v.toMap().value(QStringLiteral("id")).toInt() == 2) name = v.toMap().value(QStringLiteral("name")).toString();
        check(name == QLatin1String("Port side"), QStringLiteral("preset renamed on the camera and in the list (%1)").arg(name));

        const QVariantMap aux = ctrl.ptz()->auxStates();
        check(aux.value(QStringLiteral("wiper")).toBool(), QStringLiteral("wiper switch on, state confirmed from the camera"));
        check(!aux.contains(QStringLiteral("stab")), QStringLiteral("unmapped stabilisation switch flips back after the camera refuses"));

        const double moved = std::fmod(ctrl.ptz()->az() - *azBefore + 540.0, 360.0) - 180.0;
        check(moved > 10.0, QStringLiteral("click to point at the right edge turns the camera right (%1 deg)").arg(moved));

        QString uptime, cpuLevel;
        for (const QVariant &v : ctrl.health()->items()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("id")).toString() == QLatin1String("uptime")) uptime = m.value(QStringLiteral("text")).toString();
            if (m.value(QStringLiteral("id")).toString() == QLatin1String("cpu")) cpuLevel = m.value(QStringLiteral("level")).toString();
        }
        check(uptime == QLatin1String("1d 2h") && cpuLevel == QLatin1String("ok"),
              QStringLiteral("camera health read from the profile (uptime %1, cpu %2)").arg(uptime, cpuLevel));
        check(ctrl.profile().contains(QStringLiteral("ptz")) && ctrl.profile().contains(QStringLiteral("aux")),
              QStringLiteral("camera profile loaded"));
        check(!ctrl.coordinator()->enabled() && ctrl.ptz()->controlHeld(),
              QStringLiteral("team control is off without a key, local control unchanged"));
    });
    // Zoom speed: the zoom command is the input times the zoom speed, and is capped like pan/tilt on a weak link.
    QTimer::singleShot(8000, &ctrl, [&ctrl, check, win]() {
        // Controls on / hidden / full screen: where the chips on the picture may sit, and what the title bar does.
        if (QObject *live = win ? win->findChild<QObject *>(QStringLiteral("liveView")) : nullptr) {
            const double shownLeft = live->property("contentLeft").toDouble();
            const bool shownBar = live->property("ov").toBool();
            live->setProperty("overlays", false);
            const double hiddenLeft = live->property("contentLeft").toDouble();
            const bool hiddenBar = live->property("ov").toBool();
            live->setProperty("overlays", true);
            live->setProperty("isFullScreen", true);
            const bool fsBar = live->property("ov").toBool();
            live->setProperty("isFullScreen", false);
            check(shownBar && !hiddenBar && !fsBar && shownLeft > hiddenLeft && hiddenLeft == 14.0,
                  QStringLiteral("overlays: shown, hidden with H, and gone in full screen; the picture's chips move out to the edge when hidden (%1 -> %2)").arg(shownLeft).arg(hiddenLeft));
        } else {
            check(false, QStringLiteral("live view found for the overlay check"));
        }
        auto *p = ctrl.ptz();
        p->setZoomSpeed(60);
        p->move(0, 0, 1.0, QStringLiteral("test"));
        const int full = p->targetZoom();
        p->move(0, 0, -0.5, QStringLiteral("test"));
        const int half = p->targetZoom();
        check(full == 60 && half == -30, QStringLiteral("zoom command = input x zoom speed (%1 and %2 at speed 60)").arg(full).arg(half));
        p->setZoomSpeed(100);
        p->move(0, 0, 1.0, QStringLiteral("test"));
        check(p->targetZoom() == 100, QStringLiteral("zoom speed 100 drives the lens at full rate (%1)").arg(p->targetZoom()));
        p->setZoomSpeed(60);
        ctrl.link()->setSimulate(QStringLiteral("degraded"));
        p->move(0, 0, 1.0, QStringLiteral("test"));
        check(p->effectiveZoomSpeed() == 30 && p->targetZoom() == 30, QStringLiteral("a degraded link caps the zoom at 30 (%1)").arg(p->targetZoom()));
        p->setZoomSpeed(20);
        p->move(0, 0, 1.0, QStringLiteral("test"));
        check(p->targetZoom() == 20, QStringLiteral("below the cap the slider value is used (%1)").arg(p->targetZoom()));
        ctrl.link()->setSimulate(QString());
        p->setZoomSpeed(60);
        p->stopAll();
    });
    QTimer::singleShot(8600, &ctrl, [&ctrl, check]() {
        check(ctrl.ptz()->canZoomLevel(), QStringLiteral("the camera can set a zoom level directly"));
        ctrl.ptz()->setZoomLevel(12.0);
    });
    QTimer::singleShot(11400, &ctrl, [&ctrl, check, fails]() {
        check(std::fabs(ctrl.ptz()->zoom() - 12.0) < 0.3, QStringLiteral("zoom level slider: the lens went to 12x (reads %1)").arg(ctrl.ptz()->zoom()));
        qInfo().noquote() << "SELFTEST failures:" << *fails;
        QCoreApplication::exit(*fails);
    });
}

// Team control end to end: the app's coordinator against a second "laptop" in this
// process. Run with CORINTH_SELFTEST=2 and CORINTH_INSTANCE, CORINTH_CONTROL_KEY,
// CORINTH_CONTROL_PORT (this app), CORINTH_CONTROL_PEERS=127.0.0.1:<peer port>,
// CORINTH_TEST_PEER_PORT (the second laptop).
static void waitUntil(QObject *ctx, std::function<bool()> cond, std::function<void(bool)> then, int timeoutMs, int elapsed = 0)
{
    if (cond()) { then(true); return; }
    if (elapsed >= timeoutMs) { then(false); return; }
    QTimer::singleShot(100, ctx, [=]() { waitUntil(ctx, cond, then, timeoutMs, elapsed + 100); });
}

static void runTeamTest(AppController &ctrl)
{
    auto fails = std::make_shared<int>(0);
    auto check = [fails](bool ok, const QString &what) {
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << what;
        if (!ok) ++*fails;
    };
    auto *ctl = ctrl.coordinator();
    auto *ptz = ctrl.ptz();
    ctrl.setActive(QStringLiteral("demo"));

    ControlCoordinator::Config c;
    c.enabled = true;
    c.key = qEnvironmentVariable("CORINTH_CONTROL_KEY");
    c.port = qEnvironmentVariable("CORINTH_TEST_PEER_PORT").toUShort();
    c.peers << QStringLiteral("127.0.0.1:%1").arg(qEnvironmentVariable("CORINTH_CONTROL_PORT"));
    c.operatorName = QStringLiteral("Other laptop");
    c.host = QStringLiteral("other-pc");
    c.instanceId = QStringLiteral("zz-other");
    c.multicast = false;
    auto other = std::make_shared<ControlCoordinator>(c);

    check(ctl->enabled() && ctl->state() == QLatin1String("listening") && ptz->locked(),
          QStringLiteral("with a team key the app starts locked, listening for other operators (%1)").arg(ctl->state()));

    waitUntil(&ctrl, [ctl]() { return ctl->isMine(); }, [=, &ctrl](bool mine) {
        check(mine && !ptz->locked(), QStringLiteral("alone on the camera it takes control and unlocks"));
        other->setCamera(QStringLiteral("demo"));
        waitUntil(&ctrl, [other]() { return other->state() == QLatin1String("other"); }, [=, &ctrl](bool seen) {
            check(seen && other->holderName() == ptz->operatorName(), QStringLiteral("the second laptop sees who is in control"));
            ptz->move(0.5, 0, 0, QStringLiteral("test"));
            check(ptz->moving(), QStringLiteral("the holder can move the camera"));
            other->claim();
            waitUntil(&ctrl, [ctl]() { return !ctl->requesterName().isEmpty(); }, [=, &ctrl](bool asked) {
                check(asked && ctl->requesterName() == QLatin1String("Other laptop"), QStringLiteral("the holder is shown who is asking"));
                ctl->grant();
                check(ptz->locked() && !ptz->moving() && ptz->lastText() == QLatin1String("Stop (control handed over)"),
                      QStringLiteral("handing over stops the camera and locks this laptop (%1)").arg(ptz->lastText()));
                ptz->gotoPreset(1, QStringLiteral("Bow"));
                check(ptz->lastState() == QLatin1String("blocked") && ptz->lastError() == QLatin1String("View only"),
                      QStringLiteral("a view-only laptop cannot send commands (%1)").arg(ptz->lastError()));
                waitUntil(&ctrl, [other]() { return other->isMine(); }, [=, &ctrl](bool granted) {
                    check(granted, QStringLiteral("the second laptop receives control"));
                    ptz->takeControl();
                    check(ctl->state() == QLatin1String("requesting") && ptz->locked(), QStringLiteral("asking while another holds it waits, still locked"));
                    waitUntil(&ctrl, [other]() { return !other->requesterName().isEmpty(); }, [=, &ctrl](bool asked2) {
                        check(asked2, QStringLiteral("the other laptop gets the request"));
                        other->grant();
                        waitUntil(&ctrl, [ctl]() { return ctl->isMine(); }, [=, &ctrl](bool back) {
                            check(back && !ptz->locked() && ptz->lastText() == QLatin1String("Stop (took control)"),
                                  QStringLiteral("a granted request unlocks this laptop and sends a stop first (%1)").arg(ptz->lastText()));
                            other->takeOver();
                            waitUntil(&ctrl, [ctl]() { return !ctl->isMine(); }, [=](bool lost) {
                                check(lost && ptz->locked(), QStringLiteral("an override by the other laptop locks this one at once"));
                                qInfo().noquote() << "TEAM SELFTEST failures:" << *fails;
                                QCoreApplication::exit(*fails);
                            }, 5000);
                        }, 8000);
                    }, 5000);
                }, 5000);
            }, 8000);
        }, 8000);
    }, 12000);
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Corinth"));
    // CORINTH_INSTANCE=<name> gives this run its own settings, logs and camera list, so tests
    // and a second copy on the same PC (to try team control) never touch the real ones.
    // The self-test and screenshot modes change settings (bindings, theme, presets), so they
    // default to their own store, "Corinth PTZ test", and never touch the real cameras or bindings.
    QString instance = qEnvironmentVariable("CORINTH_INSTANCE");
    if (instance.isEmpty() && (!qEnvironmentVariable("CORINTH_SELFTEST").isEmpty() || !qEnvironmentVariable("CORINTH_SHOTS").isEmpty()))
        instance = QStringLiteral("test");
    QGuiApplication::setApplicationName(instance.isEmpty() ? QStringLiteral("Corinth PTZ")
                                                           : QStringLiteral("Corinth PTZ ") + instance);
    QGuiApplication::setApplicationVersion(QStringLiteral(APP_VERSION_STRING));

    // Basic style so every control can be fully restyled to the Corinth design.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    AppController controller;

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));

    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("app"), &controller);
    ctx->setContextProperty(QStringLiteral("ptz"), controller.ptz());
    ctx->setContextProperty(QStringLiteral("link"), controller.link());
    ctx->setContextProperty(QStringLiteral("auditLog"), controller.log());
    ctx->setContextProperty(QStringLiteral("capture"), controller.capture());
    ctx->setContextProperty(QStringLiteral("discovery"), controller.discovery());
    ctx->setContextProperty(QStringLiteral("joystick"), controller.joystick());
    ctx->setContextProperty(QStringLiteral("health"), controller.health());
    ctx->setContextProperty(QStringLiteral("control"), controller.coordinator());

    engine.load(QUrl(QStringLiteral("qrc:/Corinth/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    // CORINTH_WINDOW=1920x1080 opens the window at that size (for screenshots and layout checks).
    const QString winSize = qEnvironmentVariable("CORINTH_WINDOW");
    if (!winSize.isEmpty()) {
        const QStringList wh = winSize.split(QLatin1Char('x'));
        if (wh.size() == 2)
            if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0))) w->resize(wh.at(0).toInt(), wh.at(1).toInt());
    }

    const QString shots = qEnvironmentVariable("CORINTH_SHOTS");
    if (!shots.isEmpty())
        runScreenshotTour(engine, controller, shots);
    const QString selfTest = qEnvironmentVariable("CORINTH_SELFTEST");
    if (selfTest == QLatin1String("2"))
        runTeamTest(controller);
    else if (!selfTest.isEmpty())
        runSelfTest(engine, controller);

    return app.exec();
}
