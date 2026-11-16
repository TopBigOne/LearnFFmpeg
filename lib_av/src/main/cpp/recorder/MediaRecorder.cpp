/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#include "MediaRecorder.h"
#include "RecorderMuxer.h"
#include <sys/system_properties.h>

// 编码线程等待新数据的超时时间：超时只是为了定期检查退出标志，有数据或停止时会被立即唤醒
#define QUEUE_WAIT_TIMEOUT_MS 50

// 写文件头之前最多缓存的包数（音频约 43 包/秒，足够覆盖硬件编码器出第一帧的时间）
#define MAX_PENDING_PACKETS 1000

// 硬件编码视频流的时间基：90kHz 是视频的常用时间基，1 小时的时长也不会让 32 位的字段溢出
static const AVRational kHwVideoTimeBase = {1, 90000};
static const AVRational kMicrosecondTimeBase = {1, 1000000};

// 单次纠偏的上限：超过它说明时间戳本身不可信（例如时基不一致），宁可不纠也不要塞进几分钟的静音
#define MAX_AUDIO_CORRECTION_US 5000000

/**
 * @brief MediaRecorder构造函数
 * @param url 输出文件路径
 * @param param 录制参数（包含视频和音频编码参数）
 */
MediaRecorder::MediaRecorder(const char *url, RecorderParam *param) {
    LOGCATE("MediaRecorder::MediaRecorder url=%s", url);
    strncpy(m_OutUrl, url, sizeof(m_OutUrl) - 1);
    m_RecorderParam = *param;
}

/**
 * @brief MediaRecorder析构函数：确保线程已停止、资源已释放
 */
MediaRecorder::~MediaRecorder() {
    StopRecord();
}

/**
 * @brief 开始录制
 * @return 0表示成功，负数表示失败
 *
 * 初始化FFmpeg格式上下文、添加音视频流、打开编码器、写文件头，然后启动两个编码线程
 */
int MediaRecorder::StartRecord() {
    LOGCATE("MediaRecorder::StartRecord");
    int result = -1;
    do {
        /* 根据文件扩展名推断输出格式 */
        avformat_alloc_output_context2(&m_FormatCtx, NULL, NULL, m_OutUrl);
        if (!m_FormatCtx) {
            LOGCATE("MediaRecorder::StartRecord Could not deduce output format from file extension: using MPEG.");
            avformat_alloc_output_context2(&m_FormatCtx, NULL, "mpeg", m_OutUrl);
        }
        if (!m_FormatCtx) break;

        m_OutputFormat = m_FormatCtx->oformat;

        /* 添加音频和视频流，每一步都检查返回值。视频优先硬件编码，不可用时回退到格式默认的软件编码器 */
        if (m_OutputFormat->video_codec != AV_CODEC_ID_NONE) {
            if (m_RecorderParam.preferHardwareVideo && OpenHardwareVideo()) {
                m_HwVideo = true;
            } else if (AddStream(&m_VideoStream, &m_VideoCodec, m_OutputFormat->video_codec) < 0) {
                break;
            }
            m_EnableVideo = true;
        }
        if (m_OutputFormat->audio_codec != AV_CODEC_ID_NONE) {
            if (AddStream(&m_AudioStream, &m_AudioCodec, m_OutputFormat->audio_codec) < 0) break;
            m_EnableAudio = true;
        }

        /* 打开编码器并分配编码缓冲区 */
        if (m_EnableVideo && !m_HwVideo && OpenVideo(m_VideoCodec, &m_VideoStream) < 0) break;
        if (m_EnableAudio && OpenAudio(m_AudioCodec, &m_AudioStream) < 0) break;

        av_dump_format(m_FormatCtx, 0, m_OutUrl, 1);

        /* 打开输出文件 */
        if (!(m_OutputFormat->flags & AVFMT_NOFILE)) {
            int ret = avio_open(&m_FormatCtx->pb, m_OutUrl, AVIO_FLAG_WRITE);
            if (ret < 0) {
                LOGCATE("MediaRecorder::StartRecord Could not open '%s': %s", m_OutUrl, av_err2str(ret));
                break;
            }
        }

        /* 写入文件头。硬件编码要等编码器给出 SPS/PPS（OnVideoConfig）才能写 */
        if (!m_HwVideo) {
            int ret = WriteRecorderHeader(m_FormatCtx);   // mp4 使用分段格式，见 RecorderMuxer.h
            if (ret < 0) {
                LOGCATE("MediaRecorder::StartRecord Error occurred when opening output file: %s", av_err2str(ret));
                break;
            }
            m_HeaderWritten = true;
        }
        result = 0;
    } while (false);

    if (result < 0) {
        // 任何一步失败都完整释放，不会留下半初始化的对象
        Release();
        return result;
    }

    // 两路时间戳的 0 点。必须在 m_Started 置位前确定：生产线程看到 m_Started 后就会读它
    m_StartTimeUs = GetBootTimeUs();
    m_Started = true;
    // 音频、视频各一个编码线程，彼此不再互相等待；硬件编码时视频由编码器的输出线程负责
    if (m_EnableAudio) m_pAudioThread = new thread(AudioEncodeThread, this);
    if (m_HwVideo) {
        m_HwEncoder->Start(this);
    } else if (m_EnableVideo) {
        m_pVideoThread = new thread(VideoEncodeThread, this);
    }
    LOGCATE("MediaRecorder::StartRecord video encoder: %s",
            m_HwVideo ? ("MediaCodec " + m_HwEncoder->GetName()).c_str() : "x264 (software)");
    return 0;
}

