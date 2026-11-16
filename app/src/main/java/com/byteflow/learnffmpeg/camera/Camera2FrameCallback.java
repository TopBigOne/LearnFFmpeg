package com.byteflow.learnffmpeg.camera;

public interface Camera2FrameCallback {
    /**
     * @param timestampNs 画面的采集时刻，已换算到 SystemClock.elapsedRealtimeNanos() 时基，
     *                    可以直接和音频采集时刻比较（用于录制时的音视频同步）
     */
    void onPreviewFrame(byte[] data, int width, int height, long timestampNs);
    void onCaptureFrame(byte[] data, int width, int height);
}
