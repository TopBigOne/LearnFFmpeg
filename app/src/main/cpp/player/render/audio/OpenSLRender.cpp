/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#include <LogUtil.h>
#include <unistd.h>
#include "OpenSLRender.h"

void OpenSLRender::Init() {
    LOGCATE("OpenSLRender::Init");
    m_Exit = false;

    int result = -1;
    do {
        result = CreateEngine();
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::Init CreateEngine fail. result=%d", result);
            break;
        }

        result = CreateOutputMixer();
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::Init CreateOutputMixer fail. result=%d", result);
            break;
        }

        result = CreateAudioPlayer();
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::Init CreateAudioPlayer fail. result=%d", result);
            break;
        }

        result = (*m_AudioPlayerPlay)->SetPlayState(m_AudioPlayerPlay, SL_PLAYSTATE_PLAYING);
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::Init SetPlayState fail. result=%d", result);
            break;
        }

        // 手动触发第一次"回调"，送入一块静音启动回调链；
        // 之后每播完一块，OpenSL 都会回调一次，不再需要额外的等待线程
        HandleAudioFrameQueue();

    } while (false);

    if(result != SL_RESULT_SUCCESS) {
        LOGCATE("OpenSLRender::Init fail. result=%d", result);
        UnInit();
    }

}

void OpenSLRender::RenderAudioFrame(uint8_t *pData, int dataSize, int64_t pts) {
    LOGCATV("OpenSLRender::RenderAudioFrame pData=%p, dataSize=%d, pts=%lld", pData, dataSize, (long long)pts);
    if(m_AudioPlayerPlay == nullptr || pData == nullptr || dataSize <= 0) return;

    std::unique_lock<std::mutex> lock(m_Mutex);
    // 队列满了就阻塞等待（不再 sleep 轮询），音频解码的节奏由声卡消费速度决定
    m_Cond.wait(lock, [this] { return m_AudioFrameQueue.size() < MAX_QUEUE_BUFFER_SIZE || m_Exit; });
    if(m_Exit) return;
    m_AudioFrameQueue.push(new AudioFrame(pData, dataSize, true, pts));
}

void OpenSLRender::ClearQueueLocked() {
    // 注意：不能写成 for(i = 0; i < size(); ++i) pop()，size() 在循环中变小，只能清掉一半
    while(!m_AudioFrameQueue.empty()) {
        delete m_AudioFrameQueue.front();
        m_AudioFrameQueue.pop();
    }
}

void OpenSLRender::UnInit() {
    LOGCATE("OpenSLRender::UnInit");

    if (m_AudioPlayerPlay) {
        (*m_AudioPlayerPlay)->SetPlayState(m_AudioPlayerPlay, SL_PLAYSTATE_STOPPED);
    }

    // 唤醒可能阻塞在 RenderAudioFrame 里的生产者
    Abort();

    // Destroy 会等待正在执行的回调返回，之后不会再有回调，m_PlayingFrame 可以安全释放
    if (m_AudioPlayerObj) {
        (*m_AudioPlayerObj)->Destroy(m_AudioPlayerObj);
        m_AudioPlayerObj = nullptr;
    }

    std::unique_lock<std::mutex> lock(m_Mutex);
    m_AudioPlayerPlay = nullptr;
    m_BufferQueue = nullptr;
    m_AudioPlayerVolume = nullptr;
    ClearQueueLocked();
    if(m_PlayingFrame) {
        delete m_PlayingFrame;
        m_PlayingFrame = nullptr;
    }
    m_ClockPts = -1;
    lock.unlock();

    if (m_OutputMixObj) {
        (*m_OutputMixObj)->Destroy(m_OutputMixObj);
        m_OutputMixObj = nullptr;
    }

    if (m_EngineObj) {
        (*m_EngineObj)->Destroy(m_EngineObj);
        m_EngineObj = nullptr;
        m_EngineEngine = nullptr;
    }

    // 不删除 AudioGLRender 单例（GL 线程可能正在用它绘制），只清掉上一次播放的数据
    AudioGLRender::GetInstance()->ClearAudioData();
}

