/**
 * 音频输出设备接口（拉模式）。
 *
 * 设备线程在需要数据时回调 Source::OnFillBuffer 填满一块固定大小的 PCM（不够就补静音，绝不阻塞），
 * 并在每一块真正开始播放时回调 Source::OnChunkStarted，上层据此设置音频时钟：
 * "在系统时刻 time，扬声器正在播放 pts 处的样本"。
 *
 * 当前实现是 OpenSL ES（OpenSLAudioOutput）；阶段 3 计划增加 Oboe/AAudio 实现，上层不需要改动。
 * 输出格式固定为 S16 交错。
 */

#ifndef LEARNFFMPEG_AUDIOOUTPUT_H
#define LEARNFFMPEG_AUDIOOUTPUT_H

#include <cmath>
#include <cstdint>

class AudioOutput {
public:
    /** 一块数据的描述，由 Source 在填充时写入，设备在它开始播放时原样交回 */
    struct ChunkInfo {
        bool hasData = false;   // false 表示整块都是静音
        double pts = NAN;       // 块首样本的时间戳（秒）
        double endPts = NAN;    // 最后一个真实样本之后的时间戳（秒）
        int serial = -1;        // 数据所属的包序列号
    };

    class Source {
    public:
        virtual ~Source() = default;

        /** 设备线程：填满 len 字节（不足部分必须补静音），不能阻塞 */
        virtual void OnFillBuffer(uint8_t *buffer, int len, ChunkInfo *info) = 0;

        /** 设备线程：某块数据在系统时刻 time（秒）开始播放 */
        virtual void OnChunkStarted(const ChunkInfo &info, const uint8_t *data, int len, double time) = 0;
    };

    virtual ~AudioOutput() = default;

    /**
     * 打开设备（处于暂停状态）
     * @return 成功与否；失败时已释放所有资源
     */
    virtual bool Open(int sampleRate, int channels, Source *source) = 0;

    virtual void Start() = 0;

    virtual void Pause() = 0;

    /** 停止并释放设备；返回后保证不会再有回调 */
    virtual void Close() = 0;

    /** 已交给设备、尚未播放完的块中含有真实数据的块数（播放完成判断用） */
    virtual int QueuedDataChunks() const = 0;
};

#endif //LEARNFFMPEG_AUDIOOUTPUT_H
