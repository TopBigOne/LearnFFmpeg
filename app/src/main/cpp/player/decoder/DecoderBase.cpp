/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#include "DecoderBase.h"
#include "../../util/LogUtil.h"

void DecoderBase::Start() {
    if(m_Thread == nullptr) {
        StartDecodingThread();
    } else {
        std::unique_lock<std::mutex> lock(m_Mutex);
        // 已经 Stop 的解码器不允许再被"恢复"
        if(m_DecoderState != STATE_STOP) {
            m_DecoderState = STATE_DECODING;
            m_Cond.notify_all();
        }
    }
}

void DecoderBase::Pause() {
    std::unique_lock<std::mutex> lock(m_Mutex);
    if(m_DecoderState == STATE_DECODING)
        m_DecoderState = STATE_PAUSE;
}

void DecoderBase::Stop() {
    LOGCATE("DecoderBase::Stop m_MediaType=%d", m_MediaType);
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_DecoderState = STATE_STOP;
    m_Cond.notify_all();
}

void DecoderBase::SeekToPosition(float position) {
    LOGCATE("DecoderBase::SeekToPosition position=%f", position);
    std::unique_lock<std::mutex> lock(m_Mutex);
    // 用 -1 表示"没有 seek 请求"，因此 seek 到 0 也能生效
    m_SeekPosition = position < 0 ? 0 : position;
    // 播放结束后处于暂停状态，seek 需要把解码线程唤醒
    if(m_DecoderState != STATE_STOP) {
        m_DecoderState = STATE_DECODING;
        m_Cond.notify_all();
    }
}

float DecoderBase::GetCurrentPosition() {
    //单位 ms，m_CurTimeStamp 是原子变量，可以无锁读取
    return m_CurTimeStamp;
}

int DecoderBase::Init(const char *url, AVMediaType mediaType) {
    LOGCATE("DecoderBase::Init url=%s, mediaType=%d", url, mediaType);
    // 限长拷贝，避免超长 url 写越界
    strncpy(m_Url, url, MAX_PATH - 1);
    m_Url[MAX_PATH - 1] = '\0';
    m_MediaType = mediaType;
    return 0;
}

void DecoderBase::UnInit() {
    LOGCATE("DecoderBase::UnInit m_MediaType=%d", m_MediaType);
    if(m_Thread) {
        Stop();
        m_Thread->join();
        delete m_Thread;
        m_Thread = nullptr;
    }
    LOGCATE("DecoderBase::UnInit end, m_MediaType=%d", m_MediaType);
}

