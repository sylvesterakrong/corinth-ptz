#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

struct ProfileRequest
{
    bool valid = false;
    QString method;
    QString path;
    QByteArray body;
};

// A camera's endpoints and request bodies as data instead of code, so a captured
// endpoint can be added by editing a JSON file and clicking "Reload profile".
//
// Layers (later ones win, key by key; a key set to null removes the entry):
//   1. built-in profile   profiles/hikvision-isapi.json (compiled in)
//   2. <AppData>/profiles/default.json       every camera on this laptop
//   3. <AppData>/profiles/<cameraId>.json    one camera
//
// Requests are {method, path, body} entries. {name} placeholders in path and
// body are filled in by the driver ({ch}, {id}, {pan}, {state} ...).
class CameraProfile
{
public:
    static CameraProfile load(const QString &cameraId, bool useBuiltIn);
    static QString userFolder();
    static QString builtInText();

    const QJsonObject &root() const { return m_root; }
    QVariantMap toVariant() const { return m_root.toVariantMap(); }
    QStringList sources() const { return m_sources; }
    QString error() const { return m_error; }

    QJsonValue value(const QString &dotted) const;
    bool has(const QString &dotted) const;
    QString string(const QString &dotted, const QString &def = QString()) const;
    int integer(const QString &dotted, int def) const;
    double number(const QString &dotted, double def) const;
    bool boolean(const QString &dotted, bool def) const;

    // The {method, path, body} entry at `dotted`, placeholders filled in.
    ProfileRequest request(const QString &dotted, const QVariantMap &vars) const;
    // Children of an object entry, ordered by their "order" field then name.
    // Each item is the child's fields plus "id" (its key).
    QVariantList list(const QString &dotted) const;

    // Replaces {name} with vars[name]. Unknown names are left as written.
    static QString expand(const QString &tmpl, const QVariantMap &vars, bool xmlEscape);
    static QJsonObject merge(const QJsonObject &base, const QJsonObject &over);

private:
    QJsonObject m_root;
    QStringList m_sources;
    QString m_error;
};
