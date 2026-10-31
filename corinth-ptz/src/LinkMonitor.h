#pragma once

#include <QObject>
#include <QPointer>
#include <QVector>

class CameraDriver;
class AuditLog;
class QTimer;

// Probes the camera once a second and classifies the link:
//   ok        normal
//   degraded  RTT over 500 ms or loss over 3%  -> sub stream + PTZ speed cap 30
//   lost      no reply for 5 s                 -> stop sent, controls locked
// "simulate" overrides the measured state for drills and demos.
class LinkMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(int rttMs READ rttMs NOTIFY metricsChanged)
    Q_PROPERTY(int jitterMs READ jitterMs NOTIFY metricsChanged)
    Q_PROPERTY(double lossPct READ lossPct NOTIFY metricsChanged)
    Q_PROPERTY(bool hasSample READ hasSample NOTIFY metricsChanged)
    Q_PROPERTY(QString simulate READ simulate WRITE setSimulate NOTIFY stateChanged)
public:
    LinkMonitor(AuditLog *log, QObject *parent = nullptr);

    void setDriver(CameraDriver *driver);

    QString state() const;
    int rttMs() const { return m_rtt; }
    int jitterMs() const { return m_jitter; }
    double lossPct() const { return m_loss; }
    bool hasSample() const { return m_hasSample; }
    QString simulate() const { return m_simulate; }
    void setSimulate(const QString &s);

signals:
    void stateChanged();
    void metricsChanged();

private:
    void probe();
    void onResult(bool ok, int ms);
    void evaluate();

    AuditLog *m_log;
    QPointer<CameraDriver> m_driver;
    QTimer *m_timer = nullptr;
    QVector<int> m_samples;   // ms, -1 = lost
    int m_consecutiveFails = 0;
    int m_rtt = 0, m_jitter = 0;
    double m_loss = 0;
    bool m_hasSample = false;
    QString m_measured = QStringLiteral("ok");
    QString m_simulate;
    QString m_lastReported = QStringLiteral("ok");
    bool m_waiting = false;
};
