// The low-latency player against a live stream served from this process with
// libavformat (MPEG-TS and FLV over HTTP, real-time pacing, 640x360, 25 fps). Every
// picture carries its frame number as 16 black/white blocks, so the test reads which
// frame the player delivered and how long after it was generated: arrival delay at the
// UI thread, which excludes the camera's encoder, the network and the screen.
//
// RTSP cannot be served by libavformat, so the RTSP-specific options (TCP / UDP) are
// not exercised here; they only select the transport inside the same demux/decode code.
#include "LowLatencyPlayer.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QThread>
#include <QVideoFrame>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

static int g_fail = 0;

static void check(bool ok, const QString &what)
{
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << what;
    if (!ok) ++g_fail;
}

static bool waitFor(const std::function<bool()> &cond, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!cond()) {
        if (t.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
    return true;
}

constexpr int kW = 640, kH = 360, kFps = 25, kBlock = 24, kBits = 16;

// ---- a live stream source ---------------------------------------------------------

class StreamServer
{
public:
    StreamServer(const std::string &url, const std::string &format, AVCodecID codec)
        : m_url(url), m_format(format), m_codec(codec)
    {
        m_gen.reset(new std::atomic<qint64>[8192]);
        for (int i = 0; i < 8192; ++i) m_gen[i] = 0;
        m_thread = std::thread([this] { run(); });
    }
    ~StreamServer() { stop(); }
    void stop()
    {
        m_stop = true;
        if (m_thread.joinable()) m_thread.join();
    }
    qint64 generatedAt(int idx) const { return (idx >= 0 && idx < 8192) ? m_gen[idx].load() : 0; }
    int framesSent() const { return m_sent; }
    bool clientConnected() const { return m_connected; }

private:
    static int interrupt(void *p) { return static_cast<StreamServer *>(p)->m_stop ? 1 : 0; }

    void fill(AVFrame *f, int idx)
    {
        // background: soft gradient plus a bar that sweeps across, so the picture really changes
        for (int y = 0; y < kH; ++y) {
            uint8_t *row = f->data[0] + y * f->linesize[0];
            for (int x = 0; x < kW; ++x) row[x] = uint8_t(60 + (x * 100) / kW + (y * 30) / kH);
        }
        const int bar = (idx * 8) % kW;
        for (int y = kBlock + 8; y < kH; ++y) {
            uint8_t *row = f->data[0] + y * f->linesize[0];
            for (int x = bar; x < std::min(kW, bar + 30); ++x) row[x] = 220;
        }
        for (int p = 1; p < 3; ++p)
            for (int y = 0; y < kH / 2; ++y) memset(f->data[p] + y * f->linesize[p], 128, size_t(kW / 2));
        // frame number, most significant bit first, as black / white blocks along the top
        for (int b = 0; b < kBits; ++b) {
            const bool one = (idx >> (kBits - 1 - b)) & 1;
            for (int y = 0; y < kBlock; ++y)
                memset(f->data[0] + y * f->linesize[0] + b * kBlock, one ? 235 : 16, size_t(kBlock));
        }
    }

    void run()
    {
        AVFormatContext *oc = nullptr;
        avformat_alloc_output_context2(&oc, nullptr, m_format.c_str(), m_url.c_str());
        if (!oc) return;
        oc->interrupt_callback.callback = &StreamServer::interrupt;
        oc->interrupt_callback.opaque = this;
        AVStream *st = avformat_new_stream(oc, nullptr);
        const AVCodec *enc = avcodec_find_encoder(m_codec);
        if (!enc || !st) { avformat_free_context(oc); return; }
        AVCodecContext *cc = avcodec_alloc_context3(enc);
        cc->width = kW;
        cc->height = kH;
        cc->pix_fmt = AV_PIX_FMT_YUV420P;
        cc->time_base = AVRational{1, kFps};
        cc->framerate = AVRational{kFps, 1};
        cc->gop_size = kFps;
        cc->max_b_frames = 0;
        cc->bit_rate = 3000000;
        if (oc->oformat->flags & AVFMT_GLOBALHEADER) cc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(cc, enc, nullptr) < 0) { avcodec_free_context(&cc); avformat_free_context(oc); return; }
        avcodec_parameters_from_context(st->codecpar, cc);
        st->time_base = cc->time_base;
        oc->flags |= AVFMT_FLAG_FLUSH_PACKETS;

        // HTTP server mode: waits here for one client (the player) to connect.
        AVDictionary *io = nullptr;
        av_dict_set(&io, "listen", "1", 0);
        if (avio_open2(&oc->pb, m_url.c_str(), AVIO_FLAG_WRITE, &oc->interrupt_callback, &io) < 0) {
            av_dict_free(&io);
            avcodec_free_context(&cc);
            avformat_free_context(oc);
            return;
        }
        av_dict_free(&io);
        m_connected = true;
        AVDictionary *mux = nullptr;
        av_dict_set(&mux, "muxdelay", "0", 0);
        av_dict_set(&mux, "muxpreload", "0", 0);
        bool headerOk = avformat_write_header(oc, &mux) >= 0;
        av_dict_free(&mux);

        AVFrame *frame = av_frame_alloc();
        frame->format = AV_PIX_FMT_YUV420P;
        frame->width = kW;
        frame->height = kH;
        av_frame_get_buffer(frame, 32);
        AVPacket *pkt = av_packet_alloc();

        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; headerOk && !m_stop; ++i) {
            std::this_thread::sleep_until(t0 + std::chrono::milliseconds(i * 1000 / kFps));
            if (m_stop) break;
            av_frame_make_writable(frame);
            fill(frame, i);
            frame->pts = i;
            if (i < 8192) m_gen[i] = QDateTime::currentMSecsSinceEpoch();
            bool dead = false;
            if (avcodec_send_frame(cc, frame) >= 0) {
                while (avcodec_receive_packet(cc, pkt) >= 0) {
                    av_packet_rescale_ts(pkt, cc->time_base, st->time_base);
                    pkt->stream_index = st->index;
                    if (av_interleaved_write_frame(oc, pkt) < 0) dead = true;
                    av_packet_unref(pkt);
                }
            }
            m_sent = i + 1;
            if (dead) break;
        }
        if (headerOk) av_write_trailer(oc);
        av_packet_free(&pkt);
        av_frame_free(&frame);
        avio_closep(&oc->pb);
        avcodec_free_context(&cc);
        avformat_free_context(oc);
    }

    std::string m_url, m_format;
    AVCodecID m_codec;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_connected{false};
    std::atomic<int> m_sent{0};
    std::unique_ptr<std::atomic<qint64>[]> m_gen;
};

