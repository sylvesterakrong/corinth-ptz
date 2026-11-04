#include "CameraProfile.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QPair>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

namespace {
QString escapeXml(QString s)
{
    s.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    s.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    s.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    s.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return s;
}
}

QString CameraProfile::userFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/profiles");
}

QString CameraProfile::builtInText()
{
    QFile f(QStringLiteral(":/profiles/hikvision-isapi.json"));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QJsonObject CameraProfile::merge(const QJsonObject &base, const QJsonObject &over)
{
    QJsonObject out = base;
    for (auto it = over.begin(); it != over.end(); ++it) {
        if (it.value().isObject() && out.value(it.key()).isObject())
            out[it.key()] = merge(out.value(it.key()).toObject(), it.value().toObject());
        else
            out[it.key()] = it.value();   // null removes the built-in entry
    }
    return out;
}

CameraProfile CameraProfile::load(const QString &cameraId, bool useBuiltIn)
{
    CameraProfile p;
    auto readInto = [&p](const QString &text, const QString &source) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            p.m_error += QStringLiteral("%1: %2 (offset %3). ").arg(source, err.errorString()).arg(err.offset);
            return;
        }
        p.m_root = merge(p.m_root, doc.object());
        p.m_sources << source;
    };

    if (useBuiltIn)
        readInto(builtInText(), QStringLiteral("built-in"));

    const QDir dir(userFolder());
    QStringList files{QStringLiteral("default.json")};
    static const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9_-]+$"));
    if (safeId.match(cameraId).hasMatch())
        files << cameraId + QStringLiteral(".json");
    for (const QString &name : files) {
        QFile f(dir.filePath(name));
        if (f.exists() && f.open(QIODevice::ReadOnly))
            readInto(QString::fromUtf8(f.readAll()), f.fileName());
    }
    return p;
}

QJsonValue CameraProfile::value(const QString &dotted) const
{
    QJsonValue cur(m_root);
    const QStringList parts = dotted.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (!cur.isObject())
            return QJsonValue::Undefined;
        cur = cur.toObject().value(part);
    }
    return cur;
}

bool CameraProfile::has(const QString &dotted) const
{
    const QJsonValue v = value(dotted);
    return !v.isUndefined() && !v.isNull();
}

QString CameraProfile::string(const QString &dotted, const QString &def) const
{
    const QJsonValue v = value(dotted);
    if (v.isString()) return v.toString();
    if (v.isDouble()) return QString::number(v.toDouble());
    return def;
}

int CameraProfile::integer(const QString &dotted, int def) const
{
    const QJsonValue v = value(dotted);
    return v.isDouble() ? v.toInt(def) : def;
}

double CameraProfile::number(const QString &dotted, double def) const
{
    const QJsonValue v = value(dotted);
    return v.isDouble() ? v.toDouble(def) : def;
}

bool CameraProfile::boolean(const QString &dotted, bool def) const
{
    const QJsonValue v = value(dotted);
    return v.isBool() ? v.toBool(def) : def;
}

ProfileRequest CameraProfile::request(const QString &dotted, const QVariantMap &vars) const
{
    ProfileRequest r;
    const QJsonValue v = value(dotted);
    if (!v.isObject())
        return r;
    const QJsonObject o = v.toObject();
    const QString path = o.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        return r;
    r.valid = true;
    r.method = o.value(QStringLiteral("method")).toString(QStringLiteral("GET")).toUpper();
    r.path = expand(path, vars, false);
    r.body = expand(o.value(QStringLiteral("body")).toString(), vars, true).toUtf8();
    return r;
}

QVariantList CameraProfile::list(const QString &dotted) const
{
    QVariantList out;
    const QJsonValue v = value(dotted);
    if (!v.isObject())
        return out;
    const QJsonObject o = v.toObject();
    QList<QPair<double, QString>> keys;
    for (auto it = o.begin(); it != o.end(); ++it)
        if (it.value().isObject())
            keys.append({it.value().toObject().value(QStringLiteral("order")).toDouble(1000), it.key()});
    std::sort(keys.begin(), keys.end(), [](const QPair<double, QString> &a, const QPair<double, QString> &b) {
        return a.first != b.first ? a.first < b.first : a.second < b.second;
    });
    for (const auto &k : keys) {
        QVariantMap m = o.value(k.second).toObject().toVariantMap();
        m.insert(QStringLiteral("id"), k.second);
        out << m;
    }
    return out;
}

QString CameraProfile::expand(const QString &tmpl, const QVariantMap &vars, bool xmlEscape)
{
    static const QRegularExpression re(QStringLiteral("\\{([A-Za-z0-9_]+)\\}"));
    QString out;
    int last = 0;
    auto it = re.globalMatch(tmpl);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += tmpl.mid(last, m.capturedStart() - last);
        const QString key = m.captured(1);
        if (vars.contains(key)) {
            const QString s = vars.value(key).toString();
            out += xmlEscape ? escapeXml(s) : s;
        } else {
            out += m.captured(0);
        }
        last = m.capturedEnd();
    }
    out += tmpl.mid(last);
    return out;
}
