/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

package com.byteflow.learnffmpeg;

import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceHolder;

import androidx.annotation.NonNull;

import com.monday.av.media.FFMediaPlayer;
import com.monday.av.media.MySurfaceView;

import static com.monday.av.media.FFMediaPlayer.VIDEO_RENDER_ANWINDOW;

/**
 * ANativeWindow 渲染的软解播放页。播放列表、控制栏、手势、横竖屏见 {@link BasePlayerActivity}。
 *
 * 播放器随 Surface 创建和销毁：切后台 Surface 销毁时释放播放器，回来时从离开的位置继续。
 */
public class NativeMediaPlayerActivity extends BasePlayerActivity implements SurfaceHolder.Callback {
    private static final String TAG = "NativeMediaPlayer";

    private MySurfaceView mSurfaceView;
    private SurfaceHolder mSurfaceHolder = null;     // Surface 可用时非空
    private long mResumePositionMs = 0;              // Surface 重建后从这里继续播放

    @Override
    protected int getLayoutId() {
        return R.layout.activity_native_media_player;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mSurfaceView = findViewById(R.id.surface_view);
        mSurfaceView.getHolder().addCallback(this);
    }

    @Override
    protected boolean isRenderTargetReady() {
        return mSurfaceHolder != null;
    }

    @Override
    protected void initPlayer(@NonNull FFMediaPlayer player, @NonNull String url) {
        player.init(url, VIDEO_RENDER_ANWINDOW, mSurfaceHolder.getSurface());
    }

    @Override
    protected void onVideoSizeChanged(int width, int height) {
        mSurfaceView.setAspectRatio(width, height);
    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder holder) {
        Log.d(TAG, "surfaceCreated");
        mSurfaceHolder = holder;
        // 首次进入从头播放；切后台再回来时从离开的位置继续
        openCurrentSource(mResumePositionMs);
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int w, int h) {
        // 旋转屏幕也会走到这里，不在这里调用 play()，否则会把用户暂停的视频重新播起来
        Log.d(TAG, "surfaceChanged: " + w + "x" + h);
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        Log.d(TAG, "surfaceDestroyed");
        mResumePositionMs = getCurrentPositionMs();
        releasePlayer();
        mSurfaceHolder = null;
    }
}
