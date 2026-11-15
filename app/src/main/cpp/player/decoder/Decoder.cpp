#include "Decoder.h"
#include <LogUtil.h>

Decoder::Decoder(CodecContextPtr codecCtx, PacketQueue *packetQueue, FrameQueue *frameQueue,
                 std::function<void()> onQueueEmpty)
        : m_FrameQueue(frameQueue),
          m_PacketQueue(packetQueue),
          m_CodecCtx(std::move(codecCtx)),
          m_OnQueueEmpty(std::move(onQueueEmpty)),
          m_Packet(MakePacket()) {
}

Decoder::~Decoder() {
    // 派生类应已调用 Stop()；这里兜底，防止线程对象在 joinable 状态下析构直接 terminate
    if (m_Thread.joinable()) {
        LOGCATE("Decoder::~Decoder thread still running, Stop() was not called");
        m_PacketQueue->Abort();
        m_FrameQueue->Abort();
        m_Thread.join();
    }
}

void Decoder::Start() {
    m_Thread = std::thread([this] { Run(); });
}

void Decoder::Stop() {
    // 两个阻塞点（取包、等空槽）都可以被 Abort 唤醒
    m_PacketQueue->Abort();
    m_FrameQueue->Abort();
    if (m_Thread.joinable()) m_Thread.join();
}

void Decoder::SetStartPts(int64_t startPts, AVRational startPtsTimeBase) {
    m_StartPts = startPts;
    m_StartPtsTimeBase = startPtsTimeBase;
}

void Decoder::ResetForNewSerial() {
    // 丢掉解码器内部缓存的参考帧 / 待输出帧，否则 seek 后会先吐出几帧旧画面
    avcodec_flush_buffers(m_CodecCtx.get());
    m_Finished = 0;
    m_NextPts = m_StartPts;
    m_NextPtsTimeBase = m_StartPtsTimeBase;
}

int Decoder::DecodeFrame(AVFrame *frame) {
    AVCodecContext *ctx = m_CodecCtx.get();
    if (!m_Packet) return -1;

    for (;;) {
        // 1. 先把解码器里已经能输出的帧取完（只有当前段的数据才有意义）
        if (m_PacketQueue->Serial() == m_PacketSerial) {
            for (;;) {
                if (m_PacketQueue->IsAborted()) return -1;

                int ret = avcodec_receive_frame(ctx, frame);
                if (ret >= 0) {
                    if (ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
                        // best_effort_timestamp 由 FFmpeg 综合 pts/dts 推算，B 帧重排后也是显示顺序
                        frame->pts = frame->best_effort_timestamp;
                    } else if (ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
                        // 音频 pts 统一换算到 1/sample_rate，缺失时按上一帧末尾推算
                        AVRational tb{1, frame->sample_rate};
                        if (frame->pts != AV_NOPTS_VALUE)
                            frame->pts = av_rescale_q(frame->pts, ctx->pkt_timebase, tb);
                        else if (m_NextPts != AV_NOPTS_VALUE)
                            frame->pts = av_rescale_q(m_NextPts, m_NextPtsTimeBase, tb);
                        if (frame->pts != AV_NOPTS_VALUE) {
                            m_NextPts = frame->pts + frame->nb_samples;
                            m_NextPtsTimeBase = tb;
                        }
                    }
                    return 1;
                }
                if (ret == AVERROR_EOF) {
                    // draining 结束：记录完成的是哪一段，flush 后解码器可以接收 seek 之后的新数据
                    m_Finished = m_PacketSerial.load();
                    avcodec_flush_buffers(ctx);
                    return 0;
                }
                if (ret != AVERROR(EAGAIN)) {
                    LOGCATD("Decoder::DecodeFrame receive_frame error %d, type=%d", ret, ctx->codec_type);
                }
                break;
            }
        }

        // 2. 取一个属于当前段的包
        PacketQueue::Kind kind;
        for (;;) {
            if (m_OnQueueEmpty && m_PacketQueue->NbPackets() == 0) m_OnQueueEmpty();

            if (m_PacketPending) {
                m_PacketPending = false;
                kind = m_PendingKind;
            } else {
                int serial = 0;
                int oldSerial = m_PacketSerial;
                if (m_PacketQueue->Get(m_Packet.get(), &kind, &serial, true) < 0) return -1;
                m_PacketSerial = serial;
                if (oldSerial != serial) ResetForNewSerial();
            }
            if (m_PacketQueue->Serial() == m_PacketSerial) break;
            // seek 之前读出的旧包，直接丢弃
            av_packet_unref(m_Packet.get());
        }

        // 3. 送入解码器
        if (kind == PacketQueue::Kind::Flush) {
            ResetForNewSerial();
            continue;
        }

        int ret = avcodec_send_packet(ctx, kind == PacketQueue::Kind::Eos ? nullptr : m_Packet.get());
        if (ret == AVERROR(EAGAIN)) {
            // 第 1 步已把输出取到 EAGAIN，理论上不会再出现；保险起见暂存，下一轮重送
            LOGCATE("Decoder::DecodeFrame receive_frame and send_packet both returned EAGAIN");
            m_PacketPending = true;
            m_PendingKind = kind;
        } else {
            if (ret < 0 && ret != AVERROR_EOF) {
                // 损坏的包：跳过，继续解后面的数据
                LOGCATD("Decoder::DecodeFrame send_packet error %d, type=%d", ret, ctx->codec_type);
            }
            av_packet_unref(m_Packet.get());
        }
    }
}
