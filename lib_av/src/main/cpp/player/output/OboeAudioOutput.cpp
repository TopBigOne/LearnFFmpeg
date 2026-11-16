#include "OboeAudioOutput.h"
#include <algorithm>
#include <ctime>
#include <LogUtil.h>
#include <core/Clock.h>

// 暂停是同步的：返回后不会再有回调去推动音频时钟。超时只是兜底，正常几毫秒内完成
static const int64_t PAUSE_TIMEOUT_NS = 200 * 1000000LL;
// 时间戳推算出的播出时刻与 "当前时刻 + 缓冲时长" 偏差超过它就认为时间戳不可信（刚启动、重连中）
static const double MAX_PRESENT_DELAY = 1.0;
// OpenSL ES 回退路径每次回调的帧数（48kHz 下约 21ms）
static const int OPENSL_FRAMES_PER_CALLBACK = 1024;

class OboeAudioOutput::Callback : public oboe::AudioStreamDataCallback,
                                  public oboe::AudioStreamErrorCallback {
public:
    explicit Callback(OboeAudioOutput *owner) : m_Owner(owner) {}

    /** Close 在流已经停止并关闭之后调用，此后错误回调不再访问宿主 */
    void Detach() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Owner.store(nullptr);
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *stream, void *audioData, int32_t numFrames) override {
        // 数据回调只会发生在流 stop/close 之前，而 Detach 在那之后，这里读到的宿主一定有效
        OboeAudioOutput *owner = m_Owner.load();
        if (owner == nullptr) return oboe::DataCallbackResult::Stop;
        return owner->Render(stream, audioData, numFrames);
    }

    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override {
        // 持锁执行重连：Detach 会等重连结束，宿主不会在重连途中被析构
        std::lock_guard<std::mutex> lock(m_Mutex);
        OboeAudioOutput *owner = m_Owner.load();
        if (owner != nullptr) owner->OnStreamClosedByError(stream, error);
    }

private:
    std::mutex m_Mutex;
    std::atomic<OboeAudioOutput *> m_Owner;
};

OboeAudioOutput::OboeAudioOutput() = default;

OboeAudioOutput::~OboeAudioOutput() {
    Close();
}

bool OboeAudioOutput::Open(int sampleRate, int channels, Source *source) {
    if (channels != 1 && channels != 2) {
        LOGCATE("OboeAudioOutput::Open unsupported channels %d", channels);
        return false;
    }
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Stream) {
        LOGCATE("OboeAudioOutput::Open already opened");
        return false;
    }
    m_Source = source;
    m_RequestedSampleRate = sampleRate;
    m_Channels = channels;
    m_Started = false;
    m_Closed = false;
    m_Callback = std::make_shared<Callback>(this);
    if (!OpenStreamLocked()) {
        m_Callback.reset();
        m_Closed = true;
        return false;
    }
    return true;
}

bool OboeAudioOutput::OpenStreamLocked() {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
            ->setFormat(oboe::AudioFormat::I16)
            ->setChannelCount(m_Channels)
            // 设备不原生支持 S16 / 该声道数时由 Oboe 转换，保证回调拿到的始终是请求的格式
            ->setFormatConversionAllowed(true)
            ->setChannelConversionAllowed(true)
            ->setSharingMode(oboe::SharingMode::Shared)
            // 媒体播放不需要低延迟模式：普通模式的缓冲更大，功耗更低，也更不容易欠载
            ->setPerformanceMode(oboe::PerformanceMode::None)
            ->setUsage(oboe::Usage::Media)
            ->setContentType(oboe::ContentType::Movie)
            ->setDataCallback(m_Callback)
            ->setErrorCallback(m_Callback);
    if (m_RequestedSampleRate > 0) builder.setSampleRate(m_RequestedSampleRate);
    if (!oboe::AudioStreamBuilder::isAAudioRecommended()) {
        // 回退 OpenSL ES 时 Oboe 默认每次回调 192 帧、双缓冲（约 8ms），老设备上很容易欠载断音；
        // 媒体播放不在乎这点延迟（音频时钟会扣除），放大到约 20ms 一次回调
        builder.setFramesPerDataCallback(OPENSL_FRAMES_PER_CALLBACK);
    }

    std::shared_ptr<oboe::AudioStream> stream;
    oboe::Result result = builder.openStream(stream);
    if (result != oboe::Result::OK || !stream) {
        LOGCATE("OboeAudioOutput::OpenStream fail: %s", oboe::convertToText(result));
        return false;
    }
    m_SampleRate = stream->getSampleRate();
    m_Stream = std::move(stream);
    LOGCATI("OboeAudioOutput::OpenStream %s, %dHz %dch, burst=%d, buffer=%d/%d frames",
            oboe::convertToText(m_Stream->getAudioApi()), m_Stream->getSampleRate(),
            m_Stream->getChannelCount(), m_Stream->getFramesPerBurst(),
            m_Stream->getBufferSizeInFrames(), m_Stream->getBufferCapacityInFrames());
    return true;
}