int DecoderBase::InitFFDecoder() {
    int result = -1;
    // 这些都是"解封装/协议层"的参数（rtsp/udp/网络缓冲），必须传给 avformat_open_input，
    // 传给 avcodec_open2 是不生效的
    AVDictionary *pFormatOpts = nullptr;
    av_dict_set(&pFormatOpts, "buffer_size", "1024000", 0);
    av_dict_set(&pFormatOpts, "stimeout", "20000000", 0);
    av_dict_set(&pFormatOpts, "max_delay", "30000000", 0);
    av_dict_set(&pFormatOpts, "rtsp_transport", "tcp", 0);

    do {
        //1.创建封装格式上下文
        m_AVFormatContext = avformat_alloc_context();

        //2.打开文件（失败时 FFmpeg 会释放上下文并把指针置空）
        if(avformat_open_input(&m_AVFormatContext, m_Url, NULL, &pFormatOpts) != 0)
        {
            LOGCATE("DecoderBase::InitFFDecoder avformat_open_input fail.");
            break;
        }

        //3.获取音视频流信息
        if(avformat_find_stream_info(m_AVFormatContext, NULL) < 0) {
            LOGCATE("DecoderBase::InitFFDecoder avformat_find_stream_info fail.");
            break;
        }

        //4.获取音视频流索引
        m_StreamIndex = av_find_best_stream(m_AVFormatContext, m_MediaType, -1, -1, nullptr, 0);
        if(m_StreamIndex < 0) {
            LOGCATE("DecoderBase::InitFFDecoder Fail to find stream index.");
            m_StreamIndex = -1;
            break;
        }
        //5.获取解码器参数
        AVCodecParameters *codecParameters = m_AVFormatContext->streams[m_StreamIndex]->codecpar;

        //6.获取解码器
        m_AVCodec = avcodec_find_decoder(codecParameters->codec_id);
        if(m_AVCodec == nullptr) {
            LOGCATE("DecoderBase::InitFFDecoder avcodec_find_decoder fail.");
            break;
        }

        //7.创建解码器上下文
        m_AVCodecContext = avcodec_alloc_context3(m_AVCodec);
        if(m_AVCodecContext == nullptr) {
            LOGCATE("DecoderBase::InitFFDecoder avcodec_alloc_context3 fail.");
            break;
        }
        if(avcodec_parameters_to_context(m_AVCodecContext, codecParameters) < 0) {
            LOGCATE("DecoderBase::InitFFDecoder avcodec_parameters_to_context fail.");
            break;
        }
        // 解码输出帧的时间基与流保持一致，方便后续换算
        m_AVCodecContext->pkt_timebase = m_AVFormatContext->streams[m_StreamIndex]->time_base;
        if(m_MediaType == AVMEDIA_TYPE_VIDEO) {
            // 0 = 由 FFmpeg 按 CPU 核数自动决定解码线程数，软解高分辨率视频时收益明显
            m_AVCodecContext->thread_count = 0;
        }

        //8.打开解码器
        int ret = avcodec_open2(m_AVCodecContext, m_AVCodec, nullptr);
        if(ret < 0) {
            LOGCATE("DecoderBase::InitFFDecoder avcodec_open2 fail. result=%d", ret);
            break;
        }

        // us -> ms，先乘后除保留毫秒精度；直播流没有时长
        if(m_AVFormatContext->duration != AV_NOPTS_VALUE)
            m_Duration = m_AVFormatContext->duration * 1000 / AV_TIME_BASE;
        //创建 AVPacket 存放编码数据
        m_Packet = av_packet_alloc();
        //创建 AVFrame 存放解码后的数据
        m_Frame = av_frame_alloc();
        if(m_Packet == nullptr || m_Frame == nullptr) break;

        result = 0;
    } while (false);

    // 没被消费的选项需要手动释放
    av_dict_free(&pFormatOpts);

    if(result != 0 && m_MsgContext && m_MsgCallback)
        m_MsgCallback(m_MsgContext, MSG_DECODER_INIT_ERROR, 0);

    return result;
}

void DecoderBase::UnInitDecoder() {
    LOGCATE("DecoderBase::UnInitDecoder");
    // 这些 free 函数都会把指针置空，且对空指针安全
    av_frame_free(&m_Frame);
    av_packet_free(&m_Packet);

    // avcodec_free_context 内部已经包含了 close，不需要再调用已废弃的 avcodec_close
    avcodec_free_context(&m_AVCodecContext);
    m_AVCodec = nullptr;

    // avformat_close_input 会释放上下文本身，不能再调用 avformat_free_context
    avformat_close_input(&m_AVFormatContext);
}

void DecoderBase::StartDecodingThread() {
    m_Thread = new thread(DoAVDecoding, this);
}

void DecoderBase::DecodingLoop() {
    LOGCATE("DecoderBase::DecodingLoop start, m_MediaType=%d", m_MediaType);
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        // 线程启动前可能已经被 Stop，不能把 STOP 覆盖掉
        if(m_DecoderState == STATE_UNKNOWN)
            m_DecoderState = STATE_DECODING;
    }

    for(;;) {
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            if(m_DecoderState == STATE_PAUSE) {
                LOGCATE("DecoderBase::DecodingLoop paused, m_MediaType=%d", m_MediaType);
                // 阻塞等待，直到 Start/Seek/Stop 改变状态，不再每 10ms 轮询一次
                m_Cond.wait(lock, [this] { return m_DecoderState != STATE_PAUSE; });
                // 暂停的时长不计入播放进度：以当前时间戳重建系统时钟参考
                m_StartTimeStamp = GetSysCurrentTime() - m_CurTimeStamp;
            }
            if(m_DecoderState == STATE_STOP) {
                break;
            }
        }

        if(m_StartTimeStamp == -1)
            m_StartTimeStamp = GetSysCurrentTime();

        if(DecodeOnePacket() != 0) {
            //文件已读完且解码器已排空，暂停解码器，等待 seek 或退出
            LOGCATE("DecoderBase::DecodingLoop reach end of stream, m_MediaType=%d", m_MediaType);
            std::unique_lock<std::mutex> lock(m_Mutex);
            if(m_DecoderState == STATE_DECODING)
                m_DecoderState = STATE_PAUSE;
        }
    }
    LOGCATE("DecoderBase::DecodingLoop end");
}

