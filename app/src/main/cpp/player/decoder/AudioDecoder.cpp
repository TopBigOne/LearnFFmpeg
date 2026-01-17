/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#include <LogUtil.h>
#include "AudioDecoder.h"

/**
 * @brief 音频解码器准备就绪回调
 *
 * 在解码器初始化完成后被调用，用于：
 * 1. 初始化音频重采样上下文（SwrContext）
 * 2. 配置重采样参数（采样率、通道布局、采样格式等）
 * 3. 初始化音频渲染器
 */
void AudioDecoder::OnDecoderReady() {
    LOGCATE("AudioDecoder::OnDecoderReady");
    if(m_AudioRender) {
        AVCodecContext *codeCtx = GetCodecContext();

        // 分配音频重采样上下文
        m_SwrContext = swr_alloc();

        // 设置输入音频参数（从解码器获取）
        av_opt_set_int(m_SwrContext, "in_channel_layout", codeCtx->channel_layout, 0);  // 输入通道布局
        av_opt_set_int(m_SwrContext, "out_channel_layout", AUDIO_DST_CHANNEL_LAYOUT, 0); // 输出通道布局（立体声）

        av_opt_set_int(m_SwrContext, "in_sample_rate", codeCtx->sample_rate, 0);     // 输入采样率
        av_opt_set_int(m_SwrContext, "out_sample_rate", AUDIO_DST_SAMPLE_RATE, 0);   // 输出采样率（44100Hz）

        av_opt_set_sample_fmt(m_SwrContext, "in_sample_fmt", codeCtx->sample_fmt, 0);  // 输入采样格式
        av_opt_set_sample_fmt(m_SwrContext, "out_sample_fmt", DST_SAMPLT_FORMAT,  0);  // 输出采样格式（S16）

        // 初始化重采样上下文
        swr_init(m_SwrContext);

        LOGCATE("AudioDecoder::OnDecoderReady audio metadata sample rate: %d, channel: %d, format: %d, frame_size: %d, layout: %lld",
             codeCtx->sample_rate, codeCtx->channels, codeCtx->sample_fmt, codeCtx->frame_size,codeCtx->channel_layout);

        // 计算重采样后的参数
        // m_nbSamples: 重采样后每帧的采样数
        m_nbSamples = (int)av_rescale_rnd(ACC_NB_SAMPLES, AUDIO_DST_SAMPLE_RATE, codeCtx->sample_rate, AV_ROUND_UP);
        // m_DstFrameDataSze: 重采样后的帧数据大小（字节）
        m_DstFrameDataSze = av_samples_get_buffer_size(NULL, AUDIO_DST_CHANNEL_COUNTS,m_nbSamples, DST_SAMPLT_FORMAT, 1);

        LOGCATE("AudioDecoder::OnDecoderReady [m_nbSamples, m_DstFrameDataSze]=[%d, %d]", m_nbSamples, m_DstFrameDataSze);

        // 分配音频输出缓冲区
        m_AudioOutBuffer = (uint8_t *) malloc(m_DstFrameDataSze);

        // 初始化音频渲染器
        m_AudioRender->Init();

    } else {
        LOGCATE("AudioDecoder::OnDecoderReady m_AudioRender == null");
    }

}

/**
 * @brief 音频帧可用回调
 * @param frame 解码后的音频帧
 *
 * 当解码器解码出一帧音频数据时被调用，负责：
 * 1. 使用SwrContext进行音频重采样
 * 2. 将重采样后的数据送给音频渲染器播放
 */
void AudioDecoder::OnFrameAvailable(AVFrame *frame) {
    LOGCATE("AudioDecoder::OnFrameAvailable frame=%p, frame->nb_samples=%d", frame, frame->nb_samples);
    if(m_AudioRender) {
        // 进行音频重采样：将解码后的音频转换为目标格式
        // 参数：输出缓冲区、输出缓冲区大小、输入数据、输入采样数
        int result = swr_convert(m_SwrContext, &m_AudioOutBuffer, m_DstFrameDataSze / 2, (const uint8_t **) frame->data, frame->nb_samples);
        if (result > 0 ) {
            // 将重采样后的音频数据送给渲染器播放
            m_AudioRender->RenderAudioFrame(m_AudioOutBuffer, m_DstFrameDataSze);
        }
    }
}

/**
 * @brief 音频解码器完成回调
 *
 * 在解码器停止时被调用，负责释放所有资源
 */
void AudioDecoder::OnDecoderDone() {
    LOGCATE("AudioDecoder::OnDecoderDone");
    // 释放音频渲染器
    if(m_AudioRender)
        m_AudioRender->UnInit();

    // 释放音频输出缓冲区
    if(m_AudioOutBuffer) {
        free(m_AudioOutBuffer);
        m_AudioOutBuffer = nullptr;
    }

    // 释放重采样上下文
    if(m_SwrContext) {
        swr_free(&m_SwrContext);
        m_SwrContext = nullptr;
    }
}

/**
 * @brief 清空音频缓存
 *
 * 在Seek操作时被调用，清空音频渲染器中的缓存数据
 */
void AudioDecoder::ClearCache() {
    if(m_AudioRender)
        m_AudioRender->ClearAudioCache();
}
