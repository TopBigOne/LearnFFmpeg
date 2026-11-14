/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#include <LogUtil.h>
#include "AudioDecoder.h"

void AudioDecoder::Start() {
    DecoderBase::Start();
    if(m_AudioRender)
        m_AudioRender->Resume();
}

void AudioDecoder::Pause() {
    DecoderBase::Pause();
    if(m_AudioRender)
        m_AudioRender->Pause();
}

void AudioDecoder::Stop() {
    DecoderBase::Stop();
    // 解码线程可能正阻塞在 RenderAudioFrame（队列满且声音输出已暂停），必须把它唤醒
    if(m_AudioRender)
        m_AudioRender->Abort();
}

void AudioDecoder::SeekToPosition(float position) {
    DecoderBase::SeekToPosition(position);
    if(m_AudioRender)
        m_AudioRender->Resume();
}

long AudioDecoder::GetAudioClock(void *context) {
    AudioDecoder *decoder = static_cast<AudioDecoder *>(context);
    if(decoder == nullptr || decoder->m_AudioRender == nullptr) return -1;
    return static_cast<long>(decoder->m_AudioRender->GetAudioClock());
}

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
    if(m_AudioRender == nullptr) {
        LOGCATE("AudioDecoder::OnDecoderReady m_AudioRender == null");
        return;
    }

    AVCodecContext *codeCtx = GetCodecContext();

    // 部分文件（如 wav、某些 mp3）没有声明 channel_layout，此时按声道数取默认布局，否则 swr_init 会失败
    int64_t inChannelLayout = codeCtx->channel_layout != 0
            ? codeCtx->channel_layout
            : av_get_default_channel_layout(codeCtx->channels);

    // 一次性设置输出/输入参数：输出固定为 44100Hz / 立体声 / S16，与 OpenSL 播放器的配置一致
    m_SwrContext = swr_alloc_set_opts(nullptr,
                                      AUDIO_DST_CHANNEL_LAYOUT, DST_SAMPLT_FORMAT, AUDIO_DST_SAMPLE_RATE,
                                      inChannelLayout, codeCtx->sample_fmt, codeCtx->sample_rate,
                                      0, nullptr);

    // 初始化重采样上下文，失败则不播放声音（视频会自动退回系统时钟同步）
    if(m_SwrContext == nullptr || swr_init(m_SwrContext) < 0) {
        LOGCATE("AudioDecoder::OnDecoderReady swr_init fail.");
        swr_free(&m_SwrContext);
        return;
    }

    LOGCATE("AudioDecoder::OnDecoderReady audio metadata sample rate: %d, channel: %d, format: %d, frame_size: %d, layout: %lld",
            codeCtx->sample_rate, codeCtx->channels, codeCtx->sample_fmt, codeCtx->frame_size, (long long)inChannelLayout);

    // 初始化音频渲染器
    m_AudioRender->Init();
}

/**
 * @brief 音频帧可用回调
 * @param frame 解码后的音频帧
 *
 * 1. 使用SwrContext进行音频重采样
 * 2. 将重采样后的数据（以及它的时间戳）送给音频渲染器播放
 */
void AudioDecoder::OnFrameAvailable(AVFrame *frame) {
    LOGCATV("AudioDecoder::OnFrameAvailable frame=%p, frame->nb_samples=%d", frame, frame->nb_samples);
    if(m_AudioRender == nullptr || m_SwrContext == nullptr) return;

    // 本次最多会输出多少个采样：与输入采样数、采样率比例、重采样器内部缓存都有关。
    // 不能假设每帧固定 1024 个采样（AAC 是 1024，MP3 是 1152，其他格式各不相同）
    int maxOutSamples = swr_get_out_samples(m_SwrContext, frame->nb_samples);
    if(maxOutSamples > m_OutBufferSamples) {
        free(m_AudioOutBuffer);
        m_OutBufferSamples = maxOutSamples;
        int bufferSize = av_samples_get_buffer_size(NULL, AUDIO_DST_CHANNEL_COUNTS, m_OutBufferSamples, DST_SAMPLT_FORMAT, 1);
        m_AudioOutBuffer = (uint8_t *) malloc(bufferSize);
    }

    // 进行音频重采样：out_count 是"每声道采样数"，必须是缓冲区真实能容纳的数量
    int outSamples = swr_convert(m_SwrContext, &m_AudioOutBuffer, m_OutBufferSamples,
                                 (const uint8_t **) frame->extended_data, frame->nb_samples);
    if (outSamples > 0) {
        // 按实际输出的采样数计算字节数，不能用固定大小，否则会把缓冲区里的旧数据也播出去
        int dataSize = av_samples_get_buffer_size(NULL, AUDIO_DST_CHANNEL_COUNTS, outSamples, DST_SAMPLT_FORMAT, 1);
        m_AudioRender->RenderAudioFrame(m_AudioOutBuffer, dataSize, GetCurrentTimeStamp());
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
        m_OutBufferSamples = 0;
    }

    // 释放重采样上下文（swr_free 会把指针置空）
    swr_free(&m_SwrContext);
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
