package com.byteflow.learnffmpeg.media;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.view.Surface;

public class FFMediaPlayer {
    //gl render type
    public static final int VIDEO_GL_RENDER = 0;
    public static final int AUDIO_GL_RENDER = 1;
    public static final int VR_3D_GL_RENDER = 2;

    //player type
    public static final int FFMEDIA_PLAYER = 0;
    public static final int HWCODEC_PLAYER = 1;

    static {
        System.loadLibrary("learn-ffmpeg");
    }

    public static final int MSG_DECODER_INIT_ERROR      = 0;
    public static final int MSG_DECODER_READY           = 1;
    public static final int MSG_DECODER_DONE            = 2;
    public static final int MSG_REQUEST_RENDER          = 3;
    public static final int MSG_DECODING_TIME           = 4;
    // 播放过程中出错（IO 错误、网络超时等），msgValue 为下面的 MEDIA_ERROR_*；
    // MSG_DECODER_INIT_ERROR 的 msgValue 同样是 MEDIA_ERROR_*（仅 FFMPEG 播放器）
    public static final int MSG_PLAYER_ERROR            = 5;

    // 错误类别，取值与 android.media.MediaPlayer 一致
    public static final int MEDIA_ERROR_UNKNOWN         = 1;
    public static final int MEDIA_ERROR_IO              = -1004;
    public static final int MEDIA_ERROR_MALFORMED       = -1007;
    public static final int MEDIA_ERROR_UNSUPPORTED     = -1010;
    public static final int MEDIA_ERROR_TIMED_OUT       = -110;

    public static final int MEDIA_PARAM_VIDEO_WIDTH     = 0x0001;
    public static final int MEDIA_PARAM_VIDEO_HEIGHT    = 0x0002;
    public static final int MEDIA_PARAM_VIDEO_DURATION  = 0x0003;

    public static final int MEDIA_PARAM_ASSET_MANAGER   = 0x0020;
    // "边播边录"输出文件路径（String），仅 FFMPEG 播放器 + VIDEO_RENDER_ANWINDOW 渲染时生效，需在 play() 之前设置
    public static final int MEDIA_PARAM_RECORD_PATH     = 0x0021;

    public static final int VIDEO_RENDER_OPENGL         = 0;
    public static final int VIDEO_RENDER_ANWINDOW       = 1;
    public static final int VIDEO_RENDER_3D_VR          = 2;

    // 事件回调在 native 分发线程，Activity 再转到 UI 线程，可能晚于 unInit 执行：
    // unInit 先把句柄清零，之后的调用都会在 native 层被忽略，避免访问已释放的播放器
    private volatile long mNativePlayerHandle = 0;

    private EventCallback mEventCallback = null;

    private AudioTrack mAudioTrack;

    public void createTrack(int sampleRateInHz, int nb_channels) {
        int channelConfig;
        if(nb_channels == 1) {
            channelConfig = AudioFormat.CHANNEL_OUT_MONO;
        } else if(nb_channels == 2) {
            channelConfig = AudioFormat.CHANNEL_OUT_STEREO;
        } else {
            channelConfig = AudioFormat.CHANNEL_OUT_MONO;
        }
        int bufferSize = AudioTrack.getMinBufferSize(sampleRateInHz, channelConfig, AudioFormat.ENCODING_PCM_16BIT);
        mAudioTrack = new AudioTrack(AudioManager.STREAM_MUSIC, sampleRateInHz, channelConfig,
                AudioFormat.ENCODING_PCM_16BIT, bufferSize, AudioTrack.MODE_STREAM);
        mAudioTrack.play();
    }

    public void playTrack(byte[] buffer, int lenght) {
        if(mAudioTrack != null) {
            mAudioTrack.write(buffer, 0, lenght);
        }
    }

    public static String GetFFmpegVersion() {
        return native_GetFFmpegVersion();
    }

    public void init(String url, int videoRenderType, Surface surface) {
        mNativePlayerHandle = native_Init(url, FFMEDIA_PLAYER, videoRenderType, surface);
    }

    public void init(String url, int playerType, int videoRenderType, Surface surface) {
        mNativePlayerHandle = native_Init(url, playerType, videoRenderType, surface);
    }

    public void play() {
        native_Play(mNativePlayerHandle);
    }

    public void pause() {
        native_Pause(mNativePlayerHandle);
    }

    public void seekToPosition(float position) {
        native_SeekToPosition(mNativePlayerHandle, position);
    }

    public void stop() {
        native_Stop(mNativePlayerHandle);
    }

    public void unInit() {
        long handle = mNativePlayerHandle;
        mNativePlayerHandle = 0;
        native_UnInit(handle);
        if(mAudioTrack != null) {
            mAudioTrack.stop();
            mAudioTrack.release();
        }
    }

    public void addEventCallback(EventCallback callback) {
        mEventCallback = callback;
    }

    public long getMediaParams(int paramType) {
        return native_GetMediaParams(mNativePlayerHandle, paramType);
    }

    public void setMediaParams(int paramType, Object param) {
        native_SetMediaParams(mNativePlayerHandle, paramType, param);
    }

    // 由 native 事件分发线程调用（不是 UI 线程），实现方需要自行切换线程
    private void playerEventCallback(int msgType, float msgValue) {
        if(mEventCallback != null)
            mEventCallback.onPlayerEvent(msgType, msgValue);

    }

    private static native String native_GetFFmpegVersion();

    private native long native_Init(String url, int playerType, int renderType, Object surface);

    private native void native_Play(long playerHandle);

    private native void native_SeekToPosition(long playerHandle, float position);

    private native void native_Pause(long playerHandle);

    private native void native_Stop(long playerHandle);

    private native void native_UnInit(long playerHandle);

    private native long native_GetMediaParams(long playerHandle, int paramType);

    private native void native_SetMediaParams(long playerHandle, int paramType, Object param);

    //for GL render
    public static native void native_OnSurfaceCreated(int renderType);
    public static native void native_OnSurfaceChanged(int renderType, int width, int height);
    public static native void native_OnDrawFrame(int renderType);
    //update MVP matrix
    public static native void native_SetGesture(int renderType, float xRotateAngle, float yRotateAngle, float scale);
    public static native void native_SetTouchLoc(int renderType, float touchX, float touchY);

    public interface EventCallback {
        void onPlayerEvent(int msgType, float msgValue);
    }

}
