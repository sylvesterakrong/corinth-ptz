#include "AuditLog.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

AuditLog::AuditLog(QObject *parent) : QAbstractListModel(parent)
{
    m_folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs");
    QDir().mkpath(m_folder);
}

int AuditLog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant AuditLog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const Entry &e = m_items.at(index.row());
    switch (role) {
    case TimeRole: return e.ts.toString(QStringLiteral("HH:mm:ss"));
    case WhoRole: return e.who;
    case WhatRole: return e.what;
    case ResultRole: return e.result;
    case KindRole: return e.kind;
    default: return {};
    }
}

QHash<int, QByteArray> AuditLog::roleNames() const
{
    return {{TimeRole, "time"}, {WhoRole, "who"}, {WhatRole, "what"}, {ResultRole, "result"}, {KindRole, "kind"}};
}

void AuditLog::add(const QString &who, const QString &what, const QString &result, const QString &kind)
{
    Entry e{QDateTime::currentDateTimeUtc(), who, what, result, kind};
    beginInsertRows({}, 0, 0);
    m_items.prepend(e);
    endInsertRows();
    if (m_items.size() > 2000) {
        beginRemoveRows({}, int(m_items.size()) - 1, int(m_items.size()) - 1);
        m_items.removeLast();
        endRemoveRows();
    }
    appendToDailyFile(e);
    emit countChanged();
}

QString AuditLog::csv(const QString &s)
{
    QString v = s;
    v.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + v + QLatin1Char('"');
}

void AuditLog::appendToDailyFile(const Entry &e)
{
    const QString path = m_folder + QStringLiteral("/audit-") + e.ts.toString(QStringLiteral("yyyyMMdd")) + QStringLiteral(".csv");
    QFile f(path);
    const bool isNew = !f.exists();
    if (!f.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream out(&f);
    if (isNew)
        out << "utc_time,who,what,result,kind\n";
    out << e.ts.toString(Qt::ISODate) << ',' << csv(e.who) << ',' << csv(e.what) << ','
        << csv(e.result) << ',' << e.kind << '\n';
}

QString AuditLog::exportCsv()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/Corinth");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/audit-export-") +
                         QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".csv");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    QTextStream out(&f);
    out << "utc_time,who,what,result,kind\n";
    for (int i = int(m_items.size()) - 1; i >= 0; --i) {
        const Entry &e = m_items.at(i);
        out << e.ts.toString(Qt::ISODate) << ',' << csv(e.who) << ',' << csv(e.what) << ','
            << csv(e.result) << ',' << e.kind << '\n';
    }
    return path;
}