void OboeAudioOutput::Start() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Started = true;
    if (!m_Stream) return;
    oboe::Result result = m_Stream->requestStart();
    if (result != oboe::Result::OK) LOGCATE("OboeAudioOutput::Start fail: %s", oboe::convertToText(result));
}

void OboeAudioOutput::Pause() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Started = false;
    if (!m_Stream) return;
    oboe::Result result = m_Stream->pause(PAUSE_TIMEOUT_NS);
    if (result != oboe::Result::OK) LOGCATE("OboeAudioOutput::Pause fail: %s", oboe::convertToText(result));
}

void OboeAudioOutput::Close() {
    std::shared_ptr<oboe::AudioStream> stream;
    std::shared_ptr<Callback> callback;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        // 置位后错误回调线程不会再重连
        m_Closed = true;
        m_Started = false;
        stream = std::move(m_Stream);
        callback = std::move(m_Callback);
    }
    if (stream) {
        // stop 是同步的：返回时数据回调已经结束，之后不会再有回调
        stream->stop();
        stream->close();
    }
    // 不持 m_Mutex 调用：错误回调线程持有 Callback 的锁时会来获取 m_Mutex
    if (callback) callback->Detach();
}

oboe::DataCallbackResult OboeAudioOutput::Render(oboe::AudioStream *stream, void *audioData, int32_t numFrames) {
    m_Source->OnRender(static_cast<int16_t *>(audioData), numFrames, stream->getSampleRate(),
                       EstimatePresentTime(stream));
    return oboe::DataCallbackResult::Continue;
}

void OboeAudioOutput::OnStreamClosedByError(oboe::AudioStream *stream, oboe::Result error) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    // 已经关闭，或者出错的是已被替换掉的旧流
    if (m_Closed || stream != m_Stream.get()) return;
    LOGCATI("OboeAudioOutput stream closed by error: %s, reopen", oboe::convertToText(error));
    m_Stream.reset();
    if (!OpenStreamLocked()) {
        // 重开失败时没有声音，但不影响画面：音频时钟停止更新后视频会退回按自身节奏播放
        LOGCATE("OboeAudioOutput reopen fail, audio output lost");
        return;
    }
    if (m_Started) m_Stream->requestStart();
}

double OboeAudioOutput::EstimatePresentTime(oboe::AudioStream *stream) {
    const double now = Clock::Now();
    const int sampleRate = stream->getSampleRate();
    if (sampleRate <= 0) return now;

    if (stream->getAudioApi() == oboe::AudioApi::AAudio) {
        // 硬件时间戳：在 timestamp 时刻，第 position 帧正从扬声器播出。
        // 回调期间 getFramesWritten 还不包含本次要写的帧，即它就是本次第一帧的序号
        oboe::ResultWithValue<oboe::FrameTimestamp> ts = stream->getTimestamp(CLOCK_MONOTONIC);
        if (ts) {
            int64_t written = stream->getFramesWritten();
            double time = ts.value().timestamp / 1e9
                          + static_cast<double>(written - ts.value().position) / sampleRate;
            // 早于当前时刻说明设备刚欠载过，这一帧最早也只能现在播出
            if (std::fabs(time - now) < MAX_PRESENT_DELAY) return std::max(time, now);
        }
    }
    // 没有可用的时间戳（OpenSL ES、刚启动）：认为设备缓冲区是满的，本次数据在它播完后播出。
    // OpenSL ES 下不在回调里查询播放位置，部分实现会因此死锁
    return now + static_cast<double>(std::max(stream->getBufferSizeInFrames(), 0)) / sampleRate;
}
