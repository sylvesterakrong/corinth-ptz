#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QVideoFrame>

class QVideoSink;
class QProcess;
class AuditLog;

// Snapshots (from the live video frame) and recordings (ffmpeg stream copy).
// Every snapshot gets a SHA-256 hash and a JSON sidecar with the metadata the
// UI shows: UTC time, camera, az/el/zoom, channel, operator, vessel position.
class CaptureService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *videoSink READ videoSinkObject WRITE setVideoSinkObject NOTIFY videoSinkChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool ffmpegAvailable READ ffmpegAvailable CONSTANT)
    Q_PROPERTY(QString recordStarted READ recordStarted NOTIFY recordingChanged)
public:
    CaptureService(AuditLog *log, QObject *parent = nullptr);
    ~CaptureService() override;

    QObject *videoSinkObject() const;
    void setVideoSinkObject(QObject *sink);
    bool hasFrame() const { return m_hasFrame; }
    bool recording() const;
    bool ffmpegAvailable() const { return !m_ffmpeg.isEmpty(); }
    QString recordStarted() const { return m_recordStarted; }

    // meta keys used: camera, az, el, zoom, channel, operator, hashFiles (bool),
    // folder, format ("PNG"/"JPEG"). Returns {ok, path, hash, time, error}.
    Q_INVOKABLE QVariantMap snapshot(const QVariantMap &meta);
    // Returns an empty string on success, or the reason it could not start.
    Q_INVOKABLE QString startRecording(const QString &rtspUrl, const QString &cameraName,
                                       const QString &folder, int segmentMinutes);
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE QString defaultFolder(const QString &kind) const;

signals:
    void videoSinkChanged();
    void hasFrameChanged();
    void recordingChanged();
    void recordingError(const QString &message);

private:
    static QString safeName(const QString &s);

    AuditLog *m_log;
    QPointer<QVideoSink> m_sink;
    QVideoFrame m_frame;
    bool m_hasFrame = false;
    QProcess *m_proc = nullptr;
    QString m_ffmpeg;
    QString m_recordStarted;
    bool m_stopping = false;
};
