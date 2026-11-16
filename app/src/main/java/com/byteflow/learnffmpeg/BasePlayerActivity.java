package com.byteflow.learnffmpeg;

import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.os.Bundle;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.OnBackPressedCallback;
import androidx.annotation.LayoutRes;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.ActionBar;
import androidx.appcompat.app.AppCompatActivity;
import androidx.constraintlayout.widget.ConstraintLayout;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import com.monday.av.media.FFMediaPlayer;
import com.monday.av.widget.DoubleTapSeekView;
import com.monday.av.widget.PlayerControlView;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

import static com.monday.av.media.FFMediaPlayer.MEDIA_PARAM_VIDEO_DURATION;
import static com.monday.av.media.FFMediaPlayer.MEDIA_PARAM_VIDEO_HEIGHT;
import static com.monday.av.media.FFMediaPlayer.MEDIA_PARAM_VIDEO_WIDTH;
import static com.monday.av.media.FFMediaPlayer.MSG_DECODER_DONE;
import static com.monday.av.media.FFMediaPlayer.MSG_DECODER_INIT_ERROR;
import static com.monday.av.media.FFMediaPlayer.MSG_DECODER_READY;
import static com.monday.av.media.FFMediaPlayer.MSG_DECODING_TIME;
import static com.monday.av.media.FFMediaPlayer.MSG_PLAYER_ERROR;
import static com.monday.av.media.FFMediaPlayer.MSG_REQUEST_RENDER;

/**
 * 软解播放页的公共部分：播放列表（上一个/下一个）、控制栏、双击快退/快进、横竖屏全屏。
 *
 * 子类只负责渲染相关的差异：
 *  - {@link #getLayoutId()}：布局里必须包含 player_container、player_control_view、double_tap_seek_view、tv_video_title；
 *  - {@link #isRenderTargetReady()} / {@link #initPlayer}：渲染目标（Surface 或 GL）是否可用、怎么初始化播放器；
 *  - 何时调用 {@link #openCurrentSource(long)} / {@link #releasePlayer()}（随 Surface 还是随页面生命周期）。
 *
 * 播放列表：MainActivity 启动时拷贝到 {@code getExternalFilesDir()/byteflow/} 下的视频，按文件名排序。
 * 横竖屏：子类需要在清单里声明 configChanges，旋转时页面不重建、播放不中断，这里只调整布局。
 */
public abstract class BasePlayerActivity extends AppCompatActivity implements PlayerControlView.Listener {
    private static final String VIDEO_DIR = "byteflow";
    private static final String DEFAULT_VIDEO = "one_piece.mp4";
    private static final long SEEK_STEP_MS = 5000;

    // 点击倍速按钮依次切换；切换视频源时播放器会重新创建，倍速由这里保持
    private static final float[] SPEEDS = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
    private int mSpeedIndex = 2;

    protected View mPlayerContainer;
    protected PlayerControlView mControlView;
    protected DoubleTapSeekView mDoubleTapSeekView;
    private TextView mTitleView;

    // 主线程写；native 回调线程会读它来过滤旧播放器的重绘请求，所以用 volatile
    @Nullable
    private volatile FFMediaPlayer mMediaPlayer = null;

    private final List<File> mPlaylist = new ArrayList<>();
    private int mPlaylistIndex = 0;

    private long mDurationMs = 0;
    private boolean mPlaying = false;
    private boolean mCompleted = false;              // 已播放到结尾
    // 播放器准备好之后要跳转到的位置（毫秒）
    private long mPendingStartPositionMs = 0;

    private final OnBackPressedCallback mExitFullscreenOnBack = new OnBackPressedCallback(false) {
        @Override
        public void handleOnBackPressed() {
            // 横屏全屏时，返回键先退回竖屏
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
        }
    };

    @LayoutRes
    protected abstract int getLayoutId();

    /** 渲染目标是否可用；不可用时 {@link #openCurrentSource} 只重置界面，不创建播放器 */
    protected abstract boolean isRenderTargetReady();

    /** 用 url 初始化播放器（调用 player.init） */
    protected abstract void initPlayer(@NonNull FFMediaPlayer player, @NonNull String url);

    /** 拿到视频宽高，用于调整画面比例 */
    protected abstract void onVideoSizeChanged(int width, int height);

    /** 播放器请求重绘（GL 渲染需要） */
    protected void onRequestRender() {}

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(getLayoutId());

        mPlayerContainer = findViewById(R.id.player_container);
        mControlView = findViewById(R.id.player_control_view);
        mDoubleTapSeekView = findViewById(R.id.double_tap_seek_view);
        mTitleView = findViewById(R.id.tv_video_title);

        mControlView.setListener(this);
        mControlView.setSpeedText(formatSpeed(SPEEDS[mSpeedIndex]));
        setupDoubleTapSeek();

        getOnBackPressedDispatcher().addCallback(this, mExitFullscreenOnBack);

