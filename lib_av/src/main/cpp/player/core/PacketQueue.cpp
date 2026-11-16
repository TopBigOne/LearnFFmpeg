#include "PacketQueue.h"

// 缓冲判定阈值，取 ffplay 的默认值
static const int MIN_FRAMES = 25;

PacketQueue::~PacketQueue() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    ClearLocked();
}

int PacketQueue::PutLocked(AVPacket *pkt, Kind kind) {
    if (m_Aborted) return -1;

    PacketPtr node = MakePacket();
    if (!node) return AVERROR(ENOMEM);
    if (pkt != nullptr) av_packet_move_ref(node.get(), pkt);

    if (kind == Kind::Data) {
        m_NbPackets++;
        m_Size += node->size + static_cast<int>(sizeof(Node));
        m_Duration += node->duration;
    }
    m_Queue.push_back(Node{std::move(node), kind, m_Serial.load(std::memory_order_relaxed)});
    m_Cond.notify_all();
    return 0;
}

int PacketQueue::Put(AVPacket *pkt) {
    int ret;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        ret = PutLocked(pkt, Kind::Data);
    }
    // 入队失败时引用没有被转移，由这里释放，调用方不需要区分
    if (ret < 0) av_packet_unref(pkt);
    return ret;
}

int PacketQueue::PutEos() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return PutLocked(nullptr, Kind::Eos);
}

void PacketQueue::ClearLocked() {
    m_Queue.clear();
    m_NbPackets = 0;
    m_Size = 0;
    m_Duration = 0;
}

void PacketQueue::Flush() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    ClearLocked();
    // serial 只在持锁时修改：保证 "清空 + 换代 + 标记" 对取包线程是一个原子动作
    m_Serial.fetch_add(1, std::memory_order_acq_rel);
    PutLocked(nullptr, Kind::Flush);
}

void PacketQueue::Abort() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Aborted = true;
    m_Cond.notify_all();
}

void PacketQueue::Start() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Aborted = false;
    // 与 ffplay 一致：启动时先放一个 FLUSH，让解码器的 serial 与队列对齐
    m_Serial.fetch_add(1, std::memory_order_acq_rel);
    PutLocked(nullptr, Kind::Flush);
}

int PacketQueue::Get(AVPacket *pkt, Kind *kind, int *serial, bool block) {
    std::unique_lock<std::mutex> lock(m_Mutex);
    for (;;) {
        if (m_Aborted) return -1;

        if (!m_Queue.empty()) {
            Node &node = m_Queue.front();
            if (node.kind == Kind::Data) {
                m_NbPackets--;
                m_Size -= node.pkt->size + static_cast<int>(sizeof(Node));
                m_Duration -= node.pkt->duration;
            }
            av_packet_move_ref(pkt, node.pkt.get());
            if (kind) *kind = node.kind;
            if (serial) *serial = node.serial;
            m_Queue.pop_front();
            return 1;
        }

        if (!block) return 0;
        m_Cond.wait(lock);
    }
}

int PacketQueue::NbPackets() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_NbPackets;
}

int PacketQueue::Size() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Size;
}

int64_t PacketQueue::Duration() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Duration;
}

bool PacketQueue::HasEnoughPackets(const AVStream *stream) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Aborted) return true;
    // 封面图（mp3 内嵌图片）只有一个包，不需要继续缓冲
    if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) return true;
    return m_NbPackets > MIN_FRAMES
           && (m_Duration == 0 || av_q2d(stream->time_base) * m_Duration > 1.0);
}

bool PacketQueue::IsAborted() {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Aborted;
}
