#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

class AuditLog;
class CameraDriver;

// Polls the health items defined in the camera profile ("health": cpu, uptime,
// SD card, temperature ... whatever the camera exposes) and grades each one
// ok / warn / crit against the thresholds in the profile. Exposed to QML as
// "health". Changes to warn or crit, and recoveries, are written to the audit log.
//
// Polling pauses while the link is degraded or lost, so it never competes with
// PTZ commands on a weak link; the last values stay on screen marked stale.
class HealthMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)       // [{ id, label, text, level, error }]
    Q_PROPERTY(QString overall READ overall NOTIFY changed)        // ok | warn | crit | unknown
    Q_PROPERTY(QString updated READ updated NOTIFY changed)        // HH:mm:ss UTC of the last good read
    Q_PROPERTY(bool stale READ stale NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)       // the profile defines health items
public:
    explicit HealthMonitor(AuditLog *log, QObject *parent = nullptr);

    void setDriver(CameraDriver *driver);
    void setLinkState(const QString &state);

    QVariantList items() const { return m_items; }
    QString overall() const { return m_overall; }
    QString updated() const { return m_updated; }
    bool stale() const { return m_stale; }
    bool busy() const { return m_busy; }
    bool available() const { return m_available; }

    Q_INVOKABLE void refresh();

    // Exposed for tests.
    static QString levelFor(const QVariantMap &spec, const QString &value);
    static QString formatValue(const QVariantMap &spec, const QString &value);

signals:
    void changed();

private:
    void finish(int generation);

    AuditLog *m_log;
    QPointer<CameraDriver> m_driver;
    QString m_link = QStringLiteral("ok");
    QVariantList m_specs;
    QVariantList m_items;
    QString m_overall = QStringLiteral("unknown");
    QString m_updated;
    bool m_stale = false;
    bool m_busy = false;
    bool m_available = false;
    int m_generation = 0;
    int m_pending = 0;
    QHash<QString, QVariantMap> m_results;   // id -> { value, error }
    QHash<QString, QString> m_levels;        // id -> last graded level
};
