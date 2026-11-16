#include "Demuxer.h"
#include <cstring>
#include <LogUtil.h>

extern "C" {
#include <libavutil/time.h>
};

// 两个流的包队列合计超过 15MB 就不再读（ffplay MAX_QUEUE_SIZE），防止高码率文件占满内存
static const int MAX_QUEUE_SIZE = 15 * 1024 * 1024;

// 打开 + 探测流信息允许的最长时间：网络源握手、探测可能较慢
static const int64_t OPEN_TIMEOUT_US = 20 * 1000 * 1000LL;
// 单次读包 / seek 允许的最长时间，超过视为网络卡死
static const int64_t IO_TIMEOUT_US = 15 * 1000 * 1000LL;

// 连续这么多次非 EOF 的读错误（且 AVIOContext 没有报告错误）就放弃，避免坏文件让读线程空转
static const int MAX_CONSECUTIVE_READ_ERRORS = 100;

Demuxer::Demuxer(const std::atomic<bool> *externalAbort)
        : m_ExternalAbort(externalAbort) {
}

Demuxer::~Demuxer() {
    Abort();
    Join();
}

int Demuxer::InterruptCallback(void *opaque) {
    Demuxer *demuxer = static_cast<Demuxer *>(opaque);
    if (demuxer->m_Abort.load() || (demuxer->m_ExternalAbort && demuxer->m_ExternalAbort->load()))
        return 1;
    int64_t deadline = demuxer->m_DeadlineUs.load();
    if (deadline > 0 && av_gettime_relative() > deadline) {
        demuxer->m_TimedOut = true;
        return 1;
    }
    return 0;
}

void Demuxer::ArmDeadline(int64_t timeoutUs) {
    if (timeoutUs > 0) {
        // 只在开始新操作时清除超时标记，操作结束（传 0）后调用方还要读取它
        m_TimedOut = false;
        m_DeadlineUs = av_gettime_relative() + timeoutUs;
    } else {
        m_DeadlineUs = 0;
    }
}

