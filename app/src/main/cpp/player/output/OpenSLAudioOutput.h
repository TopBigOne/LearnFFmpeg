/**
 * OpenSL ES 音频输出。
 *
 * 使用 AndroidSimpleBufferQueue，固定 BUFFER_COUNT 块轮转缓冲：
 *  - Open 时先放入 BUFFER_COUNT 块静音启动回调链；
 *  - 每播完一块，OpenSL 回调一次：此时队列里最旧的那块刚开始播放（上报给 Source 设置时钟），
 *    再让 Source 把刚播完的那块重新填满并入队。
 * 数据始终由本类持有的缓冲区承载，Enqueue 之后到下次回调之前不会被改写（修复了旧实现的释放后使用）。
 *
 * 线程：回调在 OpenSL 内部线程；Open/Start/Pause/Close 只由播放器控制线程调用。
 * Destroy 播放器对象会等待正在执行的回调返回，之后不再回调，因此 Close 返回后 Source 可以安全销毁。
 */

#ifndef LEARNFFMPEG_OPENSLAUDIOOUTPUT_H
#define LEARNFFMPEG_OPENSLAUDIOOUTPUT_H

#include <atomic>
#include <vector>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include "AudioOutput.h"

class OpenSLAudioOutput : public AudioOutput {
public:
    OpenSLAudioOutput() = default;

    ~OpenSLAudioOutput() override { Close(); }

    bool Open(int sampleRate, int channels, Source *source) override;

    void Start() override;

    void Pause() override;

    void Close() override;

    int QueuedDataChunks() const override { return m_QueuedDataChunks.load(); }

private:
    // 3 块 × 1024 帧：44.1kHz 下每块约 23ms，总缓冲约 70ms；块数多一些可以抵御回调线程偶发的调度延迟
    static const int BUFFER_COUNT = 3;
    static const int FRAMES_PER_BUFFER = 1024;

    static void BufferQueueCallback(SLAndroidSimpleBufferQueueItf bufferQueue, void *context);

    void OnBufferDone();

    SLObjectItf m_EngineObj = nullptr;
    SLEngineItf m_Engine = nullptr;
    SLObjectItf m_OutputMixObj = nullptr;
    SLObjectItf m_PlayerObj = nullptr;
    SLPlayItf m_Play = nullptr;
    SLAndroidSimpleBufferQueueItf m_BufferQueue = nullptr;

    Source *m_Source = nullptr;
    int m_BufferBytes = 0;
    std::vector<uint8_t> m_Buffers[BUFFER_COUNT];
    ChunkInfo m_ChunkInfos[BUFFER_COUNT];
    int m_FillIndex = 0;                         // 下一个播完的块（即最早入队的块），只在回调线程访问
    std::atomic<int> m_QueuedDataChunks{0};
    std::atomic<bool> m_Closing{false};
    bool m_EnqueueErrorLogged = false;
};

#endif //LEARNFFMPEG_OPENSLAUDIOOUTPUT_H