// ---- reading the frame number back out of a delivered picture ------------------------

static int frameNumber(const QVideoFrame &in)
{
    QVideoFrame f = in;
    if (!f.map(QVideoFrame::ReadOnly)) return -1;
    int n = 0;
    for (int b = 0; b < kBits; ++b) {
        const int x = b * kBlock + kBlock / 2, y = kBlock / 2;
        n = (n << 1) | (f.bits(0)[y * f.bytesPerLine(0) + x] > 128 ? 1 : 0);
    }
    f.unmap();
    return n;
}

struct Run
{
    std::vector<double> latencies;   // ms from generation to arrival at the UI thread
    int firstIdx = -1, lastIdx = -1, frames = 0;
    QSize size;
    QVideoFrameFormat::PixelFormat pix = QVideoFrameFormat::Format_Invalid;
};

static double median(std::vector<double> v)
{
    if (v.empty()) return -1;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// Connects a player to `url`, collects frames until `count` have arrived.
static Run capture(LowLatencyPlayer &pl, StreamServer &srv, const QString &url, const QString &mode, int count, int timeoutMs)
{
    Run r;
    auto conn = QObject::connect(&pl, &LowLatencyPlayer::frameDelivered, [&](const QVideoFrame &f, qint64 ui) {
        const int idx = frameNumber(f);
        if (idx < 0) return;
        if (r.firstIdx < 0) { r.firstIdx = idx; r.size = f.size(); r.pix = f.pixelFormat(); }
        r.lastIdx = idx;
        ++r.frames;
        const qint64 g = srv.generatedAt(idx);
        if (g > 0) r.latencies.push_back(double(ui - g));
    });
    pl.setLatencyMode(mode);
    pl.setSource(url);
    waitFor([&] { return r.frames >= count; }, timeoutMs);
    QObject::disconnect(conn);
    return r;
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    av_log_set_level(AV_LOG_QUIET);
    avformat_network_init();

    check(LowLatencyPlayer::isAvailable(), QStringLiteral("player built with FFmpeg"));

    struct Case { const char *name; const char *url; const char *fmt; AVCodecID codec; int port; };
    const Case cases[] = {
        {"MPEG-TS over HTTP", "http://127.0.0.1:18091/live.ts", "mpegts", AV_CODEC_ID_MPEG4, 18091},
        {"HTTP-FLV", "http://127.0.0.1:18092/live.flv", "flv", AV_CODEC_ID_FLV1, 18092},
    };

    for (const Case &c : cases) {
        for (const char *mode : {"low", "smooth"}) {
            LowLatencyPlayer pl;
            StreamServer srv(c.url, c.fmt, c.codec);
            const Run r = capture(pl, srv, QString::fromLatin1(c.url), QString::fromLatin1(mode), 90, 20000);
            const double med = median(r.latencies);
            check(r.frames >= 90, QStringLiteral("%1 [%2]: %3 frames delivered").arg(QString::fromLatin1(c.name), QString::fromLatin1(mode)).arg(r.frames));
            if (r.frames > 0) {
                check(r.size == QSize(kW, kH) && r.pix == QVideoFrameFormat::Format_YUV420P,
                      QStringLiteral("%1 [%2]: picture is %3x%4 YUV420P").arg(QString::fromLatin1(c.name), QString::fromLatin1(mode)).arg(r.size.width()).arg(r.size.height()));
                qInfo().noquote() << QStringLiteral("  MEASURED %1 [%2]: median arrival delay %3 ms, fps %4, decode %5 ms, backlog %6 ms, dropped %7")
                                         .arg(QString::fromLatin1(c.name), QString::fromLatin1(mode)).arg(med, 0, 'f', 0).arg(pl.measuredFps(), 0, 'f', 1)
                                         .arg(pl.decodeMs(), 0, 'f', 1).arg(pl.backlogMs(), 0, 'f', 0).arg(pl.droppedFrames());
                if (std::string(mode) == "low")
                    check(med >= 0 && med < 400, QStringLiteral("%1 [low]: median arrival delay under 400 ms (%2 ms)").arg(QString::fromLatin1(c.name)).arg(med, 0, 'f', 0));
            }
            pl.stop();
            check(pl.state() == QLatin1String("idle"), QStringLiteral("%1 [%2]: stop() ends it").arg(QString::fromLatin1(c.name), QString::fromLatin1(mode)));
        }
    }

    // ---- the UI thread falls behind: the player must jump to the newest frame ----------------
    {
        LowLatencyPlayer pl;
        StreamServer srv("http://127.0.0.1:18093/live.ts", "mpegts", AV_CODEC_ID_MPEG4);
        Run warm = capture(pl, srv, QStringLiteral("http://127.0.0.1:18093/live.ts"), QStringLiteral("low"), 30, 20000);
        check(warm.frames >= 30, QStringLiteral("stall test: stream is running"));
        int resumeIdx = -1;
        auto conn = QObject::connect(&pl, &LowLatencyPlayer::frameDelivered, [&](const QVideoFrame &f, qint64) { if (resumeIdx < 0) resumeIdx = frameNumber(f); });
        const int dropBefore = pl.droppedFrames();
        QThread::msleep(1200);   // the UI thread is busy for 1.2 s: ~30 frames pile up
        const int newest = srv.framesSent();
        waitFor([&] { return resumeIdx >= 0; }, 3000);
        QObject::disconnect(conn);
        check(resumeIdx >= newest - 4, QStringLiteral("after a 1.2 s stall the first frame shown is the newest, not a queued old one (shown #%1, newest #%2)").arg(resumeIdx).arg(newest));
        waitFor([&] { return pl.droppedFrames() > dropBefore + 10; }, 1000);
        check(pl.droppedFrames() > dropBefore + 10, QStringLiteral("the frames in between were dropped, not queued (%1 dropped)").arg(pl.droppedFrames() - dropBefore));
        pl.stop();
    }

    // ---- connection lost, then the source comes back --------------------------------------------
    {
        LowLatencyPlayer pl;
        auto srv = std::make_unique<StreamServer>("http://127.0.0.1:18094/live.ts", "mpegts", AV_CODEC_ID_MPEG4);
        capture(pl, *srv, QStringLiteral("http://127.0.0.1:18094/live.ts"), QStringLiteral("low"), 30, 20000);
        check(pl.playing(), QStringLiteral("reconnect test: playing"));
        srv.reset();   // the camera goes away
        check(waitFor([&] { return pl.state() == QLatin1String("reconnecting"); }, 8000),
              QStringLiteral("when the stream stops the player says it is reconnecting (%1)").arg(pl.error()));
        check(!pl.error().isEmpty(), QStringLiteral("and it reports why: \"%1\"").arg(pl.error()));
        srv = std::make_unique<StreamServer>("http://127.0.0.1:18094/live.ts", "mpegts", AV_CODEC_ID_MPEG4);
        int after = 0;
        auto conn = QObject::connect(&pl, &LowLatencyPlayer::frameDelivered, [&](const QVideoFrame &, qint64) { ++after; });
        check(waitFor([&] { return after >= 20 && pl.playing(); }, 15000), QStringLiteral("when the stream is back it plays again by itself (%1 frames)").arg(after));
        QObject::disconnect(conn);
        QElapsedTimer t;
        t.start();
        pl.stop();
        check(t.elapsed() < 1500 && pl.state() == QLatin1String("idle"), QStringLiteral("stop() returns promptly (%1 ms)").arg(t.elapsed()));
    }

    // ---- nothing there: no crash, an error, and it keeps trying ----------------------------------
    {
        LowLatencyPlayer pl;
        pl.setSource(QStringLiteral("http://127.0.0.1:1/nothing.flv"));
        check(waitFor([&] { return pl.state() == QLatin1String("reconnecting") && !pl.error().isEmpty(); }, 8000),
              QStringLiteral("an unreachable address gives an error and keeps retrying (%1)").arg(pl.error()));
        pl.setSource(QString());
        check(pl.state() == QLatin1String("idle"), QStringLiteral("clearing the source stops it"));
    }

    qInfo().noquote() << "VIDEO TEST failures:" << g_fail;
    return g_fail;
}
