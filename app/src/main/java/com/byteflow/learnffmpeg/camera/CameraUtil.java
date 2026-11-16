package com.byteflow.learnffmpeg.camera;

import android.graphics.ImageFormat;
import android.graphics.Point;
import android.media.Image;
import android.util.Size;

import java.nio.ByteBuffer;

public class CameraUtil {
    public static Size getFitInScreenSize(int previewWidth, int previewHeight, int screenWidth,
                                          int screenHeight) {
        Point res = new Point(0, 0);
        float ratioPreview = previewWidth *1f / previewHeight;
        float ratioScreen = 0.0f;

        //landscape
        if (screenWidth > screenHeight) {
            ratioScreen = screenWidth * 1f / screenHeight;
            if (ratioPreview >= ratioScreen) {
                res.x = screenWidth;
                res.y = (int)(res.x * previewHeight * 1f / previewWidth);
            }else {
                res.y = screenHeight;
                res.x = (int)(res.y * previewWidth * 1f / previewHeight);
            }
        //portrait
        }else {
            ratioScreen = screenHeight * 1f / screenWidth;
            if (ratioPreview >= ratioScreen) {
                res.y = screenHeight;
                res.x = (int)(res.y * previewHeight * 1f / previewWidth);
            }else {
                res.x = screenWidth;
                res.y = (int)(res.x * previewWidth * 1f / previewHeight);
            }
        }
        return new Size(res.x, res.y);
    }

    /**
     * 把 YUV_420_888 的 Image 转成 I420（Y、U、V 三个平面依次紧密排列）。
     * 每次调用都分配新数组，只适合拍照这类低频场景；预览帧请用 {@link I420Converter} 复用缓冲。
     */
    public static byte[] YUV_420_888_data(Image image) {
        return new I420Converter().convert(image);
    }

    /**
     * 可复用缓冲的 YUV_420_888 → I420 转换器。
     *
     * 预览帧每秒几十帧、每帧上 MB（720p 约 1.4MB），每帧 new 一个数组会让 GC 频繁运行
     * （实测录制时约每秒 2 次）。转换器持有输出数组和行缓冲，尺寸不变就一直复用。
     *
     * 约束：
     *  - 非线程安全，一个转换器只能在一个线程上使用；
     *  - convert 返回的数组下一次 convert 时会被覆盖，调用方不能持有它，需要保留请自行拷贝。
     */
    public static final class I420Converter {
        private byte[] mData;   // 输出缓冲
        private byte[] mRow;    // 交错平面（pixelStride > 1）逐行读取用的缓冲

        public byte[] convert(Image image) {
            final int imageWidth = image.getWidth();
            final int imageHeight = image.getHeight();
            final int size = imageWidth * imageHeight * ImageFormat.getBitsPerPixel(ImageFormat.YUV_420_888) / 8;
            if (mData == null || mData.length != size) {
                mData = new byte[size];
            }
            final byte[] data = mData;
            final Image.Plane[] planes = image.getPlanes();
            int offset = 0;

            for (int plane = 0; plane < planes.length; ++plane) {
                final ByteBuffer buffer = planes[plane].getBuffer();
                final int rowStride = planes[plane].getRowStride();
                // U、V 平面通常 pixelStride = 2（NV12/NV21 式交错存放）
                final int pixelStride = planes[plane].getPixelStride();
                final int planeWidth = (plane == 0) ? imageWidth : imageWidth / 2;
                final int planeHeight = (plane == 0) ? imageHeight : imageHeight / 2;
                final int base = buffer.position();

                if (pixelStride == 1 && rowStride == planeWidth) {
                    // 紧密排列：整个平面一次拷贝
                    buffer.get(data, offset, planeWidth * planeHeight);
                    offset += planeWidth * planeHeight;
                } else if (pixelStride == 1) {
                    // 只有行尾对齐填充：逐行整块拷贝，跳过填充
                    for (int row = 0; row < planeHeight; ++row) {
                        buffer.position(base + row * rowStride);
                        buffer.get(data, offset, planeWidth);
                        offset += planeWidth;
                    }
                } else {
                    // 交错存放：逐行读入行缓冲，再按 pixelStride 取样
                    if (mRow == null || mRow.length < rowStride) {
                        mRow = new byte[rowStride];
                    }
                    final byte[] rowData = mRow;
                    for (int row = 0; row < planeHeight; ++row) {
                        buffer.position(base + row * rowStride);
                        // 部分设备最后一行不足 rowStride 字节，只读剩余部分
                        // 见 https://developer.android.com/reference/android/media/Image.Plane#getBuffer()
                        buffer.get(rowData, 0, Math.min(rowStride, buffer.remaining()));
                        for (int col = 0; col < planeWidth; ++col) {
                            data[offset++] = rowData[col * pixelStride];
                        }
                    }
                }
            }
            return data;
        }
    }
}
