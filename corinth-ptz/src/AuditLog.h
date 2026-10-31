#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QVector>

// Session audit log: every command, control change, link event, snapshot and
// recording. Newest first in the UI; also appended to a daily CSV file under
// the app data folder so the record survives a crash.
class AuditLog : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString logFolder READ logFolder CONSTANT)
public:
    enum Roles { TimeRole = Qt::UserRole + 1, WhoRole, WhatRole, ResultRole, KindRole };

    explicit AuditLog(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_items.size()); }
    QString logFolder() const { return m_folder; }

    // kind: "ok", "warn" or "crit"
    Q_INVOKABLE void add(const QString &who, const QString &what, const QString &result,
                         const QString &kind = QStringLiteral("ok"));
    Q_INVOKABLE QString exportCsv();

signals:
    void countChanged();

private:
    struct Entry { QDateTime ts; QString who, what, result, kind; };
    static QString csv(const QString &s);
    void appendToDailyFile(const Entry &e);

    QVector<Entry> m_items;
    QString m_folder;
};