/**
 * @brief 将音频帧添加到编码队列（AudioRecord 线程调用）
 */
int MediaRecorder::OnFrame2Encode(AudioFrame *inputFrame, int64_t captureTimeUs) {
    LOGCATV("MediaRecorder::OnFrame2Encode inputFrame->data=%p, inputFrame->dataSize=%d", inputFrame->data, inputFrame->dataSize);
    if (!m_Started || m_Exit || !m_EnableAudio) return 0;
    int64_t samples = inputFrame->dataSize / m_AudioBytesPerSample;
    if (samples <= 0) return 0;
    if (captureTimeUs == CAPTURE_TIME_UNKNOWN) {
        // 调用方没给采集时间：这块的最后一个采样大约就是"现在"采到的，往前推一个块长。
        // 交付延迟带来的抖动由编码线程的纠偏窗口过滤
        captureTimeUs = GetBootTimeUs() - SamplesToUs(samples, m_RecorderParam.audioSampleRate);
    }
    // 给这块编号（在入队流中的采样序号）：被队列丢弃的块会在编号上留下缺口，编码线程据此原位补静音
    int64_t samplePos = m_AudioPushedSamples.fetch_add(samples);
    AudioChunk *chunk = new AudioChunk(inputFrame->data, static_cast<int>(samples * m_AudioBytesPerSample),
                                       captureTimeUs, samplePos);
    AudioChunk *dropped = m_AudioFrameQueue.Push(chunk);
    if (dropped) {
        LOGCATE("MediaRecorder::OnFrame2Encode audio queue full, drop oldest chunk (will be filled with silence)");
        delete dropped;
    }
    return 0;
}

/**
 * @brief 将视频帧添加到编码队列（GL 线程调用）
 */
int MediaRecorder::OnFrame2Encode(NativeImage *inputFrame, int64_t captureTimeUs) {
    // 硬件编码时画面直接画到编码器 Surface，不走回读
    if (!m_Started || m_Exit || !m_EnableVideo || m_HwVideo) return 0;
    LOGCATV("MediaRecorder::OnFrame2Encode [w,h,format]=[%d,%d,%d]", inputFrame->width, inputFrame->height, inputFrame->format);
    if (captureTimeUs == CAPTURE_TIME_UNKNOWN) captureTimeUs = GetBootTimeUs();
    // 点击开始之前就已采集的画面不属于本次录制（拷贝之前判断，省掉一次整帧拷贝）
    if (captureTimeUs < m_StartTimeUs) return 0;
    VideoFrame *pFrame = ObtainVideoFrame(inputFrame->width, inputFrame->height, inputFrame->format);
    NativeImageUtil::CopyNativeImage(inputFrame, pFrame);
    pFrame->captureTimeUs = captureTimeUs;
    VideoFrame *dropped = m_VideoFrameQueue.Push(pFrame);
    if (dropped) {
        // 编码跟不上：丢弃最旧的帧，避免内存无限增长
        LOGCATE("MediaRecorder::OnFrame2Encode video queue full, drop oldest frame");
        RecycleVideoFrame(dropped);
    }
    return 0;
}

VideoFrame *MediaRecorder::ObtainVideoFrame(int width, int height, int format) {
    {
        std::lock_guard<std::mutex> lock(m_FramePoolMutex);
        while (!m_FramePool.empty()) {
            VideoFrame *frame = m_FramePool.back();
            m_FramePool.pop_back();
            if (frame->width == width && frame->height == height && frame->format == format)
                return frame;
            // 尺寸/格式不同的旧缓冲直接释放
            NativeImageUtil::FreeNativeImage(frame);
            delete frame;
        }
    }
    VideoFrame *frame = new VideoFrame();
    frame->width = width;
    frame->height = height;
    frame->format = format;
    NativeImageUtil::AllocNativeImage(frame);
    return frame;
}

void MediaRecorder::RecycleVideoFrame(VideoFrame *frame) {
    if (frame == nullptr) return;
    {
        std::lock_guard<std::mutex> lock(m_FramePoolMutex);
        if (m_FramePool.size() < MAX_VIDEO_FRAME_POOL) {
            m_FramePool.push_back(frame);
            return;
        }
    }
    NativeImageUtil::FreeNativeImage(frame);
    delete frame;
}

