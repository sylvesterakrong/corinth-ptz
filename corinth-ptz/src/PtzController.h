#pragma once

#include <QObject>
#include <QPointer>
#include <QElapsedTimer>
#include <QHash>
#include <QVariantMap>

class CameraDriver;
class AuditLog;
class ControlCoordinator;
class QTimer;

// Turns UI and joystick input into camera commands, with the safety rules from
// the design doc:
//  - continuous moves are sent on change and re-sent every 250 ms while moving
//  - a stop (all zeros) is sent on release, focus loss, link loss or control release
//  - speed is capped at 30 while the link is degraded
//  - nothing is sent while view-only or link lost ("Not sent" is logged)
class PtzController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastText READ lastText NOTIFY lastChanged)
    Q_PROPERTY(QString lastState READ lastState NOTIFY lastChanged)   // idle | ack | sending | blocked | failed
    Q_PROPERTY(int lastMs READ lastMs NOTIFY lastChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastChanged)
    Q_PROPERTY(double az READ az NOTIFY positionChanged)
    Q_PROPERTY(double el READ el NOTIFY positionChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY positionChanged)
    Q_PROPERTY(bool positionKnown READ positionKnown NOTIFY positionChanged)
    Q_PROPERTY(bool zoomKnown READ zoomKnown NOTIFY positionChanged)
    Q_PROPERTY(int speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(int zoomSpeed READ zoomSpeed WRITE setZoomSpeed NOTIFY speedChanged)
    Q_PROPERTY(int effectiveZoomSpeed READ effectiveZoomSpeed NOTIFY speedChanged)
    Q_PROPERTY(bool canZoomLevel READ canZoomLevel NOTIFY driverChanged)
    Q_PROPERTY(int speedCap READ speedCap NOTIFY speedChanged)
    Q_PROPERTY(int effectiveSpeed READ effectiveSpeed NOTIFY speedChanged)
    Q_PROPERTY(bool controlHeld READ controlHeld NOTIFY lockChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY lockChanged)
    Q_PROPERTY(bool moving READ moving NOTIFY movingChanged)
    Q_PROPERTY(bool hasPtz READ hasPtz NOTIFY driverChanged)
    Q_PROPERTY(bool canPoint READ canPoint NOTIFY driverChanged)
    Q_PROPERTY(bool canBoxZoom READ canBoxZoom NOTIFY driverChanged)
    Q_PROPERTY(QVariantMap auxStates READ auxStates NOTIFY auxChanged)   // name -> bool; absent = not known
    Q_PROPERTY(QString operatorName READ operatorName WRITE setOperatorName NOTIFY operatorChanged)

public:
    PtzController(AuditLog *log, QObject *parent = nullptr);

    void setDriver(CameraDriver *driver);
    void setLinkState(const QString &state);
    // With a coordinator that is enabled, control is shared between laptops;
    // otherwise Take/Release work on this laptop only.
    void setCoordinator(ControlCoordinator *coord);

    QString lastText() const { return m_lastText; }
    QString lastState() const { return m_lastState; }
    int lastMs() const { return m_lastMs; }
    QString lastError() const { return m_lastError; }
    double az() const { return m_az; }
    double el() const { return m_el; }
    double zoom() const { return m_zoom; }
    bool positionKnown() const { return m_positionKnown; }
    bool zoomKnown() const { return m_zoomKnown; }
    int speed() const { return m_speed; }
    void setSpeed(int s);
    // How fast the zoom keys, Wide/Tele buttons and the joystick zoom axis drive the lens, 1..100.
    // Capped like pan/tilt while the link is degraded.
    int zoomSpeed() const { return m_zoomSpeed; }
    void setZoomSpeed(int s);
    int effectiveZoomSpeed() const { return qMin(m_zoomSpeed, speedCap()); }
    int targetZoom() const { return m_tZoom; }   // what is being sent, -100..100 (tests)
    bool canZoomLevel() const;
    int speedCap() const { return m_linkState == QLatin1String("degraded") ? 30 : 100; }
    int effectiveSpeed() const { return qMin(m_speed, speedCap()); }
    bool controlHeld() const;
    bool locked() const { return !controlHeld() || m_linkState == QLatin1String("lost"); }
    bool moving() const { return m_tPan || m_tTilt || m_tZoom; }
    bool hasPtz() const;
    bool canPoint() const;
    bool canBoxZoom() const;
    QVariantMap auxStates() const { return m_aux; }
    QString operatorName() const { return m_operator; }
    void setOperatorName(const QString &n);

    // pan/tilt/zoom in -1..1 (pan right +, tilt up +, zoom in +). Pan/tilt are
    // scaled by the effective speed; zoom runs at full speed.
    Q_INVOKABLE void move(double pan, double tilt, double zoom, const QString &source = QString());
    Q_INVOKABLE void stopMove();
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE void gotoPreset(int id, const QString &name);
    Q_INVOKABLE void setPreset(int id, const QString &name);
    Q_INVOKABLE void home();
    Q_INVOKABLE void setHome();
    Q_INVOKABLE void focus(int dir);
    Q_INVOKABLE void iris(int dir);
    Q_INVOKABLE void onePushFocus();
    Q_INVOKABLE void aux(const QString &name, bool on, const QString &label);
    // nx, ny: 0..1 inside the visible picture, origin top-left
    // Go straight to a zoom level (1.0 = widest); pan and tilt stay where they are.
    Q_INVOKABLE void setZoomLevel(double times);
    Q_INVOKABLE void pointAt(double nx, double ny);
    Q_INVOKABLE void boxZoom(double x1, double y1, double x2, double y2);
    // false when the command was blocked (view-only, link lost, no camera)
    Q_INVOKABLE bool renamePreset(int id, const QString &name);
    Q_INVOKABLE void takeControl();
    Q_INVOKABLE void releaseControl();

signals:
    void lastChanged();
    void positionChanged();
    void speedChanged();
    void lockChanged();
    void movingChanged();
    void driverChanged();
    void operatorChanged();
    void auxChanged();

private:
    bool guard(const QString &label);
    void sending(const QString &label);
    void onCommandDone(const QString &label, bool ok, int ms, const QString &error);
    void onControlState();
    void stopForHandover(const QString &label);
    void pump();
    QString who() const;

    AuditLog *m_log;
    QPointer<CameraDriver> m_driver;
    QPointer<ControlCoordinator> m_coord;
    QTimer *m_pump = nullptr;
    QTimer *m_status = nullptr;
    QElapsedTimer m_lastSend;

    QString m_lastText = QStringLiteral("No commands yet");
    QString m_lastState = QStringLiteral("idle");
    QString m_lastError;
    int m_lastMs = 0;
    double m_az = 0, m_el = 0, m_zoom = 1;
    bool m_positionKnown = false, m_zoomKnown = false;
    int m_speed = 40;
    int m_zoomSpeed = 60;
    bool m_controlHeld = true;
    QString m_linkState = QStringLiteral("ok");
    QString m_operator;

    int m_tPan = 0, m_tTilt = 0, m_tZoom = 0;   // target, -100..100
    int m_sPan = 0, m_sTilt = 0, m_sZoom = 0;   // last sent
    bool m_movedSinceStop = false;
    bool m_wasHeld = true;

    struct PendingAux
    {
        QString name;
        bool on = false;
        QVariant previous;   // invalid = the state was not known
    };
    QVariantMap m_aux;
    QHash<QString, PendingAux> m_pendingAux;   // by command label
};
