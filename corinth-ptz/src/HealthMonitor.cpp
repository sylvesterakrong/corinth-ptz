#include "HealthMonitor.h"
#include "AuditLog.h"
#include "drivers/CameraDriver.h"

#include <QDateTime>

namespace {
int rank(const QString &level)
{
    return level == QLatin1String("crit") ? 3 : level == QLatin1String("warn") ? 2 : level == QLatin1String("ok") ? 1 : 0;
}
bool listHas(const QVariant &list, const QString &value)
{
    const QStringList l = list.toStringList();
    for (const QString &s : l)
        if (s.compare(value, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}
}

HealthMonitor::HealthMonitor(AuditLog *log, QObject *parent) : QObject(parent), m_log(log) {}

void HealthMonitor::setDriver(CameraDriver *driver)
{
    m_driver = driver;
    ++m_generation;            // late answers from the old camera are ignored
    m_busy = false;
    m_pending = 0;
    m_results.clear();
    m_levels.clear();
    m_items.clear();
    m_overall = QStringLiteral("unknown");
    m_updated.clear();
    m_stale = false;
    m_specs = driver ? driver->profile().list(QStringLiteral("health")) : QVariantList();
    m_available = !m_specs.isEmpty();
    if (driver)
        connect(driver, &CameraDriver::profileChanged, this, [this]() {
            m_specs = m_driver ? m_driver->profile().list(QStringLiteral("health")) : QVariantList();
            m_available = !m_specs.isEmpty();
            refresh();
        });
    emit changed();
}

void HealthMonitor::setLinkState(const QString &state)
{
    if (state == m_link) return;
    m_link = state;
    if (state != QLatin1String("ok") && !m_items.isEmpty()) {
        m_stale = true;
        emit changed();
    } else if (state == QLatin1String("ok") && m_stale) {
        refresh();
    }
}

void HealthMonitor::refresh()
{
    if (!m_driver || m_busy)
        return;
    if (m_link != QLatin1String("ok")) {
        if (!m_items.isEmpty() && !m_stale) { m_stale = true; emit changed(); }
        return;
    }
    if (m_specs.isEmpty()) {
        emit changed();
        return;
    }

    // One request per endpoint: several items usually read the same document.
    QHash<QString, bool> endpoints;
    for (const QVariant &v : std::as_const(m_specs))
        endpoints.insert(v.toMap().value(QStringLiteral("ep")).toString(), true);

    m_busy = true;
    m_pending = int(endpoints.size());
    m_results.clear();
    const int gen = m_generation;
    emit changed();

    const QVariantMap vars{{QStringLiteral("ch"), m_driver->profile().integer(QStringLiteral("channel"), 1)},
                           {QStringLiteral("vch"), 1}};
    const QList<QString> eps = endpoints.keys();
    for (const QString &ep : eps) {
        const QString path = CameraProfile::expand(ep, vars, false);
        m_driver->request(QStringLiteral("GET"), path, {}, [this, gen, ep](bool ok, int status, const QByteArray &data, int) {
            if (gen != m_generation) return;
            const QString xml = QString::fromUtf8(data);
            for (const QVariant &v : std::as_const(m_specs)) {
                const QVariantMap spec = v.toMap();
                if (spec.value(QStringLiteral("ep")).toString() != ep) continue;
                QVariantMap r;
                if (ok) r[QStringLiteral("value")] = Xml::tagText(xml, spec.value(QStringLiteral("tag")).toString());
                else r[QStringLiteral("error")] = status ? QStringLiteral("HTTP %1").arg(status) : QString::fromUtf8(data).left(60);
                m_results.insert(spec.value(QStringLiteral("id")).toString(), r);
            }
            if (--m_pending == 0)
                finish(gen);
        });
    }
}

void HealthMonitor::finish(int generation)
{
    if (generation != m_generation) return;
    m_busy = false;

    QVariantList items;
    QString worst = QStringLiteral("unknown");
    bool anyRead = false;
    for (const QVariant &v : std::as_const(m_specs)) {
        const QVariantMap spec = v.toMap();
        const QString id = spec.value(QStringLiteral("id")).toString();
        const QString label = spec.value(QStringLiteral("label"), id).toString();
        const QVariantMap r = m_results.value(id);
        const QString value = r.value(QStringLiteral("value")).toString();
        const QString error = r.value(QStringLiteral("error")).toString();

        QString level = QStringLiteral("unknown");
        QString text = QStringLiteral("—");
        if (error.isEmpty() && !value.isEmpty()) {
            anyRead = true;
            level = levelFor(spec, value);
            text = formatValue(spec, value);
        }

        const QString before = m_levels.value(id);
        if (level != before) {
            if (level == QLatin1String("warn") || level == QLatin1String("crit"))
                m_log->add(QStringLiteral("System"), QStringLiteral("Camera health · %1 %2").arg(label, text),
                           level == QLatin1String("crit") ? QStringLiteral("Critical") : QStringLiteral("Warning"), level);
            else if (level == QLatin1String("ok") && (before == QLatin1String("warn") || before == QLatin1String("crit")))
                m_log->add(QStringLiteral("System"), QStringLiteral("Camera health · %1 %2").arg(label, text),
                           QStringLiteral("Back to normal"), QStringLiteral("ok"));
            m_levels.insert(id, level);
        }
        if (rank(level) > rank(worst))
            worst = level;

        QVariantMap item;
        item[QStringLiteral("id")] = id;
        item[QStringLiteral("label")] = label;
        item[QStringLiteral("text")] = text;
        item[QStringLiteral("level")] = level;
        item[QStringLiteral("error")] = error;
        items << item;
    }
    m_items = items;
    m_overall = worst;
    if (anyRead) {
        m_updated = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss"));
        m_stale = false;
    }
    emit changed();
}

QString HealthMonitor::levelFor(const QVariantMap &spec, const QString &value)
{
    if (value.isEmpty())
        return QStringLiteral("unknown");
    if (listHas(spec.value(QStringLiteral("critValues")), value))
        return QStringLiteral("crit");
    const QVariant okValues = spec.value(QStringLiteral("okValues"));
    if (!okValues.toStringList().isEmpty()) {
        if (listHas(okValues, value))
            return QStringLiteral("ok");
        return spec.value(QStringLiteral("badLevel"), QStringLiteral("warn")).toString();
    }
    if (listHas(spec.value(QStringLiteral("warnValues")), value))
        return QStringLiteral("warn");

    bool numeric = false;
    const double n = value.toDouble(&numeric);
    if (numeric) {
        auto has = [&spec](const char *k) { return spec.contains(QLatin1String(k)); };
        auto num = [&spec](const char *k) { return spec.value(QLatin1String(k)).toDouble(); };
        if (has("critAbove") && n >= num("critAbove")) return QStringLiteral("crit");
        if (has("critBelow") && n <= num("critBelow")) return QStringLiteral("crit");
        if (has("warnAbove") && n >= num("warnAbove")) return QStringLiteral("warn");
        if (has("warnBelow") && n <= num("warnBelow")) return QStringLiteral("warn");
    }
    return QStringLiteral("ok");
}

QString HealthMonitor::formatValue(const QVariantMap &spec, const QString &value)
{
    if (spec.value(QStringLiteral("format")).toString() == QLatin1String("duration")) {
        bool ok = false;
        qlonglong s = value.toLongLong(&ok);
        if (ok && s >= 0) {
            const qlonglong d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
            return d ? QStringLiteral("%1d %2h").arg(d).arg(h) : h ? QStringLiteral("%1h %2m").arg(h).arg(m) : QStringLiteral("%1m").arg(m);
        }
    }
    return value + spec.value(QStringLiteral("unit")).toString();
}
