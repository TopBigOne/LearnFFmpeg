/**
 *
 * Created by 公众号：字节流动 on 2021/12/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#include "HWCodecPlayer.h"

/**
 * @brief FFmpeg日志回调函数
 * 将FFmpeg的日志重定向到Android Logcat
 */
void av_log_callback(void*ptr, int level, const char* fmt, va_list vl) {
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
 * @param videoRenderType 视频渲染类型（本类中未使用）
 * @param surface Android Surface对象，用于视频渲染
 */
void HWCodecPlayer::Init(JNIEnv *jniEnv, jobject obj, char *url, int videoRenderType, jobject surface) {
    LOGCATE("HWCodecPlayer::Init");
    // 设置FFmpeg日志级别和回调
    av_log_set_level(AV_LOG_DEBUG);
    av_log_set_callback(av_log_callback);

    // 保存媒体文件路径
    strcpy(m_Url,url);

    // 从Surface创建NativeWindow用于视频渲染
    m_ANativeWindow = ANativeWindow_fromSurface(jniEnv, surface);

    // 创建音视频数据包队列
    m_VideoPacketQueue = new AVPacketQueue();
    m_AudioPacketQueue = new AVPacketQueue();

    // 启动队列
    m_VideoPacketQueue->Start();
    m_AudioPacketQueue->Start();

    // 保存JNI相关对象
    jniEnv->GetJavaVM(&m_JavaVM);
    m_JavaObj = jniEnv->NewGlobalRef(obj);

}

/**
 * @brief 释放播放器资源
 */
void HWCodecPlayer::UnInit() {
    LOGCATE("HWCodecPlayer::UnInit");
    // 先停止播放
    Stop();

    // 等待解封装线程结束
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
    }

    // 删除JNI全局引用
    bool isAttach = false;
    JNIEnv *env = GetJNIEnv(&isAttach);
    env->DeleteGlobalRef(m_JavaObj);
    if(m_AssetMgr)
        env->DeleteGlobalRef(m_AssetMgr);
    if(isAttach)
        GetJavaVM()->DetachCurrentThread();

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
        m_PlayerState = PLAYER_STATE_PLAYING;
        m_Cond.notify_all();  // 唤醒所有等待的线程
    }
}

/**
 * @brief 暂停播放
 */
void HWCodecPlayer::Pause() {
    LOGCATE("HWCodecPlayer::Pause");
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_PlayerState = PLAYER_STATE_PAUSE;
}

/**
 * @brief 停止播放
 */
void HWCodecPlayer::Stop() {
    LOGCATE("HWCodecPlayer::Stop");
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_PlayerState = PLAYER_STATE_STOP;
    m_Cond.notify_all();  // 唤醒所有等待的线程
}

/**
 * @brief 跳转到指定位置
 * @param position 跳转位置（0.0-1.0之间的浮点数）
 */
void HWCodecPlayer::SeekToPosition(float position) {
    LOGCATE("HWCodecPlayer::SeekToPosition position=%f", position);
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_SeekPosition = position;
    m_PlayerState = PLAYER_STATE_PLAYING;
    m_Cond.notify_all();
}

/**
 * @brief 获取媒体参数
 * @param paramType 参数类型
 * @return 参数值
 */
long HWCodecPlayer::GetMediaParams(int paramType) {
    LOGCATE("HWCodecPlayer::GetMediaParams paramType=%d", paramType);
    long value = 0;
    switch(paramType)
    {
        case MEDIA_PARAM_VIDEO_WIDTH:
            value = m_VideoCodecCtx != nullptr ? m_VideoCodecCtx->width : 0;
            break;
        case MEDIA_PARAM_VIDEO_HEIGHT:
            value = m_VideoCodecCtx != nullptr ? m_VideoCodecCtx->height : 0;
            break;
        case MEDIA_PARAM_VIDEO_DURATION:
            value = m_Duration * 1.0f / 1000;  // 毫秒转秒
            break;
    }
    return value;
}

/**
 * @brief 获取JNI环境指针
 * @param isAttach 输出参数，指示当前线程是否被附加到JVM
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
 */
