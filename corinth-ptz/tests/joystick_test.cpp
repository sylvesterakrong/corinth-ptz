// Joystick detection with a virtual SDL joystick that looks like a flight stick (a
// device Windows' XInput would never show): it is listed with its real counts, can be
// picked and is remembered by GUID, its buttons and hat arrive as raw ids whatever
// diagram is selected, its stick drives pan/tilt, and unplugging it stops the camera.
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "JoystickManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <functional>

static int g_fail = 0;

static void check(bool ok, const QString &what)
{
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << what;
    if (!ok) ++g_fail;
}

static bool waitFor(const std::function<bool()> &cond, int timeoutMs = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

static int attachVirtual(const char *name)
{
    SDL_VirtualJoystickDesc d;
    SDL_zero(d);
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_FLIGHT_STICK;
    d.naxes = 4;
    d.nbuttons = 12;
    d.nhats = 1;
    d.vendor_id = 0x1234;
    d.product_id = 0x5678;
    d.name = name;
    return SDL_JoystickAttachVirtualEx(&d);
}

static QVariantMap findDevice(const JoystickManager &jm, const QString &name)
{
    for (const QVariant &v : jm.devices())
        if (v.toMap().value(QStringLiteral("name")).toString() == name) return v.toMap();
    return {};
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("CorinthTest"));
    QCoreApplication::setApplicationName(QStringLiteral("joystick_test"));
    QTemporaryDir tmp;   // settings go to a throwaway ini file, never the registry
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());

    JoystickManager jm;
    check(jm.available() && jm.backend() == QLatin1String("SDL2"), QStringLiteral("SDL2 is the backend (not XInput)"));

    QStringList raw, pressedActions, releasedActions;
    QList<QVector<double>> axisCmds;
    QObject::connect(&jm, &JoystickManager::rawInput, [&](const QString &i) { raw << i; });
    QObject::connect(&jm, &JoystickManager::actionPressed, [&](const QString &a) { pressedActions << a; });
    QObject::connect(&jm, &JoystickManager::actionReleased, [&](const QString &a) { releasedActions << a; });
    QObject::connect(&jm, &JoystickManager::axisCommand, [&](double p, double t, double z) { axisCmds.append({p, t, z}); });

    // ---- plug in a device that is not a gamepad ------------------------------------
    const QString name = QStringLiteral("Test Flight Stick");
    const int index = attachVirtual("Test Flight Stick");
    check(index >= 0, QStringLiteral("virtual flight stick attached (%1)").arg(QString::fromUtf8(SDL_GetError())));
    check(waitFor([&] { return !findDevice(jm, name).isEmpty(); }), QStringLiteral("hot-plugged device appears in the device list"));
    const QVariantMap dev = findDevice(jm, name);
    check(dev.value(QStringLiteral("axes")).toInt() == 4 && dev.value(QStringLiteral("buttons")).toInt() == 12
              && dev.value(QStringLiteral("hats")).toInt() == 1 && !dev.value(QStringLiteral("gamepad")).toBool(),
          QStringLiteral("listed with 4 axes, 12 buttons, 1 hat, not a gamepad"));
    const QString guid = dev.value(QStringLiteral("guid")).toString();
    check(!guid.isEmpty(), QStringLiteral("it has a GUID (%1)").arg(guid));

    jm.selectDevice(guid);
    check(waitFor([&] { return jm.connected() && jm.deviceName() == name; }), QStringLiteral("picking it connects it by name"));
    check(jm.selectedGuid() == guid && jm.axisCount() == 4 && jm.buttonCount() == 12 && jm.hatCount() == 1 && !jm.standardMapping(),
          QStringLiteral("the open device reports its counts and joystick (not gamepad) mapping"));
    check(jm.suggestedType() == QLatin1String("flight") || jm.suggestedType() == QLatin1String("generic"),
          QStringLiteral("a non-gamepad suggests a joystick type, not Xbox (%1)").arg(jm.suggestedType()));

    SDL_Joystick *vjs = SDL_JoystickOpen(index);
    check(vjs != nullptr, QStringLiteral("test handle to drive the virtual stick"));

    // ---- raw ids while assigning, whatever diagram is selected ----------------------
    jm.setDeviceType(QStringLiteral("xbox"));   // a pad diagram on a non-pad device
    jm.setListening(true);
    raw.clear();
    SDL_JoystickSetVirtualButton(vjs, 2, 1);
    check(waitFor([&] { return raw.contains(QStringLiteral("b3")); }), QStringLiteral("button 3 arrives as b3 under the Xbox diagram (%1)").arg(raw.join(QLatin1Char(','))));
    SDL_JoystickSetVirtualButton(vjs, 2, 0);
    waitFor([&] { return !jm.pressed().contains(QStringLiteral("b3")); });
    raw.clear();
    SDL_JoystickSetVirtualHat(vjs, 0, SDL_HAT_UP);
    check(waitFor([&] { return raw.contains(QStringLiteral("hatU")); }), QStringLiteral("the hat arrives as hatU"));
    SDL_JoystickSetVirtualHat(vjs, 0, SDL_HAT_CENTERED);
    waitFor([&] { return !jm.pressed().contains(QStringLiteral("hatU")); });

    jm.setDeviceType(QStringLiteral("flight"));
    raw.clear();
    SDL_JoystickSetVirtualButton(vjs, 0, 1);
    check(waitFor([&] { return raw.contains(QStringLiteral("trigger")); }), QStringLiteral("under the flight diagram button 1 keeps its name, trigger (%1)").arg(raw.join(QLatin1Char(','))));
    SDL_JoystickSetVirtualButton(vjs, 0, 0);
    waitFor([&] { return !jm.pressed().contains(QStringLiteral("trigger")); });
    jm.setListening(false);

    // ---- actions and axes -------------------------------------------------------------
    jm.setDeviceType(QStringLiteral("generic"));
    jm.restoreDefaults();
    pressedActions.clear();
    releasedActions.clear();
    SDL_JoystickSetVirtualButton(vjs, 0, 1);   // generic default: b1 = snapshot
    check(waitFor([&] { return pressedActions.contains(QStringLiteral("snapshot")); }), QStringLiteral("a bound button triggers its action"));
    SDL_JoystickSetVirtualButton(vjs, 0, 0);
    check(waitFor([&] { return releasedActions.contains(QStringLiteral("snapshot")); }), QStringLiteral("releasing it ends the action"));

    axisCmds.clear();
    SDL_JoystickSetVirtualAxis(vjs, 0, 32000);   // stick right
    check(waitFor([&] { for (const auto &c : axisCmds) if (c[0] > 0.5) return true; return false; }),
          QStringLiteral("deflecting the stick sends pan"));
    check(jm.axes().value(0).toDouble() > 0.9, QStringLiteral("the axis bars follow the stick (%1)").arg(jm.axes().value(0).toDouble()));

    // ---- unplug while the stick is deflected: the camera must be told to stop ----------
    axisCmds.clear();
    SDL_JoystickClose(vjs);
    vjs = nullptr;
    SDL_JoystickDetachVirtual(index);
    check(waitFor([&] { return !jm.connected(); }), QStringLiteral("unplugging disconnects"));
    check(waitFor([&] { return !axisCmds.isEmpty() && axisCmds.last()[0] == 0 && axisCmds.last()[1] == 0 && axisCmds.last()[2] == 0; }),
          QStringLiteral("unplugging mid-move sends a stop (0, 0, 0)"));
    check(waitFor([&] { return findDevice(jm, name).isEmpty(); }), QStringLiteral("the device leaves the list"));

    // ---- plugged back in: the remembered GUID is picked again ---------------------------
    const int again = attachVirtual("Test Flight Stick");
    check(again >= 0 && waitFor([&] { return jm.connected() && jm.selectedGuid() == guid; }),
          QStringLiteral("plugging it back in selects it again by GUID"));
    SDL_JoystickDetachVirtual(again);

    qInfo().noquote() << "JOYSTICK TEST failures:" << g_fail;
    return g_fail;
}
