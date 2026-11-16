#include "Clock.h"

extern "C" {
#include <libavutil/time.h>
};

Clock::Clock(const std::atomic<int> *queueSerial)
        : m_QueueSerial(queueSerial != nullptr ? queueSerial : &m_SelfSerial) {
}

double Clock::Now() {
    return av_gettime_relative() / 1000000.0;
}

double Clock::GetLocked(double now) const {
    if (m_QueueSerial->load(std::memory_order_acquire) != m_Serial) return NAN;
    if (m_Paused) return m_Pts;
    return m_PtsDrift + now - (now - m_LastUpdated) * (1.0 - m_Speed);
}

double Clock::Get() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return GetLocked(Now());
}

void Clock::SetAtLocked(double pts, int serial, double time) {
    m_Pts = pts;
    m_LastUpdated = time;
    m_PtsDrift = pts - time;
    m_Serial = serial;
    if (m_QueueSerial == &m_SelfSerial) m_SelfSerial.store(serial, std::memory_order_release);
}

void Clock::SetAt(double pts, int serial, double time) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    SetAtLocked(pts, serial, time);
}

void Clock::SetAt(double pts, int serial, double time, double speed) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    SetAtLocked(pts, serial, time);
    m_Speed = speed;
}

void Clock::Set(double pts, int serial) {
    SetAt(pts, serial, Now());
}

void Clock::SetPaused(bool paused) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Paused == paused) return;
    double now = Now();
    if (paused) {
        // 冻结在此刻的值（过期时保持 NAN，恢复后仍是过期状态）
        double value = GetLocked(now);
        if (!std::isnan(value)) m_Pts = value;
        m_Paused = true;
    } else {
        m_Paused = false;
        // 以恢复时刻重新锚定，暂停时长不计入
        if (!std::isnan(m_Pts)) SetAtLocked(m_Pts, m_Serial, now);
    }
}

void Clock::SetSpeed(double speed) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    double now = Now();
    double value = GetLocked(now);
    if (!std::isnan(value)) SetAtLocked(value, m_Serial, now);
    m_Speed = speed;
}

int Clock::Serial() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Serial;
}

double Clock::LastUpdated() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_LastUpdated;
}

void Clock::SyncToSlave(Clock &slave) {
    // 先读 slave 再锁自己：两把锁不嵌套，避免两个时钟互相同步时死锁
    double slaveClock = slave.Get();
    int slaveSerial = slave.Serial();
    if (std::isnan(slaveClock)) return;

    std::lock_guard<std::mutex> lock(m_Mutex);
    double now = Now();
    double clock = GetLocked(now);
    if (std::isnan(clock) || std::fabs(clock - slaveClock) > AV_NOSYNC_THRESHOLD)
        SetAtLocked(slaveClock, slaveSerial, now);
}
