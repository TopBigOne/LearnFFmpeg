/**
 * 压缩数据包队列（参照 ffplay 的 PacketQueue）。
 *
 * 由 Demuxer 读包线程写入、解码线程读出，每个流一个。
 *
 * serial（序列号）机制：
 *  - 每次 seek 调用 Flush()：清空旧包、serial + 1，并放入一个 FLUSH 标记；
 *  - 入队的每个包都记录入队时的 serial，解码器据此丢弃 seek 之前的旧包，
 *    帧队列、时钟也都带 serial，下游只要比较 serial 就知道数据是否已过期，
 *    不需要跨线程去"清空"别人的缓冲区。
 *
 * 统计：包数、字节数（含节点开销）、总时长（流 time_base），Demuxer 据此判断缓冲是否已满。
 *
 * 退出：Abort() 唤醒所有阻塞在 Get 上的线程，之后 Put/Get 都立即失败。
 */

#ifndef LEARNFFMPEG_PACKETQUEUE_H
#define LEARNFFMPEG_PACKETQUEUE_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include "FFPtr.h"

class PacketQueue {
public:
    /** 队列元素类型 */
    enum class Kind {
        Data,   // 普通数据包
        Flush,  // seek 后插入的标记：解码器收到后 avcodec_flush_buffers
        Eos     // 输入结束：解码器收到后送空包进入 draining，把缓存的帧全部取出
    };

    PacketQueue() = default;
    ~PacketQueue();

    PacketQueue(const PacketQueue &) = delete;
    PacketQueue &operator=(const PacketQueue &) = delete;

    /**
     * 入队数据包：转移 pkt 的引用（调用后 pkt 变为空包，可直接复用）
     * @return 0 成功；<0 已终止（pkt 被 unref）
     */
    int Put(AVPacket *pkt);

    /** 入队 EOS 标记 */
    int PutEos();

    /**
     * 清空队列、serial + 1，并插入 FLUSH 标记（seek 时调用）
     * 即使清空后没有新包到来，解码器也能通过 FLUSH 标记得知"已经 seek 过"
     */
    void Flush();

    /** 终止：唤醒所有等待者，之后 Put/Get 都失败 */
    void Abort();

    /** 取消终止状态（Prepare 时调用） */
    void Start();

    /**
     * 取包
     * @param pkt    输出：数据包（调用方负责 av_packet_unref）；FLUSH/EOS 时为空包
     * @param kind   输出：元素类型
     * @param serial 输出：该包入队时的 serial
     * @param block  队列为空时是否阻塞等待
     * @return 1 取到；0 队列为空（非阻塞模式）；-1 已终止
     */
    int Get(AVPacket *pkt, Kind *kind, int *serial, bool block);

    /** 当前 serial；时钟用它判断自己是否过期，因此是原子变量 */
    int Serial() const { return m_Serial.load(std::memory_order_acquire); }

    /** 供 Clock 持有的 serial 地址 */
    const std::atomic<int> *SerialPtr() const { return &m_Serial; }

    int NbPackets();
    int Size();
    int64_t Duration();

    /**
     * 是否已缓冲"足够"的数据（ffplay stream_has_enough_packets）：
     * 包数超过 MIN_FRAMES 且时长超过 1 秒；时长未知时只看包数
     */
    bool HasEnoughPackets(const AVStream *stream);

    bool IsAborted();

private:
    struct Node {
        PacketPtr pkt;
        Kind kind;
        int serial;
    };

    int PutLocked(AVPacket *pkt, Kind kind);
    void ClearLocked();

    std::mutex m_Mutex;
    std::condition_variable m_Cond;
    std::deque<Node> m_Queue;
    int m_NbPackets = 0;            // 只统计数据包
    int m_Size = 0;                 // 字节数（含节点开销）
    int64_t m_Duration = 0;         // 流 time_base
    bool m_Aborted = true;          // 构造后处于终止状态，Start() 后才可用（与 ffplay 一致）
    std::atomic<int> m_Serial{0};
};

#endif //LEARNFFMPEG_PACKETQUEUE_H
