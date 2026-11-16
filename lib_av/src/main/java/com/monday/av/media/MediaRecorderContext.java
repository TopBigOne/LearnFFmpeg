package com.monday.av.media;

import java.nio.ByteBuffer;

public abstract class MediaRecorderContext {
    static {
        // 原来依赖 FFMediaPlayer 先被加载时顺带加载 so，单独使用录制模块会找不到 native 方法
        System.loadLibrary("learn-ffmpeg");
    }

    public static final int IMAGE_FORMAT_RGBA = 0x01;
    public static final int IMAGE_FORMAT_NV21 = 0x02;
    public static final int IMAGE_FORMAT_NV12 = 0x03;
    public static final int IMAGE_FORMAT_I420 = 0x04;

    public static final int RECORDER_TYPE_SINGLE_VIDEO   = 0; //仅录制视频
    public static final int RECORDER_TYPE_SINGLE_AUDIO   = 1; //仅录制音频
    public static final int RECORDER_TYPE_AV             = 2; //同时录制音频和视频,打包成 MP4 文件

    private long mNativeContextHandle;

    protected native void native_CreateContext();

    protected native void native_DestroyContext();

    protected native int native_Init();

    protected native int native_UnInit();

    protected native int native_StartRecord(int recorderType, String outUrl, int frameWidth, int frameHeight, long videoBitRate, int fps);

    /**
     * @param captureTimeNs 第一个采样的采集时刻，SystemClock.elapsedRealtimeNanos() 时基；<=0 表示未知
     */
    protected native void native_OnAudioData(byte[] data, int len, long captureTimeNs);

    /**
     * @param captureTimeNs 画面的采集时刻，SystemClock.elapsedRealtimeNanos() 时基；<=0 表示未知
     */
    protected native void native_OnPreviewFrame(int format, byte[] data, int width, int height, long captureTimeNs);

    /**
     * 相机原始图像（YUV_420_888 三个平面，direct ByteBuffer），Native 层用 libyuv 直接转进渲染缓冲。
     * @param captureTimeNs 画面的采集时刻，SystemClock.elapsedRealtimeNanos() 时基；<=0 表示未知
     */
    protected native void native_OnPreviewImage(ByteBuffer y, int yRowStride,
                                                ByteBuffer u, int uRowStride,
                                                ByteBuffer v, int vRowStride,
                                                int uvPixelStride, int width, int height,
                                                long captureTimeNs);

    protected native int native_StopRecord();

    protected native void native_SetTransformMatrix(float translateX, float translateY, float scaleX, float scaleY, int degree, int mirror);

    protected native void native_OnSurfaceCreated();

    protected native void native_OnSurfaceChanged(int width, int height);

    protected native void native_OnDrawFrame();

    protected native void native_SetFilterData(int index, int format, int width, int height, byte[] bytes);

    protected native void native_SetFragShader(int index, String str);
}