void HWCodecPlayer::PostMessage(void *context, int msgType, float msgCode) {
    if(context != nullptr)
    {
        HWCodecPlayer *player = static_cast<HWCodecPlayer *>(context);
        bool isAttach = false;
        JNIEnv *env = player->GetJNIEnv(&isAttach);
        LOGCATE("HWCodecPlayer::PostMessage env=%p", env);
        if(env == nullptr)
            return;
        jobject javaObj = player->GetJavaObj();
        jmethodID mid = env->GetMethodID(env->GetObjectClass(javaObj), JAVA_PLAYER_EVENT_CALLBACK_API_NAME, "(IF)V");
        env->CallVoidMethod(javaObj, mid, msgType, msgCode);
        if(isAttach)
            player->GetJavaVM()->DetachCurrentThread();

    }
}

/**
 * @brief 解封装线程入口函数
 * @param player HWCodecPlayer实例指针
 *
 * 线程执行流程：
 * 1. 初始化解码器（打开媒体文件、创建解码器等）
 * 2. 执行解封装循环（读取数据包并分发）
 * 3. 释放解码器资源
 * 4. 通知Java层解码完成
 */
void HWCodecPlayer::DeMuxThreadProc(HWCodecPlayer *player) {
    LOGCATE("HWCodecPlayer::DeMuxThreadProc start");
    do {
        // 初始化解码器
        if(player->InitDecoder() != 0) break;
        // 执行解封装循环
        player->DoMuxLoop();
    } while (false);
    // 释放解码器资源
    player->UnInitDecoder();
    // 通知Java层解码完成
    player->PostMessage(player, MSG_DECODER_DONE, 0);
    LOGCATE("HWCodecPlayer::DeMuxThreadProc end");
}

/**
 * @brief 解封装循环
 * @return 0成功
 *
 * 主要功能：
 * 1. 从媒体文件中读取音视频数据包(AVPacket)
 * 2. 将数据包分发到对应的队列（视频队列或音频队列）
 * 3. 处理暂停/停止/Seek等控制逻辑
 * 4. 控制缓冲区大小，防止缓冲过多数据
 */
