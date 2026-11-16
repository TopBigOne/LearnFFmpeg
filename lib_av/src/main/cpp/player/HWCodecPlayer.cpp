/**
 *
 * Created by 公众号：字节流动 on 2021/12/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#include "HWCodecPlayer.h"
#include <vector>

// 音频输出参数：与 Java 层 AudioTrack 的创建参数保持一致
#define AUDIO_OUT_SAMPLE_RATE    44100
#define AUDIO_OUT_CHANNELS       2
#define AUDIO_OUT_CHANNEL_LAYOUT AV_CH_LAYOUT_STEREO
#define AUDIO_OUT_SAMPLE_FMT     AV_SAMPLE_FMT_S16

/**
 * JNI 环境的 RAII 守卫：
 * 构造时如果当前线程还没有附加到 JVM 就附加，析构时（且仅当是自己附加的）自动分离。
 * native 线程退出前必须 DetachCurrentThread，否则 ART 会直接 abort 进程，
 * 用 RAII 可以保证任何 return/break 路径都不会漏掉。
 */
class ScopedJNIEnv {
public:
    explicit ScopedJNIEnv(JavaVM *vm) : m_VM(vm) {
        if (m_VM == nullptr) return;
        if (m_VM->GetEnv((void **) &m_Env, JNI_VERSION_1_4) != JNI_OK) {
            if (m_VM->AttachCurrentThread(&m_Env, nullptr) == JNI_OK) {
                m_Attached = true;
            } else {
                m_Env = nullptr;
            }
        }
    }

    ~ScopedJNIEnv() {
        if (m_Attached) m_VM->DetachCurrentThread();
    }

    JNIEnv *get() const { return m_Env; }

private:
    JavaVM *m_VM = nullptr;
    JNIEnv *m_Env = nullptr;
    bool m_Attached = false;
};

/** 调用 Java 方法后检查并清除异常：长期附加的线程上残留的异常会让后续 JNI 调用全部出错 */
static void ClearJavaException(JNIEnv *env) {
    if (env != nullptr && env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

/**
 * @brief FFmpeg日志回调函数
 * 将FFmpeg的日志重定向到Android Logcat
 */
void av_log_callback(void*ptr, int level, const char* fmt, va_list vl) {
    if (level > av_log_get_level()) return;
    va_list vl2;
    char line[1024] = {0};
    static int print_prefix = 1;
    va_copy(vl2, vl);
    av_log_format_line(ptr, level, fmt, vl2, line, sizeof(line), &print_prefix);
    va_end(vl2);
    LOGCATI("FFMPEG: %s", line);
}

/**
 * @brief 初始化硬件解码播放器
 * @param jniEnv JNI环境指针
 * @param obj Java对象
 * @param url 媒体文件路径
 * @param videoRenderType 视频渲染类型（本类中未使用，MediaCodec 直接渲染到 Surface）
 * @param surface Android Surface对象，用于视频渲染
 */
void HWCodecPlayer::Init(JNIEnv *jniEnv, jobject obj, char *url, int videoRenderType, jobject surface) {
    LOGCATE("HWCodecPlayer::Init");
    // FFmpeg 日志：DEBUG 级别每个包都会打印，调试版用 INFO，发布版只保留警告
#ifdef NDEBUG
    av_log_set_level(AV_LOG_WARNING);
#else
    av_log_set_level(AV_LOG_INFO);
#endif
    av_log_set_callback(av_log_callback);

    // 保存媒体文件路径（限长拷贝）
    strncpy(m_Url, url, MAX_PATH - 1);
    m_Url[MAX_PATH - 1] = '\0';

    // 从Surface创建NativeWindow用于视频渲染
    m_ANativeWindow = ANativeWindow_fromSurface(jniEnv, surface);

    // 创建音视频数据包队列
    m_VideoPacketQueue = new AVPacketQueue();
    m_AudioPacketQueue = new AVPacketQueue();

    // 启动队列
    m_VideoPacketQueue->Start();
    m_AudioPacketQueue->Start();

    // 保存JNI相关对象；回调方法 ID 查一次缓存起来
    jniEnv->GetJavaVM(&m_JavaVM);
    m_JavaObj = jniEnv->NewGlobalRef(obj);
    jclass clazz = jniEnv->GetObjectClass(obj);
    m_EventCallbackMid = jniEnv->GetMethodID(clazz, JAVA_PLAYER_EVENT_CALLBACK_API_NAME, "(IF)V");
    jniEnv->DeleteLocalRef(clazz);
}

/**
 * @brief 释放播放器资源
 */
void HWCodecPlayer::UnInit() {
    LOGCATE("HWCodecPlayer::UnInit");
    // 先停止播放：会 Abort 队列，唤醒所有阻塞的线程
    Stop();

    // 等待解封装线程结束（它会负责等待两个解码线程结束并释放解码器）
    if(m_DeMuxThread) {
        m_DeMuxThread->join();
        delete m_DeMuxThread;
        m_DeMuxThread = nullptr;
    }

    // 释放数据包队列
    if(m_VideoPacketQueue) {
        delete m_VideoPacketQueue;
        m_VideoPacketQueue = nullptr;
    }

    if(m_AudioPacketQueue) {
        delete m_AudioPacketQueue;
        m_AudioPacketQueue = nullptr;
    }

    // 释放NativeWindow
    if(m_ANativeWindow) {
        ANativeWindow_release(m_ANativeWindow);
        m_ANativeWindow = nullptr;
    }

    // 删除JNI全局引用
    ScopedJNIEnv jni(m_JavaVM);
    if(jni.get() != nullptr && m_JavaObj != nullptr) {
        jni.get()->DeleteGlobalRef(m_JavaObj);
        m_JavaObj = nullptr;
    }
}

/**
 * @brief 开始播放
 */
void HWCodecPlayer::Play() {
    LOGCATE("HWCodecPlayer::Play");
    if(m_DeMuxThread == nullptr) {
        // 首次播放，创建解封装线程
        m_DeMuxThread = new thread(DeMuxThreadProc, this);
    } else {
        // 从暂停恢复播放
        std::unique_lock<std::mutex> lock(m_Mutex);
        if(m_PlayerState != PLAYER_STATE_STOP) {
            m_PlayerState = PLAYER_STATE_PLAYING;
            m_Cond.notify_all();  // 唤醒所有等待的线程
        }
    }
    m_AudioClock.Resume();
}

/**
 * @brief 暂停播放
 */
void HWCodecPlayer::Pause() {
    LOGCATE("HWCodecPlayer::Pause");
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        if(m_PlayerState == PLAYER_STATE_PLAYING)
            m_PlayerState = PLAYER_STATE_PAUSE;
    }
    // 暂停期间音频时钟冻结，否则恢复播放时视频会认为自己严重落后而快进
    m_AudioClock.Pause();
}

/**
 * @brief 停止播放
 */
void HWCodecPlayer::Stop() {
    LOGCATE("HWCodecPlayer::Stop");
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_PlayerState = PLAYER_STATE_STOP;
        m_Cond.notify_all();  // 唤醒等待状态变化的线程
    }
    // 解码线程可能正阻塞在空队列的 GetPacket 上，只改状态唤不醒它，必须 Abort 队列
    if(m_VideoPacketQueue) m_VideoPacketQueue->Abort();
    if(m_AudioPacketQueue) m_AudioPacketQueue->Abort();
}

