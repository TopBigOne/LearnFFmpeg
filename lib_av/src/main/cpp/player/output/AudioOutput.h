/**
 * 音频输出设备接口（拉模式）。
 *
 * 设备线程在需要数据时回调 Source::OnRender，要求以 sampleRate 采样率填满 frames 帧 S16 交错 PCM（不够就补静音，绝不阻塞），
 * 同时告诉 Source 这块数据的第一帧预计在什么系统时刻（Clock::Now() 的时间基准）从扬声器播出。
 * 上层据此设置音频时钟："在系统时刻 presentTime，扬声器正在播放 pts 处的样本"。
 *
 * 预计播出时刻由实现负责估算，已包含设备缓冲、混音器和硬件的输出延迟（能拿到硬件时间戳时以它为准）。
 *
 * 当前实现：OboeAudioOutput（AAudio 优先，低版本由 Oboe 回退 OpenSL ES）。
 */

#ifndef LEARNFFMPEG_AUDIOOUTPUT_H
#define LEARNFFMPEG_AUDIOOUTPUT_H

#include <cstdint>

class AudioOutput {
public:
    class Source {
    public:
        virtual ~Source() = default;

        /**
         * 设备线程：填满 frames 帧（每帧 channels 个 int16），不足部分必须补静音；不能阻塞、不能长时间占用 CPU
         * @param sampleRate  设备当前的采样率。耳机插拔等导致设备重连后可能与 Open 时不同，必须以这里为准
         * @param presentTime buffer 第一帧预计开始播放的系统时刻（秒）
         */
        virtual void OnRender(int16_t *buffer, int frames, int sampleRate, double presentTime) = 0;
    };

    virtual ~AudioOutput() = default;

    /**
     * 打开设备（处于暂停状态，不会回调）
     * @param sampleRate 期望的采样率，0 表示使用设备的原生采样率（延迟和功耗最低，推荐）
     * @param channels   1 或 2
     * @return 成功与否；失败时已释放所有资源
     */
    virtual bool Open(int sampleRate, int channels, Source *source) = 0;

    /** Open 成功时设备的实际采样率；之后设备重连可能变化，以 OnRender 传入的为准 */
    virtual int SampleRate() const = 0;

    virtual void Start() = 0;

    virtual void Pause() = 0;

    /** 停止并释放设备；返回后保证不会再有回调 */
    virtual void Close() = 0;
};

#endif //LEARNFFMPEG_AUDIOOUTPUT_H
