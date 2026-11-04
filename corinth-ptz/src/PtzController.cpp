#include "PtzController.h"
#include "AuditLog.h"
#include "ControlCoordinator.h"
#include "drivers/CameraDriver.h"

#include <QSettings>
#include <QTimer>
#include <QtMath>

PtzController::PtzController(AuditLog *log, QObject *parent)
    : QObject(parent), m_log(log), m_pump(new QTimer(this)), m_status(new QTimer(this))
{
    {
        QSettings s;
        m_speed = qBound(1, s.value(QStringLiteral("ptz/speed"), 40).toInt(), 100);
        m_zoomSpeed = qBound(1, s.value(QStringLiteral("ptz/zoomSpeed"), 60).toInt(), 100);
    }
    m_operator = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER", QStringLiteral("Operator")));
    connect(m_pump, &QTimer::timeout, this, &PtzController::pump);
    m_pump->start(50);   // 20 Hz
    connect(m_status, &QTimer::timeout, this, [this]() { if (m_driver) m_driver->queryStatus(); });
    m_status->start(500);
    m_lastSend.start();
}

QString PtzController::who() const
{
    return m_operator + QStringLiteral(" · Operator");
}

bool PtzController::hasPtz() const
{
    return m_driver && m_driver->hasPtz();
}

bool PtzController::canPoint() const
{
    return m_driver && m_driver->supportsPoint();
}

bool PtzController::canBoxZoom() const
{
    return m_driver && m_driver->supportsBoxZoom();
}

bool PtzController::controlHeld() const
{
    return m_coord && m_coord->enabled() ? m_coord->isMine() : m_controlHeld;
}

void PtzController::setCoordinator(ControlCoordinator *coord)
{
    if (m_coord)
        disconnect(m_coord, nullptr, this, nullptr);
    m_coord = coord;
    m_wasHeld = controlHeld();
    if (m_coord) {
        connect(m_coord, &ControlCoordinator::stateChanged, this, &PtzController::onControlState);
        connect(m_coord, &ControlCoordinator::configChanged, this, &PtzController::onControlState);
    }
}

// Control moved between laptops. Whoever loses it stops the camera first, so a
// move that was in progress cannot run on unattended; whoever gains it sends a
// stop too, in case the previous holder's laptop died mid-move.
void PtzController::onControlState()
{
    const bool held = controlHeld();
    if (held != m_wasHeld) {
        m_wasHeld = held;
        stopForHandover(held ? QStringLiteral("Stop (took control)") : QStringLiteral("Stop (control handed over)"));
    }
    emit lockChanged();
}

void PtzController::stopForHandover(const QString &label)
{
    m_tPan = m_tTilt = m_tZoom = 0;
    emit movingChanged();
    if (!m_driver) return;
    sending(label);
    m_driver->continuousMove(0, 0, 0, label);
    m_sPan = m_sTilt = m_sZoom = 0;
}

void PtzController::setOperatorName(const QString &n)
{
    if (n == m_operator || n.trimmed().isEmpty()) return;
    m_operator = n.trimmed();
    emit operatorChanged();
}

void PtzController::setDriver(CameraDriver *driver)
{
    if (m_driver)
        disconnect(m_driver, nullptr, this, nullptr);
    m_driver = driver;
    m_tPan = m_tTilt = m_tZoom = m_sPan = m_sTilt = m_sZoom = 0;
    m_positionKnown = m_zoomKnown = false;
    m_aux.clear();
    m_pendingAux.clear();
    emit auxChanged();
    emit positionChanged();
    if (m_driver) {
        connect(m_driver, &CameraDriver::commandDone, this, &PtzController::onCommandDone);
        connect(m_driver, &CameraDriver::auxState, this, [this](const QString &name, bool on) {
            m_aux[name] = on;
            emit auxChanged();
        });
        connect(m_driver, &CameraDriver::statusUpdated, this, [this](double a, double e, double z) {
            m_az = a; m_el = e;
            m_zoomKnown = z > 0;
            if (m_zoomKnown) m_zoom = z;
            m_positionKnown = true;
            emit positionChanged();
        });
    }
    emit driverChanged();
}

void PtzController::setLinkState(const QString &state)
{
    if (state == m_linkState) return;
    const bool wasLocked = locked();
    m_linkState = state;
    if (state == QLatin1String("lost")) {
        // Try to stop the camera; if the link is truly gone the camera's own
        // continuous-move timeout is the last line of defence.
        if (m_driver && (m_sPan || m_sTilt || m_sZoom))
            m_driver->continuousMove(0, 0, 0, QStringLiteral("Stop (link lost)"));
        m_tPan = m_tTilt = m_tZoom = m_sPan = m_sTilt = m_sZoom = 0;
        emit movingChanged();
    }
    emit speedChanged();
    if (wasLocked != locked()) emit lockChanged();
}

