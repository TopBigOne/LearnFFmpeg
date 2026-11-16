#include "AudioPlayback.h"
#include <algorithm>
#include <cstring>
#include <LogUtil.h>
#include <render/audio/AudioGLRender.h>
#include <sonic.h>
#include "OboeAudioOutput.h"

extern "C" {
#include <libavutil/channel_layout.h>
};

static const float MIN_SPEED = 0.25f;
static const float MAX_SPEED = 4.0f;

float AudioPlayback::ClampSpeed(float speed) {
    if (std::isnan(speed)) return 1.0f;
    return std::max(MIN_SPEED, std::min(MAX_SPEED, speed));
}

void AudioPlayback::SonicDeleter::operator()(sonicStreamStruct *stream) const {
    sonicDestroyStream(stream);
}

AudioPlayback::AudioPlayback(FrameQueue *sampleQueue, PacketQueue *packetQueue, AVSync *sync)
        : m_SampleQueue(sampleQueue),
          m_PacketQueue(packetQueue),
          m_Sync(sync) {
}

AudioPlayback::~AudioPlayback() {
    Close();
    av_freep(&m_ResampleBuffer);
}

bool AudioPlayback::Open() {
    // 采样率传 0：使用设备原生采样率，由这里的 swr 一次重采样到位
    std::unique_ptr<AudioOutput> output(new OboeAudioOutput());
    if (!output->Open(0, OUT_CHANNELS, this)) return false;

    m_OutRate = output->SampleRate();
    m_Sonic.reset(sonicCreateStream(m_OutRate, OUT_CHANNELS));
    if (!m_Sonic) {
        LOGCATE("AudioPlayback::Open sonicCreateStream fail");
        output->Close();
        return false;
    }
    m_SonicSpeed = m_TargetSpeed.load();
    sonicSetSpeed(m_Sonic.get(), m_SonicSpeed);
    m_VisualBuffer.assign(VISUAL_BUFFER_SIZE / sizeof(int16_t), 0);
    m_VisualFill = 0;
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

void AudioPlayback::SetSpeed(float speed) {
    m_TargetSpeed.store(ClampSpeed(speed));
}

void AudioPlayback::SetInputFinishedProbe(std::function<bool()> probe) {
    m_InputFinished = std::move(probe);
}

bool AudioPlayback::IsDrained() const {
    return m_SampleQueue->NbRemaining() == 0 && !m_HasBufferedData.load()
           && Clock::Now() >= m_DataEndTime.load();
}

bool AudioPlayback::FeedNextFrame(int serial) {
    for (;;) {
        // 设备线程不能等：没有现成的帧就让调用方补静音
        if (m_SampleQueue->NbRemaining() == 0) return false;
        DecodedFrame *af = m_SampleQueue->Peek();
        if (af->serial != serial) {
            // 比本次回调开始时更新的 serial（回调途中发生了 seek）：留给下一次回调处理，
            // 否则新旧两段数据会混在 sonic 里
            if (af->serial == m_PacketQueue->Serial()) return false;
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
                                                 AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, m_OutRate,
                                                 layout, static_cast<AVSampleFormat>(frame->format),
                                                 frame->sample_rate, 0, nullptr));
            if (!swr || swr_init(swr.get()) < 0) {
                LOGCATE("AudioPlayback::FeedNextFrame swr_init fail, fmt=%d rate=%d ch=%d",
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
        int outCount = static_cast<int>(static_cast<int64_t>(frame->nb_samples) * m_OutRate
                                        / frame->sample_rate + 256);
        int outBytes = av_samples_get_buffer_size(nullptr, OUT_CHANNELS, outCount, AV_SAMPLE_FMT_S16, 0);
        if (outBytes < 0) {
            m_SampleQueue->Next();
            continue;
        }
        // 只在帧变长时才真正分配，稳定播放后不再分配
        av_fast_malloc(&m_ResampleBuffer, &m_ResampleCapacity, static_cast<size_t>(outBytes));
        if (m_ResampleBuffer == nullptr) {
            m_ResampleCapacity = 0;
            m_SampleQueue->Next();
            return false;
        }

        int samples = swr_convert(m_Swr.get(), &m_ResampleBuffer, outCount,
                                  const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
        double pts = af->pts;
        m_SampleQueue->Next();
        if (samples <= 0) {
            if (samples < 0) LOGCATD("AudioPlayback::FeedNextFrame swr_convert fail %d", samples);
            continue;
        }

        if (!sonicWriteShortToStream(m_Sonic.get(), reinterpret_cast<const short *>(m_ResampleBuffer), samples)) {
            LOGCATE("AudioPlayback::FeedNextFrame sonicWriteShortToStream out of memory");
            return false;
        }
        double duration = static_cast<double>(samples) / m_OutRate;
        if (!std::isnan(pts)) {
            m_InputEndPts = pts + duration;
        } else if (!std::isnan(m_InputEndPts)) {
            // 个别帧没有时间戳：按连续数据外推
            m_InputEndPts += duration;
        }
        m_PendingMediaSec += duration;
        m_SonicSerial = serial;
        m_SonicHasInput = true;
        return true;
    }
}

void AudioPlayback::OnSonicOutputConsumed(int frames) {
    int oldFrames = std::min(frames, m_OldSpeedFrames);
    m_OldSpeedFrames -= oldFrames;
    m_PendingMediaSec -= (oldFrames * static_cast<double>(m_OldSpeed)
                          + (frames - oldFrames) * static_cast<double>(m_SonicSpeed)) / m_OutRate;
    // WSOLA 的输出长度不是严格按比例的，累计误差可能让它略小于 0
    if (m_PendingMediaSec < 0) m_PendingMediaSec = 0;
}

double AudioPlayback::NextOutputPts() const {
    return std::isnan(m_InputEndPts) ? NAN : m_InputEndPts - m_PendingMediaSec;
}

float AudioPlayback::CurrentOutputSpeed() const {
    return m_OldSpeedFrames > 0 ? m_OldSpeed : m_SonicSpeed;
}

void AudioPlayback::DiscardSonic() {
    // sonic 没有 "清空" 接口，重设采样率会丢弃缓冲中的全部数据（倍速等设置保留）
    sonicSetSampleRate(m_Sonic.get(), m_OutRate);
    m_SonicSerial = -1;
    m_SonicHasInput = false;
    m_InputEndPts = NAN;
    m_PendingMediaSec = 0;
    m_OldSpeedFrames = 0;
}

void AudioPlayback::ChangeOutputRate(int sampleRate) {
    LOGCATI("AudioPlayback output sample rate %d -> %d", m_OutRate, sampleRate);
    m_OutRate = sampleRate;
    m_Swr.reset();
    DiscardSonic();
}

void AudioPlayback::ApplyTargetSpeed() {
    float target = m_TargetSpeed.load();
    if (target == m_SonicSpeed) return;
    if (m_SonicHasInput) {
        // 积压的输入按旧速度处理完再切换，之前和之后的数据各自按自己的速度计时
        sonicFlushStream(m_Sonic.get());
        m_SonicHasInput = false;
    }
    // 输出缓冲里尚未读出的样本都是按旧速度生成的（连续两次切换时，更早的那部分也近似按这次的旧速度计）
    m_OldSpeedFrames = sonicSamplesAvailable(m_Sonic.get());
    m_OldSpeed = m_SonicSpeed;
    m_SonicSpeed = target;
    sonicSetSpeed(m_Sonic.get(), target);
    LOGCATI("AudioPlayback speed %.2f -> %.2f", m_OldSpeed, target);
}

void AudioPlayback::OnRender(int16_t *buffer, int frames, int sampleRate, double presentTime) {
    if (sampleRate > 0 && sampleRate != m_OutRate) ChangeOutputRate(sampleRate);

    const int serial = m_PacketQueue->Serial();
    // seek 之后：sonic 里还是旧数据，直接丢弃
    if (m_SonicSerial != -1 && m_SonicSerial != serial) DiscardSonic();
    ApplyTargetSpeed();

    sonicStream sonic = m_Sonic.get();
    int written = 0;
    bool hasData = false;
    double chunkPts = NAN;
    float chunkSpeed = m_SonicSpeed;
    while (written < frames) {
        int available = sonicSamplesAvailable(sonic);
        if (available <= 0) {
            if (FeedNextFrame(serial)) continue;
            // 帧队列空了：如果这一段已经全部解码，把 sonic 里积压的尾巴冲出来
            if (m_SonicHasInput && m_InputFinished && m_InputFinished()) {
                sonicFlushStream(sonic);
                m_SonicHasInput = false;
                if (sonicSamplesAvailable(sonic) > 0) continue;
            }
            break;
        }
        if (!hasData) {
            hasData = true;
            chunkPts = NextOutputPts();
            chunkSpeed = CurrentOutputSpeed();
        }
        int read = sonicReadShortFromStream(sonic, buffer + written * OUT_CHANNELS,
                                            std::min(available, frames - written));
        if (read <= 0) break;
        OnSonicOutputConsumed(read);
        written += read;
    }
    if (written < frames) {
        memset(buffer + written * OUT_CHANNELS, 0,
               static_cast<size_t>(frames - written) * OUT_CHANNELS * sizeof(int16_t));
    }

    if (hasData) {
        if (!std::isnan(chunkPts)) {
            m_Sync->audioClock.SetAt(chunkPts, m_SonicSerial, presentTime, chunkSpeed);
            m_Sync->externalClock.SyncToSlave(m_Sync->audioClock);
        }
        m_LastEndPts = NextOutputPts();
        m_LastEndSerial = m_SonicSerial;
        m_DataEndTime.store(presentTime + static_cast<double>(written) / m_OutRate);
        PushVisualData(buffer, written);
    } else if (!std::isnan(m_LastEndPts)) {
        // 欠载或播完：时钟停在最后一个真实样本处，避免主时钟在没有声音时继续走
        m_Sync->audioClock.SetAt(m_LastEndPts, m_LastEndSerial, presentTime);
    }
    m_HasBufferedData.store(m_SonicHasInput || sonicSamplesAvailable(sonic) > 0);
}

void AudioPlayback::PushVisualData(const int16_t *data, int frames) {
    size_t count = static_cast<size_t>(frames) * OUT_CHANNELS;
    while (count > 0 && !m_VisualBuffer.empty()) {
        size_t n = std::min(count, m_VisualBuffer.size() - m_VisualFill);
        memcpy(m_VisualBuffer.data() + m_VisualFill, data, n * sizeof(int16_t));
        m_VisualFill += n;
        data += n;
        count -= n;
        if (m_VisualFill == m_VisualBuffer.size()) {
            // AudioGLRender 会拷贝一份，这里不转移所有权
            AudioFrame visual(reinterpret_cast<uint8_t *>(m_VisualBuffer.data()),
                              static_cast<int>(m_VisualBuffer.size() * sizeof(int16_t)), false);
            AudioGLRender::GetInstance()->UpdateAudioFrame(&visual);
            m_VisualFill = 0;
        }
    }
}