int HWCodecPlayer::DoMuxLoop() {
    LOGCATE("HWCodecPlayer::DoMuxLoop start");
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_PlayerState = PLAYER_STATE_PLAYING;
        lock.unlock();
    }
    int result = 0;
    AVPacket avPacket = {0};
    for(;;) {
        // 处理暂停状态
        double passTimes = 0;
        while (m_PlayerState == PLAYER_STATE_PAUSE) {
            double lastSysTime = GetSysCurrentTime();
            std::unique_lock<std::mutex> lock(m_Mutex);
            LOGCATE("HWCodecPlayer::DoMuxLoop waiting .......");
            m_Cond.wait_for(lock, std::chrono::milliseconds(10));
            // 计算暂停时间，调整时间基准
            passTimes = GetSysCurrentTime() - lastSysTime;
            m_CommonStartBase += passTimes;
        }

        // 处理停止状态
        if(m_PlayerState == PLAYER_STATE_STOP) {
            break;
        }

        // 处理Seek请求
        if(m_SeekPosition >= 0) {
            LOGCATE("HWCodecPlayer::DoMuxLoop seeking m_SeekPosition=%f", m_SeekPosition);
            // 计算Seek目标时间（转换为微秒）
            int64_t seek_target = static_cast<int64_t>(m_SeekPosition * 1000000);
            int64_t seek_min = INT64_MIN;
            int64_t seek_max = INT64_MAX;
            int seek_ret = avformat_seek_file(m_AVFormatContext, -1, seek_min, seek_target, seek_max, 0);
            LOGCATE("HWCodecPlayer::DoMuxLoop seeking 11 m_SeekPosition=%f", m_SeekPosition);
            if (seek_ret < 0) {
                m_SeekSuccess = false;
                LOGCATE("HWCodecPlayer::DoMuxLoop error while seeking");
            } else {
                std::unique_lock<std::mutex> vBufLock(m_VideoBufMutex);
                LOGCATE("HWCodecPlayer::DoMuxLoop seeking 22 m_SeekPosition=%f", m_SeekPosition);
                avcodec_flush_buffers(m_VideoCodecCtx);
                AMediaCodec_flush(m_MediaCodec);
                m_VideoStartBase = 0;
                m_VideoPacketQueue->Flush();
                vBufLock.unlock();
                LOGCATE("HWCodecPlayer::DoMuxLoop seeking 222 m_SeekPosition=%f", m_SeekPosition);
                unique_lock<mutex> aBufLock(m_AudioBufMutex);
                LOGCATE("HWCodecPlayer::DoMuxLoop seeking 33 m_SeekPosition=%f", m_SeekPosition);
                avcodec_flush_buffers(m_AudioCodecCtx);
                m_AudioStartBase = 0;
                m_AudioPacketQueue->Flush();
                aBufLock.unlock();
                LOGCATE("HWCodecPlayer::DoMuxLoop seeking 333 m_SeekPosition=%f", m_SeekPosition);
                m_SeekPosition = -1;
                //ClearCache();
                m_SeekSuccess = true;
                LOGCATE("HWCodecPlayer::DoMuxLoop seekFrame pos=%f", m_SeekPosition);
            }
        }

        result = av_read_frame(m_AVFormatContext, &avPacket);
        if(result >= 0) {
            double bufferDuration = m_VideoPacketQueue->GetDuration() * av_q2d(m_VideoTimeBase);
            LOGCATE("HWCodecPlayer::DoMuxLoop bufferDuration=%lfs", bufferDuration);
            //防止缓冲数据包过多
            while (BUFF_MAX_VIDEO_DURATION < bufferDuration && m_PlayerState == PLAYER_STATE_PLAYING && m_SeekPosition < 0) {
                bufferDuration = m_VideoPacketQueue->GetDuration() * av_q2d(m_VideoTimeBase);
                usleep(10 * 1000);
            }

            if(avPacket.stream_index == m_VideoStreamIdx) {
                m_VideoPacketQueue->PushPacket(&avPacket);
            } else if(avPacket.stream_index == m_AudioStreamIdx) {
                m_AudioPacketQueue->PushPacket(&avPacket);
            } else {
                av_packet_unref(&avPacket);
            }
        } else {
            //解码结束，暂停解码器
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_PlayerState = PLAYER_STATE_PAUSE;
        }
    }
    LOGCATE("HWCodecPlayer::DoMuxLoop end");
    return 0;
}