void PtzController::setSpeed(int s)
{
    s = qBound(1, s, 100);
    if (s == m_speed) return;
    m_speed = s;
    QSettings().setValue(QStringLiteral("ptz/speed"), s);
    emit speedChanged();
}

void PtzController::setZoomSpeed(int s)
{
    s = qBound(1, s, 100);
    if (s == m_zoomSpeed) return;
    m_zoomSpeed = s;
    QSettings().setValue(QStringLiteral("ptz/zoomSpeed"), s);
    emit speedChanged();
}

bool PtzController::canZoomLevel() const
{
    return m_driver && m_driver->supportsZoomLevel();
}

void PtzController::setZoomLevel(double times)
{
    const QString label = QStringLiteral("Zoom to %1×").arg(times, 0, 'f', 1);
    if (!guard(label)) return;
    if (m_sZoom) {   // a zoom key or button is still held: its stop would cancel this
        m_lastText = label; m_lastState = QStringLiteral("blocked"); m_lastError = QStringLiteral("Release the zoom control first");
        emit lastChanged();
        return;
    }
    sending(label);
    m_driver->setZoomLevel(times, label);
}

void PtzController::sending(const QString &label)
{
    m_lastText = label;
    m_lastState = QStringLiteral("sending");
    m_lastError.clear();
    m_lastMs = 0;
    emit lastChanged();
}

bool PtzController::guard(const QString &label)
{
    if (!m_driver) {
        m_lastText = label; m_lastState = QStringLiteral("blocked"); m_lastError = QStringLiteral("No camera selected");
        emit lastChanged();
        return false;
    }
    if (locked()) {
        m_lastText = label;
        m_lastState = QStringLiteral("blocked");
        m_lastError = controlHeld() ? QStringLiteral("Link lost") : QStringLiteral("View only");
        emit lastChanged();
        m_log->add(who(), label, QStringLiteral("Not sent · ") + m_lastError, QStringLiteral("crit"));
        return false;
    }
    return true;
}

void PtzController::onCommandDone(const QString &label, bool ok, int ms, const QString &error)
{
    // An aux switch was flipped on screen before the camera answered: put it
    // back if the camera refused, or read the real state if it accepted.
    const auto pa = m_pendingAux.find(label);
    if (pa != m_pendingAux.end()) {
        const PendingAux p = pa.value();
        m_pendingAux.erase(pa);
        if (!ok) {
            if (p.previous.isValid()) m_aux[p.name] = p.previous;
            else m_aux.remove(p.name);
            emit auxChanged();
        } else if (m_driver) {
            m_driver->queryAux();
        }
    }

    const bool continuous = label.startsWith(QLatin1String("Continuous"));
    // Only update the "last command" bar for the command it currently shows,
    // or for any non-continuous command.
    if (!continuous || m_lastText == label) {
        m_lastText = label;
        m_lastState = ok ? QStringLiteral("ack") : QStringLiteral("failed");
        m_lastMs = ms;
        m_lastError = error;
        emit lastChanged();
    }
    if (!continuous || label.endsWith(QLatin1String("stop")))
        m_log->add(who(), label, ok ? QStringLiteral("Ack %1 ms").arg(ms) : QStringLiteral("Failed · ") + error,
                   ok ? QStringLiteral("ok") : QStringLiteral("crit"));
}

void PtzController::move(double pan, double tilt, double zoom, const QString &source)
{
    Q_UNUSED(source);
    const int sp = effectiveSpeed();
    const int p = qRound(qBound(-1.0, pan, 1.0) * sp);
    const int t = qRound(qBound(-1.0, tilt, 1.0) * sp);
    const int z = qRound(qBound(-1.0, zoom, 1.0) * effectiveZoomSpeed());
    const bool wasMoving = moving();
    if ((p || t || z) && locked()) {
        if (!wasMoving && (m_lastState != QLatin1String("blocked")))
            guard(QStringLiteral("Continuous move"));
        return;
    }
    m_tPan = p; m_tTilt = t; m_tZoom = z;
    if (wasMoving != moving()) emit movingChanged();
}

void PtzController::stopMove()
{
    const bool wasMoving = moving();
    m_tPan = m_tTilt = m_tZoom = 0;
    if (wasMoving) emit movingChanged();
}

void PtzController::pump()
{
    if (!m_driver) return;
    const bool changed = m_tPan != m_sPan || m_tTilt != m_sTilt || m_tZoom != m_sZoom;
    const bool movingNow = m_tPan || m_tTilt || m_tZoom;
    const bool resend = movingNow && m_lastSend.elapsed() > 250;
    if (!changed && !resend) return;
    if (locked() && movingNow) { m_tPan = m_tTilt = m_tZoom = 0; return; }

    const bool isStop = !movingNow;
    const QString label = isStop ? QStringLiteral("Continuous move → stop") : QStringLiteral("Continuous move");
    if (changed) sending(label);
    m_driver->continuousMove(m_tPan, m_tTilt, m_tZoom, label);
    m_sPan = m_tPan; m_sTilt = m_tTilt; m_sZoom = m_tZoom;
    m_lastSend.restart();
}