void DecoderBase::UpdateTimeStamp() {
    // best_effort_timestamp 是 FFmpeg 推算出的显示时间戳；
    // pkt_dts 是解码时间戳，有 B 帧时和显示顺序不一致，不能用来做同步
    int64_t ts = m_Frame->best_effort_timestamp;
    if(ts == AV_NOPTS_VALUE) ts = m_Frame->pts;

    if(ts != AV_NOPTS_VALUE) {
        int64_t timeStamp = av_rescale_q(ts, m_AVFormatContext->streams[m_StreamIndex]->time_base, AVRational{1, 1000});
        // 减去文件的起始时间（如 TS 文件常从 1.4s 开始），让进度从 0 开始
        // 用文件级而不是流级的 start_time，保证音视频两条流的时间轴一致
        if(m_AVFormatContext->start_time != AV_NOPTS_VALUE)
            timeStamp -= m_AVFormatContext->start_time / 1000;
        m_CurTimeStamp = timeStamp;
    }
    // 没有任何时间戳时沿用上一帧的值，保持单调

    if(m_ResetClockOnNextFrame) {
        // seek 之后的第一帧：以它为基准重建系统时钟参考
        m_StartTimeStamp = GetSysCurrentTime() - m_CurTimeStamp;
        m_ResetClockOnNextFrame = false;
    }
}

bool DecoderBase::AVSync() {
    int64_t now = GetSysCurrentTime();
    int64_t curTimeStamp = m_CurTimeStamp;

    // 读取主时钟（音频时钟），-1 表示当前不可用
    int64_t masterClock = -1;
    if(m_AVSyncCallback != nullptr)
        masterClock = m_AVSyncCallback(m_AVDecoderContext);

    // 上报播放进度：音频负责上报；没有可用音频时钟时由视频上报。节流到 100ms 一次
    bool shouldReport = m_MediaType == AVMEDIA_TYPE_AUDIO || masterClock < 0;
    if(shouldReport && m_MsgContext && m_MsgCallback &&
       (m_LastProgressTime < 0 || now - m_LastProgressTime >= 100)) {
        m_LastProgressTime = now;
        m_MsgCallback(m_MsgContext, MSG_DECODING_TIME, curTimeStamp * 1.0f / 1000);
    }

    // 音频不主动睡眠：音频渲染器的有界队列满了会阻塞写入，节奏由声卡决定
    if(m_MediaType == AVMEDIA_TYPE_AUDIO)
        return true;

    int64_t diff;   // 正数：视频超前，需要等待；负数：视频落后
    if(masterClock >= 0) {
        diff = curTimeStamp - masterClock;
        // 同步更新系统时钟参考，音频时钟失效时（如音轨提前结束）可以无缝切换
        m_StartTimeStamp = now - masterClock;
    } else {
        //基于系统时钟计算从开始播放流逝的时间
        int64_t elapsedTime = now - m_StartTimeStamp;
        diff = curTimeStamp - elapsedTime;
    }

    if(diff > 0) {
        //限制休眠时间不能过长，下一帧会继续校正
        int64_t sleepTime = diff > DELAY_THRESHOLD ? DELAY_THRESHOLD : diff;
        av_usleep(static_cast<unsigned int>(sleepTime * 1000));
        return true;
    }

    // 严重落后时丢弃该帧（不渲染），尽快追上主时钟
    if(diff < -DROP_THRESHOLD) {
        LOGCATV("DecoderBase::AVSync drop video frame, diff=%lld ms", (long long)diff);
        return false;
    }
    return true;
}