#define AUDIO_DST_SAMPLE_RATE 44100
void HWCodecPlayer::AudioDecodeThreadProc(HWCodecPlayer *player) {
    LOGCATE("HWCodecPlayer::AudioDecodeThreadProc start");

    AVPacketQueue* audioPacketQueue = player->m_AudioPacketQueue;
    AVCodecContext* audioCodecCtx = player->m_AudioCodecCtx;
    AVPacket *audioPacket = av_packet_alloc();
    AVFrame *audioFrame = av_frame_alloc();

    uint8_t *audioOutBuffer = (uint8_t *)malloc(AUDIO_DST_SAMPLE_RATE * 2);
    int outChannelNb = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);

    bool isAttach = false;
    JNIEnv *env = player->GetJNIEnv(&isAttach);
    JavaVM *javaVm = player->m_JavaVM;

    jclass activityClass = env->GetObjectClass(player->m_JavaObj);
    jmethodID createTrackMid = env->GetMethodID(activityClass, "createTrack", "(II)V");
    jmethodID playTrackMid = env->GetMethodID(activityClass, "playTrack", "([BI)V");
    env->CallVoidMethod(player->m_JavaObj, createTrackMid, AUDIO_DST_SAMPLE_RATE, outChannelNb);

    for(;;) {
        while (player->m_PlayerState == PLAYER_STATE_PAUSE) {
            std::unique_lock<std::mutex> lock(player->m_Mutex);
            LOGCATE("HWCodecPlayer::AudioDecodeThreadProc waiting .......");
            player->m_Cond.wait_for(lock, std::chrono::milliseconds(10));
        }

        if(player->m_PlayerState == PLAYER_STATE_STOP) {
            break;
        }

        bool isLocked = true;
        std::unique_lock<std::mutex> lock(player->m_AudioBufMutex);
        if(audioPacketQueue->GetPacketSize() == 0) {
            isLocked = false;
            lock.unlock();
        }
        if(audioPacketQueue->GetPacket(audioPacket) < 0)
            break;

        int ret = avcodec_send_packet(audioCodecCtx, audioPacket);
        if (ret < 0) {
            LOGCATE("HWCodecPlayer::AudioDecodeThreadProc Error submitting a av_packet for audio decoding (%s)", av_err2str(ret));
            return;
        }

        while (ret >= 0) {
            ret = avcodec_receive_frame(audioCodecCtx, audioFrame);
            if (ret < 0) {
                LOGCATE("HWCodecPlayer::AudioDecodeThreadProc Error during decoding (%s)", av_err2str(ret));
                break;
            }

            SyncClock* audioClock = &player->m_AudioClock;
            double presentationNano = audioFrame->pts * av_q2d(player->m_AudioTimeBase) * 1000;
            audioClock->SetClock(presentationNano, GetSysCurrentTime());
//            player->AVSync();
//            if(player->m_AudioStartBase <= 0)
//                player->m_AudioStartBase = GetSysCurrentTime() - presentationNano;
//
//            if(player->m_AudioStartBase > player->m_VideoStartBase)
//                player->m_CommonStartBase = player->m_AudioStartBase;
//
//            int delay = player->m_CommonStartBase + presentationNano - GetSysCurrentTime();
//            if(delay > 0) {
//                int sleepMs = delay > MAX_SYNC_SLEEP_TIME ? MAX_SYNC_SLEEP_TIME : delay;
//                usleep(sleepMs * 1000);
//            }

            if(player->m_SeekPosition < 0)
                PostMessage(player, MSG_DECODING_TIME, presentationNano * 1.0f / 1000);
            LOGCATE("HWCodecPlayer::AudioDecodeThreadProc sync audio curPts = %lf", presentationNano);

            swr_convert(player->m_SwrCtx, &audioOutBuffer, AUDIO_DST_SAMPLE_RATE * 2, (const uint8_t **)audioFrame->data, audioFrame->nb_samples);
            int buffer_size = av_samples_get_buffer_size(NULL, outChannelNb, audioFrame->nb_samples, AV_SAMPLE_FMT_S16, 1);
            jbyteArray audio_sample_array = env->NewByteArray(buffer_size);
            env->SetByteArrayRegion(audio_sample_array, 0, buffer_size, (const jbyte *)audioOutBuffer);
            env->CallVoidMethod(player->m_JavaObj, playTrackMid, audio_sample_array, buffer_size);
            env->DeleteLocalRef(audio_sample_array);
            av_frame_unref(audioFrame);
            av_packet_unref(audioPacket);
        }
        if(isLocked) lock.unlock();
    }

    if(audioPacket != nullptr) {
        av_packet_free(&audioPacket);
        audioPacket = nullptr;
    }

    if(audioFrame != nullptr) {
        av_frame_free(&audioFrame);
        audioFrame = nullptr;
    }

    if(audioOutBuffer) {
        free(audioOutBuffer);
    }

    if(isAttach)
        javaVm->DetachCurrentThread();

    LOGCATE("HWCodecPlayer::AudioDecodeThreadProc end");
}

