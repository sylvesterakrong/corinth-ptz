#include "AppController.h"

#include "AuditLog.h"
#include "CaptureService.h"
#include "Discovery.h"
#include "JoystickManager.h"
#include "LinkMonitor.h"
#include "PtzController.h"
#include "drivers/DemoDriver.h"
#include "drivers/IsapiDriver.h"
#include "drivers/OnvifDriver.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>
#include <QUuid>
#include <memory>

AppController::AppController(QObject *parent) : QObject(parent)
{
    m_log = new AuditLog(this);
    m_ptz = new PtzController(m_log, this);
    m_link = new LinkMonitor(m_log, this);
    m_capture = new CaptureService(m_log, this);
    m_discovery = new Discovery(this);
    m_joy = new JoystickManager(this);
    m_holdTimer = new QTimer(this);
    m_holdTimer->setSingleShot(true);
    m_holdTimer->setInterval(1000);

    QSettings s;
    m_theme = s.value(QStringLiteral("app/theme"), QStringLiteral("dark")).toString();
    m_userStream = s.value(QStringLiteral("app/userStream"), QStringLiteral("main")).toString();
    const QString op = s.value(QStringLiteral("app/operatorName")).toString();
    if (!op.isEmpty()) m_ptz->setOperatorName(op);
    connect(m_ptz, &PtzController::operatorChanged, this, [this]() {
        QSettings().setValue(QStringLiteral("app/operatorName"), m_ptz->operatorName());
    });

    // Link state drives the PTZ safety rules and the stream choice.
    connect(m_link, &LinkMonitor::stateChanged, this, [this]() {
        m_ptz->setLinkState(m_link->state());
        emit streamChanged();
    });

    // Physical joystick -> camera.
    connect(m_joy, &JoystickManager::axisCommand, this, [this](double p, double t, double z) {
        m_ptz->move(p, t, z, QStringLiteral("joystick"));
    });
    connect(m_joy, &JoystickManager::actionPressed, this, &AppController::onJoystickPressed);
    connect(m_joy, &JoystickManager::actionReleased, this, &AppController::onJoystickReleased);
    connect(m_holdTimer, &QTimer::timeout, this, [this]() {
        if (m_ptz->controlHeld()) m_ptz->releaseControl(); else m_ptz->takeControl();
    });

    loadCameras();
    rebuildDriver();
    m_log->add(QStringLiteral("System"), QStringLiteral("Corinth PTZ %1 started").arg(version()),
               QStringLiteral("Joystick backend: ") + m_joy->backend());
}

AppController::~AppController()
{
    if (m_driver) m_driver->deleteLater();
}

QString AppController::version() const
{
    return QCoreApplication::applicationVersion();
}

void AppController::loadCameras()
{
    QSettings s;
    const QJsonArray arr = QJsonDocument::fromJson(s.value(QStringLiteral("cameras/list")).toByteArray()).array();
    m_cams.clear();
    for (const QJsonValue &v : arr)
        m_cams.append(CameraConfig::fromMap(v.toObject().toVariantMap()));

    if (m_cams.isEmpty()) {
        // First run: a demo camera so the app works without hardware, plus the
        // CJ3000 at its Hikvision factory address, ready to have a password set.
        CameraConfig demo;
        demo.id = QStringLiteral("demo"); demo.name = QStringLiteral("Demo camera"); demo.ip = QStringLiteral("127.0.0.1");
        demo.proto = QStringLiteral("Demo"); demo.shown = true;
        CameraConfig cj;
        cj.id = QStringLiteral("cj3000"); cj.name = QStringLiteral("CJ3000"); cj.ip = QStringLiteral("192.168.1.64");
        cj.proto = QStringLiteral("ISAPI"); cj.user = QStringLiteral("admin"); cj.shown = true;
        m_cams << demo << cj;
        saveCameras();
    }
    m_activeId = s.value(QStringLiteral("cameras/active"), m_cams.first().id).toString();
    if (indexOf(m_activeId) < 0) m_activeId = m_cams.first().id;
}

void AppController::saveCameras()
{
    // Note: passwords are stored in the user's QSettings store (registry on
    // Windows). For field use, move them to the Windows Credential Manager.
    QJsonArray arr;
    for (const CameraConfig &c : m_cams)
        arr.append(QJsonObject::fromVariantMap(c.toMap(true)));
    QSettings s;
    s.setValue(QStringLiteral("cameras/list"), QJsonDocument(arr).toJson(QJsonDocument::Compact));
    s.setValue(QStringLiteral("cameras/active"), m_activeId);
}

int AppController::indexOf(const QString &id) const
{
    for (int i = 0; i < m_cams.size(); ++i)
        if (m_cams.at(i).id == id) return i;
    return -1;
}

