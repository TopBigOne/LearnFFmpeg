#include "Player.h"
#include <chrono>
#include <cmath>
#include <LogUtil.h>
#include <core/AVSync.h>
#include <core/FrameQueue.h>
#include <core/PacketQueue.h>
#include <decoder/AudioDecoder.h>
#include <decoder/VideoDecoder.h>
#include <demux/Demuxer.h>
#include <output/AudioPlayback.h>
#include <output/VideoOutput.h>

// Playing 状态下上报进度、检查播放完成的周期
static const std::chrono::milliseconds TICK_INTERVAL(100);

static const char *StateName(PlaybackState state) {
    switch (state) {
        case PlaybackState::Idle: return "Idle";
        case PlaybackState::Preparing: return "Preparing";
        case PlaybackState::Prepared: return "Prepared";
        case PlaybackState::Playing: return "Playing";
        case PlaybackState::Paused: return "Paused";
        case PlaybackState::Completed: return "Completed";
        case PlaybackState::Stopped: return "Stopped";
        case PlaybackState::Error: return "Error";
        case PlaybackState::Released: return "Released";
    }
    return "Unknown";
}

/**
 * 一次 prepare 到 stop/release 之间的全部播放资源。
 *
 * 成员声明顺序即依赖顺序：队列和时钟最先构造、最后析构；
 * 解码器引用 Demuxer 的 AVFormatContext/AVStream，所以 Demuxer 在解码器之前声明。
 * 析构前先 Shutdown()，按 "先打断、再停输出、再停解码、最后停读包" 的顺序回收线程。
 */
struct Player::Session : public Demuxer::Listener, public VideoOutput::Listener {
    Session(Player *owner, int sessionId, const std::atomic<bool> *interrupt)
            : player(owner),
              id(sessionId),
              audioFrames(FrameQueue::SAMPLE_QUEUE_SIZE, false),
              videoFrames(FrameQueue::VIDEO_PICTURE_QUEUE_SIZE, true),
              sync(audioPackets, videoPackets),
              demuxer(interrupt) {
    }

    ~Session() override { Shutdown(); }

    Player *player;
    const int id;
    PacketQueue audioPackets;
    PacketQueue videoPackets;
    FrameQueue audioFrames;
    FrameQueue videoFrames;
    AVSync sync;
    Demuxer demuxer;
    std::unique_ptr<AudioDecoder> audioDecoder;
    std::unique_ptr<VideoDecoder> videoDecoder;
    std::unique_ptr<AudioPlayback> audioPlayback;
    std::unique_ptr<VideoOutput> videoOutput;
    int64_t startTimeUs = 0;
    bool recordingStarted = false;

    void SetPaused(bool paused) {
        if (paused) {
            // 先停声音设备（不再有回调更新音频时钟），再冻结时钟
            if (audioPlayback) audioPlayback->Pause();
            sync.SetPaused(true);
            if (videoOutput) videoOutput->SetPaused(true);
            demuxer.SetPaused(true);
        } else {
            sync.SetPaused(false);
            if (videoOutput) videoOutput->SetPaused(false);
            if (audioPlayback) audioPlayback->Start();
            demuxer.SetPaused(false);
        }
    }

    void Shutdown() {
        // 1. 打断所有阻塞点：读包（interrupt_callback + 条件变量）、取包、等空槽
        demuxer.Abort();
        audioPackets.Abort();
        videoPackets.Abort();
        audioFrames.Abort();
        videoFrames.Abort();
        // 2. 停输出：刷新线程不在任何队列上阻塞；OpenSL Destroy 等待回调返回，回调本身不阻塞
        if (videoOutput) videoOutput->Stop();
        if (audioPlayback) audioPlayback->Close();
        // 3. 停解码线程
        if (audioDecoder) audioDecoder->Stop();
        if (videoDecoder) videoDecoder->Stop();
        // 4. 读包线程
        demuxer.Join();
    }

