#pragma once

#include <QObject>
#include <QPointer>
#include <QElapsedTimer>

class CameraDriver;
class AuditLog;
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
    Q_PROPERTY(int speedCap READ speedCap NOTIFY speedChanged)
    Q_PROPERTY(int effectiveSpeed READ effectiveSpeed NOTIFY speedChanged)
    Q_PROPERTY(bool controlHeld READ controlHeld NOTIFY lockChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY lockChanged)
    Q_PROPERTY(bool moving READ moving NOTIFY movingChanged)
    Q_PROPERTY(bool hasPtz READ hasPtz NOTIFY driverChanged)
    Q_PROPERTY(QString operatorName READ operatorName WRITE setOperatorName NOTIFY operatorChanged)

public:
    PtzController(AuditLog *log, QObject *parent = nullptr);

    void setDriver(CameraDriver *driver);
    void setLinkState(const QString &state);

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
    int speedCap() const { return m_linkState == QLatin1String("degraded") ? 30 : 100; }
    int effectiveSpeed() const { return qMin(m_speed, speedCap()); }
    bool controlHeld() const { return m_controlHeld; }
    bool locked() const { return !m_controlHeld || m_linkState == QLatin1String("lost"); }
    bool moving() const { return m_tPan || m_tTilt || m_tZoom; }
    bool hasPtz() const;
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

private:
    bool guard(const QString &label);
    void sending(const QString &label);
    void onCommandDone(const QString &label, bool ok, int ms, const QString &error);
    void pump();
    QString who() const;

    AuditLog *m_log;
    QPointer<CameraDriver> m_driver;
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
    bool m_controlHeld = true;
    QString m_linkState = QStringLiteral("ok");
    QString m_operator;

    int m_tPan = 0, m_tTilt = 0, m_tZoom = 0;   // target, -100..100
    int m_sPan = 0, m_sTilt = 0, m_sZoom = 0;   // last sent
    bool m_movedSinceStop = false;
};
