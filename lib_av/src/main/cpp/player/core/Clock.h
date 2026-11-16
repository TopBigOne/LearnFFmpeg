/**
 * 播放时钟（参照 ffplay 的 Clock），单位：秒。
 *
 * 记录 "在系统时刻 lastUpdated 正在播放 pts"，读取时按流逝的时间外推：
 *     clock = ptsDrift + now - (now - lastUpdated) * (1 - speed)，其中 ptsDrift = pts - lastUpdated
 * 即 clock = pts + (now - lastUpdated) * speed。speed 是倍速：每过 1 秒真实时间，媒体时间前进 speed 秒。
 * lastUpdated 可以晚于当前时刻（音频输出按 "这块数据将在何时播出" 设置），外推公式同样成立。
 *
 * serial：时钟记录了设置它时数据所属的包序列号；读取时如果与对应 PacketQueue 的当前 serial 不同，
 * 说明 seek 之后还没有新数据到来，时钟已过期，返回 NAN。这样 seek 不需要显式 "重置" 各个时钟。
 *
 * 暂停：冻结在暂停那一刻的值；恢复时以当前系统时间重新锚定，暂停时长不计入。
 *
 * 由音频回调线程、视频刷新线程写，控制线程/视频解码线程读，内部加锁。
 */

#ifndef LEARNFFMPEG_CLOCK_H
#define LEARNFFMPEG_CLOCK_H

#include <atomic>
#include <cmath>
#include <mutex>

/** 两个时钟相差超过该值（秒）就认为不同步得无法修正，直接对齐（ffplay AV_NOSYNC_THRESHOLD） */
static const double AV_NOSYNC_THRESHOLD = 10.0;

class Clock {
public:
    /**
     * @param queueSerial 对应 PacketQueue 的 serial 地址；传 nullptr 表示使用自身 serial（外部时钟），永不过期
     */
    explicit Clock(const std::atomic<int> *queueSerial);

    Clock(const Clock &) = delete;
    Clock &operator=(const Clock &) = delete;

    /** 当前值；过期（serial 不匹配）或从未设置时返回 NAN */
    double Get();

    /** 在系统时刻 time（秒）时正在播放 pts */
    void SetAt(double pts, int serial, double time);

    /** 同上，并原子地设置从该时刻起的倍速（音频输出知道每块数据实际按什么速度播放） */
    void SetAt(double pts, int serial, double time, double speed);

    /** 以当前系统时间设置 */
    void Set(double pts, int serial);

    /** 暂停冻结 / 恢复继续 */
    void SetPaused(bool paused);

    /** 倍速：以当前值为起点，此后按新速度推进（ffplay set_clock_speed） */
    void SetSpeed(double speed);

    int Serial();

    double LastUpdated();

    /**
     * 如果 slave 有效且本时钟无效或偏差超过 AV_NOSYNC_THRESHOLD，则把本时钟对齐到 slave
     * （ffplay sync_clock_to_slave：外部时钟跟随音频/视频时钟）
     */
    void SyncToSlave(Clock &slave);

    /** 单调系统时间（秒） */
    static double Now();

private:
    double GetLocked(double now) const;
    void SetAtLocked(double pts, int serial, double time);

    std::mutex m_Mutex;
    double m_Pts = NAN;
    double m_PtsDrift = NAN;
    double m_LastUpdated = 0;
    double m_Speed = 1.0;
    int m_Serial = -1;
    bool m_Paused = false;
    std::atomic<int> m_SelfSerial{0};
    const std::atomic<int> *m_QueueSerial;
};

#endif //LEARNFFMPEG_CLOCK_H