/**
 * @brief 停止录制
 *
 * 通知编码线程不会再有新数据 -> 线程把队列里剩余的数据编码完并刷新编码器后退出 -> 写文件尾 -> 释放资源
 */
int MediaRecorder::StopRecord() {
    LOGCATE("MediaRecorder::StopRecord");
    m_Exit = true;
    m_VideoFrameQueue.WakeUpAll();
    m_AudioFrameQueue.WakeUpAll();

    if (m_pAudioThread != nullptr) {
        m_pAudioThread->join();
        delete m_pAudioThread;
        m_pAudioThread = nullptr;
    }

    if (m_pVideoThread != nullptr) {
        m_pVideoThread->join();
        delete m_pVideoThread;
        m_pVideoThread = nullptr;
    }

    // 硬件编码：通知输入结束并取完剩余码流（GL 侧已解绑，不会再有新帧）
    if (m_HwEncoder) m_HwEncoder->Stop();

    if (m_Started && !m_HeaderWritten) {
        LOGCATE("MediaRecorder::StopRecord no video config received, file header never written");
    }
    if (m_Started && m_HeaderWritten) {
        // 写入文件尾部信息（mp4 的 moov 索引在这里写入，没有它文件无法播放）
        int ret = av_write_trailer(m_FormatCtx);
        LOGCATE("MediaRecorder::StopRecord av_write_trailer %s", av_err2str(ret));
    }
    m_Started = false;

    Release();
    return 0;
}

void MediaRecorder::Release() {
    if (m_HwEncoder) {
        m_HwEncoder->Stop();
        m_HwEncoder.reset();
    }
    {
        std::lock_guard<std::mutex> lock(m_WriteMutex);
        for (PendingPacket &p : m_PendingPackets) av_packet_free(&p.pkt);
        m_PendingPackets.clear();
    }
    // 清理队列中剩余的帧
    while (VideoFrame *pFrame = m_VideoFrameQueue.Pop()) {
        NativeImageUtil::FreeNativeImage(pFrame);
        delete pFrame;
    }
    while (AudioChunk *pAudio = m_AudioFrameQueue.Pop()) {
        delete pAudio;
    }
    {
        std::lock_guard<std::mutex> lock(m_FramePoolMutex);
        for (VideoFrame *frame : m_FramePool) {
            NativeImageUtil::FreeNativeImage(frame);
            delete frame;
        }
        m_FramePool.clear();
    }

    /* 关闭每个编码器 */
    CloseStream(&m_VideoStream);
    CloseStream(&m_AudioStream);

    if (m_AudioFifo) {
        av_audio_fifo_free(m_AudioFifo);
        m_AudioFifo = nullptr;
    }

    if (m_FormatCtx) {
        /* 关闭输出文件 */
        if (m_OutputFormat && !(m_OutputFormat->flags & AVFMT_NOFILE))
            avio_closep(&m_FormatCtx->pb);
        /* 释放上下文和流 */
        avformat_free_context(m_FormatCtx);
        m_FormatCtx = nullptr;
    }
    m_HeaderWritten = false;
}

/**
 * @brief 音频编码线程
 *
 * 每块 PCM 先经过 ProcessAudioChunk 对齐到时间轴，再写入 FIFO：
 * AudioRecord 每次给的采样数和编码器要求的帧长（AAC 为 1024 采样）不一致，凑满一整帧再编码。
 */
void MediaRecorder::AudioEncodeThread(MediaRecorder *recorder) {
    LOGCATE("MediaRecorder::AudioEncodeThread start");
    AVOutputStream *aOs = &recorder->m_AudioStream;

    for (;;) {
        AudioChunk *chunk = recorder->m_AudioFrameQueue.WaitPop(QUEUE_WAIT_TIMEOUT_MS);
        if (chunk) {
            recorder->ProcessAudioChunk(chunk);
            delete chunk;
        } else if (recorder->m_Exit) {
            // 已停止且队列已空
            break;
        }
    }

    // 不足一帧的剩余采样作为最后一帧编码（编码器允许最后一帧较短），然后刷新编码器
    int remain = av_audio_fifo_size(recorder->m_AudioFifo);
    if (remain > 0) recorder->EncodeAudioSamples(remain);
    recorder->EncodeAndWrite(aOs, nullptr);
    LOGCATE("MediaRecorder::AudioEncodeThread end");
}

