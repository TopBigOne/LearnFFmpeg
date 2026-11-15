/**
 * 解复用器：播放器里唯一的 AVFormatContext 和唯一的读包线程（参照 ffplay read_thread）。
 *
 * - Open() 在调用线程（播放器控制线程）里同步打开输入、探测流信息、选择音视频流；
 * - Start() 启动读包线程，按 stream_index 把包分发到音频/视频 PacketQueue；
 * - 缓冲足够（总字节数超过上限，或每个流都缓冲了 1 秒以上）时在条件变量上等待，
 *   解码线程发现队列空了会调用 WakeUp() 让它立即继续读；
 * - seek 只记录请求并唤醒读包线程，由读包线程自己执行 avformat_seek_file，
 *   成功后 Flush 各个包队列（serial + 1）。读包线程忙时连续到来的请求会互相覆盖，只执行最后一次；
 * - 所有阻塞的 FFmpeg 调用都受 interrupt_callback 控制：Abort()/外部释放标志立即打断，
 *   每次操作还有超时（网络卡死时不会永远阻塞）。
 */

#ifndef LEARNFFMPEG_DEMUXER_H
#define LEARNFFMPEG_DEMUXER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <core/FFPtr.h>
#include <core/PacketQueue.h>

class Demuxer {
public:
    /** 读包线程的事件回调，实现方不能在回调里阻塞（应投递到自己的线程处理） */
    class Listener {
    public:
        virtual ~Listener() = default;

        /**
         * seek 执行完毕（无论成功与否都会回调，保证上层的 "seek 中" 状态能结束）
         * @param targetSec 目标位置（秒，流的绝对时间）
         * @param success   失败时包队列没有被清空，播放从原位置继续
         */
        virtual void OnSeekCompleted(double targetSec, bool success) = 0;

        /** 不可恢复的读取错误（IO 错误、超时），之后读包线程退出 */
        virtual void OnDemuxerError(int error) = 0;
    };

    /**
     * @param externalAbort 播放器的释放标志：释放时即使控制线程正阻塞在 Open() 里也能立即打断
     */
    explicit Demuxer(const std::atomic<bool> *externalAbort);

    ~Demuxer();

    Demuxer(const Demuxer &) = delete;
    Demuxer &operator=(const Demuxer &) = delete;

    /**
     * 打开输入并选择流（阻塞，可被打断）
     * @return 0 成功；AVERROR 错误码（被打断时为 AVERROR_EXIT）
     */
    int Open(const std::string &url);

    AVFormatContext *Context() const { return m_FormatCtx.get(); }

    /** 选中的流索引，没有则为 -1 */
    int AudioStreamIndex() const { return m_AudioIndex; }
    int VideoStreamIndex() const { return m_VideoIndex; }

    /** 不再分发该流的包（解码器打开失败时调用，必须在 Start 之前） */
    void DisableStream(int streamIndex);

    /** 总时长（毫秒），未知（直播）为 0 */
    int64_t DurationMs() const;

    /** 文件起始时间（秒），对外的播放位置都以它为 0 点 */
    double StartTimeSec() const;

    /** 相邻两帧 pts 差超过该值（秒）视为时间戳跳变（ffplay max_frame_duration） */
    double MaxFrameDuration() const;

    /**
     * 启动读包线程
     * @param audioQueue 可为 nullptr（没有音频流）
     * @param videoQueue 可为 nullptr（没有视频流）
     */
    void Start(PacketQueue *audioQueue, PacketQueue *videoQueue, Listener *listener);

    /** 请求 seek（非阻塞），targetSec 为流的绝对时间 */
    void RequestSeek(double targetSec);

    /** 暂停/恢复读取（RTSP 等需要通知服务器） */
    void SetPaused(bool paused);

    /** 解码线程发现包队列为空时调用，让读包线程立即继续读 */
    void WakeUp();

    /** 终止读包线程并打断正在进行的 FFmpeg 调用 */
    void Abort();

    /** 等待读包线程退出 */
    void Join();

    /** 已经读到文件末尾（seek 后重置） */
    bool IsEof() const { return m_Eof.load(); }

private:
    static int InterruptCallback(void *opaque);

    /** 设置当前阻塞操作的超时（微秒），0 表示不限 */
    void ArmDeadline(int64_t timeoutUs);

    void ReadLoop();

    void DoSeek(int64_t targetUs);

    bool BufferIsFull();

    /** 在条件变量上最多等待 ms 毫秒，有 seek / 终止 / WakeUp 时提前返回 */
    void WaitFor(int ms);

    FormatContextPtr m_FormatCtx;
    int m_AudioIndex = -1;
    int m_VideoIndex = -1;

    PacketQueue *m_AudioQueue = nullptr;
    PacketQueue *m_VideoQueue = nullptr;
    Listener *m_Listener = nullptr;
    std::thread m_Thread;

    const std::atomic<bool> *m_ExternalAbort;
    std::atomic<bool> m_Abort{false};
    std::atomic<int64_t> m_DeadlineUs{0};   // av_gettime_relative() 时间，0 表示没有超时
    std::atomic<bool> m_TimedOut{false};
    std::atomic<bool> m_Eof{false};

    // 以下由 m_Mutex 保护
    std::mutex m_Mutex;
    std::condition_variable m_Cond;
    bool m_SeekRequested = false;
    int64_t m_SeekTargetUs = 0;
    bool m_Paused = false;
    bool m_WakeUp = false;
};

#endif //LEARNFFMPEG_DEMUXER_H