QVariantList AppController::cameras() const
{
    QVariantList l;
    for (const CameraConfig &c : m_cams) l << c.toMap(false);
    return l;
}

QVariantMap AppController::activeCamera() const
{
    const int i = indexOf(m_activeId);
    return i >= 0 ? m_cams.at(i).toMap(false) : QVariantMap();
}

QString AppController::driverKind() const
{
    return m_driver ? m_driver->kind() : QString();
}

CameraDriver *AppController::makeDriver(const CameraConfig &cfg, QObject *parent) const
{
    if (cfg.proto == QLatin1String("Demo")) return new DemoDriver(cfg, parent);
    if (cfg.proto == QLatin1String("ONVIF")) return new OnvifDriver(cfg, parent);
    if (cfg.proto == QLatin1String("RTSP only")) return new RtspOnlyDriver(cfg, parent);
    return new IsapiDriver(cfg, parent);
}

void AppController::rebuildDriver()
{
    m_ptz->stopAll();
    if (m_driver) m_driver->deleteLater();
    m_driver = nullptr;
    const int i = indexOf(m_activeId);
    if (i >= 0) {
        m_driver = makeDriver(m_cams.at(i), this);
        connect(m_driver, &CameraDriver::streamUrlsChanged, this, &AppController::streamChanged);
        m_log->add(QStringLiteral("System"), QStringLiteral("Connected to %1 (%2)").arg(m_cams.at(i).name, m_driver->kind()),
                   m_cams.at(i).proto == QLatin1String("Demo") ? QStringLiteral("Simulated") : m_cams.at(i).ip);
    }
    m_ptz->setDriver(m_driver);
    m_link->setDriver(m_driver);
    emit activeChanged();
    emit streamChanged();
    emit presetsChanged();
}

QString AppController::effectiveStream() const
{
    return streamForced() ? QStringLiteral("sub") : m_userStream;
}

bool AppController::streamForced() const
{
    return m_link->state() != QLatin1String("ok");
}

QString AppController::streamUrl() const
{
    if (!m_driver) return {};
    return m_driver->streamUrl(effectiveStream() == QLatin1String("main"), m_channel);
}

QString AppController::recordUrl() const
{
    // Record the main stream when the link allows; it is the evidence copy.
    if (!m_driver) return {};
    return m_driver->streamUrl(!streamForced(), m_channel);
}

void AppController::setUserStream(const QString &s)
{
    const QString v = s == QLatin1String("sub") ? s : QStringLiteral("main");
    if (v == m_userStream) return;
    m_userStream = v;
    QSettings().setValue(QStringLiteral("app/userStream"), v);
    emit streamChanged();
}

void AppController::setChannel(int c)
{
    c = qBound(1, c, 2);
    if (c == m_channel) return;
    m_channel = c;
    emit streamChanged();
}

void AppController::setTheme(const QString &t)
{
    if (t == m_theme) return;
    m_theme = t;
    QSettings().setValue(QStringLiteral("app/theme"), t);
    emit themeChanged();
}

QStringList AppController::presetNames() const
{
    const QStringList def = {QStringLiteral("Bow"), QStringLiteral("Port beam"), QStringLiteral("Starboard beam"),
                             QStringLiteral("Stern"), QStringLiteral("Horizon scan")};
    return QSettings().value(QStringLiteral("presets/") + m_activeId, def).toStringList();
}

void AppController::setPresetName(int index, const QString &name)
{
    QStringList l = presetNames();
    while (l.size() <= index) l << QStringLiteral("Preset %1").arg(l.size() + 1);
    l[index] = name.trimmed().isEmpty() ? QStringLiteral("Preset %1").arg(index + 1) : name.trimmed();
    QSettings().setValue(QStringLiteral("presets/") + m_activeId, l);
    emit presetsChanged();
}

QString AppController::saveCamera(const QVariantMap &cam)
{
    CameraConfig c = CameraConfig::fromMap(cam);
    if (c.name.trimmed().isEmpty()) c.name = c.ip;
    int i = indexOf(c.id);
    if (c.id.isEmpty() || i < 0) {
        if (c.id.isEmpty() || c.id.startsWith(QLatin1String("manual-")) || c.id.startsWith(QLatin1String("found-")))
            c.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        m_cams.append(c);
        m_log->add(m_ptz->operatorName(), QStringLiteral("Camera added · ") + c.name, c.ip + QStringLiteral(" · ") + c.proto);
    } else {
        if (c.pass.isEmpty()) c.pass = m_cams.at(i).pass;   // blank = keep the stored password
        m_cams[i] = c;
        m_log->add(m_ptz->operatorName(), QStringLiteral("Camera updated · ") + c.name, c.ip + QStringLiteral(" · ") + c.proto);
    }
    saveCameras();
    emit camerasChanged();
    if (c.id == m_activeId) rebuildDriver();
    return c.id;
}

