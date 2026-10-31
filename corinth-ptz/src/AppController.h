#pragma once

#include <QObject>
#include <QJSValue>
#include <QPointer>
#include <QVariantList>
#include <QVector>

#include "drivers/CameraDriver.h"

class AuditLog;
class PtzController;
class LinkMonitor;
class CaptureService;
class Discovery;
class JoystickManager;
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
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)
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
    QString theme() const { return m_theme; }
    void setTheme(const QString &t);
    QStringList presetNames() const;
    QString version() const;

    Q_INVOKABLE QString saveCamera(const QVariantMap &cam);
    Q_INVOKABLE void removeCamera(const QString &id);
    Q_INVOKABLE void setCameraShown(const QString &id, bool shown);
    Q_INVOKABLE void setActive(const QString &id);
    Q_INVOKABLE void setPresetName(int index, const QString &name);

    // Raw camera HTTP (ISAPI paths) for the Settings pages.
    // callback(ok, status, body, ms)
    Q_INVOKABLE void request(const QString &method, const QString &path, const QString &body, QJSValue callback);
    // callback(ok, message)
    Q_INVOKABLE void testConnection(const QVariantMap &cam, QJSValue callback);

    Q_INVOKABLE QVariant localValue(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE void setLocalValue(const QString &key, const QVariant &value);

signals:
    void camerasChanged();
    void activeChanged();
    void streamChanged();
    void themeChanged();
    void presetsChanged();
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

    AuditLog *m_log = nullptr;
    PtzController *m_ptz = nullptr;
    LinkMonitor *m_link = nullptr;
    CaptureService *m_capture = nullptr;
    Discovery *m_discovery = nullptr;
    JoystickManager *m_joy = nullptr;
    QPointer<CameraDriver> m_driver;
    QTimer *m_holdTimer = nullptr;

    QVector<CameraConfig> m_cams;
    QString m_activeId;
    QString m_userStream = QStringLiteral("main");
    int m_channel = 1;
    QString m_theme = QStringLiteral("dark");
};