    // Demuxer::Listener，读包线程调用
    void OnSeekCompleted(int64_t targetUs, bool success) override {
        if (success) {
            // 必须在新数据显示之前对齐外部时钟（ffplay 也在读包线程里做），
            // 否则无音轨时视频会按 seek 前的时钟等待或狂丢帧
            sync.externalClock.Set(targetUs / static_cast<double>(AV_TIME_BASE), 0);
        }
        Command command{CommandType::SeekCompleted};
        command.value64 = targetUs;
        command.success = success;
        command.sessionId = id;
        player->Post(std::move(command));
    }

    void OnDemuxerError(int error) override {
        Command command{CommandType::StreamError};
        command.error = error;
        command.sessionId = id;
        player->Post(std::move(command));
    }

    // VideoOutput::Listener，刷新线程调用
    void OnVideoFrameRendered() override {
        player->Notify(MSG_REQUEST_RENDER, 0);
    }
};

/** FFmpeg 错误码 → 对外的错误类别，同时把原始错误打印出来 */
static int ToMediaError(int ffError) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(ffError, text, sizeof(text));
    LOGCATE("Player error %d (%s)", ffError, text);
    switch (ffError) {
        case AVERROR(ETIMEDOUT):
            return MEDIA_ERROR_TIMED_OUT;
        case AVERROR_INVALIDDATA:
            return MEDIA_ERROR_MALFORMED;
        case AVERROR_DECODER_NOT_FOUND:
        case AVERROR_DEMUXER_NOT_FOUND:
        case AVERROR_PROTOCOL_NOT_FOUND:
        case AVERROR_STREAM_NOT_FOUND:
        case AVERROR_PATCHWELCOME:
            return MEDIA_ERROR_UNSUPPORTED;
        default:
            // 文件不存在、无权限、读写失败、连接断开等都归为 IO 错误
            return ffError < 0 ? MEDIA_ERROR_IO : MEDIA_ERROR_UNKNOWN;
    }
}

static CodecContextPtr OpenCodec(AVStream *stream) {
    AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        LOGCATE("Player OpenCodec no decoder for codec_id=%d", stream->codecpar->codec_id);
        return CodecContextPtr();
    }
    CodecContextPtr ctx(avcodec_alloc_context3(codec));
    if (!ctx || avcodec_parameters_to_context(ctx.get(), stream->codecpar) < 0) return CodecContextPtr();
    // 解码输出的时间戳与流的 time_base 一致
    ctx->pkt_timebase = stream->time_base;
    if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        // 0 = 按 CPU 核数自动决定线程数（帧级 + 片级并行），软解高分辨率视频收益明显
        ctx->thread_count = 0;
    }
    int ret = avcodec_open2(ctx.get(), codec, nullptr);
    if (ret < 0) {
        LOGCATE("Player OpenCodec avcodec_open2 %s fail, ret=%d", codec->name, ret);
        return CodecContextPtr();
    }
    LOGCATI("Player OpenCodec %s", codec->name);
    return ctx;
}

Player::Player(Listener *listener, VideoRender *videoRender)
        : m_Listener(listener), m_VideoRender(videoRender) {
    m_ControlThread = std::thread(&Player::ControlLoop, this);
}

Player::~Player() {
    Release();
}

void Player::SetDataSource(const std::string &url) {
    Command command{CommandType::SetDataSource};
    command.text = url;
    Post(std::move(command));
}

void Player::PrepareAsync() {
    Post(Command{CommandType::Prepare});
}

void Player::Start() {
    Post(Command{CommandType::Start});
}

void Player::Pause() {
    Post(Command{CommandType::Pause});
}

void Player::SeekTo(double positionSec) {
    Command command{CommandType::Seek};
    command.value = positionSec;
    Post(std::move(command));
}

void Player::Stop() {
    Post(Command{CommandType::Stop});
}

