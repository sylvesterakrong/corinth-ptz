#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QDir>
#include <QFile>
#include <QJSValue>
#include <QTimer>
#include <functional>
#include <memory>

#include "AppController.h"
#include "AuditLog.h"
#include "PtzController.h"
#include "LinkMonitor.h"
#include "CaptureService.h"
#include "Discovery.h"
#include "JoystickManager.h"

// Developer aid: CORINTH_SHOTS=<folder> walks through the screens and themes,
// saves a PNG of each, then quits. Useful for design review and CI.
static void runScreenshotTour(QQmlApplicationEngine &engine, AppController &ctrl, const QString &dir)
{
    QDir().mkpath(dir);
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!win) return;
    QObject *settings = win->findChild<QObject *>(QStringLiteral("settingsPage"));

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
           << Step{QStringLiteral("12-input-kb-night"), [win, &ctrl]() { ctrl.setTheme(QStringLiteral("night")); ctrl.joystick()->setDeviceType(QStringLiteral("kb")); }};

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
    QTimer::singleShot(5600, &ctrl, [fails]() {
        qInfo().noquote() << "SELFTEST failures:" << *fails;
        QCoreApplication::exit(*fails);
    });
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Corinth"));
    QGuiApplication::setApplicationName(QStringLiteral("Corinth PTZ"));
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

    engine.load(QUrl(QStringLiteral("qrc:/Corinth/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    const QString shots = qEnvironmentVariable("CORINTH_SHOTS");
    if (!shots.isEmpty())
        runScreenshotTour(engine, controller, shots);
    if (!qEnvironmentVariable("CORINTH_SELFTEST").isEmpty())
        runSelfTest(engine, controller);

    return app.exec();
}
