#include "AppController.h"

#include "AuditLog.h"
#include "CredentialStore.h"
#include "drivers/TlsPin.h"
#include "CaptureService.h"
#include "ControlCoordinator.h"
#include "Discovery.h"
#include "HealthMonitor.h"
#include "JoystickManager.h"
#include "LinkMonitor.h"
#include "PtzController.h"
#include "drivers/DemoDriver.h"
#include "drivers/IsapiDriver.h"
#include "drivers/OnvifDriver.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <algorithm>
#include <memory>

AppController::AppController(QObject *parent) : QObject(parent)
{
    m_log = new AuditLog(this);
    m_ptz = new PtzController(m_log, this);
    m_link = new LinkMonitor(m_log, this);
    m_capture = new CaptureService(m_log, this);
    m_discovery = new Discovery(this);
    m_joy = new JoystickManager(this);
    m_health = new HealthMonitor(m_log, this);
    m_coord = new ControlCoordinator(this);
    m_ptz->setCoordinator(m_coord);
    m_holdTimer = new QTimer(this);
    m_holdTimer->setSingleShot(true);
    m_holdTimer->setInterval(1000);
    m_slowTimer = new QTimer(this);
    m_slowTimer->setInterval(15000);
    connect(m_slowTimer, &QTimer::timeout, this, &AppController::slowPoll);
    m_presetTimer = new QTimer(this);
    m_presetTimer->setSingleShot(true);
    m_presetTimer->setInterval(300);
    connect(m_presetTimer, &QTimer::timeout, this, &AppController::refreshPresets);

    QSettings s;
    m_theme = s.value(QStringLiteral("app/theme"), QStringLiteral("dark")).toString();
    m_userStream = s.value(QStringLiteral("app/userStream"), QStringLiteral("main")).toString();
    m_pipEnabled = s.value(QStringLiteral("app/pip"), false).toBool();
    const QString op = s.value(QStringLiteral("app/operatorName")).toString();
    if (!op.isEmpty()) m_ptz->setOperatorName(op);
    m_coord->setOperatorName(m_ptz->operatorName());
    connect(m_ptz, &PtzController::operatorChanged, this, [this]() {
        QSettings().setValue(QStringLiteral("app/operatorName"), m_ptz->operatorName());
        m_coord->setOperatorName(m_ptz->operatorName());
    });
    m_coord->reload();
    connect(m_coord, &ControlCoordinator::notice, this, [this](const QString &text, const QString &kind) {
        m_log->add(m_ptz->operatorName(), text, kind == QLatin1String("warn") ? QStringLiteral("Control change") : QStringLiteral("OK"), kind);
    });

    // Link state drives the PTZ safety rules and the stream choice.
    connect(m_link, &LinkMonitor::stateChanged, this, [this]() {
        m_ptz->setLinkState(m_link->state());
        m_health->setLinkState(m_link->state());
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
    bool migrate = false;
    for (const QJsonValue &v : arr) {
        CameraConfig c = CameraConfig::fromMap(v.toObject().toVariantMap());
        if (!c.pass.isEmpty())
            migrate = true;   // saved by an older version with the password inside the settings
        else
            c.pass = CredentialStore::load(credentialTarget(c.id));
        m_cams.append(c);
    }

    if (m_cams.isEmpty()) {
        // First run: a demo camera so the app works without hardware, plus the
        // CJ3000 at its Hikvision factory address, ready to have a password set.
        CameraConfig demo;
        demo.id = QStringLiteral("demo"); demo.name = QStringLiteral("Demo camera"); demo.ip = QStringLiteral("127.0.0.1");
        demo.proto = QStringLiteral("Demo"); demo.shown = true;
        CameraConfig cj;
        cj.id = QStringLiteral("cj3000"); cj.name = QStringLiteral("CJ3000"); cj.ip = QStringLiteral("192.168.1.64");
        cj.proto = QStringLiteral("ONVIF"); cj.shown = true;   // the CJ3000 speaks ONVIF; its authentication is off, so no login
        m_cams << demo << cj;
        saveCameras();
    }
    m_activeId = s.value(QStringLiteral("cameras/active"), m_cams.first().id).toString();
    if (indexOf(m_activeId) < 0) m_activeId = m_cams.first().id;
    if (migrate)
        saveCameras();   // moves those passwords into the Credential Manager, then rewrites the list without them
}

QString AppController::credentialTarget(const QString &cameraId)
{
    return QStringLiteral("%1/camera/%2").arg(QCoreApplication::applicationName(), cameraId);
}

void AppController::saveCameras()
{
    // Passwords go to the Windows Credential Manager, not the settings (registry). If the
    // store refuses one, it stays in the settings rather than being lost, and that is logged.
    QJsonArray arr;
    for (const CameraConfig &c : m_cams) {
        bool stored = true;
        if (!c.pass.isEmpty())
            stored = CredentialStore::save(credentialTarget(c.id), c.user, c.pass);
        else
            CredentialStore::remove(credentialTarget(c.id));
        if (!stored)
            m_log->add(QStringLiteral("System"), QStringLiteral("Could not store the password for %1 securely").arg(c.name),
                       QStringLiteral("Kept in the settings instead"), QStringLiteral("crit"));
        arr.append(QJsonObject::fromVariantMap(c.toMap(!stored)));
    }
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

QString AppController::cameraKey(const CameraConfig &c) const
{
    // The same physical camera must get the same key on every laptop.
    return c.proto == QLatin1String("Demo") ? QStringLiteral("demo") : QStringLiteral("%1:%2").arg(c.ip).arg(c.httpPort);
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
        connect(m_driver, &CameraDriver::presetsListed, this, &AppController::onPresetsListed);
        connect(m_driver, &CameraDriver::certificateUntrusted, this, &AppController::onCertificateUntrusted);
        connect(m_driver, &CameraDriver::presetsDirty, m_presetTimer, qOverload<>(&QTimer::start));
        connect(m_driver, &CameraDriver::profileChanged, this, [this]() {
            m_profileVar = m_driver ? m_driver->profile().toVariant() : QVariantMap();
            emit profileChanged();
            emit streamChanged();
            refreshPresets();
            if (m_driver) m_driver->queryAux();
        });
        m_log->add(QStringLiteral("System"), QStringLiteral("Connected to %1 (%2)").arg(m_cams.at(i).name, m_driver->kind()),
                   m_cams.at(i).proto == QLatin1String("Demo") ? QStringLiteral("Simulated") : m_cams.at(i).ip);
        if (!m_driver->profile().error().isEmpty())
            m_log->add(QStringLiteral("System"), QStringLiteral("Camera profile has errors"), m_driver->profile().error(), QStringLiteral("warn"));
    }
    m_profileVar = m_driver ? m_driver->profile().toVariant() : QVariantMap();
    if (m_channel == 2 && !hasChannel2())
        m_channel = 1;
    loadLocalPresets();
    m_presetErrorLogged = false;
    m_ptz->setDriver(m_driver);
    m_link->setDriver(m_driver);
    m_health->setDriver(m_driver);
    m_coord->setCamera(i >= 0 ? cameraKey(m_cams.at(i)) : QString());
    emit activeChanged();
    emit streamChanged();
    emit presetsChanged();
    emit profileChanged();
    emit channelChanged();

    // Ask the camera for what it holds once it has had a moment to answer.
    QPointer<CameraDriver> drv = m_driver;
    QTimer::singleShot(800, this, [this, drv]() {
        if (drv && drv == m_driver) { drv->queryPresets(); drv->queryAux(); }
    });
    QTimer::singleShot(1500, this, [this, drv]() {
        if (drv && drv == m_driver) m_health->refresh();
    });
    m_slowTimer->start();
}

void AppController::slowPoll()
{
    if (!m_driver) return;
    m_health->refresh();
    if (m_link->state() == QLatin1String("ok"))
        m_driver->queryAux();
}

QString AppController::effectiveStream() const
{
    return streamForced() ? QStringLiteral("sub") : m_userStream;
}

bool AppController::streamForced() const
{
    return m_link->state() != QLatin1String("ok");
}

QString AppController::latencyMode() const
{
    return localValue(QStringLiteral("latencyMode"), QStringLiteral("Low latency")).toString().contains(QLatin1String("smooth"), Qt::CaseInsensitive)
               ? QStringLiteral("smooth") : QStringLiteral("low");
}

QString AppController::transport() const
{
    return localValue(QStringLiteral("transport"), QStringLiteral("TCP")).toString().compare(QLatin1String("udp"), Qt::CaseInsensitive) == 0
               ? QStringLiteral("udp") : QStringLiteral("tcp");
}

QString AppController::videoSource() const
{
    return localValue(QStringLiteral("videoSource"), QStringLiteral("RTSP")).toString().contains(QLatin1String("flv"), Qt::CaseInsensitive)
               ? QStringLiteral("flv") : QStringLiteral("rtsp");
}

QString AppController::streamUrl() const
{
    if (!m_driver) return {};
    const bool main = effectiveStream() == QLatin1String("main");
    // The camera's own HTTP-FLV stream, when chosen and given: main stream, visible channel only.
    if (main && m_channel == 1 && videoSource() == QLatin1String("flv") && !m_driver->config().flvUrl.isEmpty())
        return m_driver->config().flvUrl;
    return m_driver->streamUrl(main, m_channel);
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

bool AppController::hasChannel2() const
{
    return m_driver && m_driver->hasChannel2();
}

void AppController::setChannel(int c)
{
    c = qBound(1, c, 2);
    if (c == m_channel) return;
    if (c == 2 && !hasChannel2()) {
        m_log->add(m_ptz->operatorName(), QStringLiteral("Thermal channel"), QStringLiteral("Not available on this camera"), QStringLiteral("warn"));
        return;
    }
    m_channel = c;
    emit streamChanged();
    emit channelChanged();
}

void AppController::setPipEnabled(bool on)
{
    if (on == m_pipEnabled) return;
    m_pipEnabled = on;
    QSettings().setValue(QStringLiteral("app/pip"), on);
    emit streamChanged();
}

QString AppController::pipUrl() const
{
    // Always the sub stream: the inset must not double the bandwidth.
    return m_driver ? m_driver->streamUrl(false, pipChannel()) : QString();
}

bool AppController::pipActive() const
{
    // Off on a degraded or lost link, where the second stream would only hurt.
    return m_pipEnabled && hasChannel2() && m_link->state() == QLatin1String("ok") && !pipUrl().isEmpty();
}

void AppController::setTheme(const QString &t)
{
    if (t == m_theme) return;
    m_theme = t;
    QSettings().setValue(QStringLiteral("app/theme"), t);
    emit themeChanged();
}

// ---- presets ------------------------------------------------------------------

QStringList AppController::presetNames() const
{
    QStringList names;
    for (const QVariant &v : m_presets) names << v.toMap().value(QStringLiteral("name")).toString();
    return names;
}

bool AppController::canRenamePreset() const
{
    return m_driver && m_driver->supportsPresetRename();
}

void AppController::loadLocalPresets()
{
    QSettings s;
    m_presets.clear();
    const QJsonArray arr = QJsonDocument::fromJson(s.value(QStringLiteral("presetsList/") + m_activeId).toByteArray()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const int id = o.value(QStringLiteral("id")).toInt();
        if (id <= 0) continue;
        QVariantMap m;
        m[QStringLiteral("id")] = id;
        m[QStringLiteral("name")] = o.value(QStringLiteral("name")).toString();
        m_presets << m;
    }
    if (m_presets.isEmpty()) {
        // First run, or the older format: names for slots 1..N.
        const QStringList def = {QStringLiteral("Bow"), QStringLiteral("Port beam"), QStringLiteral("Starboard beam"),
                                 QStringLiteral("Stern"), QStringLiteral("Horizon scan")};
        const QStringList names = s.value(QStringLiteral("presets/") + m_activeId, def).toStringList();
        for (int i = 0; i < names.size(); ++i) {
            QVariantMap m;
            m[QStringLiteral("id")] = i + 1;
            m[QStringLiteral("name")] = names.at(i);
            m_presets << m;
        }
    }
    m_presetsFromCamera = false;
}

void AppController::saveLocalPresets() const
{
    QJsonArray arr;
    for (const QVariant &v : m_presets) {
        const QVariantMap m = v.toMap();
        arr.append(QJsonObject{{QStringLiteral("id"), m.value(QStringLiteral("id")).toInt()},
                               {QStringLiteral("name"), m.value(QStringLiteral("name")).toString()}});
    }
    QSettings().setValue(QStringLiteral("presetsList/") + m_activeId, QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void AppController::onPresetsListed(bool ok, const QVariantList &list, const QString &error)
{
    if (!ok) {
        if (!m_presetErrorLogged) {
            m_presetErrorLogged = true;
            m_log->add(QStringLiteral("System"), QStringLiteral("Presets not read from the camera"),
                       (error.isEmpty() ? QString() : error + QStringLiteral(" · ")) + QStringLiteral("using names stored on this laptop"),
                       QStringLiteral("warn"));
        }
        return;
    }
    m_presetErrorLogged = false;
    m_presets = list;
    m_presetsFromCamera = true;
    saveLocalPresets();
    emit presetsChanged();
}

void AppController::refreshPresets()
{
    if (m_driver) m_driver->queryPresets();
}

bool AppController::renamePreset(int id, const QString &name)
{
    const QString n = name.trimmed().isEmpty() ? QStringLiteral("Preset %1").arg(id) : name.trimmed();
    // On the camera too, when its list was read from it and the protocol can rename.
    if (m_driver && m_driver->supportsPresetRename() && m_presetsFromCamera) {
        if (!m_ptz->renamePreset(id, n))
            return false;   // view-only or link lost: change nothing anywhere
    }
    for (QVariant &v : m_presets) {
        QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toInt() == id) {
            m[QStringLiteral("name")] = n;
            v = m;
        }
    }
    saveLocalPresets();
    emit presetsChanged();
    return true;
}

int AppController::nextPresetId() const
{
    QSet<int> used;
    for (const QVariant &v : m_presets) used.insert(v.toMap().value(QStringLiteral("id")).toInt());
    int id = 1;
    while (used.contains(id)) ++id;
    return id;
}

bool AppController::addPreset(const QString &name)
{
    const int id = nextPresetId();
    const QString n = name.trimmed().isEmpty() ? QStringLiteral("Preset %1").arg(id) : name.trimmed();
    const bool blocked = m_ptz->locked();
    m_ptz->setPreset(id, n);   // stores the camera's present position under the new number
    if (blocked) return false;
    QVariantMap m;
    m[QStringLiteral("id")] = id;
    m[QStringLiteral("name")] = n;
    m_presets << m;
    std::sort(m_presets.begin(), m_presets.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("id")).toInt() < b.toMap().value(QStringLiteral("id")).toInt();
    });
    saveLocalPresets();
    emit presetsChanged();
    return true;
}

QVariantList AppController::auxItems() const
{
    return m_driver ? m_driver->auxItems() : QVariantList();
}

QVariantMap AppController::profile() const
{
    return m_profileVar;
}

// ---- cameras ------------------------------------------------------------------

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
    CredentialStore::remove(credentialTarget(id));
    TlsPin::forget(id);
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
    const bool noAuth = c.noCredentials();
    auto done = std::make_shared<bool>(false);
    auto finish = [d, done, reply](bool ok, const QString &msg) mutable {
        if (*done) return;
        *done = true;
        reply(ok, msg);
        d->deleteLater();
    };
    QTimer::singleShot(6000, d, [finish]() mutable { finish(false, QStringLiteral("No answer within 6 s · check IP, port and network")); });
    connect(d, &CameraDriver::certificateUntrusted, d, [finish](const QString &fp, bool changed) mutable {
        finish(false, changed ? QStringLiteral("The camera's HTTPS certificate is not the one pinned (SHA-256 %1)").arg(fp)
                              : QStringLiteral("This camera uses HTTPS with a certificate nobody has approved yet (SHA-256 %1). Save the camera; you will be asked to trust it.").arg(fp));
    });

    if (c.proto == QLatin1String("Demo")) {
        finish(true, QStringLiteral("Demo camera · simulated"));
    } else if (c.proto == QLatin1String("ISAPI")) {
        d->request(QStringLiteral("GET"), QStringLiteral("/ISAPI/System/deviceInfo"), {},
                   [finish, noAuth](bool ok, int status, const QByteArray &data, int ms) mutable {
                       if (ok) {
                           const QString model = Xml::tagText(QString::fromUtf8(data), QStringLiteral("model"));
                           finish(true, QStringLiteral("Connected · %1ISAPI · %2 · %3 ms").arg(noAuth ? QStringLiteral("no authentication · ") : QString())
                                            .arg(model.isEmpty() ? QStringLiteral("model unknown") : model).arg(ms));
                       } else if (status == 401) {
                           finish(false, QStringLiteral("Authentication failed · check username and password (repeated failures lock the camera)"));
                       } else {
                           finish(false, status ? QStringLiteral("HTTP %1 · is ISAPI enabled?").arg(status) : QString::fromUtf8(data).left(140));
                       }
                   });
    } else if (c.proto == QLatin1String("ONVIF")) {
        connect(d, &CameraDriver::streamUrlsChanged, d, [finish, noAuth]() mutable {
            finish(true, noAuth ? QStringLiteral("Connected · no authentication · ONVIF profiles and stream address read")
                                : QStringLiteral("Connected · ONVIF · profiles and stream address read"));
        });
    } else {
        connect(d, &CameraDriver::pingResult, d, [finish](bool ok, int ms) mutable {
            finish(ok, ok ? QStringLiteral("RTSP port reachable · %1 ms").arg(ms) : QStringLiteral("RTSP port not reachable"));
        });
        d->ping();
    }
}

bool AppController::settingsSupported(const QString &path) const
{
    return m_driver && m_driver->supportsSettingsEndpoint(path);
}

void AppController::onCertificateUntrusted(const QString &fingerprint, bool changed)
{
    if (!m_driver || m_rejectedCerts.contains(fingerprint) || m_pendingCert.value(QStringLiteral("fingerprint")).toString() == fingerprint)
        return;
    const int i = indexOf(m_activeId);
    m_pendingCert = {{QStringLiteral("camera"), i >= 0 ? m_cams.at(i).name : QString()},
                     {QStringLiteral("host"), i >= 0 ? m_cams.at(i).ip : QString()},
                     {QStringLiteral("fingerprint"), fingerprint},
                     {QStringLiteral("changed"), changed}};
    m_log->add(QStringLiteral("System"), changed ? QStringLiteral("HTTPS certificate CHANGED") : QStringLiteral("HTTPS certificate not yet trusted"),
               QStringLiteral("SHA-256 ") + fingerprint, changed ? QStringLiteral("crit") : QStringLiteral("warn"));
    emit certificateChanged();
}

void AppController::trustCertificate()
{
    if (!m_driver || m_pendingCert.isEmpty()) return;
    const QString fp = m_pendingCert.value(QStringLiteral("fingerprint")).toString();
    TlsPin::pin(m_driver->pinKey(), fp);
    m_log->add(m_ptz->operatorName(), QStringLiteral("HTTPS certificate pinned · ") + m_pendingCert.value(QStringLiteral("camera")).toString(),
               QStringLiteral("SHA-256 ") + fp);
    m_pendingCert.clear();
    emit certificateChanged();
}

void AppController::rejectCertificate()
{
    if (m_pendingCert.isEmpty()) return;
    m_rejectedCerts << m_pendingCert.value(QStringLiteral("fingerprint")).toString();
    m_pendingCert.clear();
    emit certificateChanged();
}

QVariant AppController::localValue(const QString &key, const QVariant &fallback) const
{
    return QSettings().value(QStringLiteral("local/") + key, fallback);
}

void AppController::setLocalValue(const QString &key, const QVariant &value)
{
    QSettings().setValue(QStringLiteral("local/") + key, value);
    emit localValueChanged(key);
    if (key.startsWith(QLatin1String("ctl")))
        m_coord->reload();   // team key, peers or port changed
    if (key == QLatin1String("latencyMode") || key == QLatin1String("transport") || key == QLatin1String("videoSource")) {
        emit videoOptionsChanged();
        emit streamChanged();
    }
}

// ---- camera profile and endpoint tester -----------------------------------------

QString AppController::profilePath(const QString &dottedKey) const
{
    if (!m_driver) return {};
    const QVariantMap v{{QStringLiteral("ch"), m_driver->profile().integer(QStringLiteral("channel"), 1)},
                        {QStringLiteral("vch"), m_channel}};
    return CameraProfile::expand(m_driver->profile().string(dottedKey), v, false);
}

QVariantMap AppController::profileInfo() const
{
    const QString folder = CameraProfile::userFolder();
    QVariantMap m;
    m[QStringLiteral("folder")] = QDir::toNativeSeparators(folder);
    m[QStringLiteral("defaultPath")] = QDir::toNativeSeparators(folder + QStringLiteral("/default.json"));
    m[QStringLiteral("cameraPath")] = QDir::toNativeSeparators(folder + QLatin1Char('/') + m_activeId + QStringLiteral(".json"));
    m[QStringLiteral("defaultExists")] = QFile::exists(folder + QStringLiteral("/default.json"));
    m[QStringLiteral("cameraExists")] = QFile::exists(folder + QLatin1Char('/') + m_activeId + QStringLiteral(".json"));
    m[QStringLiteral("sources")] = m_driver ? m_driver->profile().sources() : QStringList();
    m[QStringLiteral("error")] = m_driver ? m_driver->profile().error() : QString();
    m[QStringLiteral("cameraId")] = m_activeId;
    return m;
}

QVariantMap AppController::createProfileFile(bool forThisCamera)
{
    const QString folder = CameraProfile::userFolder();
    QDir().mkpath(folder);
    const QString path = folder + QLatin1Char('/') + (forThisCamera ? m_activeId : QStringLiteral("default")) + QStringLiteral(".json");
    QVariantMap r;
    r[QStringLiteral("path")] = QDir::toNativeSeparators(path);
    if (QFile::exists(path)) {
        r[QStringLiteral("ok")] = true;
        r[QStringLiteral("existed")] = true;
        return r;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        r[QStringLiteral("ok")] = false;
        r[QStringLiteral("error")] = f.errorString();
        return r;
    }
    f.write("{\n"
            "  \"_help\": \"Entries here override the built-in camera profile key by key; set a key to null to remove one. "
            "Save, then click Reload profile in Settings > Endpoint tester. See README > Camera profiles.\",\n"
            "  \"ptz\": {},\n"
            "  \"aux\": {},\n"
            "  \"health\": {},\n"
            "  \"settings\": {},\n"
            "  \"actions\": {}\n"
            "}\n");
    r[QStringLiteral("ok")] = true;
    r[QStringLiteral("existed")] = false;
    return r;
}

void AppController::reloadProfile()
{
    if (m_driver) m_driver->reloadProfile();
}

void AppController::openProfileFolder()
{
    const QString folder = CameraProfile::userFolder();
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

QString AppController::folderPath(const QString &kind) const
{
    // Same rules as CaptureService and AuditLog: a folder set under Settings > Recording wins.
    QString custom;
    if (kind == QLatin1String("snapshots")) custom = localValue(QStringLiteral("snapFolder")).toString().trimmed();
    else if (kind == QLatin1String("recordings")) custom = localValue(QStringLiteral("recFolder")).toString().trimmed();
    QString path = custom;
    if (path.isEmpty()) {
        if (kind == QLatin1String("snapshots")) path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Corinth");
        else if (kind == QLatin1String("recordings")) path = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/Corinth");
        else if (kind == QLatin1String("exports")) path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Corinth");
        else path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
    }
    return QDir::toNativeSeparators(path);
}

void AppController::openFolder(const QString &kind)
{
    const QString path = folderPath(kind);
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void AppController::copyText(const QString &text)
{
    if (QClipboard *cb = QGuiApplication::clipboard())
        cb->setText(text);
}

QString AppController::prettyXml(const QString &xml) const
{
    QXmlStreamReader r(xml);
    QString out;
    QXmlStreamWriter w(&out);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(2);
    while (!r.atEnd()) {
        r.readNext();
        if (r.tokenType() == QXmlStreamReader::Characters && r.isWhitespace())
            continue;
        if (r.tokenType() == QXmlStreamReader::StartDocument || r.tokenType() == QXmlStreamReader::EndDocument)
            continue;
        if (r.hasError())
            break;
        w.writeCurrentToken(r);
    }
    return r.hasError() ? xml : out;
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
