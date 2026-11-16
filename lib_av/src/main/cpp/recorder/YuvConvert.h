/**
 * 录制模块的图像格式转换，统一由 libyuv 完成（NEON/SVE 加速，运行时按 CPU 选最快实现）
 */

#ifndef LEARNFFMPEG_YUVCONVERT_H
#define LEARNFFMPEG_YUVCONVERT_H

#include <cstdint>
#include <vector>
#include <ImageDef.h>

extern "C" {
#include <libavutil/frame.h>
}

/**
 * Android YUV_420_888 图像的三个平面（对应 Java 层 Image.getPlanes()）。
 *
 * YUV_420_888 只规定 4:2:0 采样和三个平面，实际内存排列由硬件决定：
 * uvPixelStride == 1 时是 I420 式三平面，== 2 时是 NV12/NV21 式交错存放（U、V 指向同一块内存）。
 * libyuv::Android420ToI420 会自己判断并选择对应的快速路径。
 */
struct Android420Planes {
    const uint8_t *y = nullptr;
    int yRowStride = 0;
    const uint8_t *u = nullptr;
    int uRowStride = 0;
    const uint8_t *v = nullptr;
    int vRowStride = 0;
    int uvPixelStride = 0;
    int width = 0;
    int height = 0;
};

namespace YuvConvert {

/** YUV_420_888 → I420，写入调用方提供的三个平面。成功返回 true */
bool Android420ToI420(const Android420Planes &src,
                      uint8_t *dstY, int dstStrideY,
                      uint8_t *dstU, int dstStrideU,
                      uint8_t *dstV, int dstStrideV);

}

/**
 * 把待编码的画面（RGBA / NV21 / NV12 / I420，任意尺寸）转换成编码器要求的 YUV420P AVFrame。
 *
 * 尺寸一致时直接写入目标帧；不一致时先转成源尺寸的 I420 中间缓冲，再用 libyuv 缩放。
 * 中间缓冲在多次调用间复用。非线程安全，每个编码线程各持有一个。
 */
class EncoderFrameConverter {
public:
    /** @return 0 成功，负数失败（不支持的格式或参数错误） */
    int Convert(const NativeImage &src, AVFrame *dst);

    /** 同上，目标是调用方给出的 I420 三个平面（dstWidth x dstHeight），用于写入 MediaCodec 输入缓冲 */
    int ConvertToI420(const NativeImage &src,
                      uint8_t *dstY, int dstStrideY,
                      uint8_t *dstU, int dstStrideU,
                      uint8_t *dstV, int dstStrideV,
                      int dstWidth, int dstHeight);

private:
    std::vector<uint8_t> m_Scratch;
};

#endif //LEARNFFMPEG_YUVCONVERT_H
