/**
 * 视频解码线程（ffplay video_thread + get_video_frame）：解出的帧放入视频帧队列，
 * 何时显示由视频刷新线程决定，这里从不 sleep。
 *
 * 提前丢帧：主时钟不是视频时，如果刚解出的帧已经落后于主时钟（且后面还有包可解），
 * 直接丢弃不入队，避免在解码跟不上时把时间浪费在注定要被丢掉的帧上。
 */

#ifndef LEARNFFMPEG_VIDEODECODER_H
#define LEARNFFMPEG_VIDEODECODER_H

#include <atomic>
#include <core/AVSync.h>
#include "Decoder.h"

class VideoDecoder : public Decoder {
public:
    VideoDecoder(CodecContextPtr codecCtx, PacketQueue *packetQueue, FrameQueue *frameQueue,
                 std::function<void()> onQueueEmpty, AVFormatContext *formatCtx, AVStream *stream,
                 AVSync *sync);

    ~VideoDecoder() override { Stop(); }

    /** 解码后因落后而提前丢弃的帧数 */
    int EarlyDroppedFrames() const { return m_EarlyDrops.load(); }

protected:
    void Run() override;

private:
    AVFormatContext *m_FormatCtx;
    AVStream *m_Stream;
    AVSync *m_Sync;
    std::atomic<int> m_EarlyDrops{0};
};

#endif //LEARNFFMPEG_VIDEODECODER_H
