#pragma once

#include <QMutex>
#include <QObject>
#include <QString>
#include <QVideoFrame>
#include <QVideoSink>
#include <QElapsedTimer>
#include <QtQml/qqmlregistration.h>
#include <atomic>

class DecodeThread;

// Network video player built for low delay instead of smoothness: libavformat and
// libavcodec on a worker thread, no input buffering, slice threading only, and only
// ever the newest decoded frame goes to the screen (older ones are dropped when the
// UI is behind). Reconnects by itself with a growing back-off. Takes rtsp:// and
// http:// (HTTP-FLV) addresses and feeds a QVideoSink, so the VideoOutput and the
// snapshot code work as with MediaPlayer.
//
// Without FFmpeg at build time `available` is false and nothing plays; the QML falls
// back to MediaPlayer.
class LowLatencyPlayer : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    Q_PROPERTY(QString latencyMode READ latencyMode WRITE setLatencyMode NOTIFY optionsChanged)   // low | smooth
    Q_PROPERTY(QString transport READ transport WRITE setTransport NOTIFY optionsChanged)         // tcp | udp (RTSP)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)     // idle | connecting | playing | reconnecting
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(double measuredFps READ measuredFps NOTIFY statsChanged)
    Q_PROPERTY(double decodeMs READ decodeMs NOTIFY statsChanged)       // packet in to frame ready
    Q_PROPERTY(double backlogMs READ backlogMs NOTIFY statsChanged)     // how far behind the stream's own clock
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY statsChanged)
    Q_PROPERTY(bool available READ available CONSTANT)
public:
    explicit LowLatencyPlayer(QObject *parent = nullptr);
    ~LowLatencyPlayer() override;

    QString source() const { return m_source; }
    void setSource(const QString &s);
    QVideoSink *videoSink() const { return m_sink; }
    void setVideoSink(QVideoSink *sink);
    QString latencyMode() const { return m_latency; }
    void setLatencyMode(const QString &m);
    QString transport() const { return m_transport; }
    void setTransport(const QString &t);
    QString state() const { return m_state; }
    bool playing() const { return m_state == QLatin1String("playing"); }
    QString error() const { return m_error; }
    double measuredFps() const { return m_fps; }
    double decodeMs() const { return m_decodeMs; }
    double backlogMs() const { return m_backlogMs; }
    int droppedFrames() const { return m_dropped; }
    bool available() const { return isAvailable(); }
    static bool isAvailable();

    Q_INVOKABLE void stop();

signals:
    void sourceChanged();
    void videoSinkChanged();
    void optionsChanged();
    void stateChanged();
    void errorChanged();
    void statsChanged();
    // Every frame handed to the sink, with the time it reached the UI thread. For tests and diagnostics.
    void frameDelivered(const QVideoFrame &frame, qint64 uiMsecs);

private:
    friend class DecodeThread;
    // Called from the decode thread.
    void postFrame(const QVideoFrame &frame, double decodeMs, double backlogMs);
    void postState(const QString &state);
    void postError(const QString &error);

    void restart();
    void deliver();
    void setState(const QString &s);
    void setError(const QString &e);

    QString m_source;
    QVideoSink *m_sink = nullptr;
    QString m_latency = QStringLiteral("low");
    QString m_transport = QStringLiteral("tcp");
    QString m_state = QStringLiteral("idle");
    QString m_error;
    double m_fps = 0, m_decodeMs = 0, m_backlogMs = 0;
    int m_dropped = 0;

    DecodeThread *m_thread = nullptr;
    QMutex m_mutex;
    QVideoFrame m_latest;
    double m_latestDecodeMs = 0, m_latestBacklogMs = 0;
    std::atomic<bool> m_pending{false};
    int m_frameCount = 0;
    QElapsedTimer m_fpsClock;
};
