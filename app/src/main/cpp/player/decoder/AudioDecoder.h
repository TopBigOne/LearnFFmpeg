/**
  ____        _             _____ _
 | __ ) _   _| |_ ___      |  ___| | _____      __
 |  _ \| | | | __/ _ \_____| |_  | |/ _ \ \ /\ / /
 | |_) | |_| | ||  __/_____|  _| | | (_) \ V  V /
 |____/ \__, |\__\___|     |_|   |_|\___/ \_/\_/
        |___/
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#ifndef LEARNFFMPEG_AUDIODECODER_H
#define LEARNFFMPEG_AUDIODECODER_H

extern "C" {
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
};

#include <render/audio/AudioRender.h>
#include <SingleAudioRecorder.h>
#include "Decoder.h"
#include "DecoderBase.h"

// 音频目标采样率（44.1kHz，CD音质标准）
static const int AUDIO_DST_SAMPLE_RATE = 44100;
// 音频目标通道数（立体声）
static const int AUDIO_DST_CHANNEL_COUNTS = 2;
// 音频目标声道布局（立体声）
static const uint64_t AUDIO_DST_CHANNEL_LAYOUT = AV_CH_LAYOUT_STEREO;
// 音频编码比特率（64kbps）
static const int AUDIO_DST_BIT_RATE = 64000;
// AAC音频一帧的采样数（1024是AAC标准帧大小）
static const int ACC_NB_SAMPLES = 1024;

/**
 * @brief 音频解码器类
 *
 * 继承自DecoderBase，实现音频解码功能
 * 主要功能：
 * - 使用FFmpeg解码音频流
 * - 使用SwrContext进行音频重采样
 * - 将解码后的音频数据送给AudioRender播放
 *
 * 音频处理流程：
 * 1. 解码音频帧（可能是任意采样率、通道数、采样格式）
 * 2. 重采样为统一格式（44100Hz、立体声、S16格式）
 * 3. 送给AudioRender播放
 */
class AudioDecoder : public DecoderBase{

public:
    /**
     * @brief 构造函数
     * @param url 媒体文件路径
     *
     * 自动初始化音频解码器，指定媒体类型为AVMEDIA_TYPE_AUDIO
     */
    AudioDecoder(char *url){
        Init(url, AVMEDIA_TYPE_AUDIO);
    }

    /**
     * @brief 析构函数
     * 自动释放解码器资源
     */
    virtual ~AudioDecoder(){
        UnInit();
    }

    /**
     * @brief 设置音频渲染器
     * @param audioRender 音频渲染器指针
     *
     * 必须在开始解码前设置，用于接收解码后的音频数据
     */
    void SetAudioRender(AudioRender *audioRender)
    {
        m_AudioRender = audioRender;
    }

    /** 恢复/开始播放：同时恢复声音输出 */
    virtual void Start();

    /** 暂停：同时暂停声音输出，音频时钟随之冻结 */
    virtual void Pause();

    /** 停止：先唤醒可能阻塞在音频队列上的解码线程，否则 join 会卡住 */
    virtual void Stop();

    /** seek 会让解码器恢复运行，声音输出也要一并恢复 */
    virtual void SeekToPosition(float position);

    /**
     * @brief 获取音频时钟（毫秒），作为视频同步的主时钟
     * 签名与 AVSyncCallback 一致，可直接作为回调传给视频解码器
     * @return -1 表示当前不可用
     */
    static long GetAudioClock(void *context);

private:
    /**
     * @brief 解码器准备就绪回调
     * 初始化SwrContext、计算重采样参数、初始化AudioRender
     */
    virtual void OnDecoderReady();

    /**
     * @brief 解码器完成回调
     * 释放SwrContext、AudioRender等资源
     */
    virtual void OnDecoderDone();

    /**
     * @brief 音频帧可用回调
     * @param frame 解码后的音频帧
     *
     * 对音频帧进行重采样，然后送给AudioRender播放
     */
    virtual void OnFrameAvailable(AVFrame *frame);

    /**
     * @brief 清空音频缓存
     * 在Seek操作时调用，清空AudioRender中的缓存数据
     */
    virtual void ClearCache();

    // 目标采样格式（16位有符号整数，最常用的PCM格式）
    const AVSampleFormat DST_SAMPLT_FORMAT = AV_SAMPLE_FMT_S16;

    AudioRender  *m_AudioRender = nullptr;      // 音频渲染器

    SwrContext   *m_SwrContext = nullptr;       // 音频重采样上下文（用于格式转换）

    uint8_t      *m_AudioOutBuffer = nullptr;   // 音频输出缓冲区（存放重采样后的数据）

    int           m_OutBufferSamples = 0;       // 输出缓冲区能容纳的采样数（每声道），不够时按需扩容
};


#endif //LEARNFFMPEG_AUDIODECODER_H
