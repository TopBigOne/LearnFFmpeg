/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

package com.byteflow.learnffmpeg;

import android.content.pm.ActivityInfo;
import android.content.res.Configuration;
import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;

import androidx.activity.OnBackPressedCallback;
import androidx.annotation.NonNull;
import androidx.appcompat.app.ActionBar;
import androidx.appcompat.app.AppCompatActivity;
import androidx.constraintlayout.widget.ConstraintLayout;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

import com.monday.av.media.FFMediaPlayer;
import com.monday.av.media.MySurfaceView;
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
import static com.monday.av.media.FFMediaPlayer.VIDEO_RENDER_ANWINDOW;

/**
 * ANativeWindow 渲染的软解播放页。
 *
 * 播放列表：MainActivity 启动时拷贝到 {@code getExternalFilesDir()/byteflow/} 下的视频，按文件名排序。
 * 横竖屏：清单里声明了 configChanges，旋转时页面和 Surface 都不重建，播放不中断，只调整布局。
 */
public class NativeMediaPlayerActivity extends AppCompatActivity implements SurfaceHolder.Callback, PlayerControlView.Listener {
    private static final String TAG = "NativeMediaPlayer";
    private static final String VIDEO_DIR = "byteflow";
    private static final String DEFAULT_VIDEO = "one_piece.mp4";

    // 点击倍速按钮依次切换；切换视频源或 Surface 重建时播放器会重新创建，倍速由这里保持
    private static final float[] SPEEDS = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
    private int mSpeedIndex = 2;

    private View mPlayerContainer;
    private MySurfaceView mSurfaceView;
    private PlayerControlView mControlView;
    private TextView mTitleView;

    private FFMediaPlayer mMediaPlayer = null;
    private SurfaceHolder mSurfaceHolder = null;     // Surface 可用时非空

    private final List<File> mPlaylist = new ArrayList<>();
    private int mPlaylistIndex = 0;

    private long mDurationMs = 0;
    private boolean mPlaying = false;
    // 播放器准备好之后要跳转到的位置（毫秒）：切后台再回来时从原位置继续
    private long mPendingStartPositionMs = 0;

    private final OnBackPressedCallback mExitFullscreenOnBack = new OnBackPressedCallback(false) {
        @Override
        public void handleOnBackPressed() {
            // 横屏全屏时，返回键先退回竖屏
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_native_media_player);

        mPlayerContainer = findViewById(R.id.player_container);
        mSurfaceView = findViewById(R.id.surface_view);
        mControlView = findViewById(R.id.player_control_view);
        mTitleView = findViewById(R.id.tv_video_title);

        mSurfaceView.getHolder().addCallback(this);
        // 点视频区域显示/隐藏控制栏
        mPlayerContainer.setOnClickListener(v -> mControlView.toggleVisibility());

        mControlView.setListener(this);
        mControlView.setSpeedText(formatSpeed(SPEEDS[mSpeedIndex]));

        getOnBackPressedDispatcher().addCallback(this, mExitFullscreenOnBack);

        loadPlaylist();
        updateSourceInfo();
        applyOrientation(getResources().getConfiguration().orientation);
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

    /** 释放当前播放器，用当前视频源和 Surface 新建一个并开始播放 */
    private void openCurrentSource(long startPositionMs) {
        releasePlayer();
        mControlView.reset();
        mDurationMs = 0;
        if (mSurfaceHolder == null) return;   // Surface 还没准备好，surfaceCreated 时再打开

        mPendingStartPositionMs = startPositionMs;
        final FFMediaPlayer player = new FFMediaPlayer();
        mMediaPlayer = player;
        // 回调发生在 native 线程；切换视频源后，旧播放器可能还有事件在路上，按实例过滤掉
        player.addEventCallback((msgType, msgValue) -> runOnUiThread(() -> {
            if (player == mMediaPlayer) onPlayerEvent(msgType, msgValue);
        }));
        player.init(mPlaylist.get(mPlaylistIndex).getAbsolutePath(), VIDEO_RENDER_ANWINDOW, mSurfaceHolder.getSurface());
        player.setPlaybackSpeed(SPEEDS[mSpeedIndex]);
        player.play();
        setPlaying(true);
    }

    private void releasePlayer() {
        if (mMediaPlayer == null) return;
        FFMediaPlayer player = mMediaPlayer;
        mMediaPlayer = null;   // 先置空，之后到达的旧事件会被过滤
        player.stop();
        player.unInit();
        mPlaying = false;
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
                // 播完：停在结尾，再点播放从头开始（播放器状态机 Completed --Start--> 从头播放）
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
        int videoWidth = (int) mMediaPlayer.getMediaParams(MEDIA_PARAM_VIDEO_WIDTH);
        int videoHeight = (int) mMediaPlayer.getMediaParams(MEDIA_PARAM_VIDEO_HEIGHT);
        if (videoWidth > 0 && videoHeight > 0) {
            mSurfaceView.setAspectRatio(videoWidth, videoHeight);
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

    // ---------------------------------------------------------------- Surface

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder holder) {
        Log.d(TAG, "surfaceCreated");
        mSurfaceHolder = holder;
        // 首次进入从头播放；切后台再回来时从离开的位置继续
        openCurrentSource(mPendingStartPositionMs);
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int w, int h) {
        // 旋转屏幕也会走到这里，不在这里调用 play()，否则会把用户暂停的视频重新播起来
        Log.d(TAG, "surfaceChanged: " + w + "x" + h);
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        Log.d(TAG, "surfaceDestroyed");
        mPendingStartPositionMs = mControlView.getPosition();
        releasePlayer();
        mSurfaceHolder = null;
    }

    // ---------------------------------------------------------------- 控制栏回调

    @Override
    public void onPlayPauseClick() {
        if (mMediaPlayer == null) return;
        if (mPlaying) {
            mMediaPlayer.pause();
            setPlaying(false);
        } else {
            mMediaPlayer.play();
            setPlaying(true);
        }
    }

    @Override
    public void onSeek(long positionMs) {
        if (mMediaPlayer != null) mMediaPlayer.seekToPosition(positionMs / 1000f);
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
