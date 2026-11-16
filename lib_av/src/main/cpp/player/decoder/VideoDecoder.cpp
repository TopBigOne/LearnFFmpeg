#include "VideoDecoder.h"
#include <LogUtil.h>

VideoDecoder::VideoDecoder(CodecContextPtr codecCtx, PacketQueue *packetQueue, FrameQueue *frameQueue,
                           std::function<void()> onQueueEmpty, AVFormatContext *formatCtx,
                           AVStream *stream, AVSync *sync)
        : Decoder(std::move(codecCtx), packetQueue, frameQueue, std::move(onQueueEmpty)),
          m_FormatCtx(formatCtx),
          m_Stream(stream),
          m_Sync(sync) {
}

void VideoDecoder::Run() {
    LOGCATI("VideoDecoder::Run start");
    FramePtr frame = MakeFrame();
    if (!frame) return;

    const AVRational tb = m_Stream->time_base;
    const AVRational frameRate = av_guess_frame_rate(m_FormatCtx, m_Stream, nullptr);
    // 帧时长的兜底估计：两帧 pts 差不可用（首帧、时间戳异常）时使用
    const double frameDuration = frameRate.num && frameRate.den ? av_q2d(AVRational{frameRate.den, frameRate.num}) : 0;

    for (;;) {
        int ret = DecodeFrame(frame.get());
        if (ret < 0) break;
        if (ret == 0) continue;

        double pts = frame->pts == AV_NOPTS_VALUE ? NAN : frame->pts * av_q2d(tb);

        if (m_Sync->Master() != SyncMaster::Video && !std::isnan(pts)) {
            double diff = pts - m_Sync->GetMasterClock();
            // 只在同一段数据内比较（seek 后视频时钟还没对齐时不丢），且后面还有包时才丢，
            // 保证最后一帧总能显示
            if (!std::isnan(diff) && std::fabs(diff) < AV_NOSYNC_THRESHOLD && diff < 0
                && PacketSerial() == m_Sync->videoClock.Serial()
                && m_PacketQueue->NbPackets() > 0) {
                m_EarlyDrops++;
                LOGCATV("VideoDecoder::Run early drop, pts=%.3f diff=%.3f", pts, diff);
                av_frame_unref(frame.get());
                continue;
            }
        }

        DecodedFrame *vp = m_FrameQueue->PeekWritable();
        if (vp == nullptr) break;

        vp->pts = pts;
        vp->duration = frameDuration;
        vp->pos = frame->pkt_pos;
        vp->serial = PacketSerial();
        vp->width = frame->width;
        vp->height = frame->height;
        vp->format = frame->format;
        av_frame_move_ref(vp->frame.get(), frame.get());
        m_FrameQueue->Push();
    }
    LOGCATI("VideoDecoder::Run exit, early dropped %d frames", m_EarlyDrops.load());
}
