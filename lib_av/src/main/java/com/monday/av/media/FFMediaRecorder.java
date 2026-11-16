package com.monday.av.media;

import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.opengl.GLSurfaceView;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class FFMediaRecorder extends MediaRecorderContext implements GLSurfaceView.Renderer {
    private static final String TAG = "CameraRender";
    private GLSurfaceView mGLSurfaceView;

    public FFMediaRecorder() {
    }

    public void init(GLSurfaceView surfaceView) { //for Video
        mGLSurfaceView = surfaceView;
        mGLSurfaceView.setEGLContextClientVersion(2);
        mGLSurfaceView.setRenderer(this);
        mGLSurfaceView.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);

        native_CreateContext();
        native_Init();
    }

    public void init() { //for audio
        native_CreateContext();
        native_Init();
    }

    public void requestRender() {
        if (mGLSurfaceView != null) {
            mGLSurfaceView.requestRender();
        }
    }

    public void setTransformMatrix(int degree, int mirror) {
        Log.d(TAG, "setTransformMatrix() called with: degree = [" + degree + "], mirror = [" + mirror + "]");
        native_SetTransformMatrix(0, 0, 1, 1, degree, mirror);
    }

    public void startRecord(int recorderType, String outUrl, int frameWidth, int frameHeight, long videoBitRate, int fps) {
        Log.d(TAG, "startRecord() called with: recorderType = [" + recorderType + "], outUrl = [" + outUrl + "], frameWidth = [" + frameWidth + "], frameHeight = [" + frameHeight + "], videoBitRate = [" + videoBitRate + "], fps = [" + fps + "]");
        native_StartRecord(recorderType, outUrl, frameWidth, frameHeight, videoBitRate, fps);
    }

    /**
     * 送入一帧相机画面。
     *
     * @param captureTimeNs 画面的采集时刻，必须是 {@link android.os.SystemClock#elapsedRealtimeNanos()} 时基
     *                      （Camera2 换算方法见 Camera2Wrapper）；传 0 表示未知，Native 层用到达时间近似
     */
    public void onPreviewFrame(int format, byte[] data, int width, int height, long captureTimeNs) {
        native_OnPreviewFrame(format, data, width, height, captureTimeNs);
    }

    /** 不带采集时刻的旧接口：以到达时间近似，音视频同步精度较差 */
    public void onPreviewFrame(int format, byte[] data, int width, int height) {
        onPreviewFrame(format, data, width, height, 0);
    }

    /**
     * 送入一块 PCM。
     *
     * @param captureTimeNs 第一个采样的采集时刻，{@link android.os.SystemClock#elapsedRealtimeNanos()} 时基
     *                      （见 AudioRecorder）；传 0 表示未知，Native 层用到达时间近似
     */
    public void onAudioData(byte[] data, int size, long captureTimeNs) {
        native_OnAudioData(data, size, captureTimeNs);
    }

    /** 不带采集时刻的旧接口：以到达时间近似 */
    public void onAudioData(byte[] data, int size) {
        onAudioData(data, size, 0);
    }

    public void stopRecord() {
        Log.d(TAG, "stopRecord() called");
        native_StopRecord();
    }

    public void loadShaderFromAssetsFile(int shaderIndex, Resources r) {
        String result = null;
        try {
            InputStream in = r.getAssets().open("shaders/fshader_" + shaderIndex + ".glsl");
            int ch = 0;
            ByteArrayOutputStream baos = new ByteArrayOutputStream();
            while ((ch = in.read()) != -1) {
                baos.write(ch);
            }
            byte[] buff = baos.toByteArray();
            baos.close();
            in.close();
            result = new String(buff, "UTF-8");
            result = result.replaceAll("\\r\\n", "\n");
        } catch (Exception e) {
            e.printStackTrace();
        }

        if (result != null) {
            setFragShader(shaderIndex, result);
        }
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        Log.d(TAG, "onSurfaceCreated() called with: gl = [" + gl + "], config = [" + config + "]");
        native_OnSurfaceCreated();

    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        Log.d(TAG, "onSurfaceChanged() called with: gl = [" + gl + "], width = [" + width + "], height = [" + height + "]");
        native_OnSurfaceChanged(width, height);

    }

    @Override
    public void onDrawFrame(GL10 gl) {
        // 每帧调用，不打日志：字符串拼接 + logcat 写入每秒几十次，徒增 GC 和 CPU 开销
        native_OnDrawFrame();
    }

    public void setFilterData(int index, int format, int width, int height, byte[] bytes) {
        native_SetFilterData(index, format, width, height, bytes);
    }

    public void setFragShader(int index, String str) {
        native_SetFragShader(index, str);
    }

    public void unInit() {
        native_UnInit();
        native_DestroyContext();
    }
}