void HWCodecPlayer::VideoDecodeThreadProc(HWCodecPlayer *player) {
    LOGCATE("HWCodecPlayer::VideoDecodeThreadProc start");
    AVPacketQueue* videoPacketQueue = player->m_VideoPacketQueue;
    AMediaCodec* videoCodec = player->m_MediaCodec;
    AVPacket *packet = av_packet_alloc();
    for(;;) {
        while (player->m_PlayerState == PLAYER_STATE_PAUSE) {
            std::unique_lock<std::mutex> lock(player->m_Mutex);
            LOGCATE("HWCodecPlayer::VideoDecodeThreadProc waiting .......");
            player->m_Cond.wait_for(lock, std::chrono::milliseconds(10));
        }

        if(player->m_PlayerState == PLAYER_STATE_STOP) {
            break;
        }

        bool isLocked = true;
        std::unique_lock<std::mutex> lock(player->m_VideoBufMutex);
        if(videoPacketQueue->GetPacketSize() == 0) {
            isLocked = false;
            lock.unlock();
        }
        if(videoPacketQueue->GetPacket(packet) < 0) {
            break;
        }
        LOGCATI("HWCodecPlayer::VideoDecodeThreadProc packetSize=%d, buffTime=%lfs",videoPacketQueue->GetPacketSize(), videoPacketQueue->GetDuration()* av_q2d(player->m_VideoTimeBase));
        ssize_t bufIdx = -1;
        bufIdx = AMediaCodec_dequeueInputBuffer(videoCodec, 0);
        if (bufIdx >= 0) {
            size_t bufSize;
            auto buf = AMediaCodec_getInputBuffer(videoCodec, bufIdx, &bufSize);
            av_bitstream_filter_filter(player->m_Bsfc, player->m_VideoCodecCtx, NULL, &packet->data, &packet->size, packet->data, packet->size,
                                       packet->flags & AV_PKT_FLAG_KEY);
            LOGCATI("HWCodecPlayer::VideoDecodeThreadProc 0x%02X 0x%02X 0x%02X 0x%02X \n",packet->data[0],packet->data[1],packet->data[2],packet->data[3]);
            memcpy(buf, packet->data, packet->size);
            AMediaCodec_queueInputBuffer(videoCodec, bufIdx, 0, packet->size, packet->pts, 0);
        }
        av_packet_unref(packet);
        AMediaCodecBufferInfo info;
        auto status = AMediaCodec_dequeueOutputBuffer(videoCodec, &info, 1000);
        LOGCATI("HWCodecPlayer::VideoDecodeThreadProc status: %d\n", status);
        uint8_t* buffer;
        if (status >= 0) {
            SyncClock* videoClock = &player->m_VideoClock;
            double presentationNano = info.presentationTimeUs * av_q2d(player->m_VideoTimeBase) * 1000;
            videoClock->SetClock(presentationNano, GetSysCurrentTime());
            player->AVSync();
//            if(player->m_VideoStartBase <= 0)
//                player->m_VideoStartBase = GetSysCurrentTime() - presentationNano;
//
//            if(player->m_VideoStartBase > player->m_AudioStartBase)
//                player->m_CommonStartBase = player->m_VideoStartBase;
//
//            int delay = player->m_CommonStartBase + presentationNano - GetSysCurrentTime();
//            if(delay > 0) {
//                int sleepMs = delay > MAX_SYNC_SLEEP_TIME ? MAX_SYNC_SLEEP_TIME : delay;
//                usleep(sleepMs * 1000);
//            }

            size_t size;
            LOGCATI("HWCodecPlayer::VideoDecodeThreadProc sync video curPts = %lf", presentationNano);
            buffer = AMediaCodec_getOutputBuffer(videoCodec, status, &size);
            LOGCATI("HWCodecPlayer::VideoDecodeThreadProc buffer: %p, buffer size: %d", buffer, size);
            AMediaCodec_releaseOutputBuffer(videoCodec, status, info.size != 0);
        } else if (status == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            LOGCATI("HWCodecPlayer::VideoDecodeThreadProc output buffers changed");
        } else if (status == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            LOGCATI("HWCodecPlayer::VideoDecodeThreadProc output format changed");
        } else if (status == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            LOGCATI("HWCodecPlayer::VideoDecodeThreadProc no output buffer right now");
        } else {
            LOGCATI("HWCodecPlayer::VideoDecodeThreadProc unexpected info code: %zd", status);
        }
        if(isLocked) lock.unlock();
    }

    if(packet != nullptr) {
        av_packet_free(&packet);
        packet = nullptr;
    }
    LOGCATE("HWCodecPlayer::VideoDecodeThreadProc end");
}