/**
 * @brief 跳转到指定位置
 * @param position 跳转位置（秒）
 */
void HWCodecPlayer::SeekToPosition(float position) {
    LOGCATE("HWCodecPlayer::SeekToPosition position=%f", position);
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_SeekPosition = position < 0 ? 0 : position;
        if(m_PlayerState != PLAYER_STATE_STOP)
            m_PlayerState = PLAYER_STATE_PLAYING;
        m_Cond.notify_all();
    }
    m_AudioClock.Resume();
}

/**
 * @brief 获取媒体参数
 * @param paramType 参数类型
 * @return 参数值
 */
long HWCodecPlayer::GetMediaParams(int paramType) {
    LOGCATE("HWCodecPlayer::GetMediaParams paramType=%d", paramType);
    long value = 0;
    // 只读初始化时缓存下来的值：解码上下文可能正被解封装线程释放，不能在 JNI 线程上直接访问
    switch(paramType)
    {
        case MEDIA_PARAM_VIDEO_WIDTH:
            value = m_VideoWidth;
            break;
        case MEDIA_PARAM_VIDEO_HEIGHT:
            value = m_VideoHeight;
            break;
        case MEDIA_PARAM_VIDEO_DURATION:
            value = static_cast<long>(m_Duration / 1000);  // 毫秒转秒
            break;
    }
    return value;
}

/**
 * @brief 获取JNI环境指针
 * @param isAttach 输出参数，指示当前线程是否是本次新附加到JVM的（调用方需负责分离）
 * @return JNI环境指针
 */
JNIEnv *HWCodecPlayer::GetJNIEnv(bool *isAttach) {
    JNIEnv *env;
    int status;
    if (nullptr == m_JavaVM) {
        LOGCATE("HWCodecPlayer::GetJNIEnv m_JavaVM == nullptr");
        return nullptr;
    }
    *isAttach = false;
    // 尝试获取当前线程的JNI环境
    status = m_JavaVM->GetEnv((void **)&env, JNI_VERSION_1_4);
    if (status != JNI_OK) {
        // 如果当前线程未附加到JVM，则附加它
        status = m_JavaVM->AttachCurrentThread(&env, nullptr);
        if (status != JNI_OK) {
            LOGCATE("HWCodecPlayer::GetJNIEnv failed to attach current thread");
            return nullptr;
        }
        *isAttach = true;
    }
    return env;
}

jobject HWCodecPlayer::GetJavaObj() {
    return m_JavaObj;
}

JavaVM *HWCodecPlayer::GetJavaVM() {
    return m_JavaVM;
}

/**
 * @brief 向Java层发送消息
 * @param context 上下文对象（HWCodecPlayer实例指针）
 * @param msgType 消息类型
 * @param msgCode 消息代码
 *
 * 各工作线程在入口处已经用 ScopedJNIEnv 附加到 JVM，这里不会再重复附加/分离
 */
void HWCodecPlayer::PostMessage(void *context, int msgType, float msgCode) {
    if(context == nullptr) return;
    HWCodecPlayer *player = static_cast<HWCodecPlayer *>(context);
    if(player->m_JavaObj == nullptr || player->m_EventCallbackMid == nullptr) return;

    ScopedJNIEnv jni(player->m_JavaVM);
    JNIEnv *env = jni.get();
    LOGCATV("HWCodecPlayer::PostMessage env=%p, msgType=%d", env, msgType);
    if(env == nullptr) return;
    env->CallVoidMethod(player->m_JavaObj, player->m_EventCallbackMid, msgType, msgCode);
    ClearJavaException(env);
}

int64_t HWCodecPlayer::ToMs(int64_t ts, AVRational timeBase) {
    if(ts == AV_NOPTS_VALUE) return -1;
    return av_rescale_q(ts, timeBase, AVRational{1, 1000}) - m_StartTime;
}

