/**
 * 软解播放器内核：状态机 + 控制线程，对外唯一入口。
 *
 * 状态迁移：
 *   Idle / Stopped  --PrepareAsync/Start-->  Preparing
 *   Preparing       --成功-->                Prepared（之前调用过 Start 则直接进入 Playing）
 *   Preparing       --失败-->                Error
 *   Prepared/Paused --Start-->               Playing
 *   Playing         --Pause-->               Paused
 *   Playing         --播放到结尾-->           Completed
 *   Completed       --Start / SeekTo-->      Playing（Start 从头播放）
 *   Prepared/Playing/Paused/Completed --读取错误--> Error
 *   Prepared/Playing/Paused/Completed/Error --Stop--> Stopped
 *   任意状态        --Release-->             Released
 *
 * 线程模型：
 *  - 所有公开接口（Start/Pause/SeekTo/Stop/...）只是把命令投递到控制线程的队列，立即返回；
 *    控制线程串行执行命令，状态只由它修改，因此状态迁移不需要考虑并发；
 *  - 控制线程在 Playing 状态下每 100ms 醒来一次：上报进度、检查是否播放完成；
 *  - Release() 是唯一的同步接口：先置位中断标志（打断可能正在进行的打开 / 读取），
 *    再投递 Release 命令并 join 控制线程，所有工作线程都在控制线程里按顺序停止和回收。
 *  - 事件通过 Listener 回调，调用发生在控制线程或视频刷新线程，实现方不能阻塞（应转投到自己的线程）。
 */

#ifndef LEARNFFMPEG_PLAYER_H
#define LEARNFFMPEG_PLAYER_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <render/video/VideoRender.h>
#include "PlayerMessage.h"

enum class PlaybackState {
    Idle,
    Preparing,
    Prepared,
    Playing,
    Paused,
    Completed,
    Stopped,
    Error,
    Released
};

class Player {
public:
    class Listener {
    public:
        virtual ~Listener() = default;

        /** msg 见 PlayerMessage；在播放器内部线程调用，不能阻塞 */
        virtual void OnPlayerEvent(int msg, float value) = 0;
    };

    /**
     * @param listener    事件接收者，生命周期必须长于 Player
     * @param videoRender 视频渲染器（不持有），生命周期必须长于 Player；可为 nullptr（只播声音）
     */
    Player(Listener *listener, VideoRender *videoRender);

    /** 析构时会自动 Release */
    ~Player();

    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    void SetDataSource(const std::string &url);

    void PrepareAsync();

    /** 开始/恢复播放；Idle/Stopped 时会先 prepare；Completed 时从头播放 */
    void Start();

    void Pause();

    /** 跳转到 positionSec（秒，从 0 开始）；快速连续调用只执行最后一次 */
    void SeekTo(double positionSec);

    /** 停止播放并释放解码资源，之后可以再次 Start（重新 prepare） */
    void Stop();

    /** "边播边录" 输出路径，需在开始播放前设置，仅 ANativeWindow 渲染时生效 */
    void SetRecordPath(const std::string &path);

    /**
     * 同步释放：停止所有线程并回收资源，保证在有限时间内返回；可重复调用。
     * 不能在 Listener 回调里调用（回调线程正是要被回收的线程之一）
     */
    void Release();

    // 以下查询接口可在任意线程调用（读原子变量，不经过控制线程）
    PlaybackState GetState() const { return m_State.load(); }

    int VideoWidth() const { return m_VideoWidth.load(); }

    int VideoHeight() const { return m_VideoHeight.load(); }

    int64_t DurationMs() const { return m_DurationMs.load(); }

    double CurrentPositionSec() const { return m_PositionSec.load(); }

private:
    struct Session;

    enum class CommandType {
        SetDataSource,
        Prepare,
        Start,
        Pause,
        Seek,
        Stop,
        SetRecordPath,
        SeekCompleted,      // 来自读包线程
        StreamError,        // 来自读包线程
        Release
    };

    struct Command {
        explicit Command(CommandType t) : type(t) {}

        CommandType type;
        double value = 0;
        int64_t value64 = 0;
        int error = 0;
        bool success = false;
        int sessionId = 0;      // 来自工作线程的命令所属的会话，过期会话的命令直接忽略
        std::string text;
    };

    void Post(Command command);

    void ControlLoop();

    /** @return true 表示控制线程应退出 */
    bool HandleCommand(const Command &command);

    void DoPrepare();
    void DoStart();
    void DoPause();
    void DoSeek(double positionSec);
    void DoStop();
    void DoSeekCompleted(int64_t targetUs, bool success);
    void DoStreamError(int error);

    /** 启动/恢复 Session 的输出，进入 Playing */
    void Resume();

    void RequestSeek(double positionSec);

    /** 按顺序停止所有工作线程并释放 Session */
    void Teardown();

    /** Playing 状态下的周期任务：上报进度、检查播放完成 */
    void OnTick();

    void UpdatePosition();

    void CheckCompletion();

    void SetState(PlaybackState state);

    void Notify(int msg, float value);

    Listener *m_Listener;
    VideoRender *m_VideoRender;

    std::thread m_ControlThread;
    std::mutex m_CommandMutex;
    std::condition_variable m_CommandCond;
    std::deque<Command> m_Commands;     // 由 m_CommandMutex 保护
    bool m_ReleaseRequested = false;    // 由 m_CommandMutex 保护
    std::atomic<bool> m_Interrupt{false};

    // 以下只在控制线程访问
    std::unique_ptr<Session> m_Session;
    int m_NextSessionId = 1;
    std::string m_Url;
    std::string m_RecordPath;
    bool m_PlayWhenReady = false;
    double m_PendingSeekSec = -1;       // prepare 完成前收到的 seek，<0 表示没有
    bool m_SeekInFlight = false;        // 已交给 Demuxer、尚未完成的 seek
    int64_t m_SeekTargetUs = 0;         // 最近一次 seek 的目标（流绝对时间）
    double m_SeekTargetSec = 0;         // 最近一次 seek 的目标（对外位置）

    std::atomic<PlaybackState> m_State{PlaybackState::Idle};
    std::atomic<int> m_VideoWidth{0};
    std::atomic<int> m_VideoHeight{0};
    std::atomic<int64_t> m_DurationMs{0};
    std::atomic<double> m_PositionSec{0};
};

#endif //LEARNFFMPEG_PLAYER_H
