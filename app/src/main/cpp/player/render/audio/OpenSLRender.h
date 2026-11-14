//
// Created by 字节流动 on 2020/6/23.
//

#ifndef LEARNFFMPEG_OPENSLRENDER_H
#define LEARNFFMPEG_OPENSLRENDER_H

#include <cstdint>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <queue>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "AudioRender.h"
#include "AudioGLRender.h"

#define MAX_QUEUE_BUFFER_SIZE 3                 // 待播放队列上限，满了生产者（解码线程）阻塞

// 输出格式固定为 44100Hz / 双声道 / S16，与 AudioDecoder 的重采样参数一致
#define OPENSL_SAMPLE_RATE      44100
#define OPENSL_BYTES_PER_SAMPLE 4               // 2 声道 * 2 字节
// 没有数据时送入的静音块：10ms，用来维持 OpenSL 回调链不断
#define SILENCE_BUFFER_SIZE     (OPENSL_SAMPLE_RATE / 100 * OPENSL_BYTES_PER_SAMPLE)

/**
 * OpenSL ES 音频渲染器
 *
 * 数据流：解码线程 RenderAudioFrame() -> m_AudioFrameQueue -> OpenSL 回调线程取出并 Enqueue。
 *
 * 关键点：
 *  1. Enqueue 只是把指针交给 OpenSL，数据在之后才被异步读取，
 *     所以送进去的帧必须保留到下一次回调（表示它已播完）才能释放（m_PlayingFrame）。
 *  2. OpenSL 回调线程绝不能阻塞：队列为空时送一小段静音，保持回调链不断。
 *  3. 当前正在播放的帧的 pts + 已播放时长 = 音频时钟，供视频同步使用。
 */
class OpenSLRender : public AudioRender {
public:
    OpenSLRender(){}
    virtual ~OpenSLRender(){}
    virtual void Init();
    virtual void ClearAudioCache();
    virtual void RenderAudioFrame(uint8_t *pData, int dataSize, int64_t pts);
    virtual void UnInit();
    virtual void Pause();
    virtual void Resume();
    virtual void Abort();
    virtual int64_t GetAudioClock();

private:
    int CreateEngine();
    int CreateOutputMixer();
    int CreateAudioPlayer();
    void HandleAudioFrameQueue();
    void ClearQueueLocked();
    static void AudioPlayerCallback(SLAndroidSimpleBufferQueueItf bufferQueue, void *context);

    SLObjectItf m_EngineObj = nullptr;
    SLEngineItf m_EngineEngine = nullptr;
    SLObjectItf m_OutputMixObj = nullptr;
    SLObjectItf m_AudioPlayerObj = nullptr;
    SLPlayItf m_AudioPlayerPlay = nullptr;
    SLVolumeItf m_AudioPlayerVolume = nullptr;
    SLAndroidSimpleBufferQueueItf m_BufferQueue = nullptr;

    std::queue<AudioFrame *> m_AudioFrameQueue;     // 待播放的帧
    AudioFrame  *m_PlayingFrame = nullptr;          // 已交给 OpenSL、正在播放的帧（下次回调时释放）
    uint8_t      m_SilenceBuffer[SILENCE_BUFFER_SIZE] = {0};

    std::mutex   m_Mutex;
    std::condition_variable m_Cond;                 // 队列有空位时唤醒生产者
    std::atomic<bool> m_Exit{false};

    // 音频时钟（受 m_Mutex 保护）
    int64_t m_ClockPts = -1;                        // 正在播放的帧的 pts（毫秒），-1 表示无效
    int64_t m_ClockUpdateTime = 0;                  // 该帧开始播放时的系统时间
    int64_t m_ClockFrameDuration = 0;               // 该帧时长（毫秒）
    bool    m_Paused = false;
    int64_t m_PauseTime = 0;
};


#endif //LEARNFFMPEG_OPENSLRENDER_H