void MediaRecorder::ProcessAudioChunk(AudioChunk *chunk) {
    int sampleRate = m_AudioStream.m_pCodecCtx->sample_rate;
    int64_t samples = chunk->dataSize / m_AudioBytesPerSample;

    // 1. 队列满时丢弃的块：编号出现缺口，按缺失的采样数原位补静音。
    //    原来直接丢掉，之后所有音频整体提前，每丢一次不同步就加重一分
    int64_t lostSamples = chunk->samplePos - m_AudioNextSamplePos;
    m_AudioNextSamplePos = chunk->samplePos + samples;
    if (lostSamples > 0 && m_AudioAnchored) {
        LOGCATE("MediaRecorder::ProcessAudioChunk %lld samples dropped by queue, fill with silence",
                (long long) lostSamples);
        AppendAudioSilence(lostSamples);
    }

    // 这块第一个采样"应该"落在时间轴上的时刻（待丢弃的采样会从本块开头扣掉，所以要减回去）
    int64_t expectedUs = m_StartTimeUs + SamplesToUs(m_AudioTimelineSamples - m_AudioPendingSkip, sampleRate);
    // 偏差 > 0：音频落后于采集时钟（时间轴上缺了数据）；< 0：音频超前
    int64_t deviationUs = chunk->captureTimeUs - expectedUs;

    if (!m_AudioAnchored) {
        // 2. 开头对齐：起点到第一个采样之间补静音，起点之前采集的采样裁掉，音频从 0 开始
        m_AudioAnchored = true;
        LOGCATE("MediaRecorder::ProcessAudioChunk first audio at %lld us after start", (long long) deviationUs);
        CorrectAudioTimeline(deviationUs);
    } else {
        // 3. 时钟漂移：声卡晶振和系统时钟有 ppm 级差异，按采样数推进的时间轴会慢慢偏离。
        //    每个窗口取最小偏差（交付延迟只会让偏差变大），超过阈值时以系统时钟为准拉回
        if (m_DriftWindowStartUs < 0) {
            m_DriftWindowStartUs = chunk->captureTimeUs;
            m_DriftWindowMinUs = deviationUs;
        } else if (deviationUs < m_DriftWindowMinUs) {
            m_DriftWindowMinUs = deviationUs;
        }
        if (chunk->captureTimeUs - m_DriftWindowStartUs >= AUDIO_DRIFT_WINDOW_US) {
            if (llabs(m_DriftWindowMinUs) > AUDIO_DRIFT_THRESHOLD_US) {
                LOGCATE("MediaRecorder::ProcessAudioChunk audio drift %lld us, correct it",
                        (long long) m_DriftWindowMinUs);
                CorrectAudioTimeline(m_DriftWindowMinUs);
            }
            m_DriftWindowStartUs = -1;
        }
    }

    WriteAudioSamples(chunk->data, samples);
}

void MediaRecorder::CorrectAudioTimeline(int64_t deviationUs) {
    if (llabs(deviationUs) > MAX_AUDIO_CORRECTION_US) {
        LOGCATE("MediaRecorder::CorrectAudioTimeline deviation %lld us is implausible, ignore",
                (long long) deviationUs);
        return;
    }
    int64_t samples = UsToSamples(llabs(deviationUs), m_AudioStream.m_pCodecCtx->sample_rate);
    if (deviationUs > 0) {
        // 先抵消还没执行的丢弃，剩下的才补静音
        int64_t cancel = std::min(samples, m_AudioPendingSkip);
        m_AudioPendingSkip -= cancel;
        AppendAudioSilence(samples - cancel);
    } else {
        // 从接下来的数据开头丢掉这些采样（可以跨多个块）
        m_AudioPendingSkip += samples;
    }
}

void MediaRecorder::AppendAudioSilence(int64_t samples) {
    int blockSamples = static_cast<int>(m_SilenceBuffer.size() / m_AudioBytesPerSample);
    while (samples > 0) {
        int n = static_cast<int>(std::min<int64_t>(samples, blockSamples));
        FeedAudioFifo(m_SilenceBuffer.data(), n);
        samples -= n;
    }
}

void MediaRecorder::WriteAudioSamples(const uint8_t *data, int64_t samples) {
    int64_t skip = std::min(samples, m_AudioPendingSkip);
    m_AudioPendingSkip -= skip;
    if (samples - skip > 0) {
        FeedAudioFifo(data + skip * m_AudioBytesPerSample, static_cast<int>(samples - skip));
    }
}

void MediaRecorder::FeedAudioFifo(const uint8_t *data, int samples) {
    void *planes[1] = {const_cast<uint8_t *>(data)};
    int written = av_audio_fifo_write(m_AudioFifo, planes, samples);
    if (written < samples) {
        LOGCATE("MediaRecorder::FeedAudioFifo av_audio_fifo_write fail, written=%d", written);
    }
    if (written > 0) m_AudioTimelineSamples += written;

    // m_pTmpFrame->nb_samples 是分配时的容量，即编码器要求的帧长
    int frameSize = m_AudioStream.m_pTmpFrame->nb_samples;
    while (av_audio_fifo_size(m_AudioFifo) >= frameSize) {
        EncodeAudioSamples(frameSize);
    }
}

/**
 * @brief 视频编码线程
 */
