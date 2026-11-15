/**
 * 音频播放：音频帧队列 → 重采样 → 音频输出设备，并维护音频时钟（参照 ffplay sdl_audio_callback /
 * audio_decode_frame）。
 *
 * 所有 "取帧 + 重采样 + 填缓冲" 都在设备回调线程里完成，且绝不阻塞：帧队列空了就补静音。
 * 重采样器、当前 PCM 缓冲等状态只在回调线程访问，不需要加锁。
 *
 * 音频时钟：设备在每块数据开始播放时回调 OnChunkStarted，用块首样本的 pts 设置时钟；
 * 静音块（欠载 / 播放结束）则把时钟钉在最后一个真实样本处，视频会等待而不是自顾自往前跑。
 * 设备自身的输出延迟（混音器、DAC）尚未扣除，留给阶段 3 用 Oboe/AAudio 的时间戳校正。
 */

#ifndef LEARNFFMPEG_AUDIOPLAYBACK_H
#define LEARNFFMPEG_AUDIOPLAYBACK_H

#include <atomic>
#include <memory>
#include <core/AVSync.h>
#include <core/FFPtr.h>
#include <core/FrameQueue.h>
#include <core/PacketQueue.h>
#include "AudioOutput.h"

class AudioPlayback : public AudioOutput::Source {
public:
    // 输出格式固定为 44.1kHz / 立体声 / S16，与 AudioGLRender 可视化的数据格式一致
    static const int OUT_SAMPLE_RATE = 44100;
    static const int OUT_CHANNELS = 2;

    AudioPlayback(FrameQueue *sampleQueue, PacketQueue *packetQueue, AVSync *sync);

    ~AudioPlayback() override;

    AudioPlayback(const AudioPlayback &) = delete;
    AudioPlayback &operator=(const AudioPlayback &) = delete;

    /** 打开音频设备（暂停状态），失败返回 false */
    bool Open();

    void Start();

    void Pause();

    /** 关闭设备；返回后不再有回调 */
    void Close();

    /** 所有已解码的音频数据都已播放完（帧队列、本地缓冲、设备队列都没有真实数据） */
    bool IsDrained() const;

private:
    void OnFillBuffer(uint8_t *buffer, int len, AudioOutput::ChunkInfo *info) override;

    void OnChunkStarted(const AudioOutput::ChunkInfo &info, const uint8_t *data, int len, double time) override;

    /**
     * 从帧队列取下一帧并重采样到 m_Buffer
     * @return 得到的字节数；没有可用的帧时返回 -1
     */
    int ResampleNextFrame();

    FrameQueue *m_SampleQueue;
    PacketQueue *m_PacketQueue;
    AVSync *m_Sync;
    std::unique_ptr<AudioOutput> m_Output;
    const int m_BytesPerSecond;

    // 以下只在设备回调线程访问
    SwrContextPtr m_Swr;
    int m_SrcFormat = -1;
    int64_t m_SrcChannelLayout = 0;
    int m_SrcSampleRate = 0;
    uint8_t *m_Buffer = nullptr;            // av_fast_malloc 管理，析构时 av_freep
    unsigned int m_BufferCapacity = 0;
    int m_BufferSize = 0;                   // 有效字节数
    int m_BufferIndex = 0;                  // 已消费字节数
    double m_BufferPts = NAN;               // m_Buffer 首样本的 pts
    int m_BufferSerial = -1;
    double m_LastEndPts = NAN;              // 最近开始播放的数据块中最后一个真实样本之后的 pts
    int m_LastEndSerial = -1;

    std::atomic<bool> m_HasBufferedData{false};
};

#endif //LEARNFFMPEG_AUDIOPLAYBACK_H
