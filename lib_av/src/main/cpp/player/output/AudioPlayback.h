/**
 * 音频播放：音频帧队列 → 重采样 → 变速（sonic）→ 音频输出设备，并维护音频时钟
 * （参照 ffplay sdl_audio_callback / audio_decode_frame）。
 *
 * 所有 "取帧 + 重采样 + 变速 + 填缓冲" 都在设备回调线程里完成，且绝不阻塞：帧队列空了就补静音。
 * 重采样器、sonic 等状态只在回调线程访问，不需要加锁；其他线程只通过原子变量交互（目标倍速、排空状态）。
 *
 * 输出格式：设备原生采样率（通常 48kHz）/ 立体声 / S16，避免系统混音器再重采样一次。
 *
 * 倍速：解码出的 PCM 全部经过 sonic（1.0x 时 sonic 直接透传，没有额外延迟），变速不变调。
 * sonic 内部会积压一部分数据，输出样本对应的媒体时间按下式推算：
 *     下一个输出样本的 pts = 最后送入样本之后的 pts − 仍在 sonic 里的媒体时长
 * "仍在 sonic 里的媒体时长" = 送入的时长 − 已读出的样本数 × 读出时的倍速 / 采样率。
 * 切换倍速时先把 sonic 里积压的数据按旧速度全部冲出来，这部分输出按旧速度计时。
 *
 * 音频时钟：每次回调时，输出设备给出这块数据的第一帧预计播出的系统时刻（已扣除设备缓冲和硬件延迟），
 * 用这块数据首样本的 pts 和该时刻设置时钟，同时把时钟速度设为这块数据的倍速。
 * 静音块（欠载 / 播放结束）则把时钟钉在最后一个真实样本处，视频会等待而不是自顾自往前跑。
 */

#ifndef LEARNFFMPEG_AUDIOPLAYBACK_H
#define LEARNFFMPEG_AUDIOPLAYBACK_H

#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include <core/AVSync.h>
#include <core/FFPtr.h>
#include <core/FrameQueue.h>
#include <core/PacketQueue.h>
#include "AudioOutput.h"

struct sonicStreamStruct;

class AudioPlayback : public AudioOutput::Source {
public:
    static const int OUT_CHANNELS = 2;

    /** 支持的倍速范围；超出时截断。sonic 本身支持更大范围，但 4 倍以上视频解码基本跟不上 */
    static float ClampSpeed(float speed);

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

    /** 任意线程：设置倍速，下一次设备回调时生效 */
    void SetSpeed(float speed);

    /**
     * 设置 "当前这一段数据是否已经全部解码完" 的查询函数，必须在 Start 之前设置。
     * 播放结束时据此把 sonic 内部积压的最后几十毫秒冲出来，否则尾音会丢，播放也无法判定为完成
     */
    void SetInputFinishedProbe(std::function<bool()> probe);

    /** 所有已解码的音频数据都已播放完（帧队列、sonic、设备缓冲里都没有真实数据） */
    bool IsDrained() const;

private:
    struct SonicDeleter {
        void operator()(sonicStreamStruct *stream) const;
    };

    void OnRender(int16_t *buffer, int frames, int sampleRate, double presentTime) override;

    /** 设备重连后采样率变化：重建重采样器，清空 sonic（丢掉的几十毫秒数据可以接受） */
    void ChangeOutputRate(int sampleRate);

    /** seek 后丢弃 sonic 里旧 serial 的数据 */
    void DiscardSonic();

    /** 目标倍速变化时，先按旧速度冲出 sonic 里的积压数据，再切换 */
    void ApplyTargetSpeed();

    /**
     * 取下一帧（属于 serial 的）重采样后送入 sonic
     * @return 是否送入了数据；帧队列空、或下一帧属于更新的 serial（本次回调之后才处理）时返回 false
     */
    bool FeedNextFrame(int serial);

    /** 从 sonic 读出 frames 个样本后更新 "仍在 sonic 里的媒体时长" */
    void OnSonicOutputConsumed(int frames);

    /** 下一个将从 sonic 读出的样本的 pts，未知时为 NAN */
    double NextOutputPts() const;

    /** 当前（下一个读出样本）对应的倍速 */
    float CurrentOutputSpeed() const;

    /** 音频可视化：攒够固定长度再送给 AudioGLRender（设备回调每次给的帧数很少，直接送会是一条直线） */
    void PushVisualData(const int16_t *data, int frames);

    FrameQueue *m_SampleQueue;
    PacketQueue *m_PacketQueue;
    AVSync *m_Sync;
    std::unique_ptr<AudioOutput> m_Output;
    std::function<bool()> m_InputFinished;

    std::atomic<float> m_TargetSpeed{1.0f};
    std::atomic<bool> m_HasBufferedData{false};     // sonic 里还有未播放的数据
    std::atomic<double> m_DataEndTime{0};           // 最后一个真实样本预计播完的系统时刻

    // 以下只在设备回调线程访问（Open 时在回调开始之前初始化）
    int m_OutRate = 0;
    SwrContextPtr m_Swr;
    int m_SrcFormat = -1;
    int64_t m_SrcChannelLayout = 0;
    int m_SrcSampleRate = 0;
    uint8_t *m_ResampleBuffer = nullptr;            // av_fast_malloc 管理，析构时 av_freep
    unsigned int m_ResampleCapacity = 0;

    std::unique_ptr<sonicStreamStruct, SonicDeleter> m_Sonic;
    float m_SonicSpeed = 1.0f;
    int m_SonicSerial = -1;                         // sonic 中数据所属的 serial，-1 表示空
    bool m_SonicHasInput = false;                   // 送入过数据且还没有 flush
    double m_InputEndPts = NAN;                     // 最后送入 sonic 的样本之后的 pts
    double m_PendingMediaSec = 0;                   // 仍在 sonic 里（含未读出的输出）的媒体时长
    int m_OldSpeedFrames = 0;                       // sonic 输出缓冲里按旧速度生成、尚未读出的样本数
    float m_OldSpeed = 1.0f;

    double m_LastEndPts = NAN;                      // 最近写给设备的最后一个真实样本之后的 pts
    int m_LastEndSerial = -1;
    std::vector<int16_t> m_VisualBuffer;
    size_t m_VisualFill = 0;
};

#endif //LEARNFFMPEG_AUDIOPLAYBACK_H