void MediaRecorder::VideoEncodeThread(MediaRecorder *recorder) {
    LOGCATE("MediaRecorder::VideoEncodeThread start");
    for (;;) {
        VideoFrame *videoFrame = recorder->m_VideoFrameQueue.WaitPop(QUEUE_WAIT_TIMEOUT_MS);
        if (videoFrame) {
            // 先编码已经取出的帧，原来在停止时会把最后取出的这一帧丢掉
            recorder->EncodeVideoFrame(videoFrame);
            recorder->RecycleVideoFrame(videoFrame);
        } else if (recorder->m_Exit) {
            break;
        }
    }
    // 刷新编码器，取出缓存的帧
    recorder->EncodeAndWrite(&recorder->m_VideoStream, nullptr);
    LOGCATE("MediaRecorder::VideoEncodeThread end");
}

/**
 * @brief 将编码数据包写入到媒体文件（两路编码线程共用，加锁串行）
 *
 * 文件头还没写时（硬件编码在等 SPS/PPS）先缓存，写头后由 OnVideoConfig 按顺序写出
 */
int MediaRecorder::WritePacket(AVRational *time_base, AVStream *st, AVPacket *pkt) {
    std::lock_guard<std::mutex> lock(m_WriteMutex);
    if (!m_HeaderWritten) {
        if (m_HeaderFailed || m_PendingPackets.size() >= MAX_PENDING_PACKETS) {
            LOGCATV("MediaRecorder::WritePacket header not written, drop packet");
            return 0;
        }
        AVPacket *clone = av_packet_clone(pkt);
        if (clone == nullptr) return AVERROR(ENOMEM);
        m_PendingPackets.push_back({clone, *time_base, st});
        return 0;
    }
    return WritePacketLocked(*time_base, st, pkt);
}

int MediaRecorder::WritePacketLocked(AVRational time_base, AVStream *st, AVPacket *pkt) {
    /* 将时间戳从编码器时间基换算到流时间基（写头时 muxer 可能改了流的时间基，所以在写入时才换算） */
    av_packet_rescale_ts(pkt, time_base, st->time_base);
    pkt->stream_index = st->index;
    LOGCATV("MediaRecorder::WritePacket stream=%d pts=%s pts_time=%s", st->index,
            av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, &st->time_base));
    return av_interleaved_write_frame(m_FormatCtx, pkt);
}

bool MediaRecorder::OpenHardwareVideo() {
    // 调试开关：adb shell setprop debug.learnffmpeg.sw_encode 1 强制走软件编码，用于验证回退路径
    char prop[PROP_VALUE_MAX] = {0};
    if (__system_property_get("debug.learnffmpeg.sw_encode", prop) > 0 && strcmp(prop, "1") == 0) {
        LOGCATE("MediaRecorder::OpenHardwareVideo disabled by debug.learnffmpeg.sw_encode");
        return false;
    }

    std::unique_ptr<HwVideoEncoder> encoder(new HwVideoEncoder());
    if (!encoder->Open(m_RecorderParam.frameWidth, m_RecorderParam.frameHeight,
                       static_cast<int>(m_RecorderParam.videoBitRate), m_RecorderParam.fps)) {
        return false;
    }

    AVStream *st = avformat_new_stream(m_FormatCtx, nullptr);
    if (st == nullptr) {
        LOGCATE("MediaRecorder::OpenHardwareVideo Could not allocate stream");
        return false;
    }
    st->id = m_FormatCtx->nb_streams - 1;
    // 编码器在 MediaCodec 里，这里只描述码流参数；SPS/PPS（extradata）等编码器输出后再填
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_H264;
    st->codecpar->width = m_RecorderParam.frameWidth;
    st->codecpar->height = m_RecorderParam.frameHeight;
    st->codecpar->format = AV_PIX_FMT_YUV420P;
    st->codecpar->bit_rate = m_RecorderParam.videoBitRate;
    st->time_base = kHwVideoTimeBase;
    st->avg_frame_rate = AVRational{m_RecorderParam.fps, 1};
    m_VideoStream.m_pStream = st;
    m_HwEncoder = std::move(encoder);
    return true;
}

ANativeWindow *MediaRecorder::GetVideoInputWindow() const {
    return m_HwEncoder ? m_HwEncoder->GetInputWindow() : nullptr;
}

int64_t MediaRecorder::NextVideoPtsUs(int64_t captureTimeUs) {
    if (!m_Started || m_Exit || !m_HwVideo) return -1;
    if (captureTimeUs == CAPTURE_TIME_UNKNOWN) captureTimeUs = GetBootTimeUs();
    // 与软件编码相同的规则：起点前采集的帧不录，第一帧固定为 0，之后严格递增
    if (captureTimeUs < m_StartTimeUs) return -1;
    int64_t pts = captureTimeUs - m_StartTimeUs;
    int64_t last = m_HwLastPtsUs.load();
    if (last < 0) pts = 0;
    else if (pts <= last) pts = last + 1000;
    m_HwLastPtsUs = pts;
    return pts;
}

