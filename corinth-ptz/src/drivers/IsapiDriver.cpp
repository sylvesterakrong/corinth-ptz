#include "IsapiDriver.h"

#include <QRegularExpression>

static const QByteArray kXml = QByteArrayLiteral("application/xml");

IsapiDriver::IsapiDriver(const CameraConfig &cfg, QObject *parent)
    : CameraDriver(cfg, parent)
{
}

QVariantMap IsapiDriver::vars() const
{
    QVariantMap v;
    v[QStringLiteral("ch")] = m_profile.integer(QStringLiteral("channel"), 1);
    v[QStringLiteral("ip")] = m_cfg.ip;
    v[QStringLiteral("rtspPort")] = m_cfg.rtspPort;
    return v;
}

void IsapiDriver::send(const QString &key, const QVariantMap &extra, const QString &label, bool changesPresets)
{
    QVariantMap v = vars();
    for (auto it = extra.begin(); it != extra.end(); ++it)
        v.insert(it.key(), it.value());
    const ProfileRequest r = m_profile.request(key, v);
    if (!r.valid) {
        emit commandDone(label, false, 0, QStringLiteral("Not defined in the camera profile: ") + key);
        return;
    }
    command(r.method, r.path, r.body, label, changesPresets);
}

void IsapiDriver::command(const QString &method, const QString &path, const QByteArray &body,
                          const QString &label, bool changesPresets)
{
    http(method, path, body, kXml, [this, label, changesPresets](bool ok, int status, const QByteArray &data, int ms) {
        QString err;
        if (!ok) {
            const QString sub = Xml::tagText(QString::fromUtf8(data), QStringLiteral("subStatusCode"));
            const QString st = Xml::tagText(QString::fromUtf8(data), QStringLiteral("statusString"));
            err = status ? QStringLiteral("HTTP %1 %2").arg(status).arg(sub.isEmpty() ? st : sub)
                         : QString::fromUtf8(data).left(120);
        }
        emit commandDone(label, ok, ms, err);
        if (ok && changesPresets)
            emit presetsDirty();
    });
}

void IsapiDriver::continuousMove(int pan, int tilt, int zoom, const QString &label)
{
    // A camera with a separate stop endpoint defines "ptz.stop" in its profile.
    const bool stop = !pan && !tilt && !zoom;
    const QString key = (stop && m_profile.has(QStringLiteral("ptz.stop"))) ? QStringLiteral("ptz.stop")
                                                                              : QStringLiteral("ptz.continuous");
    send(key, {{QStringLiteral("pan"), pan}, {QStringLiteral("tilt"), tilt}, {QStringLiteral("zoom"), zoom}}, label);
}

void IsapiDriver::gotoPreset(int id, const QString &label)
{
    send(QStringLiteral("ptz.gotoPreset"), {{QStringLiteral("id"), id}}, label);
}

void IsapiDriver::setPreset(int id, const QString &name, const QString &label)
{
    send(QStringLiteral("ptz.setPreset"), {{QStringLiteral("id"), id}, {QStringLiteral("name"), name}}, label, true);
}

void IsapiDriver::gotoHome(const QString &label)
{
    send(QStringLiteral("ptz.gotoHome"), {}, label);
}

void IsapiDriver::setHome(const QString &label)
{
    send(QStringLiteral("ptz.setHome"), {}, label);
}

void IsapiDriver::focus(int dir, const QString &label)
{
    send(QStringLiteral("ptz.focus"), {{QStringLiteral("speed"), dir * 60}}, label);
}

void IsapiDriver::iris(int dir, const QString &label)
{
    send(QStringLiteral("ptz.iris"), {{QStringLiteral("speed"), dir * 60}}, label);
}

void IsapiDriver::onePushFocus(const QString &label)
{
    // Hikvision spells this endpoint "foucs" in its own firmware.
    send(QStringLiteral("ptz.onePushFocus"), {}, label);
}

void IsapiDriver::setAux(const QString &name, bool on, const QString &label)
{
    const QString base = QStringLiteral("aux.") + name;
    if (!m_profile.has(base + QStringLiteral(".set"))) {
        // Stabilisation, heading lock and heater are vendor-specific on gimbal
        // cameras: capture them from the camera's web page and add them to the profile.
        emit commandDone(label, false, 0,
                         QStringLiteral("Not mapped yet. Add aux.%1.set to the camera profile.").arg(name));
        return;
    }
    const QString state = on ? m_profile.string(base + QStringLiteral(".on"), QStringLiteral("on"))
                             : m_profile.string(base + QStringLiteral(".off"), QStringLiteral("off"));
    send(base + QStringLiteral(".set"), {{QStringLiteral("state"), state}}, label);
}