bool HWCodecPlayer::WaitWhilePaused() {
    std::unique_lock<std::mutex> lock(m_Mutex);
    // 阻塞等待（不再每 10ms 轮询一次），Play/Seek/Stop 会 notify
    m_Cond.wait(lock, [this] { return m_PlayerState != PLAYER_STATE_PAUSE; });
    return m_PlayerState != PLAYER_STATE_STOP;
}

void HWCodecPlayer::CheckPlaybackDone() {
    bool audioFinished = m_AudioStreamIdx < 0 || m_AudioDone;
    if(m_VideoDone && audioFinished && !m_DonePosted.exchange(true)) {
        LOGCATE("HWCodecPlayer::CheckPlaybackDone playback completed");
        PostMessage(this, MSG_DECODER_DONE, 0);
    }
}

/**
 * @brief 解封装线程入口函数
 * @param player HWCodecPlayer实例指针
 *
 * 线程执行流程：
 * 1. 初始化解码器（打开媒体文件、创建解码器等）并启动两个解码线程
 * 2. 执行解封装循环（读取数据包并分发）
 * 3. 等待解码线程退出，释放解码器资源
 */
void HWCodecPlayer::DeMuxThreadProc(HWCodecPlayer *player) {
    LOGCATE("HWCodecPlayer::DeMuxThreadProc start");
    // 本线程整个生命周期只附加一次 JVM，退出时自动分离
    ScopedJNIEnv jni(player->m_JavaVM);

    // UNKNOWN -> PLAYING；如果在线程启动前已经被 Stop，则保持 STOP
    int expected = PLAYER_STATE_UNKNOWN;
    player->m_PlayerState.compare_exchange_strong(expected, PLAYER_STATE_PLAYING);

    if(player->InitDecoder() == 0) {
        // 执行解封装循环
        player->DoMuxLoop();
    }
    // 等待解码线程退出并释放解码器资源
    player->UnInitDecoder();
    LOGCATE("HWCodecPlayer::DeMuxThreadProc end");
}

/**
 * @brief 解封装循环
 * @return 0成功
 *
 * 1. 从媒体文件中读取音视频数据包(AVPacket)，分发到对应队列
 * 2. 处理暂停/停止/Seek
 * 3. 控制缓冲区大小，防止缓冲过多数据
 * 4. 文件读完后插入 EOS 标记，然后等待 seek 或停止
 */
int HWCodecPlayer::DoMuxLoop() {
    LOGCATE("HWCodecPlayer::DoMuxLoop start");
    AVPacket *avPacket = av_packet_alloc();
    bool reachEOF = false;
    bool hasAudio = m_AudioStreamIdx >= 0;

    for(;;) {
        // 处理暂停/停止
        if(!WaitWhilePaused()) break;

        // 处理Seek请求：取出并清除请求，失败也只尝试一次（原来失败会无限重试）
        float seekPosition = m_SeekPosition.exchange(-1.0f);
        if(seekPosition >= 0) {
            LOGCATE("HWCodecPlayer::DoMuxLoop seeking position=%f", seekPosition);
            // 计算Seek目标时间（AV_TIME_BASE 即微秒），加上文件起始时间
            int64_t seekTarget = static_cast<int64_t>(seekPosition * AV_TIME_BASE) + m_StartTime * 1000;
            int seekRet = avformat_seek_file(m_AVFormatContext, -1, INT64_MIN, seekTarget, INT64_MAX, 0);
            if (seekRet < 0) {
                LOGCATE("HWCodecPlayer::DoMuxLoop error while seeking, ret=%d", seekRet);
            } else {
                // 只清空队列并插入 FLUSH 标记，解码器由各自的解码线程收到标记后自己 flush，
                // 避免本线程和解码线程同时操作 MediaCodec / AVCodecContext
                m_VideoPacketQueue->Flush();
                m_VideoPacketQueue->PushFlushPacket();
                if(hasAudio) {
                    m_AudioPacketQueue->Flush();
                    m_AudioPacketQueue->PushFlushPacket();
                }
                m_AudioClock.Reset();
                m_VideoDone = false;
                m_AudioDone = false;
                m_DonePosted = false;
                reachEOF = false;
            }
        }

        if(reachEOF) {
            // 文件已读完：不再改成 PAUSE（那样解码线程会停下，队列里剩余的包就播不出来了），
            // 只是自己等待 seek 或停止
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Cond.wait(lock, [this] { return m_PlayerState == PLAYER_STATE_STOP || m_SeekPosition >= 0; });
            continue;
        }

        //防止缓冲数据包过多：等待时用条件变量，停止/seek 能立即唤醒
        double videoBufferDuration = m_VideoPacketQueue->GetDuration() * av_q2d(m_VideoTimeBase);
        double audioBufferDuration = hasAudio ? m_AudioPacketQueue->GetDuration() * av_q2d(m_AudioTimeBase) : 0;
        if(videoBufferDuration > BUFF_MAX_VIDEO_DURATION || audioBufferDuration > BUFF_MAX_AUDIO_DURATION) {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Cond.wait_for(lock, std::chrono::milliseconds(10), [this] {
                return m_PlayerState != PLAYER_STATE_PLAYING || m_SeekPosition >= 0;
            });
            continue;
        }

        int result = av_read_frame(m_AVFormatContext, avPacket);
        if(result >= 0) {
            // PushPacket 会把 avPacket 的数据引用转移到队列里，avPacket 随即可以复用
            if(avPacket->stream_index == m_VideoStreamIdx) {
                m_VideoPacketQueue->PushPacket(avPacket);
            } else if(hasAudio && avPacket->stream_index == m_AudioStreamIdx) {
                m_AudioPacketQueue->PushPacket(avPacket);
            } else {
                av_packet_unref(avPacket);
            }
        } else {
            // 文件读完：通知解码线程输入结束，让它们把解码器里缓存的帧全部取出
            LOGCATE("HWCodecPlayer::DoMuxLoop reach end of file, ret=%d", result);
            m_VideoPacketQueue->PushEOSPacket();
            if(hasAudio) m_AudioPacketQueue->PushEOSPacket();
            reachEOF = true;
        }
    }

    av_packet_free(&avPacket);
    LOGCATE("HWCodecPlayer::DoMuxLoop end");
    return 0;
}

