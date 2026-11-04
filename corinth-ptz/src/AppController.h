#pragma once

#include <QObject>
#include <QJSValue>
#include <QPointer>
#include <QVariantList>
#include <QVector>

#include "drivers/CameraDriver.h"
#include "LowLatencyPlayer.h"

class AuditLog;
class PtzController;
class LinkMonitor;
class CaptureService;
class Discovery;
class JoystickManager;
class HealthMonitor;
class ControlCoordinator;
class QTimer;

// Owns the camera list, the active camera driver and the services, and wires
// them together. Exposed to QML as "app".
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList cameras READ cameras NOTIFY camerasChanged)
    Q_PROPERTY(QString activeId READ activeId NOTIFY activeChanged)
    Q_PROPERTY(QVariantMap activeCamera READ activeCamera NOTIFY activeChanged)
    Q_PROPERTY(QString driverKind READ driverKind NOTIFY activeChanged)
    Q_PROPERTY(QString streamUrl READ streamUrl NOTIFY streamChanged)
    Q_PROPERTY(QString recordUrl READ recordUrl NOTIFY streamChanged)
    Q_PROPERTY(QString userStream READ userStream WRITE setUserStream NOTIFY streamChanged)
    Q_PROPERTY(QString effectiveStream READ effectiveStream NOTIFY streamChanged)
    Q_PROPERTY(bool streamForced READ streamForced NOTIFY streamChanged)
    Q_PROPERTY(int channel READ channel WRITE setChannel NOTIFY streamChanged)
    Q_PROPERTY(bool hasChannel2 READ hasChannel2 NOTIFY profileChanged)
    Q_PROPERTY(bool hasLowLatencyPlayer READ hasLowLatencyPlayer CONSTANT)
    Q_PROPERTY(QString latencyMode READ latencyMode NOTIFY videoOptionsChanged)   // low | smooth
    Q_PROPERTY(QString transport READ transport NOTIFY videoOptionsChanged)       // tcp | udp
    Q_PROPERTY(QString videoSource READ videoSource NOTIFY videoOptionsChanged)   // rtsp | flv
    Q_PROPERTY(bool pipEnabled READ pipEnabled WRITE setPipEnabled NOTIFY streamChanged)
    Q_PROPERTY(bool pipActive READ pipActive NOTIFY streamChanged)
    Q_PROPERTY(QString pipUrl READ pipUrl NOTIFY streamChanged)
    Q_PROPERTY(int pipChannel READ pipChannel NOTIFY streamChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)           // [{ id, name }]
    Q_PROPERTY(bool presetsFromCamera READ presetsFromCamera NOTIFY presetsChanged)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)
    Q_PROPERTY(bool canRenamePreset READ canRenamePreset NOTIFY profileChanged)
    Q_PROPERTY(QVariantList auxItems READ auxItems NOTIFY profileChanged)         // [{ id, label, supported, readable }]
    Q_PROPERTY(QVariantMap profile READ profile NOTIFY profileChanged)
    // HTTPS certificate waiting for the user's decision: { camera, host, fingerprint, changed }; empty when none
    Q_PROPERTY(QVariantMap pendingCertificate READ pendingCertificate NOTIFY certificateChanged)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    AuditLog *log() const { return m_log; }
    PtzController *ptz() const { return m_ptz; }
    LinkMonitor *link() const { return m_link; }
    CaptureService *capture() const { return m_capture; }
    Discovery *discovery() const { return m_discovery; }
    JoystickManager *joystick() const { return m_joy; }
    HealthMonitor *health() const { return m_health; }
    ControlCoordinator *coordinator() const { return m_coord; }

    QVariantList cameras() const;
    QString activeId() const { return m_activeId; }
    QVariantMap activeCamera() const;
    QString driverKind() const;
    QString streamUrl() const;
    QString recordUrl() const;
    QString userStream() const { return m_userStream; }
    void setUserStream(const QString &s);
    QString effectiveStream() const;
    bool streamForced() const;
    int channel() const { return m_channel; }
    void setChannel(int c);
    bool hasChannel2() const;
    bool hasLowLatencyPlayer() const { return LowLatencyPlayer::isAvailable(); }
    QString latencyMode() const;
    QString transport() const;
    QString videoSource() const;
    bool pipEnabled() const { return m_pipEnabled; }
    void setPipEnabled(bool on);
    bool pipActive() const;
    QString pipUrl() const;
    int pipChannel() const { return m_channel == 1 ? 2 : 1; }
    QString theme() const { return m_theme; }
    void setTheme(const QString &t);
    QVariantList presets() const { return m_presets; }
    bool presetsFromCamera() const { return m_presetsFromCamera; }
    QStringList presetNames() const;
    bool canRenamePreset() const;
    QVariantList auxItems() const;
    QVariantMap profile() const;
    QVariantMap pendingCertificate() const { return m_pendingCert; }
    QString version() const;

    Q_INVOKABLE QString saveCamera(const QVariantMap &cam);
    Q_INVOKABLE void removeCamera(const QString &id);
    // Pin the certificate the active camera showed (after its fingerprint was displayed), or refuse it.
    Q_INVOKABLE void trustCertificate();
    Q_INVOKABLE void rejectCertificate();
    // Can the active camera serve the Settings page behind this path? (false = "Not supported over ONVIF")
    Q_INVOKABLE bool settingsSupported(const QString &path) const;
    Q_INVOKABLE void setCameraShown(const QString &id, bool shown);
    Q_INVOKABLE void setActive(const QString &id);

    // Presets: the list comes from the camera when it can be read, otherwise
    // from the names stored on this laptop.
    Q_INVOKABLE void refreshPresets();
    Q_INVOKABLE bool renamePreset(int id, const QString &name);
    Q_INVOKABLE bool addPreset(const QString &name);          // stores the current position in the next free slot
    Q_INVOKABLE int nextPresetId() const;

    // Raw camera HTTP (ISAPI paths) for the Settings pages.
    // callback(ok, status, body, ms)
    Q_INVOKABLE void request(const QString &method, const QString &path, const QString &body, QJSValue callback);
    // callback(ok, message)
    Q_INVOKABLE void testConnection(const QVariantMap &cam, QJSValue callback);

    Q_INVOKABLE QVariant localValue(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE void setLocalValue(const QString &key, const QVariant &value);

    // Camera profile (endpoints as data) and the endpoint tester.
    Q_INVOKABLE QString profilePath(const QString &dottedKey) const;   // e.g. "image.color", {vch} filled in
    Q_INVOKABLE QVariantMap profileInfo() const;
    Q_INVOKABLE QVariantMap createProfileFile(bool forThisCamera);
    Q_INVOKABLE void reloadProfile();
    Q_INVOKABLE void openProfileFolder();
    Q_INVOKABLE void copyText(const QString &text);
    // Where snapshots, recordings, log exports and the daily audit logs are kept, and open that folder
    // in Explorer. kind: snapshots | recordings | exports | logs
    Q_INVOKABLE QString folderPath(const QString &kind) const;
    Q_INVOKABLE void openFolder(const QString &kind);
    Q_INVOKABLE QString prettyXml(const QString &xml) const;

signals:
    void camerasChanged();
    void activeChanged();
    void streamChanged();
    void themeChanged();
    void presetsChanged();
    void profileChanged();
    void videoOptionsChanged();
    void certificateChanged();
    void localValueChanged(const QString &key);   // a setting stored with setLocalValue changed
    void channelChanged();
    // Joystick actions that the UI handles (snapshot, record, channel, presetNext, presetPrev, night)
    void uiAction(const QString &action);

private:
    void loadCameras();
    void saveCameras();
    CameraDriver *makeDriver(const CameraConfig &cfg, QObject *parent) const;
    void rebuildDriver();
    int indexOf(const QString &id) const;
    void onJoystickPressed(const QString &action);
    void onJoystickReleased(const QString &action);
    void loadLocalPresets();
    void saveLocalPresets() const;
    void onPresetsListed(bool ok, const QVariantList &list, const QString &error);
    void slowPoll();
    void onCertificateUntrusted(const QString &fingerprint, bool changed);
    static QString credentialTarget(const QString &cameraId);
    QString cameraKey(const CameraConfig &c) const;

    AuditLog *m_log = nullptr;
    PtzController *m_ptz = nullptr;
    LinkMonitor *m_link = nullptr;
    CaptureService *m_capture = nullptr;
    Discovery *m_discovery = nullptr;
    JoystickManager *m_joy = nullptr;
    HealthMonitor *m_health = nullptr;
    ControlCoordinator *m_coord = nullptr;
    QPointer<CameraDriver> m_driver;
    QTimer *m_holdTimer = nullptr;
    QTimer *m_slowTimer = nullptr;
    QTimer *m_presetTimer = nullptr;

    QVector<CameraConfig> m_cams;
    QString m_activeId;
    QString m_userStream = QStringLiteral("main");
    int m_channel = 1;
    bool m_pipEnabled = false;
    QString m_theme = QStringLiteral("dark");
    QVariantMap m_pendingCert;
    QStringList m_rejectedCerts;   // fingerprints refused this session: do not ask again
    QVariantMap m_profileVar;   // the active camera's profile as a QML-readable map
    QVariantList m_presets;
    bool m_presetsFromCamera = false;
    bool m_presetErrorLogged = false;
};