int Demuxer::Open(const std::string &url) {
    AVFormatContext *ctx = avformat_alloc_context();
    if (ctx == nullptr) return AVERROR(ENOMEM);
    ctx->interrupt_callback.callback = InterruptCallback;
    ctx->interrupt_callback.opaque = this;

    // 解封装 / 协议层参数必须传给 avformat_open_input（传给 avcodec_open2 不生效）；
    // 对本地文件无效的选项会原样留在字典里，不影响打开
    AVDictionary *options = nullptr;
    av_dict_set(&options, "buffer_size", "1024000", 0);
    av_dict_set(&options, "stimeout", "20000000", 0);
    av_dict_set(&options, "max_delay", "30000000", 0);
    av_dict_set(&options, "rtsp_transport", "tcp", 0);

    ArmDeadline(OPEN_TIMEOUT_US);
    // 失败时 FFmpeg 会释放 ctx 并置空
    int ret = avformat_open_input(&ctx, url.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (ret < 0) {
        ArmDeadline(0);
        LOGCATE("Demuxer::Open avformat_open_input fail, ret=%d, timeout=%d", ret, m_TimedOut.load());
        return m_TimedOut ? AVERROR(ETIMEDOUT) : ret;
    }
    m_FormatCtx.reset(ctx);
    LOGCATI("Demuxer::Open input opened once, format=%s", ctx->iformat->name);

    ret = avformat_find_stream_info(ctx, nullptr);
    ArmDeadline(0);
    if (ret < 0) {
        LOGCATE("Demuxer::Open avformat_find_stream_info fail, ret=%d", ret);
        return m_TimedOut ? AVERROR(ETIMEDOUT) : ret;
    }
    // 有些格式（如 TS）探测时会把读指针移到中间，ffplay 同样会重置 eof 标志
    if (ctx->pb) ctx->pb->eof_reached = 0;

    m_VideoIndex = av_find_best_stream(ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    // 优先选择与视频流相关联的音轨（同一节目）
    m_AudioIndex = av_find_best_stream(ctx, AVMEDIA_TYPE_AUDIO, -1, m_VideoIndex, nullptr, 0);
    if (m_VideoIndex < 0) m_VideoIndex = -1;
    if (m_AudioIndex < 0) m_AudioIndex = -1;

    // mp3 等音频文件的封面图也表现为视频流，但只有一个包、没有时间轴，这里不把它当作视频播放
    if (m_VideoIndex >= 0 && (ctx->streams[m_VideoIndex]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
        LOGCATI("Demuxer::Open ignore attached picture stream %d", m_VideoIndex);
        m_VideoIndex = -1;
    }

    if (m_VideoIndex < 0 && m_AudioIndex < 0) {
        LOGCATE("Demuxer::Open no audio or video stream");
        return AVERROR_STREAM_NOT_FOUND;
    }

    // 没选中的流让 demuxer 直接丢弃，省掉无用的读取与拷贝
    for (unsigned int i = 0; i < ctx->nb_streams; ++i) {
        int index = static_cast<int>(i);
        ctx->streams[i]->discard = (index == m_AudioIndex || index == m_VideoIndex)
                                   ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }

    LOGCATI("Demuxer::Open streams: audio=%d, video=%d, duration=%lldms",
            m_AudioIndex, m_VideoIndex, (long long) DurationMs());
    return 0;
}

void Demuxer::DisableStream(int streamIndex) {
    if (!m_FormatCtx || streamIndex < 0) return;
    m_FormatCtx->streams[streamIndex]->discard = AVDISCARD_ALL;
    if (streamIndex == m_AudioIndex) m_AudioIndex = -1;
    if (streamIndex == m_VideoIndex) m_VideoIndex = -1;
}

int64_t Demuxer::DurationMs() const {
    if (!m_FormatCtx || m_FormatCtx->duration == AV_NOPTS_VALUE || m_FormatCtx->duration < 0) return 0;
    return av_rescale(m_FormatCtx->duration, 1000, AV_TIME_BASE);
}

double Demuxer::StartTimeSec() const {
    if (!m_FormatCtx || m_FormatCtx->start_time == AV_NOPTS_VALUE) return 0;
    return m_FormatCtx->start_time / static_cast<double>(AV_TIME_BASE);
}

double Demuxer::MaxFrameDuration() const {
    // 允许时间戳不连续的格式（TS 等）上，相邻帧间隔超过 10 秒就当作跳变处理
    if (m_FormatCtx && (m_FormatCtx->iformat->flags & AVFMT_TS_DISCONT)) return 10.0;
    return 3600.0;
}

void Demuxer::Start(PacketQueue *audioQueue, PacketQueue *videoQueue, Listener *listener) {
    m_AudioQueue = m_AudioIndex >= 0 ? audioQueue : nullptr;
    m_VideoQueue = m_VideoIndex >= 0 ? videoQueue : nullptr;
    m_Listener = listener;
    m_Thread = std::thread(&Demuxer::ReadLoop, this);
}

void Demuxer::RequestSeek(int64_t targetUs) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    // 直接覆盖：读包线程还没来得及处理的旧请求作废，快速连续 seek 只执行最后一次
    m_SeekTargetUs = targetUs;
    m_SeekRequested = true;
    m_Cond.notify_all();
}

void Demuxer::SetPaused(bool paused) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Paused = paused;
    m_Cond.notify_all();
}

void Demuxer::WakeUp() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_WakeUp = true;
    m_Cond.notify_all();
}

void Demuxer::Abort() {
    m_Abort = true;
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Cond.notify_all();
}

void Demuxer::Join() {
    if (m_Thread.joinable()) m_Thread.join();
}

void Demuxer::WaitFor(int ms) {
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_Cond.wait_for(lock, std::chrono::milliseconds(ms),
                    [this] { return m_Abort.load() || m_SeekRequested || m_WakeUp; });
    m_WakeUp = false;
}

bool Demuxer::BufferIsFull() {
    int totalSize = (m_AudioQueue ? m_AudioQueue->Size() : 0) + (m_VideoQueue ? m_VideoQueue->Size() : 0);
    if (totalSize > MAX_QUEUE_SIZE) return true;
    AVFormatContext *ctx = m_FormatCtx.get();
    bool audioEnough = m_AudioQueue == nullptr || m_AudioQueue->HasEnoughPackets(ctx->streams[m_AudioIndex]);
    bool videoEnough = m_VideoQueue == nullptr || m_VideoQueue->HasEnoughPackets(ctx->streams[m_VideoIndex]);
    return audioEnough && videoEnough;
}