void Player::SetRecordPath(const std::string &path) {
    Command command{CommandType::SetRecordPath};
    command.text = path;
    Post(std::move(command));
}

void Player::Release() {
    {
        std::lock_guard<std::mutex> lock(m_CommandMutex);
        if (!m_ReleaseRequested) {
            m_ReleaseRequested = true;
            // 尚未执行的命令都没有意义了，直接丢弃，让 Release 尽快执行
            m_Commands.clear();
            m_Commands.push_back(Command{CommandType::Release});
            m_CommandCond.notify_all();
        }
    }
    // 控制线程可能正阻塞在打开输入 / 探测流信息里，靠 interrupt_callback 打断
    m_Interrupt = true;

    if (m_ControlThread.joinable()) {
        if (m_ControlThread.get_id() == std::this_thread::get_id()) {
            // 不应发生（控制线程不会回调到会调用 Release 的代码），防御性处理，避免自己 join 自己
            LOGCATE("Player::Release called on control thread");
            m_ControlThread.detach();
        } else {
            m_ControlThread.join();
        }
    }
}

void Player::Post(Command command) {
    std::lock_guard<std::mutex> lock(m_CommandMutex);
    if (m_ReleaseRequested) return;
    m_Commands.push_back(std::move(command));
    m_CommandCond.notify_all();
}

void Player::ControlLoop() {
    LOGCATI("Player::ControlLoop start");
    std::unique_lock<std::mutex> lock(m_CommandMutex);
    auto nextTick = std::chrono::steady_clock::now() + TICK_INTERVAL;
    for (;;) {
        if (m_Commands.empty()) {
            if (m_State == PlaybackState::Playing) {
                m_CommandCond.wait_until(lock, nextTick, [this] { return !m_Commands.empty(); });
            } else {
                m_CommandCond.wait(lock, [this] { return !m_Commands.empty(); });
            }
        }

        if (!m_Commands.empty()) {
            Command command = std::move(m_Commands.front());
            m_Commands.pop_front();
            lock.unlock();
            bool quit = HandleCommand(command);
            lock.lock();
            if (quit) break;
        }

        auto now = std::chrono::steady_clock::now();
        if (m_State != PlaybackState::Playing) {
            nextTick = now + TICK_INTERVAL;
        } else if (now >= nextTick) {
            lock.unlock();
            OnTick();
            lock.lock();
            nextTick = now + TICK_INTERVAL;
        }
    }
    LOGCATI("Player::ControlLoop exit");
}

bool Player::HandleCommand(const Command &command) {
    // 工作线程投递的命令可能属于已经销毁的会话（stop 后重新 prepare），直接忽略
    if ((command.type == CommandType::SeekCompleted || command.type == CommandType::StreamError)
        && (!m_Session || m_Session->id != command.sessionId)) {
        return false;
    }

    switch (command.type) {
        case CommandType::SetDataSource:
            m_Url = command.text;
            break;
        case CommandType::Prepare:
            if (m_State == PlaybackState::Idle || m_State == PlaybackState::Stopped) DoPrepare();
            break;
        case CommandType::Start:
            DoStart();
            break;
        case CommandType::Pause:
            DoPause();
            break;
        case CommandType::Seek:
            DoSeek(command.value);
            break;
        case CommandType::Stop:
            DoStop();
            break;
        case CommandType::SetRecordPath:
            m_RecordPath = command.text;
            break;
        case CommandType::SeekCompleted:
            DoSeekCompleted(command.value64, command.success);
            break;
        case CommandType::StreamError:
            DoStreamError(command.error);
            break;
        case CommandType::Release:
            Teardown();
            SetState(PlaybackState::Released);
            return true;
    }
    return false;
}