void IsapiDriver::queryStatus()
{
    if (m_statusInFlight)
        return;
    const ProfileRequest r = m_profile.request(QStringLiteral("ptz.status"), vars());
    if (!r.valid)
        return;
    m_statusInFlight = true;
    http(r.method, r.path, {}, {},
         [this](bool ok, int, const QByteArray &data, int) {
             m_statusInFlight = false;
             if (!ok) return;
             const QString xml = QString::fromUtf8(data);
             // ISAPI reports tenths: azimuth 0..3600, elevation -900..2700, zoom x10.
             const double div = m_profile.number(QStringLiteral("ptz.status.divisor"), 10.0);
             bool okA = false, okE = false, okZ = false;
             const double az = Xml::tagText(xml, m_profile.string(QStringLiteral("ptz.status.azimuth"), QStringLiteral("azimuth"))).toDouble(&okA) / div;
             const double el = Xml::tagText(xml, m_profile.string(QStringLiteral("ptz.status.elevation"), QStringLiteral("elevation"))).toDouble(&okE) / div;
             const double zm = Xml::tagText(xml, m_profile.string(QStringLiteral("ptz.status.zoom"), QStringLiteral("absoluteZoom"))).toDouble(&okZ) / div;
             if (okA && okE) {
                 m_lastAz = az; m_lastEl = el; m_positionKnown = true;
                 emit statusUpdated(az, el, okZ ? zm : 1.0);
             }
         }, 2000);
}

void IsapiDriver::ping()
{
    const ProfileRequest r = m_profile.request(QStringLiteral("ping"), vars());
    http(r.valid ? r.method : QStringLiteral("GET"),
         r.valid ? r.path : QStringLiteral("/ISAPI/System/deviceInfo"), {}, {},
         [this](bool ok, int status, const QByteArray &, int ms) {
             // A 401 still proves the link is up, but means the login is wrong.
             emit pingResult(ok || status == 401, ms);
         }, 2000);
}

QString IsapiDriver::streamUrl(bool mainStream, int channel) const
{
    const QString custom = channel == 2 ? thermalOverride(mainStream) : (mainStream ? m_cfg.mainUrl : m_cfg.subUrl);
    if (!custom.isEmpty())
        return rtspWithCredentials(custom);
    QVariantMap v = vars();
    v[QStringLiteral("channel")] = qBound(1, channel, 4);
    v[QStringLiteral("n")] = mainStream ? 1 : 2;
    const QString tmpl = m_profile.string(QStringLiteral("stream.url"),
                                          QStringLiteral("rtsp://{ip}:{rtspPort}/Streaming/Channels/{channel}0{n}"));
    return rtspWithCredentials(CameraProfile::expand(tmpl, v, false));
}

void IsapiDriver::request(const QString &method, const QString &path, const QByteArray &body,
                          HttpCallback cb)
{
    http(method, path, body, body.isEmpty() ? QByteArray() : kXml, std::move(cb), 5000);
}

bool IsapiDriver::hasChannel2() const
{
    return m_profile.integer(QStringLiteral("channels"), 1) >= 2 || CameraDriver::hasChannel2();
}

bool IsapiDriver::supportsPresetRename() const
{
    return m_profile.has(QStringLiteral("ptz.renamePreset")) && m_profile.has(QStringLiteral("ptz.getPreset"));
}

void IsapiDriver::setZoomLevel(double times, const QString &label)
{
    if (!m_positionKnown) {
        emit commandDone(label, false, 0, QStringLiteral("Camera position not read yet, so pan and tilt cannot be kept"));
        return;
    }
    // ISAPI absolute positions are in tenths: azimuth 0..3600, elevation -900..2700, zoom x10.
    send(QStringLiteral("ptz.absoluteZoom"),
         {{QStringLiteral("az"), qRound(m_lastAz * 10.0)}, {QStringLiteral("el"), qRound(m_lastEl * 10.0)},
          {QStringLiteral("zoom"), qRound(times * 10.0)}}, label);
}

// ---- click to point -------------------------------------------------------

void IsapiDriver::pointAt(double nx, double ny, const QString &label)
{
    boxZoom(nx, ny, nx, ny, label);
}

void IsapiDriver::boxZoom(double x1, double y1, double x2, double y2, const QString &label)
{
    // Hikvision "3D positioning": two corners in a 0..255 grid, origin top-left.
    // Start == end centres on the point; dragging top-left to bottom-right zooms
    // in, the other way zooms out.
    const int scale = m_profile.integer(QStringLiteral("ptz.point.scale"), 255);
    const bool flipY = m_profile.boolean(QStringLiteral("ptz.point.flipY"), false);
    auto gx = [scale](double v) { return qBound(0, qRound(v * scale), scale); };
    auto gy = [scale, flipY](double v) { return qBound(0, qRound((flipY ? 1.0 - v : v) * scale), scale); };
    send(QStringLiteral("ptz.point"),
         {{QStringLiteral("x1"), gx(x1)}, {QStringLiteral("y1"), gy(y1)},
          {QStringLiteral("x2"), gx(x2)}, {QStringLiteral("y2"), gy(y2)}},
         label);
}

// ---- presets --------------------------------------------------------------