void MediaRecorder::OnVideoConfig(const uint8_t *data, size_t size) {
    std::lock_guard<std::mutex> lock(m_WriteMutex);
    if (m_HeaderWritten || m_HeaderFailed) return;
    AVCodecParameters *par = m_VideoStream.m_pStream->codecpar;
    // SPS/PPS 是 Annex B 格式（00 00 00 01 起始码），mp4 muxer 会转成 avcC，并把每帧码流转成长度前缀格式
    par->extradata = static_cast<uint8_t *>(av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE));
    if (par->extradata == nullptr) {
        m_HeaderFailed = true;
        return;
    }
    memcpy(par->extradata, data, size);
    par->extradata_size = static_cast<int>(size);

    int ret = WriteRecorderHeader(m_FormatCtx);
    if (ret < 0) {
        LOGCATE("MediaRecorder::OnVideoConfig write header fail: %s", av_err2str(ret));
        m_HeaderFailed = true;
    } else {
        m_HeaderWritten = true;
        LOGCATE("MediaRecorder::OnVideoConfig header written, flush %zu pending packets", m_PendingPackets.size());
    }
    for (PendingPacket &p : m_PendingPackets) {
        if (m_HeaderWritten) WritePacketLocked(p.timeBase, p.stream, p.pkt);
        av_packet_free(&p.pkt);
    }
    m_PendingPackets.clear();
}

void MediaRecorder::OnVideoPacket(const uint8_t *data, size_t size, int64_t ptsUs, bool keyFrame) {
    AVPacket *pkt = av_packet_alloc();
    if (pkt == nullptr || av_new_packet(pkt, static_cast<int>(size)) < 0) {
        av_packet_free(&pkt);
        return;
    }
    memcpy(pkt->data, data, size);
    // 已关闭 B 帧，dts == pts；仍保证 dts 严格递增，防止个别编码器输出的时间戳重复
    int64_t dts = ptsUs;
    if (dts <= m_HwLastDtsUs) dts = m_HwLastDtsUs + 1;
    m_HwLastDtsUs = dts;
    pkt->pts = ptsUs < dts ? dts : ptsUs;
    pkt->dts = dts;
    if (keyFrame) pkt->flags |= AV_PKT_FLAG_KEY;
    AVRational tb = kMicrosecondTimeBase;
    int ret = WritePacket(&tb, m_VideoStream.m_pStream, pkt);
    if (ret < 0) {
        LOGCATE("MediaRecorder::OnVideoPacket write fail: %s", av_err2str(ret));
    }
    av_packet_free(&pkt);
}

int MediaRecorder::EncodeAndWrite(AVOutputStream *ost, AVFrame *frame) {
    AVCodecContext *c = ost->m_pCodecCtx;
    int ret = avcodec_send_frame(c, frame);
    if (ret < 0 && ret != AVERROR_EOF) {
        LOGCATE("MediaRecorder::EncodeAndWrite avcodec_send_frame fail. ret=%s", av_err2str(ret));
        return ret;
    }

    AVPacket *pkt = av_packet_alloc();
    for (;;) {
        ret = avcodec_receive_packet(c, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            ret = 0;
            break;
        } else if (ret < 0) {
            LOGCATE("MediaRecorder::EncodeAndWrite avcodec_receive_packet fail. ret=%s", av_err2str(ret));
            break;
        }
        int writeRet = WritePacket(&c->time_base, ost->m_pStream, pkt);
        if (writeRet < 0) {
            LOGCATE("MediaRecorder::EncodeAndWrite Error while writing packet: %s", av_err2str(writeRet));
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return ret;
}

int MediaRecorder::AddStream(AVOutputStream *ost, AVCodec **codec, AVCodecID codec_id) {
    LOGCATE("MediaRecorder::AddStream");
    AVFormatContext *oc = m_FormatCtx;

    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        LOGCATE("MediaRecorder::AddStream Could not find encoder for '%s'", avcodec_get_name(codec_id));
        return -1;
    }

    ost->m_pStream = avformat_new_stream(oc, NULL);
    if (!ost->m_pStream) {
        LOGCATE("MediaRecorder::AddStream Could not allocate stream");
        return -1;
    }
    ost->m_pStream->id = oc->nb_streams - 1;
    AVCodecContext *c = avcodec_alloc_context3(*codec);
    if (!c) {
        LOGCATE("MediaRecorder::AddStream Could not alloc an encoding context");
        return -1;
    }
    ost->m_pCodecCtx = c;

    switch ((*codec)->type) {
        case AVMEDIA_TYPE_AUDIO:
            LOGCATE("MediaRecorder::AddStream AVMEDIA_TYPE_AUDIO AVCodecID=%d", codec_id);
            c->sample_fmt  = (*codec)->sample_fmts ? (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
            c->bit_rate    = 96000;
            c->sample_rate = m_RecorderParam.audioSampleRate;
            c->channel_layout = m_RecorderParam.channelLayout;
            c->channels    = av_get_channel_layout_nb_channels(c->channel_layout);
            c->time_base   = (AVRational){ 1, c->sample_rate };
            ost->m_pStream->time_base = c->time_base;
            break;

        case AVMEDIA_TYPE_VIDEO:
            LOGCATE("MediaRecorder::AddStream AVMEDIA_TYPE_VIDEO AVCodecID=%d", codec_id);
            c->codec_id = codec_id;
            c->bit_rate = m_RecorderParam.videoBitRate;
            /* Resolution must be a multiple of two. */
            c->width    = m_RecorderParam.frameWidth;
            c->height   = m_RecorderParam.frameHeight;
            // 时间基取毫秒：pts 直接使用采集时间，相机帧率波动（如暗光降帧）时画面时长仍然正确
            c->time_base = (AVRational){ 1, 1000 };
            c->framerate = (AVRational){ m_RecorderParam.fps, 1 };
            ost->m_pStream->time_base = c->time_base;
            ost->m_pStream->avg_frame_rate = c->framerate;
            c->gop_size  = m_RecorderParam.fps;   // 约 1 秒一个关键帧
            c->pix_fmt   = AV_PIX_FMT_YUV420P;
            if (c->codec_id == AV_CODEC_ID_H264) {
                // x264 默认 preset 为 medium，手机上无法实时编码；录制优先保证速度
                av_opt_set(c->priv_data, "preset", "ultrafast", 0);
                av_opt_set(c->priv_data, "tune", "zerolatency", 0);
            }
            break;

        default:
            break;
    }

    /* mp4 等格式要求编码器把 SPS/PPS 等头信息放在 extradata（全局头）里 */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    return 0;
}

AVFrame *MediaRecorder::AllocAudioFrame(AVSampleFormat sample_fmt, uint64_t channel_layout,
                                        int sample_rate, int nb_samples) {
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        LOGCATE("MediaRecorder::AllocAudioFrame Error allocating an audio frame");
        return nullptr;
    }

    frame->format = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples && av_frame_get_buffer(frame, 0) < 0) {
        LOGCATE("MediaRecorder::AllocAudioFrame Error allocating an audio buffer");
        av_frame_free(&frame);   // 失败时释放已分配的 frame
        return nullptr;
    }

    return frame;
}

