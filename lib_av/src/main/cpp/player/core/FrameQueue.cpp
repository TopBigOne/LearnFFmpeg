#include "FrameQueue.h"

FrameQueue::FrameQueue(int maxSize, bool keepLast)
        : m_Queue(static_cast<size_t>(maxSize)), m_MaxSize(maxSize), m_KeepLast(keepLast) {
    for (DecodedFrame &slot : m_Queue) {
        slot.frame = MakeFrame();
        if (!slot.frame) m_Valid = false;
    }
}

DecodedFrame *FrameQueue::PeekWritable() {
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_Cond.wait(lock, [this] { return m_Size < m_MaxSize || m_Aborted; });
    if (m_Aborted) return nullptr;
    return &m_Queue[m_WIndex];
}

void FrameQueue::Push() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (++m_WIndex == m_MaxSize) m_WIndex = 0;
    m_Size++;
    m_Cond.notify_all();
}

DecodedFrame *FrameQueue::PeekReadable() {
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_Cond.wait(lock, [this] { return m_Size - m_RIndexShown > 0 || m_Aborted; });
    if (m_Aborted) return nullptr;
    return &m_Queue[(m_RIndex + m_RIndexShown) % m_MaxSize];
}

DecodedFrame *FrameQueue::Peek() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return &m_Queue[(m_RIndex + m_RIndexShown) % m_MaxSize];
}

DecodedFrame *FrameQueue::PeekNext() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return &m_Queue[(m_RIndex + m_RIndexShown + 1) % m_MaxSize];
}

DecodedFrame *FrameQueue::PeekLast() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return &m_Queue[m_RIndex];
}

void FrameQueue::Next() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_KeepLast && !m_RIndexShown) {
        // 第一次 Next 不释放：把这一帧留作 "最后显示的帧"
        m_RIndexShown = 1;
        return;
    }
    av_frame_unref(m_Queue[m_RIndex].frame.get());
    if (++m_RIndex == m_MaxSize) m_RIndex = 0;
    m_Size--;
    m_Cond.notify_all();
}

int FrameQueue::NbRemaining() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Size - m_RIndexShown;
}

bool FrameQueue::HasShownFrame() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_RIndexShown != 0;
}

void FrameQueue::Abort() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Aborted = true;
    m_Cond.notify_all();
}

void FrameQueue::Start() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Aborted = false;
}