void Demuxer::DoSeek(int64_t targetUs) {
    AVFormatContext *ctx = m_FormatCtx.get();
    double targetSec = targetUs / static_cast<double>(AV_TIME_BASE);

    // 不限定范围：允许落到目标附近的关键帧上（ffplay 拖动进度条时的做法）
    ArmDeadline(IO_TIMEOUT_US);
    int ret = avformat_seek_file(ctx, -1, INT64_MIN, targetUs, INT64_MAX, 0);
    ArmDeadline(0);
    if (ret < 0) {
        LOGCATE("Demuxer::DoSeek seek to %.3fs fail, ret=%d", targetSec, ret);
        m_Listener->OnSeekCompleted(targetUs, false);
        return;
    }

    // 清空旧包并换代：解码器、帧队列、时钟都通过 serial 识别并丢弃旧数据
    if (m_AudioQueue) m_AudioQueue->Flush();
    if (m_VideoQueue) m_VideoQueue->Flush();
    m_Eof = false;
    LOGCATI("Demuxer::DoSeek seek to %.3fs done", targetSec);
    m_Listener->OnSeekCompleted(targetUs, true);
}

void Demuxer::ReadLoop() {
    LOGCATI("Demuxer::ReadLoop start");
    AVFormatContext *ctx = m_FormatCtx.get();
    PacketPtr pkt = MakePacket();
    if (!pkt) {
        m_Listener->OnDemuxerError(AVERROR(ENOMEM));
        return;
    }

    // RTSP 暂停后服务器不再发数据，继续读只会超时；ffplay 对它（和 mmsh）在暂停期间不读
    const bool pauseStopsReading = strcmp(ctx->iformat->name, "rtsp") == 0
                                   || (ctx->url && strncmp(ctx->url, "mmsh:", 5) == 0);
    bool lastPaused = false;
    int consecutiveErrors = 0;

    while (!m_Abort) {
        bool paused;
        bool seekRequested;
        int64_t seekTarget = 0;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            paused = m_Paused;
            seekRequested = m_SeekRequested;
            if (seekRequested) {
                seekTarget = m_SeekTargetUs;
                m_SeekRequested = false;
            }
        }

        if (paused != lastPaused) {
            lastPaused = paused;
            if (paused) av_read_pause(ctx);
            else av_read_play(ctx);
        }

        if (seekRequested) {
            DoSeek(seekTarget);
            continue;
        }

        if (paused && pauseStopsReading) {
            WaitFor(10);
            continue;
        }

        if (BufferIsFull()) {
            WaitFor(10);
            continue;
        }

        ArmDeadline(IO_TIMEOUT_US);
        int ret = av_read_frame(ctx, pkt.get());
        ArmDeadline(0);

        if (ret < 0) {
            if (m_Abort || (m_ExternalAbort && m_ExternalAbort->load())) break;

            if (m_TimedOut) {
                LOGCATE("Demuxer::ReadLoop read timeout");
                m_Listener->OnDemuxerError(AVERROR(ETIMEDOUT));
                break;
            }

            if ((ret == AVERROR_EOF || avio_feof(ctx->pb)) && !m_Eof) {
                // 每个流各放一个 EOS，解码器据此进入 draining，把缓存的帧全部吐出来
                if (m_AudioQueue) m_AudioQueue->PutEos();
                if (m_VideoQueue) m_VideoQueue->PutEos();
                m_Eof = true;
                LOGCATI("Demuxer::ReadLoop end of input");
            }

            if (ctx->pb && ctx->pb->error) {
                LOGCATE("Demuxer::ReadLoop io error %d", ctx->pb->error);
                m_Listener->OnDemuxerError(ctx->pb->error);
                break;
            }

            if (!m_Eof && ++consecutiveErrors >= MAX_CONSECUTIVE_READ_ERRORS) {
                LOGCATE("Demuxer::ReadLoop too many read errors, last=%d", ret);
                m_Listener->OnDemuxerError(ret);
                break;
            }

            // 文件读完后仍保持线程运行，等待 seek；读到末尾时没有必要频繁重试
            WaitFor(m_Eof ? 100 : 10);
            continue;
        }

        consecutiveErrors = 0;
        m_Eof = false;
        if (pkt->stream_index == m_AudioIndex && m_AudioQueue) {
            m_AudioQueue->Put(pkt.get());
        } else if (pkt->stream_index == m_VideoIndex && m_VideoQueue) {
            m_VideoQueue->Put(pkt.get());
        } else {
            av_packet_unref(pkt.get());
        }
    }
    LOGCATI("Demuxer::ReadLoop exit");
}