void AppController::removeCamera(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0 || m_cams.size() <= 1) return;
    m_cams.removeAt(i);
    if (id == m_activeId) { m_activeId = m_cams.first().id; rebuildDriver(); }
    saveCameras();
    emit camerasChanged();
}

void AppController::setCameraShown(const QString &id, bool shown)
{
    const int i = indexOf(id);
    if (i < 0) return;
    m_cams[i].shown = shown;
    saveCameras();
    emit camerasChanged();
}

void AppController::setActive(const QString &id)
{
    if (id == m_activeId || indexOf(id) < 0) return;
    m_activeId = id;
    saveCameras();
    rebuildDriver();
}

void AppController::request(const QString &method, const QString &path, const QString &body, QJSValue callback)
{
    if (!m_driver) {
        if (callback.isCallable()) callback.call({QJSValue(false), QJSValue(0), QJSValue(QStringLiteral("No camera selected")), QJSValue(0)});
        return;
    }
    m_driver->request(method, path, body.toUtf8(), [callback](bool ok, int status, const QByteArray &data, int ms) mutable {
        if (callback.isCallable())
            callback.call({QJSValue(ok), QJSValue(status), QJSValue(QString::fromUtf8(data)), QJSValue(ms)});
    });
}

void AppController::testConnection(const QVariantMap &cam, QJSValue callback)
{
    CameraConfig c = CameraConfig::fromMap(cam);
    if (c.pass.isEmpty()) {
        const int i = indexOf(c.id);
        if (i >= 0) c.pass = m_cams.at(i).pass;
    }
    auto reply = [callback](bool ok, const QString &msg) mutable {
        if (callback.isCallable()) callback.call({QJSValue(ok), QJSValue(msg)});
    };
    CameraDriver *d = makeDriver(c, this);
    auto done = std::make_shared<bool>(false);
    auto finish = [d, done, reply](bool ok, const QString &msg) mutable {
        if (*done) return;
        *done = true;
        reply(ok, msg);
        d->deleteLater();
    };
    QTimer::singleShot(6000, d, [finish]() mutable { finish(false, QStringLiteral("No answer within 6 s · check IP, port and network")); });

    if (c.proto == QLatin1String("Demo")) {
        finish(true, QStringLiteral("Demo camera · simulated"));
    } else if (c.proto == QLatin1String("ISAPI")) {
        d->request(QStringLiteral("GET"), QStringLiteral("/ISAPI/System/deviceInfo"), {},
                   [finish](bool ok, int status, const QByteArray &data, int ms) mutable {
                       if (ok) {
                           const QString model = Xml::tagText(QString::fromUtf8(data), QStringLiteral("model"));
                           finish(true, QStringLiteral("Connected · ISAPI · %1 · %2 ms").arg(model.isEmpty() ? QStringLiteral("model unknown") : model).arg(ms));
                       } else if (status == 401) {
                           finish(false, QStringLiteral("Authentication failed · check username and password (repeated failures lock the camera)"));
                       } else {
                           finish(false, status ? QStringLiteral("HTTP %1 · is ISAPI enabled?").arg(status) : QString::fromUtf8(data).left(140));
                       }
                   });
    } else if (c.proto == QLatin1String("ONVIF")) {
        connect(d, &CameraDriver::streamUrlsChanged, d, [finish]() mutable {
            finish(true, QStringLiteral("Connected · ONVIF · profiles and stream address read"));
        });
    } else {
        connect(d, &CameraDriver::pingResult, d, [finish](bool ok, int ms) mutable {
            finish(ok, ok ? QStringLiteral("RTSP port reachable · %1 ms").arg(ms) : QStringLiteral("RTSP port not reachable"));
        });
        d->ping();
    }
}

QVariant AppController::localValue(const QString &key, const QVariant &fallback) const
{
    return QSettings().value(QStringLiteral("local/") + key, fallback);
}

void AppController::setLocalValue(const QString &key, const QVariant &value)
{
    QSettings().setValue(QStringLiteral("local/") + key, value);
}

void AppController::onJoystickPressed(const QString &action)
{
    if (action == QLatin1String("stop")) m_ptz->stopAll();
    else if (action == QLatin1String("focusNear")) m_ptz->focus(-1);
    else if (action == QLatin1String("focusFar")) m_ptz->focus(1);
    else if (action == QLatin1String("onePush")) m_ptz->onePushFocus();
    else if (action == QLatin1String("home")) m_ptz->home();
    else if (action == QLatin1String("control")) m_holdTimer->start();   // hold 1 s
    else emit uiAction(action);
}

void AppController::onJoystickReleased(const QString &action)
{
    if (action == QLatin1String("focusNear") || action == QLatin1String("focusFar")) m_ptz->focus(0);
    else if (action == QLatin1String("control")) m_holdTimer->stop();
}