void Player::DoPrepare() {
    SetState(PlaybackState::Preparing);
    int64_t startMs = GetSysCurrentTime();

    std::unique_ptr<Session> session(new Session(this, m_NextSessionId++, &m_Interrupt));
    int ret = m_Url.empty() ? AVERROR(EINVAL) : session->demuxer.Open(m_Url);
    if (ret >= 0) {
        AVFormatContext *ic = session->demuxer.Context();
        Demuxer *demuxer = &session->demuxer;
        auto wakeDemuxer = [demuxer] { demuxer->WakeUp(); };

        int audioIndex = session->demuxer.AudioStreamIndex();
        if (audioIndex >= 0) {
            AVStream *stream = ic->streams[audioIndex];
            CodecContextPtr ctx = OpenCodec(stream);
            if (ctx) {
                session->audioPlayback.reset(new AudioPlayback(&session->audioFrames, &session->audioPackets,
                                                               &session->sync));
                if (!session->audioPlayback->Open()) {
                    LOGCATE("Player::DoPrepare open audio output fail");
                    session->audioPlayback.reset();
                    ctx.reset();
                }
            }
            if (ctx) {
                session->audioDecoder.reset(new AudioDecoder(std::move(ctx), &session->audioPackets,
                                                             &session->audioFrames, wakeDemuxer));
                // 无法按时间戳 seek 的裸流（如 ADTS AAC）音频包没有 pts，从流起始时间按采样数推算
                if ((ic->iformat->flags & (AVFMT_NOBINSEARCH | AVFMT_NOGENSEARCH | AVFMT_NO_BYTE_SEEK))
                    && !ic->iformat->read_seek) {
                    session->audioDecoder->SetStartPts(stream->start_time, stream->time_base);
                }
            } else {
                // 声音不可用时仍然播放画面，主时钟退回外部时钟
                LOGCATE("Player::DoPrepare audio unavailable, play video only");
                session->demuxer.DisableStream(audioIndex);
            }
        }

        int videoIndex = session->demuxer.VideoStreamIndex();
        if (videoIndex >= 0) {
            AVStream *stream = ic->streams[videoIndex];
            CodecContextPtr ctx = m_VideoRender != nullptr ? OpenCodec(stream) : CodecContextPtr();
            if (ctx) {
                session->videoOutput.reset(new VideoOutput(m_VideoRender, &session->videoFrames,
                                                           &session->videoPackets, &session->sync,
                                                           session->demuxer.MaxFrameDuration(), session.get()));
                if (!session->videoOutput->Init(stream->codecpar->width, stream->codecpar->height)) {
                    session->videoOutput.reset();
                    ctx.reset();
                }
            }
            if (ctx) {
                session->videoDecoder.reset(new VideoDecoder(std::move(ctx), &session->videoPackets,
                                                             &session->videoFrames, wakeDemuxer, ic, stream,
                                                             &session->sync));
                m_VideoWidth = stream->codecpar->width;
                m_VideoHeight = stream->codecpar->height;
            } else {
                LOGCATE("Player::DoPrepare video unavailable");
                session->demuxer.DisableStream(videoIndex);
            }
        }

        if (!session->audioDecoder && !session->videoDecoder) ret = AVERROR_DECODER_NOT_FOUND;
    }

    if (ret < 0) {
        session.reset();
        if (m_Interrupt) return;    // 被 Release 打断，紧接着会执行 Release 命令
        LOGCATE("Player::DoPrepare fail, ret=%d", ret);
        SetState(PlaybackState::Error);
        Notify(MSG_DECODER_INIT_ERROR, static_cast<float>(ToMediaError(ret)));
        return;
    }

    AVFormatContext *ic = session->demuxer.Context();
    session->startTimeUs = ic->start_time != AV_NOPTS_VALUE ? ic->start_time : 0;
    session->sync.SetMaster(session->audioDecoder ? SyncMaster::Audio : SyncMaster::External);
    // 先以暂停状态启动整条流水线：读包、解码提前缓冲，start 时可以立即出画出声
    session->sync.SetPaused(true);
    m_DurationMs = session->demuxer.DurationMs();
    m_PositionSec = 0;

    if (session->audioDecoder) {
        session->audioPackets.Start();
        session->audioDecoder->Start();
    }
    if (session->videoDecoder) {
        session->videoPackets.Start();
        session->videoDecoder->Start();
        session->videoOutput->Start();
    }
    session->demuxer.Start(session->audioDecoder ? &session->audioPackets : nullptr,
                           session->videoDecoder ? &session->videoPackets : nullptr, session.get());
    m_Session = std::move(session);

    LOGCATI("Player::DoPrepare done in %lldms, audio=%d, video=%d, duration=%lldms",
            (long long) (GetSysCurrentTime() - startMs), m_Session->audioDecoder != nullptr,
            m_Session->videoDecoder != nullptr, (long long) m_DurationMs.load());
    SetState(PlaybackState::Prepared);
    Notify(MSG_DECODER_READY, 0);

    if (m_PendingSeekSec >= 0) {
        RequestSeek(m_PendingSeekSec);
        m_PendingSeekSec = -1;
    }
    if (m_PlayWhenReady) Resume();
}

