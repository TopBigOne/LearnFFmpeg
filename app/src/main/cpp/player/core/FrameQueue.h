/**
 * 解码后帧的固定容量环形队列（参照 ffplay 的 FrameQueue）。
 *
 * 单生产者（解码线程）/ 单消费者（视频刷新线程或音频输出回调）：
 *  - 生产者：PeekWritable() 拿到空槽 → 填充 → Push()
 *  - 消费者：Peek()/PeekNext()/PeekLast() 读取 → Next() 释放
 * 槽位数组在构造时一次性分配，运行中不再分配 AVFrame，解码数据通过 av_frame_move_ref 转移引用。
 *
 * keepLast：视频队列保留"最后显示的那一帧"（rindex_shown），
 * 刷新线程计算帧间隔需要它（上一帧 pts），暂停/seek 后重绘也需要它。
 *
 * 槽位里的数据只由 "拥有它的一方" 访问：写槽只有生产者碰，已入队的槽只有消费者碰，
 * 所以读写帧内容不需要持锁；索引和计数全部在锁内修改。
 */

#ifndef LEARNFFMPEG_FRAMEQUEUE_H
#define LEARNFFMPEG_FRAMEQUEUE_H

#include <cmath>
#include <condition_variable>
#include <mutex>
#include <vector>
#include "FFPtr.h"

struct DecodedFrame {
    FramePtr frame;                 // 解码输出（引用计数，move 进来）
    int serial = 0;                 // 所属的包序列号，与 PacketQueue::Serial() 不同即为过期帧
    double pts = NAN;               // 显示时间戳（秒，流的绝对时间），未知为 NAN
    double duration = 0;            // 估计时长（秒）
    int64_t pos = -1;               // 在文件中的字节位置
    int width = 0;
    int height = 0;
    int format = -1;
};

class FrameQueue {
public:
    /** 容量与 ffplay 一致 */
    static const int VIDEO_PICTURE_QUEUE_SIZE = 3;
    static const int SAMPLE_QUEUE_SIZE = 9;

    FrameQueue(int maxSize, bool keepLast);

    FrameQueue(const FrameQueue &) = delete;
    FrameQueue &operator=(const FrameQueue &) = delete;

    /** 构造是否成功（AVFrame 分配失败时为 false） */
    bool IsValid() const { return m_Valid; }

    /**
     * 生产者：等待一个空槽
     * @return 空槽；已终止返回 nullptr
     */
    DecodedFrame *PeekWritable();

    /** 生产者：提交 PeekWritable 拿到的槽 */
    void Push();

    /**
     * 消费者：阻塞等待一个可读帧
     * @return 帧；已终止返回 nullptr
     */
    DecodedFrame *PeekReadable();

    /** 消费者：当前待显示的帧（调用前必须确认 NbRemaining() > 0） */
    DecodedFrame *Peek();

    /** 消费者：当前帧之后的一帧（调用前必须确认 NbRemaining() > 1） */
    DecodedFrame *PeekNext();

    /** 消费者：最后一次显示的帧（keepLast 时有效） */
    DecodedFrame *PeekLast();

    /** 消费者：当前帧已用完，释放槽位 */
    void Next();

    /** 尚未显示的帧数 */
    int NbRemaining();

    /** 是否已经显示过至少一帧（keepLast 时 PeekLast 才有意义） */
    bool HasShownFrame();

    /** 终止：唤醒双方，PeekWritable/PeekReadable 返回 nullptr */
    void Abort();

    void Start();

private:
    std::vector<DecodedFrame> m_Queue;
    const int m_MaxSize;
    const bool m_KeepLast;
    int m_RIndex = 0;
    int m_WIndex = 0;
    int m_Size = 0;
    int m_RIndexShown = 0;
    bool m_Aborted = false;
    bool m_Valid = true;

    std::mutex m_Mutex;
    std::condition_variable m_Cond;
};

#endif //LEARNFFMPEG_FRAMEQUEUE_H