int OpenSLRender::CreateEngine() {
    SLresult result = SL_RESULT_SUCCESS;
    do {
        result = slCreateEngine(&m_EngineObj, 0, nullptr, 0, nullptr, nullptr);
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::CreateEngine slCreateEngine fail. result=%d", result);
            break;
        }

        result = (*m_EngineObj)->Realize(m_EngineObj, SL_BOOLEAN_FALSE);
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::CreateEngine Realize fail. result=%d", result);
            break;
        }

        result = (*m_EngineObj)->GetInterface(m_EngineObj, SL_IID_ENGINE, &m_EngineEngine);
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::CreateEngine GetInterface fail. result=%d", result);
            break;
        }

    } while (false);
    return result;
}

int OpenSLRender::CreateOutputMixer() {
    SLresult result = SL_RESULT_SUCCESS;
    do {
        const SLInterfaceID mids[1] = {SL_IID_ENVIRONMENTALREVERB};
        const SLboolean mreq[1] = {SL_BOOLEAN_FALSE};

        result = (*m_EngineEngine)->CreateOutputMix(m_EngineEngine, &m_OutputMixObj, 1, mids, mreq);
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::CreateOutputMixer CreateOutputMix fail. result=%d", result);
            break;
        }

        result = (*m_OutputMixObj)->Realize(m_OutputMixObj, SL_BOOLEAN_FALSE);
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::CreateOutputMixer CreateOutputMix fail. result=%d", result);
            break;
        }

    } while (false);

    return result;
}

int OpenSLRender::CreateAudioPlayer() {
    SLDataLocator_AndroidSimpleBufferQueue android_queue = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM pcm = {
            SL_DATAFORMAT_PCM,//format type
            (SLuint32)2,//channel count
            SL_SAMPLINGRATE_44_1,//44100hz
            SL_PCMSAMPLEFORMAT_FIXED_16,// bits per sample
            SL_PCMSAMPLEFORMAT_FIXED_16,// container size
            SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,// channel mask
            SL_BYTEORDER_LITTLEENDIAN // endianness
    };
    SLDataSource slDataSource = {&android_queue, &pcm};

    SLDataLocator_OutputMix outputMix = {SL_DATALOCATOR_OUTPUTMIX, m_OutputMixObj};
    SLDataSink slDataSink = {&outputMix, nullptr};

    const SLInterfaceID ids[3] = {SL_IID_BUFFERQUEUE, SL_IID_EFFECTSEND, SL_IID_VOLUME};
    const SLboolean req[3] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};

    SLresult result;

    do {

        result = (*m_EngineEngine)->CreateAudioPlayer(m_EngineEngine, &m_AudioPlayerObj, &slDataSource, &slDataSink, 3, ids, req);
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::CreateAudioPlayer CreateAudioPlayer fail. result=%d", result);
            break;
        }

        result = (*m_AudioPlayerObj)->Realize(m_AudioPlayerObj, SL_BOOLEAN_FALSE);
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::CreateAudioPlayer Realize fail. result=%d", result);
            break;
        }

        result = (*m_AudioPlayerObj)->GetInterface(m_AudioPlayerObj, SL_IID_PLAY, &m_AudioPlayerPlay);
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::CreateAudioPlayer GetInterface fail. result=%d", result);
            break;
        }

        result = (*m_AudioPlayerObj)->GetInterface(m_AudioPlayerObj, SL_IID_BUFFERQUEUE, &m_BufferQueue);
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::CreateAudioPlayer GetInterface fail. result=%d", result);
            break;
        }

        result = (*m_BufferQueue)->RegisterCallback(m_BufferQueue, AudioPlayerCallback, this);
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::CreateAudioPlayer RegisterCallback fail. result=%d", result);
            break;
        }

        result = (*m_AudioPlayerObj)->GetInterface(m_AudioPlayerObj, SL_IID_VOLUME, &m_AudioPlayerVolume);
        if(result != SL_RESULT_SUCCESS)
        {
            LOGCATE("OpenSLRender::CreateAudioPlayer GetInterface fail. result=%d", result);
            break;
        }

    } while (false);

    return result;
}

