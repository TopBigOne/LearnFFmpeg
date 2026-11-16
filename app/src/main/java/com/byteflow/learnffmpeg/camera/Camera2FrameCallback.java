package com.byteflow.learnffmpeg.camera;

import android.media.Image;

public interface Camera2FrameCallback {
    /**
     * 预览帧回调（相机后台线程）。
     *
     * @param image       相机原始图像（YUV_420_888），回调返回后会被关闭：只能在回调内使用，
     *                    不能关闭它，也不能交给其他线程。交给录制模块请用 FFMediaRecorder.onPreviewImage，
     *                    需要 byte[] 可用 YuvUtil.android420ToI420
     * @param timestampNs 画面的采集时刻，已换算到 SystemClock.elapsedRealtimeNanos() 时基，
     *                    可以直接和音频采集时刻比较（用于录制时的音视频同步）
     */
    void onPreviewFrame(Image image, long timestampNs);
    void onCaptureFrame(byte[] data, int width, int height);
}