void IsapiDriver::queryPresets()
{
    const ProfileRequest r = m_profile.request(QStringLiteral("ptz.listPresets"), vars());
    if (!r.valid)
        return;
    http(r.method, r.path, {}, {}, [this](bool ok, int status, const QByteArray &data, int) {
        if (!ok) {
            emit presetsListed(false, {}, status ? QStringLiteral("HTTP %1").arg(status) : QString::fromUtf8(data).left(120));
            return;
        }
        const QString xml = QString::fromUtf8(data);
        const QRegularExpression block(QStringLiteral("<PTZPreset\\b[^>]*>(.*?)</PTZPreset>"),
                                       QRegularExpression::DotMatchesEverythingOption);
        QVariantList list;
        auto it = block.globalMatch(xml);
        while (it.hasNext()) {
            const QString b = it.next().captured(1);
            bool okId = false;
            const int id = Xml::tagText(b, QStringLiteral("id")).toInt(&okId);
            if (!okId || id <= 0)
                continue;
            const QString enabled = Xml::tagText(b, QStringLiteral("enabled"));
            if (!enabled.isEmpty() && enabled.compare(QLatin1String("true"), Qt::CaseInsensitive) != 0)
                continue;
            QString name = Xml::tagText(b, QStringLiteral("presetName"));
            if (name.isEmpty())
                name = QStringLiteral("Preset %1").arg(id);
            QVariantMap m;
            m[QStringLiteral("id")] = id;
            m[QStringLiteral("name")] = name;
            list << m;
        }
        emit presetsListed(true, list, QString());
    }, 4000);
}

void IsapiDriver::renamePreset(int id, const QString &name, const QString &label)
{
    // Read the preset's own XML, change only its name, write it back. If the
    // camera's XML carries the stored position, it goes back unchanged.
    const QVariantMap v = [&] { QVariantMap m = vars(); m[QStringLiteral("id")] = id; return m; }();
    const ProfileRequest g = m_profile.request(QStringLiteral("ptz.getPreset"), v);
    const ProfileRequest p = m_profile.request(QStringLiteral("ptz.renamePreset"), v);
    if (!g.valid || !p.valid) {
        emit commandDone(label, false, 0, QStringLiteral("Rename is not defined in the camera profile (ptz.getPreset, ptz.renamePreset)"));
        return;
    }
    const QString tag = m_profile.string(QStringLiteral("ptz.renamePreset.nameTag"), QStringLiteral("presetName"));
    http(g.method, g.path, {}, {}, [this, p, tag, name, label](bool ok, int status, const QByteArray &data, int ms) {
        if (!ok) {
            emit commandDone(label, false, ms, status ? QStringLiteral("HTTP %1 reading the preset").arg(status)
                                                      : QString::fromUtf8(data).left(120));
            return;
        }
        QString xml = QString::fromUtf8(data);
        const QRegularExpression re(QStringLiteral("(<(?:\\w+:)?%1(?:\\s[^>]*)?>)[^<]*(</(?:\\w+:)?%1>)")
                                        .arg(QRegularExpression::escape(tag)));
        const QRegularExpressionMatch m = re.match(xml);
        if (!m.hasMatch()) {
            emit commandDone(label, false, ms, QStringLiteral("The camera's preset XML has no <%1> to rename").arg(tag));
            return;
        }
        xml.replace(m.capturedStart(0), m.capturedLength(0), m.captured(1) + Xml::escape(name) + m.captured(2));
        command(p.method, p.path, xml.toUtf8(), label, true);
    }, 4000);
}

// ---- aux switches ---------------------------------------------------------

void IsapiDriver::queryAux()
{
    const QVariantList items = m_profile.list(QStringLiteral("aux"));
    for (const QVariant &item : items) {
        const QString name = item.toMap().value(QStringLiteral("id")).toString();
        const QString base = QStringLiteral("aux.") + name;
        const ProfileRequest r = m_profile.request(base + QStringLiteral(".get"), vars());
        if (!r.valid)
            continue;
        const QString onValue = m_profile.string(base + QStringLiteral(".on"), QStringLiteral("on"));
        const QString tag = m_profile.string(base + QStringLiteral(".get.tag"), QStringLiteral("status"));
        const QString matchTag = m_profile.string(base + QStringLiteral(".get.matchTag"));
        const QString matchValue = m_profile.string(base + QStringLiteral(".get.matchValue"));
        http(r.method, r.path, {}, {},
             [this, name, onValue, tag, matchTag, matchValue](bool ok, int, const QByteArray &data, int) {
                 if (!ok) return;
                 QString xml = QString::fromUtf8(data);
                 if (!matchTag.isEmpty()) {
                     // A list of <PTZAux> blocks: use the one whose matchTag equals matchValue.
                     const QRegularExpression block(QStringLiteral("<PTZAux\\b[^>]*>(.*?)</PTZAux>"),
                                                    QRegularExpression::DotMatchesEverythingOption);
                     bool found = false;
                     auto it = block.globalMatch(xml);
                     while (it.hasNext()) {
                         const QString b = it.next().captured(1);
                         if (Xml::tagText(b, matchTag).compare(matchValue, Qt::CaseInsensitive) == 0) {
                             xml = b;
                             found = true;
                             break;
                         }
                     }
                     if (!found) return;
                 }
                 const QString value = Xml::tagText(xml, tag);
                 if (value.isEmpty()) return;
                 emit auxState(name, value.compare(onValue, Qt::CaseInsensitive) == 0);
             }, 3000);
    }
}
