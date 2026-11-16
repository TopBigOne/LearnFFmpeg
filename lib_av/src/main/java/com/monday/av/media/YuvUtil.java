package com.monday.av.media;

import android.graphics.ImageFormat;
import android.media.Image;

import java.nio.ByteBuffer;

/**
 * YUV 格式转换（Native 层由 libyuv 实现，NEON 加速）。
 */
public final class YuvUtil {
    static {
        System.loadLibrary("learn-ffmpeg");
    }

    private YuvUtil() {
    }

    /**
     * 把 YUV_420_888 的 Image 转成 I420（Y、U、V 三个平面依次紧密排列）。
     *
     * YUV_420_888 的实际内存排列由硬件决定（三平面或 NV12/NV21 式交错，可能带行填充），
     * libyuv 会根据 rowStride / pixelStride 自动选择对应的快速路径。
     *
     * @param image 格式必须是 {@link ImageFormat#YUV_420_888}
     * @param dst   可复用的输出数组；为 null 或长度不对时新分配
     * @return I420 数据；格式不对或转换失败返回 null
     */
    public static byte[] android420ToI420(Image image, byte[] dst) {
        if (image == null || image.getFormat() != ImageFormat.YUV_420_888) return null;
        int width = image.getWidth();
        int height = image.getHeight();
        int size = width * height + ((width + 1) / 2) * ((height + 1) / 2) * 2;
        if (dst == null || dst.length != size) dst = new byte[size];
        Image.Plane[] planes = image.getPlanes();
        boolean ok = nativeAndroid420ToI420(
                planes[0].getBuffer(), planes[0].getRowStride(),
                planes[1].getBuffer(), planes[1].getRowStride(),
                planes[2].getBuffer(), planes[2].getRowStride(),
                planes[1].getPixelStride(), width, height, dst);
        return ok ? dst : null;
    }

    private static native boolean nativeAndroid420ToI420(ByteBuffer y, int yRowStride,
                                                         ByteBuffer u, int uRowStride,
                                                         ByteBuffer v, int vRowStride,
                                                         int uvPixelStride, int width, int height,
                                                         byte[] dst);
}
