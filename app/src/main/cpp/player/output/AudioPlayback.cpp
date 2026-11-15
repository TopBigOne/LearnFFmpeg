#include "AudioPlayback.h"
#include <algorithm>
#include <cstring>
#include <LogUtil.h>
#include <render/audio/AudioGLRender.h>
#include "OpenSLAudioOutput.h"

extern "C" {
#include <libavutil/channel_layout.h>
};

AudioPlayback::AudioPlayback(FrameQueue *sampleQueue, PacketQueue *packetQueue, AVSync *sync)
        : m_SampleQueue(sampleQueue),
          m_PacketQueue(packetQueue),
          m_Sync(sync),
          m_BytesPerSecond(OUT_SAMPLE_RATE * OUT_CHANNELS * 2) {
}

AudioPlayback::~AudioPlayback() {
    Close();
    av_freep(&m_Buffer);
}

bool AudioPlayback::Open() {
    std::unique_ptr<AudioOutput> output(new OpenSLAudioOutput());
    if (!output->Open(OUT_SAMPLE_RATE, OUT_CHANNELS, this)) return false;
    m_Output = std::move(output);
    return true;
}

void AudioPlayback::Start() {
    if (m_Output) m_Output->Start();
}

void AudioPlayback::Pause() {
    if (m_Output) m_Output->Pause();
}

void AudioPlayback::Close() {
    if (m_Output) {
        m_Output->Close();
        m_Output.reset();
        // 可视化单例由 GL 线程持有，不能销毁，只清掉本次播放的数据
        AudioGLRender::GetInstance()->ClearAudioData();
    }
}

bool AudioPlayback::IsDrained() const {
    return m_SampleQueue->NbRemaining() == 0 && !m_HasBufferedData.load()
           && (!m_Output || m_Output->QueuedDataChunks() == 0);
}

int AudioPlayback::ResampleNextFrame() {
    for (;;) {
        // 设备线程不能等：没有现成的帧就让调用方补静音
        if (m_SampleQueue->NbRemaining() == 0) return -1;
        DecodedFrame *af = m_SampleQueue->Peek();
        if (af->serial != m_PacketQueue->Serial()) {
            // seek 之前解出来的旧数据
            m_SampleQueue->Next();
            continue;
        }

        AVFrame *frame = af->frame.get();
        int64_t layout = (frame->channel_layout
                          && frame->channels == av_get_channel_layout_nb_channels(frame->channel_layout))
                         ? static_cast<int64_t>(frame->channel_layout)
                         : av_get_default_channel_layout(frame->channels);

        // 源格式在播放中途可能变化（如部分直播流），变化时重建重采样器
        if (!m_Swr || frame->format != m_SrcFormat || layout != m_SrcChannelLayout
            || frame->sample_rate != m_SrcSampleRate) {
            SwrContextPtr swr(swr_alloc_set_opts(nullptr,
                                                 AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, OUT_SAMPLE_RATE,
                                                 layout, static_cast<AVSampleFormat>(frame->format),
                                                 frame->sample_rate, 0, nullptr));
            if (!swr || swr_init(swr.get()) < 0) {
                LOGCATE("AudioPlayback::ResampleNextFrame swr_init fail, fmt=%d rate=%d ch=%d",
                        frame->format, frame->sample_rate, frame->channels);
                m_Swr.reset();
                m_SampleQueue->Next();
                continue;
            }
            m_Swr = std::move(swr);
            m_SrcFormat = frame->format;
            m_SrcChannelLayout = layout;
            m_SrcSampleRate = frame->sample_rate;
        }

        // 输出容量按采样率比例估算并留出余量（重采样器内部可能还缓存着上一帧的尾巴）
        int outCount = static_cast<int>(static_cast<int64_t>(frame->nb_samples) * OUT_SAMPLE_RATE
                                        / frame->sample_rate + 256);
        int outBytes = av_samples_get_buffer_size(nullptr, OUT_CHANNELS, outCount, AV_SAMPLE_FMT_S16, 0);
        if (outBytes < 0) {
            m_SampleQueue->Next();
            continue;
        }
        av_fast_malloc(&m_Buffer, &m_BufferCapacity, static_cast<size_t>(outBytes));
        if (m_Buffer == nullptr) {
            m_SampleQueue->Next();
            return -1;
        }

        int samples = swr_convert(m_Swr.get(), &m_Buffer, outCount,
                                  const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
        double pts = af->pts;
        int serial = af->serial;
        m_SampleQueue->Next();
        if (samples <= 0) {
            if (samples < 0) LOGCATD("AudioPlayback::ResampleNextFrame swr_convert fail %d", samples);
            continue;
        }

        m_BufferSize = samples * OUT_CHANNELS * 2;
        m_BufferIndex = 0;
        m_BufferPts = pts;
        m_BufferSerial = serial;
        return m_BufferSize;
    }
}

void AudioPlayback::OnFillBuffer(uint8_t *buffer, int len, AudioOutput::ChunkInfo *info) {
    int written = 0;
    const int serial = m_PacketQueue->Serial();
    while (written < len) {
        if (m_BufferIndex >= m_BufferSize || m_BufferSerial != serial) {
            // 当前缓冲用完，或者是 seek 之前的数据：取下一帧
            m_BufferIndex = m_BufferSize = 0;
            if (ResampleNextFrame() < 0) break;
        }
        if (!info->hasData) {
            info->hasData = true;
            info->serial = m_BufferSerial;
            info->pts = std::isnan(m_BufferPts) ? NAN
                        : m_BufferPts + static_cast<double>(m_BufferIndex) / m_BytesPerSecond;
        } else if (m_BufferSerial != info->serial) {
            // 一块数据只属于一个 serial，时钟才能正确换代；剩余部分补静音
            break;
        }
        int bytes = std::min(len - written, m_BufferSize - m_BufferIndex);
        memcpy(buffer + written, m_Buffer + m_BufferIndex, static_cast<size_t>(bytes));
        written += bytes;
        m_BufferIndex += bytes;
    }

    if (written < len) memset(buffer + written, 0, static_cast<size_t>(len - written));
    if (info->hasData && !std::isnan(info->pts))
        info->endPts = info->pts + static_cast<double>(written) / m_BytesPerSecond;
    m_HasBufferedData = m_BufferIndex < m_BufferSize;
}

void AudioPlayback::OnChunkStarted(const AudioOutput::ChunkInfo &info, const uint8_t *data, int len,
                                   double time) {
    if (info.hasData) {
        if (!std::isnan(info.pts)) {
            m_Sync->audioClock.SetAt(info.pts, info.serial, time);
            m_Sync->externalClock.SyncToSlave(m_Sync->audioClock);
        }
        m_LastEndPts = info.endPts;
        m_LastEndSerial = info.serial;

        // 音频可视化（AudioVisualMediaPlayerActivity），它会拷贝一份数据
        AudioFrame visual(const_cast<uint8_t *>(data), len, false);
        AudioGLRender::GetInstance()->UpdateAudioFrame(&visual);
    } else if (!std::isnan(m_LastEndPts)) {
        // 欠载或播完：时钟停在最后一个真实样本处，避免主时钟在没有声音时继续走
        m_Sync->audioClock.SetAt(m_LastEndPts, m_LastEndSerial, time);
    }
}
