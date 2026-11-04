#include "LowLatencyPlayer.h"

#include <QDateTime>
#include <QMetaObject>
#include <QThread>

#if defined(CORINTH_HAS_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace {
qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
}

// ---------------------------------------------------------------------------------
// Worker thread: connect, demux, decode, hand the newest frame to the player.

class DecodeThread : public QThread
{
public:
    DecodeThread(LowLatencyPlayer *owner, const QString &url, bool lowLatency, bool tcp)
        : m_owner(owner), m_url(url), m_low(lowLatency), m_tcp(tcp) {}
    void abort() { m_abort = true; }

protected:
    void run() override;

private:
#if defined(CORINTH_HAS_FFMPEG)
    static int interrupt(void *opaque)
    {
        auto *t = static_cast<DecodeThread *>(opaque);
        if (t->m_abort) return 1;
        const qint64 d = t->m_deadline;
        return (d > 0 && nowMs() > d) ? 1 : 0;
    }
    // Returns when the stream ends, fails, or is aborted. `played` says whether any frame arrived.
    QString session(bool &played);
    void publish(const AVFrame *f, qint64 packetAt, double ptsSeconds);
    SwsContext *m_sws = nullptr;
    qint64 m_t0Wall = 0;
    double m_t0Pts = 0;
    bool m_haveT0 = false;
#endif
    LowLatencyPlayer *m_owner;
    QString m_url;
    bool m_low;
    bool m_tcp;
    std::atomic<bool> m_abort{false};
    std::atomic<qint64> m_deadline{0};
};

#if defined(CORINTH_HAS_FFMPEG)

