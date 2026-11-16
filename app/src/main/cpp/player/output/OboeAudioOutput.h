/**
 * 基于 Oboe 的音频输出：Android 8.1+ 走 AAudio，更低版本由 Oboe 内部回退到 OpenSL ES。
 *
 * - 数据回调直接转给 Source::OnRender，本类在回调里不加锁、不分配内存；
 * - 预计播出时刻：AAudio 下用 getTimestamp(CLOCK_MONOTONIC) 拿到 "硬件在某时刻播放到第几帧"，
 *   推算出即将写入的第一帧的播出时刻，已包含混音器和硬件延迟；OpenSL ES 下没有可靠的时间戳，
 *   按设备缓冲区大小估算（低版本设备上音画同步会差几十毫秒）；
 * - 设备断开（插拔耳机、切换蓝牙）时 Oboe 会关闭流并回调 onErrorAfterClose，这里自动以相同参数重新打开，
 *   原来处于播放状态就继续播放。新设备的采样率可能不同，通过 OnRender 的 sampleRate 告诉上层。
 *
 * 线程：Open/Start/Pause/Close 由控制线程调用；数据回调在 Oboe 的音频线程；重连在 Oboe 的错误回调线程。
 * m_Mutex 只保护流的生命周期，不会在数据回调里获取。
 */

#ifndef LEARNFFMPEG_OBOEAUDIOOUTPUT_H
#define LEARNFFMPEG_OBOEAUDIOOUTPUT_H

#include <atomic>
#include <memory>
#include <mutex>
#include <oboe/Oboe.h>
#include "AudioOutput.h"

class OboeAudioOutput : public AudioOutput {
public:
    OboeAudioOutput();

    ~OboeAudioOutput() override;

    OboeAudioOutput(const OboeAudioOutput &) = delete;
    OboeAudioOutput &operator=(const OboeAudioOutput &) = delete;

    bool Open(int sampleRate, int channels, Source *source) override;

    int SampleRate() const override { return m_SampleRate.load(); }

    void Start() override;

    void Pause() override;

    void Close() override;

private:
    /**
     * 交给 Oboe 的回调对象。由流以 shared_ptr 持有：错误回调线程可能在 Close 返回之后才执行完，
     * 所以它不能直接是 OboeAudioOutput（那时可能已被析构），而是通过 Detach 后置空的指针找回宿主。
     */
    class Callback;

    bool OpenStreamLocked();

    oboe::DataCallbackResult Render(oboe::AudioStream *stream, void *audioData, int32_t numFrames);

    void OnStreamClosedByError(oboe::AudioStream *stream, oboe::Result error);

    /** 即将写入的第一帧预计开始播放的系统时刻（秒，与 Clock::Now() 同一时间基准） */
    static double EstimatePresentTime(oboe::AudioStream *stream);

    std::mutex m_Mutex;
    std::shared_ptr<oboe::AudioStream> m_Stream;    // 由 m_Mutex 保护
    std::shared_ptr<Callback> m_Callback;           // 由 m_Mutex 保护
    bool m_Started = false;                         // 由 m_Mutex 保护：期望的播放状态，重连后据此恢复
    bool m_Closed = true;                           // 由 m_Mutex 保护

    Source *m_Source = nullptr;
    int m_RequestedSampleRate = 0;
    int m_Channels = 2;
    std::atomic<int> m_SampleRate{0};
};

#endif //LEARNFFMPEG_OBOEAUDIOOUTPUT_H