int MediaRecorder::OpenAudio(AVCodec *codec, AVOutputStream *ost) {
    LOGCATE("MediaRecorder::OpenAudio");
    AVCodecContext *c = ost->m_pCodecCtx;
    int ret = avcodec_open2(c, codec, nullptr);
    if (ret < 0) {
        LOGCATE("MediaRecorder::OpenAudio Could not open audio codec: %s", av_err2str(ret));
        return -1;
    }

    // 编码器要求的每帧采样数（AAC 为 1024）；支持可变帧长的编码器取 1024
    int nb_samples = (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE) || c->frame_size <= 0
            ? 1024 : c->frame_size;

    ost->m_pFrame = AllocAudioFrame(c->sample_fmt, c->channel_layout, c->sample_rate, nb_samples);
    // 存放从 FIFO 读出的原始 S16 交错数据
    ost->m_pTmpFrame = AllocAudioFrame(AV_SAMPLE_FMT_S16, c->channel_layout, c->sample_rate, nb_samples);
    if (!ost->m_pFrame || !ost->m_pTmpFrame) return -1;

    /* 编码器打开之后再拷贝参数：此时 extradata（AAC 的 AudioSpecificConfig）才已生成 */
    ret = avcodec_parameters_from_context(ost->m_pStream->codecpar, c);
    if (ret < 0) {
        LOGCATE("MediaRecorder::OpenAudio Could not copy the stream parameters");
        return -1;
    }

    /* 采样格式转换：输入是 AudioRecord 采集的 S16 交错，输出为编码器格式（如 FLTP） */
    ost->m_pSwrCtx = swr_alloc_set_opts(nullptr,
                                        c->channel_layout, c->sample_fmt, c->sample_rate,
                                        c->channel_layout, AV_SAMPLE_FMT_S16, c->sample_rate,
                                        0, nullptr);
    if (!ost->m_pSwrCtx || swr_init(ost->m_pSwrCtx) < 0) {
        LOGCATE("MediaRecorder::OpenAudio Failed to initialize the resampling context");
        return -1;
    }

    m_AudioFifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_S16, c->channels, nb_samples * 4);
    if (!m_AudioFifo) {
        LOGCATE("MediaRecorder::OpenAudio av_audio_fifo_alloc fail");
        return -1;
    }
    m_AudioBytesPerSample = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * c->channels;
    // 补静音用：S16 的静音就是全 0，一帧长度，分块写入
    m_SilenceBuffer.assign(static_cast<size_t>(nb_samples) * m_AudioBytesPerSample, 0);
    return 0;
}

