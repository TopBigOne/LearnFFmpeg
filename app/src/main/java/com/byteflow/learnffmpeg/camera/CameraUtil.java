package com.byteflow.learnffmpeg.camera;

import android.graphics.Point;
import android.media.Image;
import android.util.Size;

import com.monday.av.media.YuvUtil;


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
     * 把 YUV_420_888 的 Image 转成 I420（Y、U、V 三个平面依次紧密排列），由 libyuv 实现。
     * 每次调用都分配新数组，只适合拍照这类低频场景；预览帧请直接把 Image 交给 FFMediaRecorder.onPreviewImage。
     */
    public static byte[] YUV_420_888_data(Image image) {
        return YuvUtil.android420ToI420(image, null);
    }
}