void HWCodecPlayer::SetMediaParams(int paramType, jobject obj) {
    //MediaPlayer::SetMediaParams(paramType, obj);
    LOGCATE("HWCodecPlayer::SetMediaParams [paramType, obj] = [%d, %p]", paramType, obj);
    switch (paramType) {
        case MEDIA_PARAM_ASSET_MANAGER:
        {
            bool isAttach = false;
            m_AssetMgr = GetJNIEnv(&isAttach)->NewGlobalRef(obj);
            if(isAttach)
                GetJavaVM()->DetachCurrentThread();
        }
            break;
        default:
            break;
    }
}

int HWCodecPlayer::InitDecoder() {
    LOGCATE("HWCodecPlayer::InitDecoder");
    int result = -1;
    do {
        //1.创建封装格式上下文
        m_AVFormatContext = avformat_alloc_context();

        //2.打开文件
        if(avformat_open_input(&m_AVFormatContext, m_Url, nullptr, nullptr) != 0)
        {
            LOGCATE("HWCodecPlayer::InitDecoder avformat_open_input fail.");
            break;
        }

        //3.获取音视频流信息
        if(avformat_find_stream_info(m_AVFormatContext, nullptr) < 0) {
            LOGCATE("HWCodecPlayer::InitDecoder avformat_find_stream_info fail.");
            break;
        }

        //4.获取音视频流索引
        AVCodec *videoCodec = nullptr, *audioCodec = nullptr;
        m_VideoStreamIdx = av_find_best_stream(m_AVFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, &videoCodec, 0);
        if(videoCodec == nullptr) {
            LOGCATE("HWCodecPlayer::InitDecoder video codec not found.");
            break;
        }

        m_AudioStreamIdx = av_find_best_stream(m_AVFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &audioCodec, 0);
        if(audioCodec == nullptr) {
            LOGCATE("HWCodecPlayer::InitDecoder audio codec not found.");
            break;
        }

        if(m_AudioStreamIdx * m_VideoStreamIdx < 0) {
            LOGCATE("HWCodecPlayer::InitDecoder audio or vide stream not found. [m_AudioStreamIdx, m_VideoStreamIdx]=[%d, %d]", m_AudioStreamIdx, m_VideoStreamIdx);
            break;
        }

        m_VideoCodecCtx = avcodec_alloc_context3(videoCodec);
        if(m_VideoCodecCtx) {
            avcodec_parameters_to_context(m_VideoCodecCtx, m_AVFormatContext->streams[m_VideoStreamIdx]->codecpar);
            m_FrameRate = m_AVFormatContext->streams[m_VideoStreamIdx]->r_frame_rate;
        }

        AVDictionary *pAVDictionary = nullptr;
        av_dict_set(&pAVDictionary, "buffer_size", "1024000", 0);
        av_dict_set(&pAVDictionary, "stimeout", "20000000", 0);
        av_dict_set(&pAVDictionary, "max_delay", "30000000", 0);
        av_dict_set(&pAVDictionary, "rtsp_transport", "tcp", 0);

        result = avcodec_open2(m_VideoCodecCtx, videoCodec, &pAVDictionary);
        if(result < 0) {
            LOGCATE("HWCodecPlayer::InitDecoder avcodec_open2 videoCodec fail. result=%d", result);
            break;
        }

        m_AudioCodecCtx = avcodec_alloc_context3(audioCodec);
        if(m_AudioCodecCtx) {
            avcodec_parameters_to_context(m_AudioCodecCtx, m_AVFormatContext->streams[m_AudioStreamIdx]->codecpar);
        }

        result = avcodec_open2(m_AudioCodecCtx, audioCodec, &pAVDictionary);
        if(result < 0) {
            LOGCATE("HWCodecPlayer::InitDecoder avcodec_open2 audioCodec fail. result=%d", result);
            break;
        }

        m_Bsfc = av_bitstream_filter_init("h264_mp4toannexb");
        if(m_Bsfc == nullptr) {
            result = -1;
            LOGCATE("HWCodecPlayer::InitDecoder av_bitstream_filter_init(\"h264_mp4toannexb\") fail.");
            break;
        }

        bool isAttach = false;
        JNIEnv *env = GetJNIEnv(&isAttach);

        off_t outStart, outLen;
        const char *fileName = "byteflow/vr.mp4";
        int fd = AAsset_openFileDescriptor(AAssetManager_open(AAssetManager_fromJava(env, m_AssetMgr), fileName, 0),&outStart, &outLen);

        m_MediaExtractor = AMediaExtractor_new();
        media_status_t err = AMediaExtractor_setDataSourceFd(m_MediaExtractor, fd,
                                                             static_cast<off64_t>(outStart),static_cast<off64_t>(outLen));
        close(fd);
        if (err != AMEDIA_OK) {
            result = -1;
            LOGCATE("HWCodecPlayer::InitDecoder AMediaExtractor_setDataSourceFd fail. err=%d", err);
            break;
        }

        int numTracks = AMediaExtractor_getTrackCount(m_MediaExtractor);

        LOGCATE("HWCodecPlayer::InitDecoder AMediaExtractor_getTrackCount %d tracks", numTracks);
        for (int i = 0; i < numTracks; i++) {
            AMediaFormat *format = AMediaExtractor_getTrackFormat(m_MediaExtractor, i);
            const char *s = AMediaFormat_toString(format);
            LOGCATE("HWCodecPlayer::InitDecoder track %d format: %s", i, s);
            const char *mime;
            if (!AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
                LOGCATE("HWCodecPlayer::InitDecoder no mime type");
                result = -1;
                break;
            } else if (!strncmp(mime, "video/", 6)) {
                // Omitting most error handling for clarity.
                // Production code should check for errors.
                AMediaExtractor_selectTrack(m_MediaExtractor, i);
                m_MediaCodec = AMediaCodec_createDecoderByType(mime);
                AMediaCodec_configure(m_MediaCodec, format, m_ANativeWindow, NULL, 0);
                AMediaCodec_start(m_MediaCodec);
            }
            AMediaFormat_delete(format);
        }

        if (m_MediaCodec == nullptr) {
            LOGCATE("HWCodecPlayer::InitDecoder create media codec fail.");
            result = -1;
            break;
        }

        if(isAttach)
            GetJavaVM()->DetachCurrentThread();

        m_SwrCtx = swr_alloc();
        uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
        enum AVSampleFormat out_format = AV_SAMPLE_FMT_S16;
        int out_sample_rate = m_AudioCodecCtx->sample_rate;
        swr_alloc_set_opts(m_SwrCtx, out_ch_layout, out_format, out_sample_rate, m_AudioCodecCtx->channel_layout,
                           m_AudioCodecCtx->sample_fmt, m_AudioCodecCtx->sample_rate, 0, NULL);
        swr_init(m_SwrCtx);

        m_VideoTimeBase = m_AVFormatContext->streams[m_VideoStreamIdx]->time_base;
        m_AudioTimeBase = m_AVFormatContext->streams[m_AudioStreamIdx]->time_base;

        m_Duration = m_AVFormatContext->duration / AV_TIME_BASE * 1000;//us to ms

        result = 0;
    } while (false);

    if(result == 0) {
        PostMessage(this, MSG_DECODER_READY, 0);
        m_VDecodeThread = new thread(VideoDecodeThreadProc, this);
        m_ADecodeThread = new thread(AudioDecodeThreadProc, this);
    } else {
        PostMessage(this, MSG_DECODER_INIT_ERROR, 0);
    }

    return result;
}