int MediaRecorder::EncodeAudioSamples(int nbSamples) {
    AVOutputStream *ost = &m_AudioStream;
    AVCodecContext *c = ost->m_pCodecCtx;

    // 从 FIFO 读出 nbSamples 个 S16 采样
    if (av_frame_make_writable(ost->m_pTmpFrame) < 0) return -1;
    int readSamples = av_audio_fifo_read(m_AudioFifo, (void **) ost->m_pTmpFrame->data, nbSamples);
    if (readSamples <= 0) return -1;

    // m_pTmpFrame->nb_samples 始终是分配时的容量；m_pFrame->nb_samples 上次可能被改小，先恢复
    ost->m_pFrame->nb_samples = ost->m_pTmpFrame->nb_samples;
    /* 编码器可能仍持有上一帧的引用，写入前确保可写 */
    if (av_frame_make_writable(ost->m_pFrame) < 0) {
        LOGCATE("MediaRecorder::EncodeAudioSamples Error while av_frame_make_writable");
        return -1;
    }

    // 采样率不变，输出采样数 = 输入采样数
    int outSamples = swr_convert(ost->m_pSwrCtx, ost->m_pFrame->data, ost->m_pFrame->nb_samples,
                                 (const uint8_t **) ost->m_pTmpFrame->data, readSamples);
    if (outSamples <= 0) {
        LOGCATE("MediaRecorder::EncodeAudioSamples Error while converting");
        return -1;
    }

    // 帧长与 pts 都按实际采样数计算：pts 每帧增加 nb_samples（原来按 1 递增，时长全错）
    ost->m_pFrame->nb_samples = outSamples;
    ost->m_pFrame->pts = av_rescale_q(ost->m_SamplesCount, (AVRational){1, c->sample_rate}, c->time_base);
    ost->m_SamplesCount += outSamples;

    return EncodeAndWrite(ost, ost->m_pFrame);
}

AVFrame *MediaRecorder::AllocVideoFrame(AVPixelFormat pix_fmt, int width, int height) {
    AVFrame *picture = av_frame_alloc();
    if (!picture)
        return nullptr;

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data（align=0 由 FFmpeg 选择适合 SIMD 的对齐） */
    if (av_frame_get_buffer(picture, 0) < 0) {
        LOGCATE("MediaRecorder::AllocVideoFrame Could not allocate frame data.");
        av_frame_free(&picture);
        return nullptr;
    }

    return picture;
}

int MediaRecorder::OpenVideo(AVCodec *codec, AVOutputStream *ost) {
    LOGCATE("MediaRecorder::OpenVideo");
    AVCodecContext *c = ost->m_pCodecCtx;

    /* open the codec */
    int ret = avcodec_open2(c, codec, nullptr);
    if (ret < 0) {
        LOGCATE("MediaRecorder::OpenVideo Could not open video codec: %s", av_err2str(ret));
        return -1;
    }

    /* allocate and init a re-usable frame */
    ost->m_pFrame = AllocVideoFrame(c->pix_fmt, c->width, c->height);
    if (!ost->m_pFrame) {
        LOGCATE("MediaRecorder::OpenVideo Could not allocate video frame");
        return -1;
    }

    /* 编码器打开之后再拷贝参数，此时 extradata（SPS/PPS）才已生成 */
    ret = avcodec_parameters_from_context(ost->m_pStream->codecpar, c);
    if (ret < 0) {
        LOGCATE("MediaRecorder::OpenVideo Could not copy the stream parameters");
        return -1;
    }
    return 0;
}

int MediaRecorder::EncodeVideoFrame(VideoFrame *videoFrame) {
    AVOutputStream *ost = &m_VideoStream;
    AVCodecContext *c = ost->m_pCodecCtx;

    /* 编码器可能仍持有上一帧的引用，写入前确保可写 */
    if (av_frame_make_writable(ost->m_pFrame) < 0) return -1;

    // 转换到编码器的 YUV420P（尺寸不同时顺带缩放）。用 libyuv 代替 sws_scale：
    // 实测 480x640 RGBA→I420，sws_scale 约 11ms/帧，是录制线程最大的 CPU 开销
    if (m_VideoConverter.Convert(*videoFrame, ost->m_pFrame) != 0) {
        LOGCATE("MediaRecorder::EncodeVideoFrame convert fail, format=%d", videoFrame->format);
        return -1;
    }

    // pts = 采集时刻相对录制起点的毫秒数（time_base 1/1000）。
    // 第一帧固定为 0：起点到第一帧之间沿用第一帧画面，文件不会以一段空白时间轴开头；
    // 之后保证严格递增（同一毫秒内来两帧时 +1）
    int64_t pts = (videoFrame->captureTimeUs - m_StartTimeUs) / 1000;
    if (ost->m_LastPts < 0) pts = 0;
    else if (pts <= ost->m_LastPts) pts = ost->m_LastPts + 1;
    ost->m_LastPts = pts;
    ost->m_pFrame->pts = pts;

    return EncodeAndWrite(ost, ost->m_pFrame);
}

void MediaRecorder::CloseStream(AVOutputStream *ost) {
    avcodec_free_context(&ost->m_pCodecCtx);
    av_frame_free(&ost->m_pFrame);
    // AVFrame 必须用 av_frame_free 释放（原来用 av_free 会泄漏其中的缓冲区）
    av_frame_free(&ost->m_pTmpFrame);
    swr_free(&ost->m_pSwrCtx);
}