void OpenSLRender::HandleAudioFrameQueue() {
    AudioFrame *frameToVisualize = nullptr;
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        if (m_BufferQueue == nullptr || m_Exit) return;

        // 走到这里说明上一块已经播完，现在才可以释放它
        if(m_PlayingFrame) {
            delete m_PlayingFrame;
            m_PlayingFrame = nullptr;
        }

        SLresult result;
        if(!m_AudioFrameQueue.empty()) {
            m_PlayingFrame = m_AudioFrameQueue.front();
            m_AudioFrameQueue.pop();
            result = (*m_BufferQueue)->Enqueue(m_BufferQueue, m_PlayingFrame->data, (SLuint32) m_PlayingFrame->dataSize);

            // 这一帧从现在开始播放：更新音频时钟
            m_ClockPts = m_PlayingFrame->pts;
            m_ClockUpdateTime = GetSysCurrentTime();
            m_ClockFrameDuration = m_PlayingFrame->dataSize * 1000LL / (OPENSL_SAMPLE_RATE * OPENSL_BYTES_PER_SAMPLE);
            frameToVisualize = m_PlayingFrame;

            // 队列腾出了空位，唤醒解码线程
            m_Cond.notify_all();
        } else {
            // 没有数据（还没开始、暂停解码、seek 中、播放结束）：送静音，绝不阻塞回调线程
            result = (*m_BufferQueue)->Enqueue(m_BufferQueue, m_SilenceBuffer, SILENCE_BUFFER_SIZE);
            m_ClockPts = -1;
        }

        if (result != SL_RESULT_SUCCESS) {
            LOGCATE("OpenSLRender::HandleAudioFrameQueue Enqueue fail. result=%d", result);
        }
    }

    // 音频可视化：m_PlayingFrame 只会在本线程的下一次回调中释放，这里在锁外使用是安全的
    if(frameToVisualize)
        AudioGLRender::GetInstance()->UpdateAudioFrame(frameToVisualize);
}

void OpenSLRender::AudioPlayerCallback(SLAndroidSimpleBufferQueueItf bufferQueue, void *context) {
    OpenSLRender *openSlRender = static_cast<OpenSLRender *>(context);
    openSlRender->HandleAudioFrameQueue();
}

void OpenSLRender::ClearAudioCache() {
    std::unique_lock<std::mutex> lock(m_Mutex);
    ClearQueueLocked();
    // seek 后旧的时钟作废，等新数据真正开始播放再生效
    m_ClockPts = -1;
    m_Cond.notify_all();
}

void OpenSLRender::Pause() {
    if(m_AudioPlayerPlay)
        (*m_AudioPlayerPlay)->SetPlayState(m_AudioPlayerPlay, SL_PLAYSTATE_PAUSED);
    std::unique_lock<std::mutex> lock(m_Mutex);
    if(!m_Paused) {
        m_Paused = true;
        m_PauseTime = GetSysCurrentTime();
    }
}

void OpenSLRender::Resume() {
    std::unique_lock<std::mutex> lock(m_Mutex);
    if(m_Paused) {
        m_Paused = false;
        // 暂停期间声卡没有消费数据，时钟也不应前进
        m_ClockUpdateTime += GetSysCurrentTime() - m_PauseTime;
    }
    lock.unlock();
    if(m_AudioPlayerPlay)
        (*m_AudioPlayerPlay)->SetPlayState(m_AudioPlayerPlay, SL_PLAYSTATE_PLAYING);
}

void OpenSLRender::Abort() {
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_Exit = true;
    m_Cond.notify_all();
}

int64_t OpenSLRender::GetAudioClock() {
    std::unique_lock<std::mutex> lock(m_Mutex);
    if(m_ClockPts < 0) return -1;
    int64_t now = m_Paused ? m_PauseTime : GetSysCurrentTime();
    int64_t elapsed = now - m_ClockUpdateTime;
    // 一帧播完之前下一次回调还没到，不让时钟超出这一帧的范围
    if(elapsed > m_ClockFrameDuration) elapsed = m_ClockFrameDuration;
    if(elapsed < 0) elapsed = 0;
    return m_ClockPts + elapsed;
}