int HWCodecPlayer::UnInitDecoder() {
    LOGCATE("HWCodecPlayer::UnInitDecoder");
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

    if(m_Bsfc) {
        av_bitstream_filter_close(m_Bsfc);
        m_Bsfc = nullptr;
    }

    if(m_MediaCodec) {
        AMediaCodec_stop(m_MediaCodec);
        AMediaCodec_delete(m_MediaCodec);
        m_MediaCodec = nullptr;
    }

    if(m_MediaExtractor) {
        AMediaExtractor_delete(m_MediaExtractor);
        m_MediaExtractor = nullptr;
    }

    if(m_VideoCodecCtx != nullptr) {
        avcodec_close(m_VideoCodecCtx);
        avcodec_free_context(&m_VideoCodecCtx);
        m_VideoCodecCtx = nullptr;
    }

    if(m_AudioCodecCtx != nullptr) {
        avcodec_close(m_AudioCodecCtx);
        avcodec_free_context(&m_AudioCodecCtx);
        m_AudioCodecCtx = nullptr;
    }

    if(m_AVFormatContext != nullptr) {
        avformat_close_input(&m_AVFormatContext);
        avformat_free_context(m_AVFormatContext);
        m_AVFormatContext = nullptr;
    }

    if(m_SwrCtx) {
        swr_close(m_SwrCtx);
        swr_free(&m_SwrCtx);
        m_SwrCtx = nullptr;
    }

    return 0;
}