void DecoderBase::DoSeekIfNeeded() {
    // 取出并清除 seek 请求：无论成功与否都只尝试一次，避免失败时每读一个包都 seek 一次
    float position = m_SeekPosition.exchange(-1.0f);
    if(position < 0) return;

    int64_t seekTarget = static_cast<int64_t>(position * AV_TIME_BASE);//微秒
    if(m_AVFormatContext->start_time != AV_NOPTS_VALUE)
        seekTarget += m_AVFormatContext->start_time;

    int ret = avformat_seek_file(m_AVFormatContext, -1, INT64_MIN, seekTarget, INT64_MAX, 0);
    if(ret < 0) {
        LOGCATE("DecoderBase::DoSeekIfNeeded error while seeking, m_MediaType=%d, ret=%d", m_MediaType, ret);
        return;
    }

    // 丢弃解码器内部缓存的旧帧，以及渲染端还没播放的旧数据
    avcodec_flush_buffers(m_AVCodecContext);
    ClearCache();
    m_InputEOF = false;
    m_ResetClockOnNextFrame = true;
    LOGCATE("DecoderBase::DoSeekIfNeeded seek to %f s, m_MediaType=%d", position, m_MediaType);
}

int DecoderBase::ReceiveFrames() {
    int frameCount = 0;
    // 一个 packet 可能解出 0 帧（解码器还在攒数据）或多帧（如部分音频格式）
    while(m_DecoderState != STATE_STOP && avcodec_receive_frame(m_AVCodecContext, m_Frame) == 0) {
        //更新时间戳
        UpdateTimeStamp();
        //同步，严重落后的视频帧直接丢弃
        if(AVSync()) {
            //渲染
            OnFrameAvailable(m_Frame);
        }
        av_frame_unref(m_Frame);
        frameCount++;
    }
    return frameCount;
}

int DecoderBase::DecodeOnePacket() {
    LOGCATV("DecoderBase::DecodeOnePacket m_MediaType=%d", m_MediaType);
    DoSeekIfNeeded();

    for(;;) {
        // 退出或有新的 seek 请求时，先回到外层循环处理
        if(m_DecoderState == STATE_STOP || m_SeekPosition >= 0)
            return 0;

        if(!m_InputEOF) {
            int ret = av_read_frame(m_AVFormatContext, m_Packet);
            if(ret < 0) {
                // 文件读完：送入空包（flush），让解码器把内部缓存的帧（如 B 帧重排队列）全部吐出来
                m_InputEOF = true;
                avcodec_send_packet(m_AVCodecContext, nullptr);
            } else if(m_Packet->stream_index != m_StreamIndex) {
                av_packet_unref(m_Packet);
                continue;
            } else {
                ret = avcodec_send_packet(m_AVCodecContext, m_Packet);
                if(ret == AVERROR(EAGAIN)) {
                    // 解码器输入已满：先把输出取走，再重新送入这个包，不能丢包
                    ReceiveFrames();
                    ret = avcodec_send_packet(m_AVCodecContext, m_Packet);
                }
                av_packet_unref(m_Packet);
                if(ret < 0 && ret != AVERROR(EAGAIN)) {
                    // 损坏的包：跳过它，继续解码后面的数据
                    LOGCATE("DecoderBase::DecodeOnePacket avcodec_send_packet fail, ret=%d", ret);
                }
            }
        }

        if(ReceiveFrames() > 0)
            return 0;

        if(m_InputEOF) {
            // 已经送过 flush 包且再也取不出帧：真正结束
            return -1;
        }
    }
}

void DecoderBase::DoAVDecoding(DecoderBase *decoder) {
    LOGCATE("DecoderBase::DoAVDecoding");
    do {
        if(decoder->InitFFDecoder() != 0) {
            break;
        }
        decoder->OnDecoderReady();
        decoder->DecodingLoop();
    } while (false);

    decoder->UnInitDecoder();
    decoder->OnDecoderDone();

}