static QString ffError(int code)
{
    char buf[256] = {0};
    av_strerror(code, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

void DecodeThread::publish(const AVFrame *f, qint64 packetAt, double ptsSeconds)
{
    const int w = f->width, h = f->height;
    if (w <= 0 || h <= 0) return;
    const bool fullRange = f->format == AV_PIX_FMT_YUVJ420P || f->color_range == AVCOL_RANGE_JPEG;

    QVideoFrameFormat ff(QSize(w, h), QVideoFrameFormat::Format_YUV420P);
    ff.setColorSpace(h >= 720 ? QVideoFrameFormat::ColorSpace_BT709 : QVideoFrameFormat::ColorSpace_BT601);
    ff.setColorRange(fullRange ? QVideoFrameFormat::ColorRange_Full : QVideoFrameFormat::ColorRange_Video);
    QVideoFrame vf(ff);
    if (!vf.map(QVideoFrame::WriteOnly))
        return;

    const int ch = (h + 1) / 2;
    if (f->format == AV_PIX_FMT_YUV420P || f->format == AV_PIX_FMT_YUVJ420P) {
        // Already the layout the sink wants: copy the planes row by row.
        for (int p = 0; p < 3; ++p) {
            const int rows = p == 0 ? h : ch;
            const int bytes = p == 0 ? w : (w + 1) / 2;
            uchar *dst = vf.bits(p);
            const int dstStride = vf.bytesPerLine(p);
            for (int y = 0; y < rows; ++y)
                memcpy(dst + y * dstStride, f->data[p] + y * f->linesize[p], size_t(bytes));
        }
    } else {
        m_sws = sws_getCachedContext(m_sws, w, h, AVPixelFormat(f->format), w, h, AV_PIX_FMT_YUV420P,
                                     SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_sws) { vf.unmap(); return; }
        uint8_t *dstData[4] = {vf.bits(0), vf.bits(1), vf.bits(2), nullptr};
        int dstStride[4] = {vf.bytesPerLine(0), vf.bytesPerLine(1), vf.bytesPerLine(2), 0};
        sws_scale(m_sws, f->data, f->linesize, 0, h, dstData, dstStride);
    }
    vf.unmap();

    const qint64 now = nowMs();
    double backlog = 0;
    if (!m_haveT0) {
        m_haveT0 = true;
        m_t0Wall = now;
        m_t0Pts = ptsSeconds;
    } else {
        backlog = double(now - m_t0Wall) - (ptsSeconds - m_t0Pts) * 1000.0;
        if (backlog < 0) backlog = 0;
    }
    m_owner->postFrame(vf, double(now - packetAt), backlog);
}

QString DecodeThread::session(bool &played)
{
    played = false;
    m_haveT0 = false;

    AVFormatContext *fmt = avformat_alloc_context();
    if (!fmt) return QStringLiteral("Out of memory");
    fmt->interrupt_callback.callback = &DecodeThread::interrupt;
    fmt->interrupt_callback.opaque = this;

    AVDictionary *opts = nullptr;
    if (m_low) {
        av_dict_set(&opts, "fflags", "nobuffer", 0);
        av_dict_set(&opts, "flags", "low_delay", 0);
        av_dict_set(&opts, "probesize", "32768", 0);
        av_dict_set(&opts, "analyzeduration", "0", 0);
        av_dict_set(&opts, "max_delay", "0", 0);
    }
    av_dict_set(&opts, "rtsp_transport", m_tcp ? "tcp" : "udp", 0);
    av_dict_set(&opts, "timeout", "5000000", 0);      // RTSP socket timeout, microseconds
    av_dict_set(&opts, "rw_timeout", "5000000", 0);   // HTTP read/write timeout

    m_deadline = nowMs() + 6000;
    int r = avformat_open_input(&fmt, m_url.toUtf8().constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (r < 0) {
        const QString e = m_abort ? QString() : ffError(r);
        return e;   // avformat_open_input frees fmt on failure
    }
    m_deadline = nowMs() + 6000;
    r = avformat_find_stream_info(fmt, nullptr);
    if (r < 0) {
        avformat_close_input(&fmt);
        return m_abort ? QString() : ffError(r);
    }

    const int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vi < 0) {
        avformat_close_input(&fmt);
        return QStringLiteral("No video stream in %1").arg(m_url.section(QLatin1Char('@'), -1));
    }
    for (unsigned i = 0; i < fmt->nb_streams; ++i)
        if (int(i) != vi) fmt->streams[i]->discard = AVDISCARD_ALL;   // no audio, no data

    AVStream *st = fmt->streams[vi];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        return QStringLiteral("No decoder for this video format");
    }
    AVCodecContext *cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(cc, st->codecpar);
    if (m_low) {
        cc->flags |= AV_CODEC_FLAG_LOW_DELAY;
        cc->thread_type = FF_THREAD_SLICE;   // frame threading would add whole frames of delay
        cc->thread_count = 2;
    } else {
        cc->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        cc->thread_count = 0;
    }
    r = avcodec_open2(cc, codec, nullptr);
    if (r < 0) {
        avcodec_free_context(&cc);
        avformat_close_input(&fmt);
        return ffError(r);
    }

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    QString err;
    const double tb = av_q2d(st->time_base);

    while (!m_abort) {
        m_deadline = nowMs() + 5000;   // a stalled network read gives up after 5 s
        r = av_read_frame(fmt, pkt);
        if (r < 0) {
            err = m_abort ? QString() : (r == AVERROR_EOF ? QStringLiteral("Stream ended") : ffError(r));
            break;
        }
        if (pkt->stream_index != vi) { av_packet_unref(pkt); continue; }
        const qint64 packetAt = nowMs();
        if (avcodec_send_packet(cc, pkt) >= 0) {
            while (avcodec_receive_frame(cc, frame) >= 0) {
                const int64_t ts = frame->best_effort_timestamp;
                publish(frame, packetAt, ts == AV_NOPTS_VALUE ? 0.0 : double(ts) * tb);
                if (!played) {
                    played = true;
                    m_owner->postState(QStringLiteral("playing"));
                    m_owner->postError(QString());
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&cc);
    avformat_close_input(&fmt);
    return err;
}

void DecodeThread::run()
{
    int backoff = 400;
    bool first = true;
    while (!m_abort) {
        m_owner->postState(first ? QStringLiteral("connecting") : QStringLiteral("reconnecting"));
        first = false;
        bool played = false;
        const qint64 began = nowMs();
        const QString err = session(played);
        if (m_abort) break;
        m_owner->postError(err.isEmpty() ? QStringLiteral("Connection lost") : err);
        m_owner->postState(QStringLiteral("reconnecting"));
        if (played && nowMs() - began > 5000) backoff = 400;   // it had been working: retry quickly
        for (int waited = 0; waited < backoff && !m_abort; waited += 50)
            msleep(50);
        backoff = qMin(backoff * 2, 5000);
    }
    if (m_sws) {
        sws_freeContext(m_sws);
        m_sws = nullptr;
    }
}

#else   // no FFmpeg: nothing plays

void DecodeThread::run() {}

#endif

// ---------------------------------------------------------------------------------

LowLatencyPlayer::LowLatencyPlayer(QObject *parent) : QObject(parent) {}

LowLatencyPlayer::~LowLatencyPlayer()
{
    stop();
}

bool LowLatencyPlayer::isAvailable()
{
#if defined(CORINTH_HAS_FFMPEG)
    return true;
#else
    return false;
#endif
}

void LowLatencyPlayer::setSource(const QString &s)
{
    if (s == m_source) return;
    m_source = s;
    emit sourceChanged();
    restart();
}

void LowLatencyPlayer::setVideoSink(QVideoSink *sink)
{
    if (sink == m_sink) return;
    m_sink = sink;
    emit videoSinkChanged();
}

void LowLatencyPlayer::setLatencyMode(const QString &m)
{
    const QString v = m == QLatin1String("smooth") ? m : QStringLiteral("low");
    if (v == m_latency) return;
    m_latency = v;
    emit optionsChanged();
    restart();
}

void LowLatencyPlayer::setTransport(const QString &t)
{
    const QString v = t == QLatin1String("udp") ? t : QStringLiteral("tcp");
    if (v == m_transport) return;
    m_transport = v;
    emit optionsChanged();
    restart();
}

void LowLatencyPlayer::stop()
{
    if (m_thread) {
        m_thread->abort();
        m_thread->wait(4000);
        delete m_thread;
        m_thread = nullptr;
    }
    {
        QMutexLocker l(&m_mutex);
        m_latest = QVideoFrame();
    }
    // A frame already queued for the UI thread must not appear after a stop.
    m_pending = false;
    setState(QStringLiteral("idle"));
}

void LowLatencyPlayer::restart()
{
    stop();
    setError(QString());
    m_fps = 0;
    m_frameCount = 0;
    emit statsChanged();
#if defined(CORINTH_HAS_FFMPEG)
    if (m_source.isEmpty()) return;
    m_thread = new DecodeThread(this, m_source, m_latency == QLatin1String("low"), m_transport == QLatin1String("tcp"));
    m_fpsClock.start();
    m_thread->start();
#endif
}

void LowLatencyPlayer::setState(const QString &s)
{
    if (s == m_state) return;
    m_state = s;
    emit stateChanged();
}

void LowLatencyPlayer::setError(const QString &e)
{
    if (e == m_error) return;
    m_error = e;
    emit errorChanged();
}

void LowLatencyPlayer::postState(const QString &state)
{
    DecodeThread *who = m_thread;   // the thread posting is the current one, or is being stopped
    Q_UNUSED(who);
    QMetaObject::invokeMethod(this, [this, state]() { if (m_thread) setState(state); }, Qt::QueuedConnection);
}

void LowLatencyPlayer::postError(const QString &error)
{
    QMetaObject::invokeMethod(this, [this, error]() { if (m_thread) setError(error); }, Qt::QueuedConnection);
}

// Decode thread: keep only the newest frame; wake the UI thread if it is not already woken.
void LowLatencyPlayer::postFrame(const QVideoFrame &frame, double decodeMs, double backlogMs)
{
    bool replaced = false;
    {
        QMutexLocker l(&m_mutex);
        replaced = m_latest.isValid();
        m_latest = frame;
        m_latestDecodeMs = decodeMs;
        m_latestBacklogMs = backlogMs;
    }
    if (replaced)
        QMetaObject::invokeMethod(this, [this]() { ++m_dropped; }, Qt::QueuedConnection);
    if (!m_pending.exchange(true))
        QMetaObject::invokeMethod(this, &LowLatencyPlayer::deliver, Qt::QueuedConnection);
}

void LowLatencyPlayer::deliver()
{
    QVideoFrame f;
    double dec = 0, back = 0;
    {
        QMutexLocker l(&m_mutex);
        f = m_latest;
        dec = m_latestDecodeMs;
        back = m_latestBacklogMs;
        m_latest = QVideoFrame();
    }
    m_pending = false;
    if (!f.isValid() || !m_thread)
        return;
    if (m_sink)
        m_sink->setVideoFrame(f);
    emit frameDelivered(f, QDateTime::currentMSecsSinceEpoch());

    ++m_frameCount;
    m_decodeMs = dec;
    m_backlogMs = back;
    if (m_fpsClock.elapsed() >= 1000) {
        m_fps = m_frameCount * 1000.0 / double(m_fpsClock.restart());
        m_frameCount = 0;
        emit statsChanged();
    }
}