void Player::DoStart() {
    switch (m_State.load()) {
        case PlaybackState::Idle:
        case PlaybackState::Stopped:
            m_PlayWhenReady = true;
            DoPrepare();
            break;
        case PlaybackState::Prepared:
        case PlaybackState::Paused:
            Resume();
            break;
        case PlaybackState::Completed:
            RequestSeek(0);
            Resume();
            break;
        default:
            LOGCATI("Player::DoStart ignored in state %s", StateName(m_State.load()));
            break;
    }
}

void Player::DoPause() {
    m_PlayWhenReady = false;
    if (m_State == PlaybackState::Playing) {
        m_Session->SetPaused(true);
        UpdatePosition();
        SetState(PlaybackState::Paused);
    }
}

void Player::DoSeek(double positionSec) {
    switch (m_State.load()) {
        case PlaybackState::Idle:
        case PlaybackState::Preparing:
        case PlaybackState::Stopped:
            // 还没有输入：记下来，prepare 完成后执行
            m_PendingSeekSec = positionSec < 0 ? 0 : positionSec;
            break;
        case PlaybackState::Prepared:
        case PlaybackState::Playing:
        case PlaybackState::Paused:
            RequestSeek(positionSec);
            break;
        case PlaybackState::Completed:
            // 与旧版行为一致：播完后拖动进度条会继续播放
            RequestSeek(positionSec);
            Resume();
            break;
        default:
            LOGCATI("Player::DoSeek ignored in state %s", StateName(m_State.load()));
            break;
    }
}

void Player::DoStop() {
    m_PlayWhenReady = false;
    m_PendingSeekSec = -1;
    switch (m_State.load()) {
        case PlaybackState::Prepared:
        case PlaybackState::Playing:
        case PlaybackState::Paused:
        case PlaybackState::Completed:
        case PlaybackState::Error:
            Teardown();
            SetState(PlaybackState::Stopped);
            break;
        default:
            break;
    }
}

void Player::DoSeekCompleted(int64_t targetUs, bool success) {
    // 只有最近一次请求完成才结束 "seek 中"：被覆盖的旧请求不会单独完成
    if (m_SeekInFlight && targetUs == m_SeekTargetUs) m_SeekInFlight = false;
    if (!success) LOGCATE("Player::DoSeekCompleted seek fail, keep playing from current position");
    UpdatePosition();
    Notify(MSG_DECODING_TIME, static_cast<float>(m_PositionSec.load()));
}

void Player::DoStreamError(int error) {
    switch (m_State.load()) {
        case PlaybackState::Prepared:
        case PlaybackState::Playing:
        case PlaybackState::Paused:
        case PlaybackState::Completed:
            LOGCATE("Player::DoStreamError error=%d", error);
            Teardown();
            SetState(PlaybackState::Error);
            Notify(MSG_PLAYER_ERROR, static_cast<float>(ToMediaError(error)));
            break;
        default:
            break;
    }
}

