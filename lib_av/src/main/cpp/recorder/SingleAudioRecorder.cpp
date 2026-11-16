/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#include <unistd.h>
#include "SingleAudioRecorder.h"

/**
 * @brief 构造函数
 * @param outUrl 输出文件路径
 * @param sampleRate 输入采样率
 * @param channelLayout 输入声道布局
 * @param sampleFormat 输入采样格式
 */
SingleAudioRecorder::SingleAudioRecorder(const char *outUrl, int sampleRate, int channelLayout, int sampleFormat) {
    LOGCATE("SingleAudioRecorder::SingleAudioRecorder outUrl=%s, sampleRate=%d, channelLayout=%d, sampleFormat=%d", outUrl, sampleRate, channelLayout, sampleFormat);
    strncpy(m_outUrl, outUrl, sizeof(m_outUrl) - 1);
    m_sampleRate = sampleRate;
    m_channelLayout = channelLayout;
    m_sampleFormat = sampleFormat;
}

/**
 * @brief 析构函数：确保编码线程已停止、资源已释放
 */
SingleAudioRecorder::~SingleAudioRecorder() {
    StopRecord();
}

/**
 * @brief 开始音频录制
 * @return 0表示成功，负数表示失败（失败时已释放所有资源）
 */
int SingleAudioRecorder::StartRecord() {
    int result = -1;
    do {
        result = avformat_alloc_output_context2(&m_pFormatCtx, nullptr, nullptr, m_outUrl);
        if(result < 0) {
            LOGCATE("SingleAudioRecorder::StartRecord avformat_alloc_output_context2 ret=%d", result);
            break;
        }

        m_pStream = avformat_new_stream(m_pFormatCtx, nullptr);
        if (m_pStream == nullptr) {
            result = -1;
            LOGCATE("SingleAudioRecorder::StartRecord avformat_new_stream fail.");
            break;
        }

        AVOutputFormat *avOutputFormat = m_pFormatCtx->oformat;
        m_pCodec = avcodec_find_encoder(avOutputFormat->audio_codec);
        if (m_pCodec == nullptr) {
            result = -1;
            LOGCATE("SingleAudioRecorder::StartRecord avcodec_find_encoder fail.");
            break;
        }
        LOGCATE("SingleAudioRecorder::StartRecord encoder=%s", m_pCodec->name);

        // 自己创建并持有编码器上下文。
        // 原来用的是已废弃的 m_pStream->codec（归 AVStream 所有），又对它 avcodec_free_context，
        // 为了避免重复释放只好不释放 AVFormatContext，导致每次录音都泄漏
        m_pCodecCtx = avcodec_alloc_context3(m_pCodec);
        if (m_pCodecCtx == nullptr) {
            result = -1;
            break;
        }
        // 采样格式取编码器支持的第一个（内置 aac 为 FLTP，libfdk_aac 为 S16）
        m_pCodecCtx->sample_fmt = m_pCodec->sample_fmts ? m_pCodec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        m_pCodecCtx->sample_rate = DEFAULT_SAMPLE_RATE;
        m_pCodecCtx->channel_layout = DEFAULT_CHANNEL_LAYOUT;
        m_pCodecCtx->channels = av_get_channel_layout_nb_channels(m_pCodecCtx->channel_layout);
        m_pCodecCtx->bit_rate = 96000;
        m_pCodecCtx->time_base = AVRational{1, m_pCodecCtx->sample_rate};
        // m4a/mp4 要求 AudioSpecificConfig 放在全局 extradata 中
        if (avOutputFormat->flags & AVFMT_GLOBALHEADER) {
            m_pCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        result = avcodec_open2(m_pCodecCtx, m_pCodec, nullptr);
        if(result < 0) {
            LOGCATE("SingleAudioRecorder::StartRecord avcodec_open2 ret=%d", result);
            break;
        }

        // 编码器打开后再拷贝参数，extradata 才是完整的
        result = avcodec_parameters_from_context(m_pStream->codecpar, m_pCodecCtx);
        if(result < 0) break;
        m_pStream->time_base = m_pCodecCtx->time_base;

        av_dump_format(m_pFormatCtx, 0, m_outUrl, 1);

        // 编码帧：编码器要求的每帧采样数
        int frameSize = m_pCodecCtx->frame_size > 0 ? m_pCodecCtx->frame_size : 1024;
        m_pFrame = av_frame_alloc();
        m_pInFrame = av_frame_alloc();
        if (m_pFrame == nullptr || m_pInFrame == nullptr) {
            result = -1;
            break;
        }
        m_pFrame->nb_samples = frameSize;
        m_pFrame->format = m_pCodecCtx->sample_fmt;
        m_pFrame->channel_layout = m_pCodecCtx->channel_layout;
        m_pFrame->sample_rate = m_pCodecCtx->sample_rate;
        result = av_frame_get_buffer(m_pFrame, 0);
        if (result < 0) break;

        // 输入帧：保存从 FIFO 读出的原始数据
        m_pInFrame->nb_samples = frameSize;
        m_pInFrame->format = m_sampleFormat;
        m_pInFrame->channel_layout = m_channelLayout;
        m_pInFrame->sample_rate = m_sampleRate;
        result = av_frame_get_buffer(m_pInFrame, 0);
        if (result < 0) break;

        // 输入数据先写入 FIFO：AudioRecord 每次给的数据长度和编码器要求的帧长不一致
        m_pFifo = av_audio_fifo_alloc(AVSampleFormat(m_sampleFormat),
                                      av_get_channel_layout_nb_channels(m_channelLayout), frameSize * 4);
        if (m_pFifo == nullptr) {
            result = -1;
            break;
        }

        // 创建音频重采样器，用于格式转换（输入 -> 编码器格式）
        m_swrCtx = swr_alloc_set_opts(nullptr,
                                      m_pCodecCtx->channel_layout, m_pCodecCtx->sample_fmt, m_pCodecCtx->sample_rate,
                                      m_channelLayout, AVSampleFormat(m_sampleFormat), m_sampleRate,
                                      0, nullptr);
        if (m_swrCtx == nullptr || swr_init(m_swrCtx) < 0) {
            result = -1;
            LOGCATE("SingleAudioRecorder::StartRecord swr_init fail.");
            break;
        }

        result = avio_open(&m_pFormatCtx->pb, m_outUrl, AVIO_FLAG_WRITE);
        if(result < 0) {
            LOGCATE("SingleAudioRecorder::StartRecord avio_open ret=%d", result);
            break;
        }

        // 写文件头
        result = avformat_write_header(m_pFormatCtx, nullptr);
        if(result < 0) {
            LOGCATE("SingleAudioRecorder::StartRecord avformat_write_header ret=%d", result);
            break;
        }
        m_headerWritten = true;
        result = 0;
    } while (false);

    if(result < 0) {
        Release();
        // 返回真实结果（原来无论成败都返回 0）
        return result;
    }

    // 初始化成功，启动编码线程（m_started 置位前准备好生产端要用的字段）
    m_bytesPerSample = av_get_bytes_per_sample(AVSampleFormat(m_sampleFormat))
                       * av_get_channel_layout_nb_channels(m_channelLayout);
    m_silenceBuffer.assign(static_cast<size_t>(m_pInFrame->nb_samples) * m_bytesPerSample, 0);
    m_started = true;
    m_encodeThread = new thread(StartAACEncoderThread, this);
    return 0;
}

/**
 * @brief 接收待编码的音频帧：复制一份并加入编码队列
 */
int SingleAudioRecorder::OnFrame2Encode(AudioFrame *inputFrame) {
    LOGCATV("SingleAudioRecorder::OnFrame2Encode inputFrame->data=%p, inputFrame->dataSize=%d", inputFrame->data, inputFrame->dataSize);
    if(!m_started || m_exit) return 0;
    int64_t samples = inputFrame->dataSize / m_bytesPerSample;
    if(samples <= 0) return 0;
    // 按入队顺序给每块编号，编码线程据此发现被丢弃的块
    int64_t samplePos = m_pushedSamples.fetch_add(samples);
    AudioChunk *chunk = new AudioChunk(inputFrame->data, static_cast<int>(samples * m_bytesPerSample), 0, samplePos);
    AudioChunk *dropped = m_frameQueue.Push(chunk);
    if(dropped) {
        LOGCATE("SingleAudioRecorder::OnFrame2Encode queue full, drop oldest chunk (will be filled with silence)");
        delete dropped;
    }
    return 0;
}

/**
 * @brief 停止音频录制：编码完剩余数据、刷新编码器、写文件尾并释放资源。可重复调用
 */
int SingleAudioRecorder::StopRecord() {
    m_exit = true;
    m_frameQueue.WakeUpAll();
    if(m_encodeThread != nullptr) {
        m_encodeThread->join();
        delete m_encodeThread;
        m_encodeThread = nullptr;

        // 不足一帧的剩余采样作为最后一帧，然后刷新编码器
        int remain = av_audio_fifo_size(m_pFifo);
        if(remain > 0) EncodeSamples(remain);
        EncodeFrame(nullptr);
        if(m_headerWritten) {
            av_write_trailer(m_pFormatCtx);
        }
    }
    m_started = false;
    Release();
    return 0;
}

void SingleAudioRecorder::Release() {
    while (AudioChunk *pAudioFrame = m_frameQueue.Pop()) {
        delete pAudioFrame;
    }

    swr_free(&m_swrCtx);
    avcodec_free_context(&m_pCodecCtx);
    av_frame_free(&m_pFrame);
    av_frame_free(&m_pInFrame);
    if(m_pFifo) {
        av_audio_fifo_free(m_pFifo);
        m_pFifo = nullptr;
    }
    if (m_pFormatCtx != nullptr) {
        avio_closep(&m_pFormatCtx->pb);
        avformat_free_context(m_pFormatCtx);
        m_pFormatCtx = nullptr;
    }
    m_headerWritten = false;
}

/**
 * @brief AAC编码线程函数（静态函数）
 *
 * 阻塞等待音频数据（不再 usleep 轮询），写入 FIFO，凑满一帧就编码
 */
void SingleAudioRecorder::StartAACEncoderThread(SingleAudioRecorder *recorder) {
    LOGCATE("SingleAudioRecorder::StartAACEncoderThread start");
    int blockSamples = static_cast<int>(recorder->m_silenceBuffer.size() / recorder->m_bytesPerSample);
    for(;;)
    {
        AudioChunk *chunk = recorder->m_frameQueue.WaitPop(50);
        if(chunk == nullptr) {
            if(recorder->m_exit) break;   // 已停止且队列为空
            continue;
        }

        int64_t samples = chunk->dataSize / recorder->m_bytesPerSample;
        // 队列满时丢弃的块：按缺失的采样数原位补静音，录音时长和真实时长保持一致
        // （原来直接丢掉，录 10 秒可能只剩 9 秒，后面的声音整体提前）
        int64_t lost = chunk->samplePos - recorder->m_nextSamplePos;
        if(lost > 0) {
            LOGCATE("SingleAudioRecorder::StartAACEncoderThread %lld samples dropped by queue, fill with silence", (long long) lost);
            while(lost > 0) {
                int n = static_cast<int>(lost < blockSamples ? lost : blockSamples);
                recorder->FeedFifo(recorder->m_silenceBuffer.data(), n);
                lost -= n;
            }
        }
        recorder->m_nextSamplePos = chunk->samplePos + samples;

        recorder->FeedFifo(chunk->data, static_cast<int>(samples));
        delete chunk;
    }

    LOGCATE("SingleAudioRecorder::StartAACEncoderThread end");
}

void SingleAudioRecorder::FeedFifo(const uint8_t *data, int samples) {
    void *planes[1] = {const_cast<uint8_t *>(data)};
    if(av_audio_fifo_write(m_pFifo, planes, samples) < samples) {
        LOGCATE("SingleAudioRecorder::FeedFifo av_audio_fifo_write fail");
    }
    // m_pInFrame->nb_samples 是分配时的容量，即编码器要求的帧长
    int frameSize = m_pInFrame->nb_samples;
    while(av_audio_fifo_size(m_pFifo) >= frameSize) {
        EncodeSamples(frameSize);
    }
}

int SingleAudioRecorder::EncodeSamples(int nbSamples) {
    // m_pInFrame->nb_samples 始终是分配时的容量（一帧）；m_pFrame->nb_samples 上次可能被改小，先恢复
    int capacity = m_pInFrame->nb_samples;
    m_pFrame->nb_samples = capacity;
    if(av_frame_make_writable(m_pInFrame) < 0 || av_frame_make_writable(m_pFrame) < 0) return -1;
    int readSamples = av_audio_fifo_read(m_pFifo, (void **) m_pInFrame->data, nbSamples);
    if(readSamples <= 0) return -1;

    int outSamples = swr_convert(m_swrCtx, m_pFrame->data, capacity,
                                 (const uint8_t **) m_pInFrame->data, readSamples);
    if(outSamples <= 0) return -1;

    // pts 以采样为单位：每帧增加 nb_samples（原来每帧只加 1，文件时长和时间戳都是错的）
    m_pFrame->nb_samples = outSamples;
    m_pFrame->pts = av_rescale_q(m_samplesCount, AVRational{1, m_pCodecCtx->sample_rate}, m_pCodecCtx->time_base);
    m_samplesCount += outSamples;
    return EncodeFrame(m_pFrame);
}

/**
 * @brief 编码单个音频帧
 * @param pFrame 待编码的帧（nullptr表示刷新编码器）
 * @return 0表示成功，负数表示失败
 */
int SingleAudioRecorder::EncodeFrame(AVFrame *pFrame) {
    LOGCATV("SingleAudioRecorder::EncodeFrame pFrame->nb_samples=%d", pFrame != nullptr ? pFrame->nb_samples : 0);
    if(m_pCodecCtx == nullptr) return -1;
    // 发送帧到编码器
    int result = avcodec_send_frame(m_pCodecCtx, pFrame);
    if(result < 0)
    {
        LOGCATE("SingleAudioRecorder::EncodeFrame avcodec_send_frame fail. ret=%d", result);
        return result;
    }
    // 从编码器接收数据包
    AVPacket *pkt = av_packet_alloc();
    for(;;) {
        result = avcodec_receive_packet(m_pCodecCtx, pkt);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            result = 0;
            break;
        } else if (result < 0) {
            LOGCATE("SingleAudioRecorder::EncodeFrame avcodec_receive_packet fail. ret=%d", result);
            break;
        }
        // 时间戳从编码器时间基换算到流时间基后写入文件
        pkt->stream_index = m_pStream->index;
        av_packet_rescale_ts(pkt, m_pCodecCtx->time_base, m_pStream->time_base);
        av_interleaved_write_frame(m_pFormatCtx, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return result;
}
