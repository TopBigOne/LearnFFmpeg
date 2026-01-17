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

    int           m_nbSamples = 0;              // 重采样后每帧的采样数

    int           m_DstFrameDataSze = 0;        // 重采样后的帧数据大小（字节）
};


#endif //LEARNFFMPEG_AUDIODECODER_H