void Player::Resume() {
    Session *session = m_Session.get();
    if (session == nullptr) return;
    if (!session->recordingStarted && !m_RecordPath.empty() && session->videoOutput) {
        session->videoOutput->StartRecording(m_RecordPath);
        session->recordingStarted = true;
    }
    m_PlayWhenReady = true;
    session->SetPaused(false);
    SetState(PlaybackState::Playing);
}

void Player::RequestSeek(double positionSec) {
    Session *session = m_Session.get();
    if (session == nullptr) return;
    double durationSec = m_DurationMs.load() / 1000.0;
    if (positionSec < 0) positionSec = 0;
    if (durationSec > 0 && positionSec > durationSec) positionSec = durationSec;

    m_SeekTargetUs = session->startTimeUs + static_cast<int64_t>(positionSec * AV_TIME_BASE);
    m_SeekTargetSec = positionSec;
    m_SeekInFlight = true;
    m_PositionSec = positionSec;
    session->demuxer.RequestSeek(m_SeekTargetUs);
    Notify(MSG_DECODING_TIME, static_cast<float>(positionSec));
}

void Player::Teardown() {
    if (!m_Session) return;
    int64_t startMs = GetSysCurrentTime();
    m_Session->Shutdown();
    m_Session.reset();
    m_SeekInFlight = false;
    LOGCATI("Player::Teardown done in %lldms", (long long) (GetSysCurrentTime() - startMs));
}

void Player::OnTick() {
    if (!m_Session) return;
    UpdatePosition();
    Notify(MSG_DECODING_TIME, static_cast<float>(m_PositionSec.load()));
    CheckCompletion();
}

void Player::UpdatePosition() {
    Session *session = m_Session.get();
    if (session == nullptr) return;
    if (m_SeekInFlight) {
        m_PositionSec = m_SeekTargetSec;
        return;
    }
    double master = session->sync.GetMasterClock();
    // seek 之后、新数据开始播放之前主时钟是 NAN，保持上一次的位置（即 seek 目标）
    if (std::isnan(master)) return;
    double position = master - session->startTimeUs / static_cast<double>(AV_TIME_BASE);
    double durationSec = m_DurationMs.load() / 1000.0;
    if (position < 0) position = 0;
    if (durationSec > 0 && position > durationSec) position = durationSec;
    m_PositionSec = position;
}

void Player::CheckCompletion() {
    Session *session = m_Session.get();
    if (session == nullptr || m_SeekInFlight) return;

    // Finished() 等于包队列当前 serial：这一段的数据已经全部解码（EOS 已排空）
    bool audioDone = !session->audioDecoder
                     || (session->audioDecoder->Finished() == session->audioPackets.Serial()
                         && session->audioPlayback->IsDrained());
    bool videoDone = !session->videoDecoder
                     || (session->videoDecoder->Finished() == session->videoPackets.Serial()
                         && session->videoFrames.NbRemaining() == 0);
    if (!audioDone || !videoDone) return;

    LOGCATI("Player::CheckCompletion playback completed");
    session->SetPaused(true);
    if (m_DurationMs.load() > 0) m_PositionSec = m_DurationMs.load() / 1000.0;
    SetState(PlaybackState::Completed);
    Notify(MSG_DECODING_TIME, static_cast<float>(m_PositionSec.load()));
    Notify(MSG_DECODER_DONE, 0);
}

void Player::SetState(PlaybackState state) {
    PlaybackState old = m_State.exchange(state);
    if (old != state) LOGCATI("Player state %s -> %s", StateName(old), StateName(state));
}

void Player::Notify(int msg, float value) {
    if (m_Listener) m_Listener->OnPlayerEvent(msg, value);
}
