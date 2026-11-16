/**
 * 录制模块的图像格式转换，统一由 libyuv 完成
 */

#include "YuvConvert.h"
#include <LogUtil.h>
#include <libyuv.h>

extern "C" {
#include <libavutil/pixfmt.h>
}

namespace YuvConvert {

bool Android420ToI420(const Android420Planes &src,
                      uint8_t *dstY, int dstStrideY,
                      uint8_t *dstU, int dstStrideU,
                      uint8_t *dstV, int dstStrideV) {
    int ret = libyuv::Android420ToI420(src.y, src.yRowStride,
                                       src.u, src.uRowStride,
                                       src.v, src.vRowStride,
                                       src.uvPixelStride,
                                       dstY, dstStrideY,
                                       dstU, dstStrideU,
                                       dstV, dstStrideV,
                                       src.width, src.height);
    if (ret != 0) {
        LOGCATE("YuvConvert::Android420ToI420 fail ret=%d [w,h]=[%d,%d] uvPixelStride=%d",
                ret, src.width, src.height, src.uvPixelStride);
    }
    return ret == 0;
}

}

int EncoderFrameConverter::Convert(const NativeImage &src, AVFrame *dst) {
    if (dst == nullptr || dst->format != AV_PIX_FMT_YUV420P) return -1;
    return ConvertToI420(src, dst->data[0], dst->linesize[0], dst->data[1], dst->linesize[1],
                         dst->data[2], dst->linesize[2], dst->width, dst->height);
}

int EncoderFrameConverter::ConvertToI420(const NativeImage &src,
                                         uint8_t *dstY, int dstStrideY,
                                         uint8_t *dstU, int dstStrideU,
                                         uint8_t *dstV, int dstStrideV,
                                         int dstWidth, int dstHeight) {
    if (dstY == nullptr || src.ppPlane[0] == nullptr || src.width <= 0 || src.height <= 0
        || dstWidth <= 0 || dstHeight <= 0) {
        return -1;
    }
    const int w = src.width;
    const int h = src.height;
    const bool needScale = w != dstWidth || h != dstHeight;

    // 尺寸一致直接写入目标；否则先写到源尺寸的 I420 中间缓冲
    uint8_t *y = dstY, *u = dstU, *v = dstV;
    int strideY = dstStrideY, strideU = dstStrideU, strideV = dstStrideV;
    if (needScale) {
        const int halfW = (w + 1) / 2, halfH = (h + 1) / 2;
        const size_t size = static_cast<size_t>(w) * h + static_cast<size_t>(halfW) * halfH * 2;
        if (m_Scratch.size() != size) m_Scratch.resize(size);
        y = m_Scratch.data();
        u = y + static_cast<size_t>(w) * h;
        v = u + static_cast<size_t>(halfW) * halfH;
        strideY = w;
        strideU = strideV = halfW;
    }

    int ret;
    switch (src.format) {
        case IMAGE_FORMAT_RGBA:
            // libyuv 按小端 32 位字命名格式："ABGR" 在内存中的字节顺序正是 R、G、B、A，
            // 即 glReadPixels(GL_RGBA) 读回的排列
            ret = libyuv::ABGRToI420(src.ppPlane[0], src.pLineSize[0],
                                     y, strideY, u, strideU, v, strideV, w, h);
            break;
        case IMAGE_FORMAT_NV21:
            ret = libyuv::NV21ToI420(src.ppPlane[0], src.pLineSize[0],
                                     src.ppPlane[1], src.pLineSize[1],
                                     y, strideY, u, strideU, v, strideV, w, h);
            break;
        case IMAGE_FORMAT_NV12:
            ret = libyuv::NV12ToI420(src.ppPlane[0], src.pLineSize[0],
                                     src.ppPlane[1], src.pLineSize[1],
                                     y, strideY, u, strideU, v, strideV, w, h);
            break;
        case IMAGE_FORMAT_I420:
            ret = libyuv::I420Copy(src.ppPlane[0], src.pLineSize[0],
                                   src.ppPlane[1], src.pLineSize[1],
                                   src.ppPlane[2], src.pLineSize[2],
                                   y, strideY, u, strideU, v, strideV, w, h);
            break;
        default:
            LOGCATE("EncoderFrameConverter::Convert unsupported format=%d", src.format);
            return -1;
    }
    if (ret != 0) {
        LOGCATE("EncoderFrameConverter::Convert fail ret=%d format=%d", ret, src.format);
        return ret;
    }

    if (needScale) {
        ret = libyuv::I420Scale(y, strideY, u, strideU, v, strideV, w, h,
                                dstY, dstStrideY, dstU, dstStrideU, dstV, dstStrideV,
                                dstWidth, dstHeight, libyuv::kFilterBilinear);
        if (ret != 0) {
            LOGCATE("EncoderFrameConverter::Convert I420Scale fail ret=%d", ret);
        }
    }
    return ret;
}