void HWCodecPlayer::AudioDecodeThreadProc(HWCodecPlayer *player) {
    LOGCATE("HWCodecPlayer::AudioDecodeThreadProc start");

    // 本线程整个生命周期只附加一次 JVM，任何退出路径都会自动分离
    ScopedJNIEnv jni(player->m_JavaVM);
    JNIEnv *env = jni.get();
    if(env == nullptr) {
        player->m_AudioDone = true;
        return;
    }

    AVPacketQueue* audioPacketQueue = player->m_AudioPacketQueue;
    AVCodecContext* audioCodecCtx = player->m_AudioCodecCtx;
    AVPacket *audioPacket = av_packet_alloc();
    AVFrame *audioFrame = av_frame_alloc();

    // 重采样输出缓冲区，按需扩容
    uint8_t *audioOutBuffer = nullptr;
    int outBufferSamples = 0;
    // 复用同一个 Java byte[]，避免每帧 NewByteArray 产生大量 GC 垃圾
    jbyteArray javaBuffer = nullptr;
    int javaBufferSize = 0;
    int64_t lastProgressTime = -1;

    jclass playerClass = env->GetObjectClass(player->m_JavaObj);
    jmethodID createTrackMid = env->GetMethodID(playerClass, "createTrack", "(II)V");
    jmethodID playTrackMid = env->GetMethodID(playerClass, "playTrack", "([BI)V");
    env->DeleteLocalRef(playerClass);
    // AudioTrack 按固定的输出参数创建，重采样也必须输出同样的格式，否则会变调
    env->CallVoidMethod(player->m_JavaObj, createTrackMid, AUDIO_OUT_SAMPLE_RATE, AUDIO_OUT_CHANNELS);
    ClearJavaException(env);

    for(;;) {
        if(!player->WaitWhilePaused()) break;

        PacketType type = PACKET_TYPE_DATA;
        // 队列为空时阻塞；Stop 会 Abort 队列使这里返回 -1
        if(audioPacketQueue->GetPacket(audioPacket, &type) < 0)
            break;

        if(type == PACKET_TYPE_FLUSH) {
            // seek：丢弃解码器里的旧数据（由本线程自己执行，没有并发问题）
            avcodec_flush_buffers(audioCodecCtx);
            player->m_AudioClock.Reset();
            continue;
        }

        int ret;
        if(type == PACKET_TYPE_EOS) {
            // 输入结束：送空包，让解码器吐出缓存的帧
            ret = avcodec_send_packet(audioCodecCtx, nullptr);
        } else {
            ret = avcodec_send_packet(audioCodecCtx, audioPacket);
            // 送入后立即释放引用（原来只在解出帧时才释放，解不出帧的包会泄漏）
            av_packet_unref(audioPacket);
            if (ret < 0) {
                // 坏包跳过即可，不能直接 return（会跳过资源释放和 JVM 分离）
                LOGCATE("HWCodecPlayer::AudioDecodeThreadProc Error submitting a packet for audio decoding (%s)", av_err2str(ret));
                continue;
            }
        }

        while (player->m_PlayerState != PLAYER_STATE_STOP && avcodec_receive_frame(audioCodecCtx, audioFrame) == 0) {
            int64_t ptsMs = player->ToMs(audioFrame->best_effort_timestamp, player->m_AudioTimeBase);
            if(ptsMs >= 0)
                player->m_AudioClock.SetClock(ptsMs, GetSysCurrentTime());

            // 上报进度，节流到 100ms 一次
            int64_t now = GetSysCurrentTime();
            if(ptsMs >= 0 && player->m_SeekPosition < 0 && (lastProgressTime < 0 || now - lastProgressTime >= 100)) {
                lastProgressTime = now;
                PostMessage(player, MSG_DECODING_TIME, ptsMs * 1.0f / 1000);
            }
            LOGCATV("HWCodecPlayer::AudioDecodeThreadProc audio pts = %lld ms", (long long)ptsMs);

            // 重采样：输出缓冲按本次最多可能输出的采样数扩容
            int maxOutSamples = swr_get_out_samples(player->m_SwrCtx, audioFrame->nb_samples);
            if(maxOutSamples > outBufferSamples) {
                free(audioOutBuffer);
                outBufferSamples = maxOutSamples;
                audioOutBuffer = (uint8_t *) malloc(av_samples_get_buffer_size(NULL, AUDIO_OUT_CHANNELS, outBufferSamples, AUDIO_OUT_SAMPLE_FMT, 1));
            }
            // out_count 是"每声道采样数"，必须等于缓冲区真实容量
            int outSamples = swr_convert(player->m_SwrCtx, &audioOutBuffer, outBufferSamples,
                                         (const uint8_t **)audioFrame->extended_data, audioFrame->nb_samples);
            av_frame_unref(audioFrame);
            if(outSamples <= 0) continue;

            // 字节数按实际输出的采样数计算
            int bufferSize = av_samples_get_buffer_size(NULL, AUDIO_OUT_CHANNELS, outSamples, AUDIO_OUT_SAMPLE_FMT, 1);
            if(bufferSize > javaBufferSize) {
                if(javaBuffer) env->DeleteGlobalRef(javaBuffer);
                jbyteArray localArray = env->NewByteArray(bufferSize);
                javaBuffer = static_cast<jbyteArray>(env->NewGlobalRef(localArray));
                env->DeleteLocalRef(localArray);
                javaBufferSize = bufferSize;
            }
            env->SetByteArrayRegion(javaBuffer, 0, bufferSize, (const jbyte *)audioOutBuffer);
            // AudioTrack.write 在缓冲满时会阻塞，音频解码的节奏由声卡决定
            env->CallVoidMethod(player->m_JavaObj, playTrackMid, javaBuffer, bufferSize);
            ClearJavaException(env);
        }

        if(type == PACKET_TYPE_EOS) {
            // 音频播完：时钟失效，视频改为参考系统时钟继续播放
            player->m_AudioClock.Reset();
            player->m_AudioDone = true;
            player->CheckPlaybackDone();
        }
    }

    av_packet_free(&audioPacket);
    av_frame_free(&audioFrame);
    free(audioOutBuffer);
    if(javaBuffer) env->DeleteGlobalRef(javaBuffer);

    LOGCATE("HWCodecPlayer::AudioDecodeThreadProc end");
}

