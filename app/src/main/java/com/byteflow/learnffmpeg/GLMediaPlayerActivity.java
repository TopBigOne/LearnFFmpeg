/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

package com.byteflow.learnffmpeg;

import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.util.Log;

import androidx.annotation.NonNull;

import com.monday.av.media.FFMediaPlayer;
import com.monday.av.media.MyGLSurfaceView;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

import static com.monday.av.media.FFMediaPlayer.VIDEO_GL_RENDER;
import static com.monday.av.media.FFMediaPlayer.VIDEO_RENDER_OPENGL;

/**
 * OpenGL ES 渲染的软解播放页。播放列表、控制栏、手势、横竖屏见 {@link BasePlayerActivity}。
 *
 * 播放器不依赖 Surface（解码出的帧交给 native 的 VideoGLRender，由 GLSurfaceView 的渲染线程绘制），
 * 所以随页面生命周期：进入时创建，切后台暂停，回来继续，退出时释放。
 * GL 画面支持拖动旋转、双指缩放，这些触摸由手势层识别完单击/双击后透传过来。
 */
public class GLMediaPlayerActivity extends BasePlayerActivity implements GLSurfaceView.Renderer, MyGLSurfaceView.OnGestureCallback {
    private static final String TAG = "GLMediaPlayer";

    private MyGLSurfaceView mGLSurfaceView;
    private boolean mResumeOnForeground = false;     // 切后台前在播放，回来时继续

    @Override
    protected int getLayoutId() {
        return R.layout.activity_gl_media_player;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mGLSurfaceView = findViewById(R.id.surface_view);
        mGLSurfaceView.setEGLContextClientVersion(3);
        mGLSurfaceView.setRenderer(this);
        mGLSurfaceView.addOnGestureCallback(this);
        mGLSurfaceView.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
        // 手势层盖在 GL 画面上，拖动、缩放等触摸透传给它
        mDoubleTapSeekView.setPassThroughTarget(mGLSurfaceView);

        openCurrentSource(0);
    }

    @Override
    protected void onResume() {
        super.onResume();
        mGLSurfaceView.onResume();
        if (mResumeOnForeground) {
            mResumeOnForeground = false;
            startPlayback();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        mResumeOnForeground = isPlaying();
        pausePlayback();
        mGLSurfaceView.onPause();
    }

    @Override
    protected void onDestroy() {
        releasePlayer();
        super.onDestroy();
    }

    // ---------------------------------------------------------------- BasePlayerActivity

    @Override
    protected boolean isRenderTargetReady() {
        return true;
    }

    @Override
    protected void initPlayer(@NonNull FFMediaPlayer player, @NonNull String url) {
        player.init(url, VIDEO_RENDER_OPENGL, null);
    }

    @Override
    protected void onVideoSizeChanged(int width, int height) {
        mGLSurfaceView.setAspectRatio(width, height);
    }

    @Override
    protected void onRequestRender() {
        mGLSurfaceView.requestRender();
    }

    // ---------------------------------------------------------------- GLSurfaceView.Renderer（GL 线程）

    @Override
    public void onSurfaceCreated(GL10 gl10, EGLConfig eglConfig) {
        FFMediaPlayer.native_OnSurfaceCreated(VIDEO_GL_RENDER);
    }

    @Override
    public void onSurfaceChanged(GL10 gl10, int w, int h) {
        Log.d(TAG, "onSurfaceChanged: " + w + "x" + h);
        FFMediaPlayer.native_OnSurfaceChanged(VIDEO_GL_RENDER, w, h);
    }

    @Override
    public void onDrawFrame(GL10 gl10) {
        FFMediaPlayer.native_OnDrawFrame(VIDEO_GL_RENDER);
    }

    // ---------------------------------------------------------------- 画面手势

    @Override
    public void onGesture(int xRotateAngle, int yRotateAngle, float scale) {
        FFMediaPlayer.native_SetGesture(VIDEO_GL_RENDER, xRotateAngle, yRotateAngle, scale);
    }

    @Override
    public void onTouchLoc(float touchX, float touchY) {
        FFMediaPlayer.native_SetTouchLoc(VIDEO_GL_RENDER, touchX, touchY);
    }
}
