#include "LinkMonitor.h"
#include "AuditLog.h"
#include "drivers/CameraDriver.h"

#include <QTimer>
#include <cmath>

LinkMonitor::LinkMonitor(AuditLog *log, QObject *parent)
    : QObject(parent), m_log(log), m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &LinkMonitor::probe);
    m_timer->start(1000);
}

void LinkMonitor::setDriver(CameraDriver *driver)
{
    if (m_driver)
        disconnect(m_driver, nullptr, this, nullptr);
    m_driver = driver;
    m_samples.clear();
    m_consecutiveFails = 0;
    m_hasSample = false;
    m_waiting = false;
    m_measured = QStringLiteral("ok");
    if (m_driver)
        connect(m_driver, &CameraDriver::pingResult, this, &LinkMonitor::onResult);
    emit metricsChanged();
    evaluate();
}

QString LinkMonitor::state() const
{
    return m_simulate.isEmpty() ? m_measured : m_simulate;
}

void LinkMonitor::setSimulate(const QString &s)
{
    const QString v = (s == QLatin1String("ok") || s == QLatin1String("degraded") || s == QLatin1String("lost")) ? s : QString();
    if (v == m_simulate) return;
    m_simulate = v;
    const QString before = m_lastReported;
    evaluate();
    if (before == m_lastReported)
        emit stateChanged();   // simulate value changed even if the state did not
}

void LinkMonitor::probe()
{
    if (!m_driver) return;
    if (m_waiting) {
        // Previous probe never answered within a second: count it as lost.
        onResult(false, 0);
    }
    m_waiting = true;
    m_driver->ping();
}

void LinkMonitor::onResult(bool ok, int ms)
{
    if (!m_waiting && ok)
        return;   // late reply after we already counted it as lost
    m_waiting = false;
    m_samples.append(ok ? ms : -1);
    while (m_samples.size() > 20)
        m_samples.removeFirst();
    m_consecutiveFails = ok ? 0 : m_consecutiveFails + 1;

    int lost = 0;
    QVector<int> good;
    for (int v : m_samples) { if (v < 0) ++lost; else good.append(v); }
    m_loss = m_samples.isEmpty() ? 0 : 100.0 * lost / m_samples.size();
    if (!good.isEmpty()) {
        const int n = qMin(5, int(good.size()));
        long sum = 0;
        for (int i = good.size() - n; i < good.size(); ++i) sum += good.at(i);
        m_rtt = int(sum / n);
        long jit = 0;
        for (int i = 1; i < good.size(); ++i) jit += std::abs(good.at(i) - good.at(i - 1));
        m_jitter = good.size() > 1 ? int(jit / (good.size() - 1)) : 0;
    }
    m_hasSample = true;
    emit metricsChanged();

    if (m_consecutiveFails >= 5)
        m_measured = QStringLiteral("lost");
    else if (m_rtt > 500 || m_loss > 3.0)
        m_measured = QStringLiteral("degraded");
    else
        m_measured = QStringLiteral("ok");
    evaluate();
}

void LinkMonitor::evaluate()
{
    const QString s = state();
    if (s == m_lastReported) return;
    m_lastReported = s;
    const QString sim = m_simulate.isEmpty() ? QString() : QStringLiteral(" (simulated)");
    if (s == QLatin1String("degraded"))
        m_log->add(QStringLiteral("System"), QStringLiteral("Link degraded%1 · RTT %2 ms, loss %3%").arg(sim).arg(m_rtt).arg(m_loss, 0, 'f', 1),
                   QStringLiteral("Auto: sub stream, speed cap 30"), QStringLiteral("warn"));
    else if (s == QLatin1String("lost"))
        m_log->add(QStringLiteral("System"), QStringLiteral("Link lost") + sim,
                   QStringLiteral("Stop sent · controls locked"), QStringLiteral("crit"));
    else
        m_log->add(QStringLiteral("System"), QStringLiteral("Link restored") + sim, QStringLiteral("OK"));
    emit stateChanged();
}