        loadPlaylist();
        updateSourceInfo();
        applyOrientation(getResources().getConfiguration().orientation);
    }

    /** 单击视频：显示/隐藏控制栏；双击左右两侧：每次后退/快进 5 秒，连点累加 */
    private void setupDoubleTapSeek() {
        mDoubleTapSeekView.setSeekStepMs(SEEK_STEP_MS);
        mDoubleTapSeekView.setCallback(new DoubleTapSeekView.Callback() {
            @Override
            public long getCurrentPosition() {
                return mControlView.getPosition();
            }

            @Override
            public long getDuration() {
                return mMediaPlayer != null ? mDurationMs : 0;
            }

            @Override
            public void onSingleTap() {
                mControlView.toggleVisibility();
            }

            @Override
            public void onSeekPreview(long targetMs) {
                mControlView.showSeekPreview(targetMs);
            }

            @Override
            public void onSeekCommit(long targetMs) {
                mControlView.showSeekPreview(targetMs);
                seekTo(targetMs);
            }
        });
    }

    // ---------------------------------------------------------------- 播放列表

    private void loadPlaylist() {
        mPlaylist.clear();
        File dir = new File(getExternalFilesDir(null), VIDEO_DIR);
        File[] files = dir.listFiles((d, name) -> name.toLowerCase(Locale.US).endsWith(".mp4"));
        if (files != null) {
            Arrays.sort(files, (a, b) -> a.getName().compareToIgnoreCase(b.getName()));
            mPlaylist.addAll(Arrays.asList(files));
        }
        if (mPlaylist.isEmpty()) {
            // 素材还没拷贝出来时仍然尝试默认路径，播放失败会有提示
            mPlaylist.add(new File(dir, DEFAULT_VIDEO));
        }
        mPlaylistIndex = 0;
        for (int i = 0; i < mPlaylist.size(); i++) {
            if (DEFAULT_VIDEO.equals(mPlaylist.get(i).getName())) {
                mPlaylistIndex = i;
                break;
            }
        }
    }

    private void switchTo(int index) {
        if (index < 0 || index >= mPlaylist.size() || index == mPlaylistIndex) return;
        mPlaylistIndex = index;
        updateSourceInfo();
        openCurrentSource(0);
    }

    private void updateSourceInfo() {
        File file = mPlaylist.get(mPlaylistIndex);
        mTitleView.setText(String.format(Locale.US, "%s（%d / %d）",
                file.getName(), mPlaylistIndex + 1, mPlaylist.size()));
        mControlView.setPreviousEnabled(mPlaylistIndex > 0);
        mControlView.setNextEnabled(mPlaylistIndex < mPlaylist.size() - 1);
    }

    // ---------------------------------------------------------------- 播放器

    /** 释放当前播放器，用当前视频源新建一个并开始播放；准备好后跳到 startPositionMs */
    protected final void openCurrentSource(long startPositionMs) {
        releasePlayer();
        mControlView.reset();
        mDurationMs = 0;
        mCompleted = false;
        if (!isRenderTargetReady()) return;   // 渲染目标就绪后由子类再次调用

        mPendingStartPositionMs = startPositionMs;
        final FFMediaPlayer player = new FFMediaPlayer();
        mMediaPlayer = player;
        // 回调发生在 native 线程；切换视频源后，旧播放器可能还有事件在路上，按实例过滤掉
        player.addEventCallback((msgType, msgValue) -> {
            if (msgType == MSG_REQUEST_RENDER) {
                // 重绘请求很频繁，不必切到主线程（GLSurfaceView.requestRender 线程安全）
                if (player == mMediaPlayer) onRequestRender();
                return;
            }
            runOnUiThread(() -> {
                if (player == mMediaPlayer) onPlayerEvent(msgType, msgValue);
            });
        });
        initPlayer(player, mPlaylist.get(mPlaylistIndex).getAbsolutePath());
        player.setPlaybackSpeed(SPEEDS[mSpeedIndex]);
        player.play();
        setPlaying(true);
    }

    protected final void releasePlayer() {
        if (mMediaPlayer == null) return;
        FFMediaPlayer player = mMediaPlayer;
        mMediaPlayer = null;   // 先置空，之后到达的旧事件会被过滤
        player.stop();
        player.unInit();
        mPlaying = false;
    }

    /** 当前播放位置（毫秒），以控制栏显示的为准 */
    protected final long getCurrentPositionMs() {
        return mControlView.getPosition();
    }

    protected final boolean isPlaying() {
        return mPlaying;
    }

    protected final void pausePlayback() {
        if (mMediaPlayer == null || !mPlaying) return;
        mMediaPlayer.pause();
        setPlaying(false);
    }

    protected final void startPlayback() {
        if (mMediaPlayer == null || mPlaying) return;
        mMediaPlayer.play();   // 播完后调用会从头播放（播放器状态机 Completed --Start--> Playing）
        mCompleted = false;
        setPlaying(true);
    }

    private void setPlaying(boolean playing) {
        mPlaying = playing;
        mControlView.setPlaying(playing);
    }

    private void onPlayerEvent(int msgType, float msgValue) {
        switch (msgType) {
            case MSG_DECODER_READY:
                onDecoderReady();
                break;
            case MSG_DECODING_TIME:
                mControlView.setPosition((long) (msgValue * 1000));
                break;
            case MSG_DECODER_DONE:
                // 播完：停在结尾，再点播放从头开始
                mCompleted = true;
                setPlaying(false);
                mControlView.setPosition(mDurationMs);
                break;
            case MSG_DECODER_INIT_ERROR:
            case MSG_PLAYER_ERROR:
                setPlaying(false);
                Toast.makeText(this, "播放失败：" + mPlaylist.get(mPlaylistIndex).getName(), Toast.LENGTH_SHORT).show();
                break;
            default:
                break;
        }
    }

    private void onDecoderReady() {
        if (mMediaPlayer == null) return;
        int videoWidth = (int) mMediaPlayer.getMediaParams(MEDIA_PARAM_VIDEO_WIDTH);
        int videoHeight = (int) mMediaPlayer.getMediaParams(MEDIA_PARAM_VIDEO_HEIGHT);
        if (videoWidth > 0 && videoHeight > 0) {
            onVideoSizeChanged(videoWidth, videoHeight);
        }
        // 播放器返回的时长单位是秒
        mDurationMs = mMediaPlayer.getMediaParams(MEDIA_PARAM_VIDEO_DURATION) * 1000;
        mControlView.setDuration(mDurationMs);

        if (mPendingStartPositionMs > 0) {
            mMediaPlayer.seekToPosition(mPendingStartPositionMs / 1000f);
            mControlView.setPosition(mPendingStartPositionMs);
            mPendingStartPositionMs = 0;
        }
    }

    /** 拖动进度条和双击快进/快退都走这里 */
    private void seekTo(long positionMs) {
        if (mMediaPlayer == null) return;
        mMediaPlayer.seekToPosition(positionMs / 1000f);
        if (mCompleted) {
            // 播放器状态机：Completed --SeekTo--> Playing，界面同步成播放中
            mCompleted = false;
            setPlaying(true);
        }
    }

    // ---------------------------------------------------------------- 控制栏回调

    @Override
    public void onPlayPauseClick() {
        if (mPlaying) pausePlayback();
        else startPlayback();
    }

    @Override
    public void onSeek(long positionMs) {
        seekTo(positionMs);
    }

    @Override
    public void onPreviousClick() {
        switchTo(mPlaylistIndex - 1);
    }

    @Override
    public void onNextClick() {
        switchTo(mPlaylistIndex + 1);
    }

    @Override
    public void onFullscreenClick(boolean toFullscreen) {
        // SENSOR_LANDSCAPE：横屏后仍可随重力在左右两个横屏方向间翻转
        setRequestedOrientation(toFullscreen
                ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                : ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
    }

    @Override
    public void onSpeedClick() {
        mSpeedIndex = (mSpeedIndex + 1) % SPEEDS.length;
        mControlView.setSpeedText(formatSpeed(SPEEDS[mSpeedIndex]));
        if (mMediaPlayer != null) mMediaPlayer.setPlaybackSpeed(SPEEDS[mSpeedIndex]);
    }

    // ---------------------------------------------------------------- 横竖屏

    @Override
    public void onConfigurationChanged(@NonNull Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        applyOrientation(newConfig.orientation);
    }

    private void applyOrientation(int orientation) {
        boolean landscape = orientation == Configuration.ORIENTATION_LANDSCAPE;

        // 竖屏：宽度铺满、16:9 置顶；横屏：铺满整个屏幕
        ConstraintLayout.LayoutParams lp = (ConstraintLayout.LayoutParams) mPlayerContainer.getLayoutParams();
        lp.dimensionRatio = landscape ? null : "H,16:9";
        lp.bottomToBottom = landscape ? ConstraintLayout.LayoutParams.PARENT_ID : ConstraintLayout.LayoutParams.UNSET;
        mPlayerContainer.setLayoutParams(lp);
        mTitleView.setVisibility(landscape ? View.GONE : View.VISIBLE);

        // 横屏隐藏状态栏、导航栏和标题栏，从屏幕边缘滑动可临时呼出
        WindowInsetsControllerCompat controller = WindowCompat.getInsetsController(getWindow(), getWindow().getDecorView());
        controller.setSystemBarsBehavior(WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        if (landscape) controller.hide(WindowInsetsCompat.Type.systemBars());
        else controller.show(WindowInsetsCompat.Type.systemBars());
        ActionBar actionBar = getSupportActionBar();
        if (actionBar != null) {
            if (landscape) actionBar.hide();
            else actionBar.show();
        }

        mControlView.setFullscreen(landscape);
        mExitFullscreenOnBack.setEnabled(landscape);
    }

    private static String formatSpeed(float speed) {
        return (speed == (int) speed ? String.format(Locale.US, "%.1f", speed) : String.valueOf(speed)) + "x";
    }
}