void PtzController::stopAll()
{
    m_tPan = m_tTilt = m_tZoom = 0;
    emit movingChanged();
    if (!m_driver) return;
    // Stop is always allowed, even when view-only: it can only make things safer.
    sending(QStringLiteral("Stop all"));
    m_driver->continuousMove(0, 0, 0, QStringLiteral("Stop all"));
    m_sPan = m_sTilt = m_sZoom = 0;
}

void PtzController::gotoPreset(int id, const QString &name)
{
    const QString label = QStringLiteral("Go to preset %1 · %2").arg(id).arg(name);
    if (!guard(label)) return;
    sending(label);
    m_driver->gotoPreset(id, label);
}

void PtzController::setPreset(int id, const QString &name)
{
    const QString label = QStringLiteral("Set preset %1 · %2").arg(id).arg(name);
    if (!guard(label)) return;
    sending(label);
    m_driver->setPreset(id, name, label);
}

void PtzController::home()
{
    const QString label = QStringLiteral("Home");
    if (!guard(label)) return;
    sending(label);
    m_driver->gotoHome(label);
}

void PtzController::setHome()
{
    const QString label = QStringLiteral("Set home position");
    if (!guard(label)) return;
    sending(label);
    m_driver->setHome(label);
}

void PtzController::focus(int dir)
{
    const QString label = dir < 0 ? QStringLiteral("Focus near") : dir > 0 ? QStringLiteral("Focus far") : QStringLiteral("Focus stop");
    if (dir != 0 && !guard(label)) return;
    if (!m_driver) return;
    if (dir != 0) sending(label);
    m_driver->focus(dir, label);
}

void PtzController::iris(int dir)
{
    const QString label = dir < 0 ? QStringLiteral("Iris close") : dir > 0 ? QStringLiteral("Iris open") : QStringLiteral("Iris stop");
    if (dir != 0 && !guard(label)) return;
    if (!m_driver) return;
    if (dir != 0) sending(label);
    m_driver->iris(dir, label);
}

void PtzController::onePushFocus()
{
    const QString label = QStringLiteral("One-push focus");
    if (!guard(label)) return;
    sending(label);
    m_driver->onePushFocus(label);
}

void PtzController::aux(const QString &name, bool on, const QString &label)
{
    const QString l = label + (on ? QStringLiteral(" on") : QStringLiteral(" off"));
    if (!guard(l)) return;
    sending(l);
    m_pendingAux.insert(l, PendingAux{name, on, m_aux.contains(name) ? m_aux.value(name) : QVariant()});
    m_aux[name] = on;
    emit auxChanged();
    m_driver->setAux(name, on, l);
}

void PtzController::pointAt(double nx, double ny)
{
    const QString label = QStringLiteral("Point at %1, %2").arg(nx, 0, 'f', 2).arg(ny, 0, 'f', 2);
    if (!guard(label)) return;
    if (m_sPan || m_sTilt || m_sZoom) {
        // A stop that is still on its way would cancel the move we are about to send.
        m_lastText = label; m_lastState = QStringLiteral("blocked"); m_lastError = QStringLiteral("Release the joystick first");
        emit lastChanged();
        return;
    }
    sending(label);
    m_driver->pointAt(nx, ny, label);
}

void PtzController::boxZoom(double x1, double y1, double x2, double y2)
{
    const QString label = QStringLiteral("Zoom box %1,%2 to %3,%4").arg(x1, 0, 'f', 2).arg(y1, 0, 'f', 2).arg(x2, 0, 'f', 2).arg(y2, 0, 'f', 2);
    if (!guard(label)) return;
    if (m_sPan || m_sTilt || m_sZoom) {
        m_lastText = label; m_lastState = QStringLiteral("blocked"); m_lastError = QStringLiteral("Release the joystick first");
        emit lastChanged();
        return;
    }
    sending(label);
    m_driver->boxZoom(x1, y1, x2, y2, label);
}

bool PtzController::renamePreset(int id, const QString &name)
{
    const QString label = QStringLiteral("Rename preset %1 · %2").arg(id).arg(name);
    if (!guard(label)) return false;
    sending(label);
    m_driver->renamePreset(id, name, label);
    return true;
}

void PtzController::takeControl()
{
    if (m_coord && m_coord->enabled()) {
        m_coord->claim();
        return;
    }
    if (m_controlHeld) return;
    m_controlHeld = true;
    m_log->add(who(), QStringLiteral("Took control"), QStringLiteral("OK"));
    emit lockChanged();
}

void PtzController::releaseControl()
{
    if (m_coord && m_coord->enabled()) {
        if (!m_coord->isMine()) return;
        stopAll();
        m_coord->release();
        return;
    }
    if (!m_controlHeld) return;
    stopAll();
    m_controlHeld = false;
    m_log->add(who(), QStringLiteral("Released control"), QStringLiteral("OK"));
    emit lockChanged();
}
