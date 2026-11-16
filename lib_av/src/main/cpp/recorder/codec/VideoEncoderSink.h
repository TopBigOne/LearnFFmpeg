/**
 * GL 渲染直接画到编码器输入 Surface 时，渲染端向录制器询问每帧 pts 的接口
 */

#ifndef LEARNFFMPEG_VIDEOENCODERSINK_H
#define LEARNFFMPEG_VIDEOENCODERSINK_H

#include <cstdint>

class VideoEncoderSink {
public:
    virtual ~VideoEncoderSink() = default;

    /**
     * 为一帧画面分配 pts（GL 线程调用）。
     * @param captureTimeUs 画面的采集时刻（CLOCK_BOOTTIME 微秒），0 表示未知
     * @return 相对录制起点的 pts（微秒）；返回负数表示这一帧不录（例如采集于录制开始之前）
     */
    virtual int64_t NextVideoPtsUs(int64_t captureTimeUs) = 0;
};

#endif //LEARNFFMPEG_VIDEOENCODERSINK_H