void HWCodecPlayer::VideoDecodeThreadProc(HWCodecPlayer *player) {
    LOGCATE("HWCodecPlayer::VideoDecodeThreadProc start");
    // 播放完成时要回调 Java，整个线程附加一次 JVM
    ScopedJNIEnv jni(player->m_JavaVM);

    AVPacketQueue* videoPacketQueue = player->m_VideoPacketQueue;
    AMediaCodec* videoCodec = player->m_MediaCodec;
    AVBSFContext* bsfc = player->m_Bsfc;
    AVPacket *packet = av_packet_alloc();      // 从队列取出的原始包
    AVPacket *filtered = av_packet_alloc();    // 经过码流过滤器、待送入 MediaCodec 的包

    bool hasPending = false;   // filtered 中有一个还没送进解码器的包（拿不到输入缓冲时保留，不丢包）
    bool pendingEOS = false;   // 需要向解码器送入"输入结束"标志
    bool inputEOS = false;     // 已送入"输入结束"
    bool outputEOS = false;    // 解码器已输出最后一帧
    int64_t sysBase = -1;      // 系统时钟参考（音频时钟不可用时用）

    for(;;) {
        bool wasPaused = player->m_PlayerState == PLAYER_STATE_PAUSE;
        if(!player->WaitWhilePaused()) break;
        if(wasPaused) sysBase = -1;   // 暂停过：系统时钟参考需要重建

        // 1. 准备一个待送入解码器的包
        if(!hasPending && !pendingEOS && (!inputEOS || outputEOS)) {
            // 码流过滤器可能还有未取出的输出，先取
            if(av_bsf_receive_packet(bsfc, filtered) == 0) {
                hasPending = true;
            } else {
                PacketType type = PACKET_TYPE_DATA;
                // 已经播完时没别的事可做，阻塞等待 seek/停止；否则非阻塞，以便继续取解码输出
                int ret = videoPacketQueue->GetPacket(packet, &type, outputEOS);
                if(ret < 0) break;   // 队列已 Abort（停止）
                if(ret > 0) {
                    if(type == PACKET_TYPE_FLUSH) {
                        // seek：由本线程自己 flush 解码器和过滤器
                        AMediaCodec_flush(videoCodec);
                        av_bsf_flush(bsfc);
                        av_packet_unref(filtered);
                        hasPending = pendingEOS = inputEOS = outputEOS = false;
                        sysBase = -1;
                        continue;
                    } else if(type == PACKET_TYPE_EOS) {
                        pendingEOS = true;
                    } else {
                        // AVCC/HVCC -> Annex-B（起始码格式），MediaCodec 只认后者
                        if(av_bsf_send_packet(bsfc, packet) < 0) {
                            av_packet_unref(packet);
                        } else if(av_bsf_receive_packet(bsfc, filtered) == 0) {
                            hasPending = true;
                        }
                    }
                }
            }
        }

        // 2. 送入解码器
        bool queuedInput = false;
        if(hasPending || pendingEOS) {
            ssize_t bufIdx = AMediaCodec_dequeueInputBuffer(videoCodec, 0);
            if(bufIdx >= 0) {
                if(hasPending) {
                    size_t bufSize = 0;
                    uint8_t *buf = AMediaCodec_getInputBuffer(videoCodec, bufIdx, &bufSize);
                    size_t dataSize = static_cast<size_t>(filtered->size);
                    if(buf == nullptr || dataSize > bufSize) {
                        // 拷贝前检查容量，防止写越界
                        LOGCATE("HWCodecPlayer::VideoDecodeThreadProc packet too large (%zu > %zu), dropped", dataSize, bufSize);
                        dataSize = 0;
                    } else {
                        memcpy(buf, filtered->data, dataSize);
                    }
                    // MediaCodec 的时间戳单位是微秒
                    int64_t ptsUs = filtered->pts != AV_NOPTS_VALUE
                            ? av_rescale_q(filtered->pts, player->m_VideoTimeBase, AVRational{1, 1000000}) : 0;
                    AMediaCodec_queueInputBuffer(videoCodec, bufIdx, 0, dataSize, ptsUs, 0);
                    av_packet_unref(filtered);
                    hasPending = false;
                } else {
                    // 通知解码器输入结束，之后它会把缓存的帧全部输出，并带上 EOS 标志
                    AMediaCodec_queueInputBuffer(videoCodec, bufIdx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    pendingEOS = false;
                    inputEOS = true;
                }
                queuedInput = true;
            }
            // 拿不到输入缓冲：包保留在 filtered 里，下一轮重试（原来会直接丢掉，导致花屏）
        }

        // 3. 取解码输出。刚送过输入时不等待，尽快继续送；否则最多等 10ms，避免空转
        AMediaCodecBufferInfo info;
        ssize_t status = AMediaCodec_dequeueOutputBuffer(videoCodec, &info, queuedInput ? 0 : CODEC_DEQUEUE_TIMEOUT_US);
        if (status >= 0) {
            bool isEOS = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            bool render = info.size > 0;
            if(render) {
                int64_t ptsMs = info.presentationTimeUs / 1000 - player->m_StartTime;
                LOGCATV("HWCodecPlayer::VideoDecodeThreadProc video pts = %lld ms", (long long)ptsMs);
                // 同步：早了等待，严重落后则不渲染（丢帧）
                render = player->AVSync(ptsMs, sysBase);
            }
            // render=true 时由 MediaCodec 直接把这一帧显示到 Surface 上
            AMediaCodec_releaseOutputBuffer(videoCodec, status, render);

            if(isEOS) {
                LOGCATE("HWCodecPlayer::VideoDecodeThreadProc video output EOS");
                outputEOS = true;
                player->m_VideoDone = true;
                player->CheckPlaybackDone();
            }
        } else if (status == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *format = AMediaCodec_getOutputFormat(videoCodec);
            LOGCATI("HWCodecPlayer::VideoDecodeThreadProc output format changed: %s", AMediaFormat_toString(format));
            AMediaFormat_delete(format);
        } else if (status == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            LOGCATV("HWCodecPlayer::VideoDecodeThreadProc output buffers changed");
        } else if (status != AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            LOGCATE("HWCodecPlayer::VideoDecodeThreadProc unexpected status: %zd", status);
        }
    }

    av_packet_free(&packet);
    av_packet_free(&filtered);
    LOGCATE("HWCodecPlayer::VideoDecodeThreadProc end");
}

void HWCodecPlayer::SetMediaParams(int paramType, jobject obj) {
    LOGCATE("HWCodecPlayer::SetMediaParams [paramType, obj] = [%d, %p]", paramType, obj);
    switch (paramType) {
        case MEDIA_PARAM_ASSET_MANAGER:
            // 原来用它读取写死的 assets/byteflow/vr.mp4 来构造 MediaCodec 的格式，
            // 现在直接用正在播放的视频流参数构造，不再需要 AssetManager
            break;
        default:
            break;
    }
}

/**
 * 把 Annex-B 格式（00 00 01 / 00 00 00 01 起始码分隔）的 H.264 extradata 拆成 SPS(csd-0) 和 PPS(csd-1)
 */
static void SetH264CodecSpecificData(AMediaFormat *format, const uint8_t *data, int size) {
    static const uint8_t startCode[4] = {0, 0, 0, 1};
    std::vector<uint8_t> sps, pps;

    // 找出每个 NAL 的负载起始位置（紧跟在 00 00 01 之后）
    std::vector<int> nalStarts;
    for(int i = 0; i + 3 <= size; ) {
        if(data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
            nalStarts.push_back(i + 3);
            i += 3;
        } else {
            i++;
        }
    }

    for(size_t n = 0; n < nalStarts.size(); n++) {
        int begin = nalStarts[n];
        int end = n + 1 < nalStarts.size() ? nalStarts[n + 1] - 3 : size;
        // 下一个起始码是 4 字节形式时，去掉多出来的那个 0
        while(end > begin && data[end - 1] == 0) end--;
        if(end <= begin) continue;

        int nalType = data[begin] & 0x1F;
        std::vector<uint8_t> *target = nalType == 7 ? &sps : (nalType == 8 ? &pps : nullptr);
        if(target) {
            target->insert(target->end(), startCode, startCode + 4);
            target->insert(target->end(), data + begin, data + end);
        }
    }

    if(!sps.empty() && !pps.empty()) {
        AMediaFormat_setBuffer(format, "csd-0", sps.data(), sps.size());
        AMediaFormat_setBuffer(format, "csd-1", pps.data(), pps.size());
    } else if(size > 0) {
        // 拆分失败时整段作为 csd-0，多数解码器也能识别
        AMediaFormat_setBuffer(format, "csd-0", const_cast<uint8_t *>(data), size);
    }
}

int HWCodecPlayer::InitVideoCodec(AVStream *videoStream) {
    AVCodecParameters *codecpar = videoStream->codecpar;

    // FFmpeg 编码 ID -> MediaCodec MIME 类型，以及需要的码流过滤器
    const char *mime = nullptr;
    const char *bsfName = "null";   // "null" 过滤器：原样透传
    switch(codecpar->codec_id) {
        case AV_CODEC_ID_H264:
            mime = "video/avc";
            bsfName = "h264_mp4toannexb";
            break;
        case AV_CODEC_ID_HEVC:
            mime = "video/hevc";
            bsfName = "hevc_mp4toannexb";
            break;
        case AV_CODEC_ID_MPEG4:
            mime = "video/mp4v-es";
            break;
        case AV_CODEC_ID_H263:
            mime = "video/3gpp";
            break;
        case AV_CODEC_ID_VP8:
            mime = "video/x-vnd.on2.vp8";
            break;
        case AV_CODEC_ID_VP9:
            mime = "video/x-vnd.on2.vp9";
            break;
        default:
            LOGCATE("HWCodecPlayer::InitVideoCodec unsupported codec id=%d", codecpar->codec_id);
            return -1;
    }

    // 码流过滤器（新 API）：mp4 里的 H.264/H.265 是"长度前缀"格式，MediaCodec 需要"起始码"格式。
    // 旧的 av_bitstream_filter_filter 每个包都会泄漏一块内存，已废弃
    const AVBitStreamFilter *filter = av_bsf_get_by_name(bsfName);
    if(filter == nullptr || av_bsf_alloc(filter, &m_Bsfc) < 0) {
        LOGCATE("HWCodecPlayer::InitVideoCodec av_bsf_alloc(%s) fail.", bsfName);
        return -1;
    }
    avcodec_parameters_copy(m_Bsfc->par_in, codecpar);
    m_Bsfc->time_base_in = videoStream->time_base;
    if(av_bsf_init(m_Bsfc) < 0) {
        LOGCATE("HWCodecPlayer::InitVideoCodec av_bsf_init(%s) fail.", bsfName);
        return -1;
    }

    // 用正在播放的视频流参数构造 MediaCodec 的格式（原来读的是写死的 assets/byteflow/vr.mp4）
    AMediaFormat *format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, codecpar->width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, codecpar->height);
    // 输入缓冲区大小：至少能放下一个未压缩帧的一半，避免大 I 帧放不下
    int32_t maxInputSize = codecpar->width * codecpar->height * 3 / 4;
    if(maxInputSize < 1024 * 1024) maxInputSize = 1024 * 1024;
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, maxInputSize);

    // 编码器配置信息（SPS/PPS 等），取过滤器输出端已经转换为起始码格式的 extradata
    const uint8_t *extradata = m_Bsfc->par_out->extradata;
    int extradataSize = m_Bsfc->par_out->extradata_size;
    if(extradata != nullptr && extradataSize > 0) {
        if(codecpar->codec_id == AV_CODEC_ID_H264) {
            SetH264CodecSpecificData(format, extradata, extradataSize);
        } else {
            // HEVC 的 VPS/SPS/PPS 全部放在 csd-0；MPEG4 的 VOL 头也放在 csd-0
            AMediaFormat_setBuffer(format, "csd-0", const_cast<uint8_t *>(extradata), extradataSize);
        }
    }
    LOGCATE("HWCodecPlayer::InitVideoCodec format: %s", AMediaFormat_toString(format));

    int result = -1;
    do {
        m_MediaCodec = AMediaCodec_createDecoderByType(mime);
        if(m_MediaCodec == nullptr) {
            LOGCATE("HWCodecPlayer::InitVideoCodec AMediaCodec_createDecoderByType(%s) fail.", mime);
            break;
        }
        // 传入 Surface：解码输出直接渲染到窗口，不经过 CPU
        media_status_t status = AMediaCodec_configure(m_MediaCodec, format, m_ANativeWindow, nullptr, 0);
        if(status != AMEDIA_OK) {
            LOGCATE("HWCodecPlayer::InitVideoCodec AMediaCodec_configure fail. status=%d", status);
            break;
        }
        status = AMediaCodec_start(m_MediaCodec);
        if(status != AMEDIA_OK) {
            LOGCATE("HWCodecPlayer::InitVideoCodec AMediaCodec_start fail. status=%d", status);
            break;
        }
        result = 0;
    } while (false);

    AMediaFormat_delete(format);
    return result;
}

