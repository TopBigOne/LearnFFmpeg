#include "AudioDecoder.h"
#include <LogUtil.h>

void AudioDecoder::Run() {
    LOGCATI("AudioDecoder::Run start");
    FramePtr frame = MakeFrame();
    if (!frame) return;

    for (;;) {
        int ret = DecodeFrame(frame.get());
        if (ret < 0) break;
        if (ret == 0) continue;     // 本段已解码完，继续等 seek 之后的新数据

        DecodedFrame *af = m_FrameQueue->PeekWritable();
        if (af == nullptr) break;

        // DecodeFrame 已把音频 pts 换算到 1/sample_rate
        AVRational tb{1, frame->sample_rate};
        af->pts = frame->pts == AV_NOPTS_VALUE ? NAN : frame->pts * av_q2d(tb);
        af->duration = frame->sample_rate > 0 ? frame->nb_samples / static_cast<double>(frame->sample_rate) : 0;
        af->pos = frame->pkt_pos;
        af->serial = PacketSerial();
        af->format = frame->format;
        av_frame_move_ref(af->frame.get(), frame.get());
        m_FrameQueue->Push();
    }
    LOGCATI("AudioDecoder::Run exit");
}
