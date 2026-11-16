/**
 *
 * Created by 公众号：字节流动 on 2021/12/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#ifndef LEARNFFMPEG_SYNCCLOCK_H
#define LEARNFFMPEG_SYNCCLOCK_H

#include <mutex>
#include <LogUtil.h>

/**
 * 同步时钟（单位：毫秒）
 *
 * 记录"某个时刻 time 正在播放 pts"，之后读取时用流逝的系统时间推算当前播放位置：
 *     clock = pts + (now - time)
 *
 * 由音频线程写、视频线程读，内部加锁，避免读到 pts 和 time 不一致的"半更新"状态。
 * 暂停时时钟冻结，恢复后从冻结的位置继续走。
 */
class SyncClock {
public:
    SyncClock() {}

    ~SyncClock() {}

    /** 设置时钟：在系统时间 time 时正在播放 pts */
    void SetClock(double pts, double time) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Pts = pts;
        m_LastUpdate = time;
        m_Valid = true;
    }

    /** 获取当前时钟；无效时返回 -1 */
    double GetClock() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_Valid) return -1;
        double now = m_Paused ? m_PauseTime : GetSysCurrentTime();
        return m_Pts + now - m_LastUpdate;
    }

    bool IsValid() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Valid;
    }

    /** 暂停：冻结时钟 */
    void Pause() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_Paused) {
            m_Paused = true;
            m_PauseTime = GetSysCurrentTime();
        }
    }

    /** 恢复：暂停期间流逝的时间不计入时钟 */
    void Resume() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_Paused) {
            m_Paused = false;
            m_LastUpdate += GetSysCurrentTime() - m_PauseTime;
        }
    }

    /** 失效（seek、播放结束后），等新数据到来重新 SetClock */
    void Reset() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Valid = false;
    }

private:
    std::mutex m_Mutex;
    double m_Pts = 0;
    double m_LastUpdate = 0;
    double m_PauseTime = 0;
    bool   m_Valid = false;
    bool   m_Paused = false;
};


#endif //LEARNFFMPEG_SYNCCLOCK_H
