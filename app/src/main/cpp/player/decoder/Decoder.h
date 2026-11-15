/**
 * 解码器基类：一个解码线程 + 严格的 send/receive 状态机（参照 ffplay decoder_decode_frame）。
 *
 *   ┌──────────── receive_frame ────────────┐
 *   │  >=0      → 输出一帧                  │
 *   │  EAGAIN   → 需要更多输入，去取包      │
 *   │  EOF      → draining 结束，finished   │
 *   └───────────────────────────────────────┘
 *   取包：只接受 serial 与队列当前 serial 一致的包，旧 serial 的包直接丢弃；
 *         FLUSH 标记 / serial 变化 → avcodec_flush_buffers，清除 finished；
 *         EOS 标记 → 送空包进入 draining；
 *   send_packet 返回 EAGAIN（输出没取完）时，包暂存为 pending，下一轮先 receive 再重送，不丢包。
 *
 * 解码线程的阻塞点只有两个：取包（PacketQueue::Get）和等空槽（FrameQueue::PeekWritable），
 * 都可以被 Abort 唤醒。解码线程从不 sleep，节奏由帧队列的容量控制。
 */

#ifndef LEARNFFMPEG_DECODER_H
#define LEARNFFMPEG_DECODER_H

#include <atomic>
#include <functional>
#include <thread>
#include <core/FFPtr.h>
#include <core/FrameQueue.h>
#include <core/PacketQueue.h>

class Decoder {
public:
    /**
     * @param codecCtx     已打开的解码器上下文（所有权转移给 Decoder）
     * @param packetQueue  输入包队列
     * @param frameQueue   输出帧队列
     * @param onQueueEmpty 包队列为空时回调（用来唤醒读包线程），可为空
     */
    Decoder(CodecContextPtr codecCtx, PacketQueue *packetQueue, FrameQueue *frameQueue,
            std::function<void()> onQueueEmpty);

    virtual ~Decoder();

    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;

    /** 启动解码线程 */
    void Start();

    /** 终止并等待解码线程退出（可重复调用）；派生类析构前必须调用 */
    void Stop();

    AVCodecContext *CodecContext() const { return m_CodecCtx.get(); }

    /**
     * 解码完成时所属的 serial：等于包队列当前 serial 说明这一段数据已经全部解码完毕（EOF 已排空），
     * 否则为 0。供播放完成判断使用
     */
    int Finished() const { return m_Finished.load(); }

    /** 当前正在解码的包的 serial */
    int PacketSerial() const { return m_PacketSerial.load(); }

    /**
     * 某些格式（无法按时间戳 seek 的裸流）的音频包没有 pts，从流的起始时间开始按采样数推算（ffplay start_pts）
     */
    void SetStartPts(int64_t startPts, AVRational startPtsTimeBase);

protected:
    /**
     * 解出一帧
     * @return 1 得到一帧；0 本段数据已解码完（EOF）；-1 已终止
     */
    int DecodeFrame(AVFrame *frame);

    /** 解码线程主循环，由派生类实现（把帧送入帧队列） */
    virtual void Run() = 0;

    FrameQueue *m_FrameQueue;
    PacketQueue *m_PacketQueue;

private:
    void ResetForNewSerial();

    CodecContextPtr m_CodecCtx;
    std::function<void()> m_OnQueueEmpty;
    std::thread m_Thread;

    PacketPtr m_Packet;
    bool m_PacketPending = false;
    PacketQueue::Kind m_PendingKind = PacketQueue::Kind::Data;

    std::atomic<int> m_PacketSerial{-1};
    std::atomic<int> m_Finished{0};

    // 音频时间戳推算（只在解码线程内使用）
    int64_t m_StartPts = AV_NOPTS_VALUE;
    AVRational m_StartPtsTimeBase{0, 1};
    int64_t m_NextPts = AV_NOPTS_VALUE;
    AVRational m_NextPtsTimeBase{0, 1};
};

#endif //LEARNFFMPEG_DECODER_H