int HWCodecPlayer::InitDecoder() {
    LOGCATE("HWCodecPlayer::InitDecoder");
    int result = -1;

    // 解封装/协议层参数（网络流用），必须传给 avformat_open_input 才生效
    AVDictionary *formatOpts = nullptr;
    av_dict_set(&formatOpts, "buffer_size", "1024000", 0);
    av_dict_set(&formatOpts, "stimeout", "20000000", 0);
    av_dict_set(&formatOpts, "max_delay", "30000000", 0);
    av_dict_set(&formatOpts, "rtsp_transport", "tcp", 0);

    do {
        //1.创建封装格式上下文
        m_AVFormatContext = avformat_alloc_context();

        //2.打开文件
        if(avformat_open_input(&m_AVFormatContext, m_Url, nullptr, &formatOpts) != 0)
        {
            LOGCATE("HWCodecPlayer::InitDecoder avformat_open_input fail.");
            break;
        }

        //3.获取音视频流信息
        if(avformat_find_stream_info(m_AVFormatContext, nullptr) < 0) {
            LOGCATE("HWCodecPlayer::InitDecoder avformat_find_stream_info fail.");
            break;
        }

        if(m_AVFormatContext->start_time != AV_NOPTS_VALUE)
            m_StartTime = m_AVFormatContext->start_time / 1000;   // us -> ms
        if(m_AVFormatContext->duration != AV_NOPTS_VALUE)
            m_Duration = av_rescale(m_AVFormatContext->duration, 1000, AV_TIME_BASE);      // us -> ms

        //4.视频流（必须有）：交给 MediaCodec 硬解，不需要打开 FFmpeg 视频解码器
        m_VideoStreamIdx = av_find_best_stream(m_AVFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if(m_VideoStreamIdx < 0) {
            LOGCATE("HWCodecPlayer::InitDecoder video stream not found.");
            break;
        }
        AVStream *videoStream = m_AVFormatContext->streams[m_VideoStreamIdx];
        m_VideoTimeBase = videoStream->time_base;
        m_VideoWidth = videoStream->codecpar->width;
        m_VideoHeight = videoStream->codecpar->height;

        if(InitVideoCodec(videoStream) != 0) {
            LOGCATE("HWCodecPlayer::InitDecoder InitVideoCodec fail.");
            break;
        }

        //5.音频流（可选）：FFmpeg 软解。没有音轨或打开失败时仍然可以只播放画面
        AVCodec *audioCodec = nullptr;
        m_AudioStreamIdx = av_find_best_stream(m_AVFormatContext, AVMEDIA_TYPE_AUDIO, -1, m_VideoStreamIdx, &audioCodec, 0);
        if(m_AudioStreamIdx >= 0 && audioCodec != nullptr) {
            AVStream *audioStream = m_AVFormatContext->streams[m_AudioStreamIdx];
            m_AudioTimeBase = audioStream->time_base;
            m_AudioCodecCtx = avcodec_alloc_context3(audioCodec);
            bool audioOk = m_AudioCodecCtx != nullptr
                    && avcodec_parameters_to_context(m_AudioCodecCtx, audioStream->codecpar) >= 0;
            if(audioOk) {
                m_AudioCodecCtx->pkt_timebase = audioStream->time_base;
                audioOk = avcodec_open2(m_AudioCodecCtx, audioCodec, nullptr) >= 0;
            }

            if(audioOk) {
                // 重采样：输出必须与 AudioTrack 一致（44100Hz/立体声/S16），原来输出的是源采样率，48kHz 片源会变调
                int64_t inLayout = m_AudioCodecCtx->channel_layout != 0
                        ? m_AudioCodecCtx->channel_layout
                        : av_get_default_channel_layout(m_AudioCodecCtx->channels);
                m_SwrCtx = swr_alloc_set_opts(nullptr,
                                              AUDIO_OUT_CHANNEL_LAYOUT, AUDIO_OUT_SAMPLE_FMT, AUDIO_OUT_SAMPLE_RATE,
                                              inLayout, m_AudioCodecCtx->sample_fmt, m_AudioCodecCtx->sample_rate,
                                              0, nullptr);
                audioOk = m_SwrCtx != nullptr && swr_init(m_SwrCtx) >= 0;
            }

            if(!audioOk) {
                LOGCATE("HWCodecPlayer::InitDecoder open audio decoder fail, play video only.");
                avcodec_free_context(&m_AudioCodecCtx);
                swr_free(&m_SwrCtx);
                m_AudioStreamIdx = -1;
            }
        } else {
            LOGCATE("HWCodecPlayer::InitDecoder no audio stream, play video only.");
            m_AudioStreamIdx = -1;
        }

        result = 0;
    } while (false);

    av_dict_free(&formatOpts);

    if(result == 0) {
        PostMessage(this, MSG_DECODER_READY, 0);
        m_VDecodeThread = new thread(VideoDecodeThreadProc, this);
        if(m_AudioStreamIdx >= 0)
            m_ADecodeThread = new thread(AudioDecodeThreadProc, this);
    } else {
        PostMessage(this, MSG_DECODER_INIT_ERROR, 0);
    }

    return result;
}

int HWCodecPlayer::UnInitDecoder() {
    LOGCATE("HWCodecPlayer::UnInitDecoder");
    // 确保阻塞在队列上的解码线程能够退出
    if(m_AudioPacketQueue) m_AudioPacketQueue->Abort();
    if(m_VideoPacketQueue) m_VideoPacketQueue->Abort();

    if(m_ADecodeThread) {
        m_ADecodeThread->join();
        delete m_ADecodeThread;
        m_ADecodeThread = nullptr;
    }

    if(m_VDecodeThread) {
        m_VDecodeThread->join();
        delete m_VDecodeThread;
        m_VDecodeThread = nullptr;
    }

    if(m_AudioPacketQueue) {
        m_AudioPacketQueue->Flush();
    }

    if(m_VideoPacketQueue) {
        m_VideoPacketQueue->Flush();
    }

    av_bsf_free(&m_Bsfc);

    if(m_MediaCodec) {
        AMediaCodec_stop(m_MediaCodec);
        AMediaCodec_delete(m_MediaCodec);
        m_MediaCodec = nullptr;
    }

    // avcodec_free_context 已包含关闭；avformat_close_input 已包含释放上下文
    avcodec_free_context(&m_AudioCodecCtx);
    avformat_close_input(&m_AVFormatContext);
    swr_free(&m_SwrCtx);

    return 0;
}

/**
 * @brief 视频帧的音视频同步（视频向音频同步）
 *
 * - 音频时钟可用：diff = 视频 pts - 音频时钟
 * - 不可用（无音轨、音频已播完、刚 seek）：用系统时钟推算，diff = 视频 pts - (now - sysBase)
 * - diff > 0 视频超前：等待（可被暂停/停止/seek 立即唤醒，且不持有任何队列锁）
 * - diff 严重为负：视频落后太多，丢弃该帧追赶
 */
bool HWCodecPlayer::AVSync(int64_t videoPts, int64_t &sysBase) {
    int64_t now = GetSysCurrentTime();
    double audioClock = m_AudioClock.GetClock();

    int64_t diff;
    if(audioClock >= 0) {
        diff = videoPts - static_cast<int64_t>(audioClock);
        // 同步更新系统时钟参考，音频时钟失效时可以无缝切换
        sysBase = now - static_cast<int64_t>(audioClock);
    } else {
        if(sysBase < 0) sysBase = now - videoPts;
        diff = videoPts - (now - sysBase);
    }
    LOGCATV("HWCodecPlayer::AVSync videoPts=%lld, audioClock=%lf, diff=%lld", (long long)videoPts, audioClock, (long long)diff);

    if(diff > 0) {
        int64_t waitMs = diff > MAX_SYNC_SLEEP_TIME ? MAX_SYNC_SLEEP_TIME : diff;
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_Cond.wait_for(lock, std::chrono::milliseconds(waitMs), [this] {
            return m_PlayerState != PLAYER_STATE_PLAYING || m_SeekPosition >= 0;
        });
        return true;
    }

    return diff >= -AV_SYNC_DROP_THRESHOLD;
}
