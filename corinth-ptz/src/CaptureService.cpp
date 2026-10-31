#include "CaptureService.h"
#include "AuditLog.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QVideoSink>

CaptureService::CaptureService(AuditLog *log, QObject *parent)
    : QObject(parent), m_log(log)
{
    m_ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

CaptureService::~CaptureService()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->write("q");
        m_proc->waitForFinished(2000);
        m_proc->kill();
    }
}

QObject *CaptureService::videoSinkObject() const { return m_sink.data(); }

void CaptureService::setVideoSinkObject(QObject *obj)
{
    auto *sink = qobject_cast<QVideoSink *>(obj);
    if (sink == m_sink) return;
    if (m_sink) disconnect(m_sink, nullptr, this, nullptr);
    m_sink = sink;
    if (m_sink) {
        connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &f) {
            if (!f.isValid()) return;
            m_frame = f;
            if (!m_hasFrame) { m_hasFrame = true; emit hasFrameChanged(); }
        });
    }
    emit videoSinkChanged();
}

bool CaptureService::recording() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

QString CaptureService::safeName(const QString &s)
{
    QString r = s;
    for (QChar &c : r)
        if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('_')) c = QLatin1Char('_');
    return r.isEmpty() ? QStringLiteral("camera") : r;
}

QString CaptureService::defaultFolder(const QString &kind) const
{
    if (kind == QLatin1String("recordings"))
        return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/Corinth");
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/Corinth");
}

QVariantMap CaptureService::snapshot(const QVariantMap &meta)
{
    QVariantMap r;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    r[QStringLiteral("time")] = now.toString(QStringLiteral("HH:mm:ss"));
    if (!m_hasFrame || !m_frame.isValid()) {
        r[QStringLiteral("ok")] = false;
        r[QStringLiteral("error")] = QStringLiteral("No video frame yet");
        m_log->add(meta.value(QStringLiteral("operator")).toString(), QStringLiteral("Snapshot"),
                   QStringLiteral("Failed · no video frame"), QStringLiteral("crit"));
        return r;
    }
    const QImage img = m_frame.toImage();
    QString folder = meta.value(QStringLiteral("folder")).toString();
    if (folder.isEmpty()) folder = defaultFolder(QStringLiteral("snapshots"));
    QDir().mkpath(folder);
    const bool jpeg = meta.value(QStringLiteral("format")).toString().compare(QLatin1String("JPEG"), Qt::CaseInsensitive) == 0;
    const QString base = folder + QLatin1Char('/') + safeName(meta.value(QStringLiteral("camera")).toString()) +
                         now.toString(QStringLiteral("_yyyyMMdd_HHmmss_zzz"));
    const QString path = base + (jpeg ? QStringLiteral(".jpg") : QStringLiteral(".png"));
    if (!img.save(path, jpeg ? "JPG" : "PNG", jpeg ? 92 : -1)) {
        r[QStringLiteral("ok")] = false;
        r[QStringLiteral("error")] = QStringLiteral("Could not write ") + path;
        return r;
    }

    QString hash;
    if (meta.value(QStringLiteral("hashFiles"), true).toBool()) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            QCryptographicHash h(QCryptographicHash::Sha256);
            h.addData(&f);
            hash = QString::fromLatin1(h.result().toHex());
        }
    }

    QJsonObject side = QJsonObject::fromVariantMap(meta);
    side.remove(QStringLiteral("folder"));
    side[QStringLiteral("utc")] = now.toString(Qt::ISODateWithMs);
    side[QStringLiteral("file")] = QFileInfo(path).fileName();
    side[QStringLiteral("width")] = img.width();
    side[QStringLiteral("height")] = img.height();
    if (!hash.isEmpty()) side[QStringLiteral("sha256")] = hash;
    QFile sf(base + QStringLiteral(".json"));
    if (sf.open(QIODevice::WriteOnly))
        sf.write(QJsonDocument(side).toJson(QJsonDocument::Indented));

    r[QStringLiteral("ok")] = true;
    r[QStringLiteral("path")] = path;
    r[QStringLiteral("hash")] = hash.isEmpty() ? QString() : hash.left(4) + QStringLiteral("…") + hash.right(4);
    m_log->add(meta.value(QStringLiteral("operator")).toString(), QStringLiteral("Snapshot"),
               QStringLiteral("Saved · ") + (hash.isEmpty() ? QFileInfo(path).fileName() : r.value(QStringLiteral("hash")).toString()));
    return r;
}

QString CaptureService::startRecording(const QString &rtspUrl, const QString &cameraName,
                                       const QString &folder, int segmentMinutes)
{
    if (recording()) return {};
    if (m_ffmpeg.isEmpty())
        return QStringLiteral("ffmpeg not found on PATH. Install it to enable recording (see README).");
    if (rtspUrl.isEmpty())
        return QStringLiteral("No stream address for this camera");

    QString dir = folder.isEmpty() ? defaultFolder(QStringLiteral("recordings")) : folder;
    QDir().mkpath(dir);
    const QString pattern = dir + QLatin1Char('/') + safeName(cameraName) + QStringLiteral("_%Y%m%d_%H%M%S.mkv");

    if (!m_proc) {
        m_proc = new QProcess(this);
        connect(m_proc, &QProcess::stateChanged, this, [this]() { emit recordingChanged(); });
        connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
            const QString err = QString::fromUtf8(m_proc->readAllStandardError()).trimmed();
            if (!m_stopping) {
                const QString msg = err.isEmpty() ? QStringLiteral("Recorder stopped (code %1)").arg(code) : err.left(200);
                m_log->add(QStringLiteral("System"), QStringLiteral("Recording stopped unexpectedly"), msg, QStringLiteral("crit"));
                emit recordingError(msg);
            }
            m_stopping = false;
            emit recordingChanged();
        });
    }
    // Stream copy (no re-encode): low CPU, exact camera bitstream for evidence.
    const QStringList args = {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-rtsp_transport"), QStringLiteral("tcp"),
        QStringLiteral("-i"), rtspUrl,
        QStringLiteral("-c"), QStringLiteral("copy"),
        QStringLiteral("-f"), QStringLiteral("segment"),
        QStringLiteral("-segment_time"), QString::number(qMax(1, segmentMinutes) * 60),
        QStringLiteral("-reset_timestamps"), QStringLiteral("1"),
        QStringLiteral("-strftime"), QStringLiteral("1"),
        pattern};
    m_stopping = false;
    m_proc->start(m_ffmpeg, args);
    if (!m_proc->waitForStarted(3000))
        return QStringLiteral("Could not start ffmpeg");
    m_recordStarted = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss"));
    emit recordingChanged();
    return {};
}

void CaptureService::stopRecording()
{
    if (!recording()) return;
    m_stopping = true;
    m_proc->write("q");   // ffmpeg finalises the file on 'q'
    QTimer::singleShot(3000, this, [this]() {
        if (m_proc && m_proc->state() != QProcess::NotRunning) m_proc->kill();
    });
}