/**
 * @brief 音视频同步函数
 *
 * 实现视频向音频同步的策略：
 * 1. 计算视频帧之间的时间间隔delay
 * 2. 以音频时钟为参考时钟（主时钟）
 * 3. 计算视频时钟与音频时钟的差值avDiff
 * 4. 根据avDiff调整视频帧显示延迟
 *
 * 同步策略：
 * - 视频慢于音频（avDiff < -threshold）：减小延迟，加快视频播放
 * - 视频快于音频（avDiff > threshold）：增大延迟，减慢视频播放
 * - 差值在阈值内：正常播放
 */
void HWCodecPlayer::AVSync() {
    LOGCATE("HWCodecPlayer::AVSync");
    // 计算两帧之间的时间差
    double delay = m_VideoClock.curPts - m_VideoClock.lastPts;

    // 计算理论帧间隔（根据帧率）
    int tickFrame = 1000 * m_FrameRate.den / m_FrameRate.num;
    LOGCATE("HWCodecPlayer::AVSync tickFrame=%dms", tickFrame);

    // 如果delay异常，使用理论帧间隔
    if(delay <= 0 || delay > VIDEO_FRAME_MAX_DELAY) {
        delay = tickFrame;
    }

    // 获取音频时钟作为参考时钟（视频向音频同步）
    double refClock = m_AudioClock.GetClock();

    // 计算音视频时钟差值（正值表示视频快于音频，负值表示视频慢于音频）
    double avDiff = m_VideoClock.lastPts - refClock;
    m_VideoClock.lastPts = m_VideoClock.curPts;

    // 计算同步阈值
    double syncThreshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
    LOGCATE("HWCodecPlayer::AVSync refClock=%lf, delay=%lf, avDiff=%lf, syncThreshold=%lf", refClock, delay, avDiff, syncThreshold);

    // 根据音视频差值调整延迟
    if(avDiff <= -syncThreshold) {
        // 视频比音频慢，减小延迟，加快视频播放
        delay = FFMAX(0,  delay + avDiff);
    }
    else if(avDiff >= syncThreshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD) {
        // 视频比音频快太多，增大延迟，减慢视频播放
        delay = delay + avDiff;
    }
    else if(avDiff >= syncThreshold) {
        // 视频稍快于音频，加倍延迟
        delay = 2 * delay;
    }

    LOGCATE("HWCodecPlayer::AVSync avDiff=%lf, delay=%lf", avDiff, delay);

    // 计算实际帧间隔（用于微调）
    double tickCur = GetSysCurrentTime();
    double tickDiff =  tickCur - m_VideoClock.frameTimer;  // 两帧实际的时间间隔
    m_VideoClock.frameTimer = tickCur;

    // 根据实际帧间隔进行微调
    if(tickDiff - tickFrame >  5) delay-=5;   // 实际间隔大于理论间隔，减小延迟
    if(tickDiff - tickFrame < -5) delay+=5;   // 实际间隔小于理论间隔，增大延迟

    LOGCATE("HWCodecPlayer::AVSync delay=%lf, tickDiff=%lf", delay, tickDiff);

    // 休眠指定时间，实现同步
    if(delay > 0) {
        usleep(1000 * delay);
    }
}



