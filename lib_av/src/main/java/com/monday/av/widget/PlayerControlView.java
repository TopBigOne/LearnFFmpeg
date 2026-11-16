package com.monday.av.widget;

import android.content.Context;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import com.monday.av.R;

import java.util.Locale;

/**
 * 播放器底部控制栏：进度条 + 播放/暂停 + 上一个/下一个 + 时间 + 倍速 + 横竖屏切换。
 *
 * 只负责显示和收集操作，不持有播放器：
 *  - 播放器的状态通过 {@link #setDuration}、{@link #setPosition}、{@link #setPlaying} 等方法告诉它；
 *  - 用户操作通过 {@link Listener} 回调出去，由页面决定怎么处理（比如切换横屏、切换视频源）。
 *
 * 时间单位统一是毫秒。所有方法都要在主线程调用。
 *
 * 显示/隐藏：调用 {@link #toggleVisibility()}（一般在点击视频区域时调用）；
 * 播放中显示后会在 {@link #AUTO_HIDE_DELAY_MS} 后自动隐藏，暂停、拖动进度时不会自动隐藏。
 */
public class PlayerControlView extends LinearLayout {

    public interface Listener {
        /** 点击播放/暂停。当前在播放时应暂停，否则开始播放 */
        void onPlayPauseClick();

        /** 拖动进度条松手，请求跳转到 positionMs */
        void onSeek(long positionMs);

        /** 点击"上一个" */
        void onPreviousClick();

        /** 点击"下一个" */
        void onNextClick();

        /** 点击横竖屏切换，toFullscreen 为 true 表示请求切到横屏全屏 */
        void onFullscreenClick(boolean toFullscreen);

        /** 点击倍速（只有调用过 {@link #setSpeedText} 后才显示倍速按钮） */
        default void onSpeedClick() {}
    }

    public static final long AUTO_HIDE_DELAY_MS = 3000;

    /** seek 之后，播放器可能还会上报几次 seek 之前的进度，在这段时间内忽略和目标差太多的进度 */
    private static final long SEEK_SETTLE_TIMEOUT_MS = 1500;
    private static final long SEEK_SETTLE_TOLERANCE_MS = 1500;

    private final SeekBar mSeekBar;
    private final ImageButton mPlayPauseButton;
    private final ImageButton mPreviousButton;
    private final ImageButton mNextButton;
    private final TextView mTimeText;
    private final TextView mSpeedButton;
    private final ImageButton mFullscreenButton;

    @Nullable
    private Listener mListener;

    private long mDurationMs = 0;
    private long mPositionMs = 0;
    private boolean mPlaying = false;
    private boolean mFullscreen = false;
    private boolean mDragging = false;

    private long mPendingSeekMs = -1;
    private long mPendingSeekTime = 0;

    private final Runnable mHideAction = this::hide;

    public PlayerControlView(Context context) {
        this(context, null);
    }

    public PlayerControlView(Context context, @Nullable AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public PlayerControlView(Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        setOrientation(VERTICAL);
        setBackgroundResource(R.drawable.av_bg_scrim);
        // 吃掉控制栏空白处的点击，避免穿透到下面的视频区域触发显示/隐藏
        setClickable(true);
        LayoutInflater.from(context).inflate(R.layout.av_player_control_view, this, true);

        mSeekBar = findViewById(R.id.av_seek_bar);
        mPlayPauseButton = findViewById(R.id.av_btn_play_pause);
        mPreviousButton = findViewById(R.id.av_btn_previous);
        mNextButton = findViewById(R.id.av_btn_next);
        mTimeText = findViewById(R.id.av_tv_time);
        mSpeedButton = findViewById(R.id.av_btn_speed);
        mFullscreenButton = findViewById(R.id.av_btn_fullscreen);

        mSeekBar.setMax(0);
        mSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                // 拖动过程中只更新时间文字，松手才真正 seek，避免连续 seek 让播放器反复清空缓冲
                if (fromUser) updateTimeText(progress);
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
                mDragging = true;
                removeCallbacks(mHideAction);
            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                mDragging = false;
                long target = seekBar.getProgress();
                mPositionMs = target;
                mPendingSeekMs = target;
                mPendingSeekTime = SystemClock.uptimeMillis();
                if (mListener != null) mListener.onSeek(target);
                scheduleAutoHide();
            }
        });

        mPlayPauseButton.setOnClickListener(v -> {
            if (mListener != null) mListener.onPlayPauseClick();
            scheduleAutoHide();
        });
        mPreviousButton.setOnClickListener(v -> {
            if (mListener != null) mListener.onPreviousClick();
            scheduleAutoHide();
        });
        mNextButton.setOnClickListener(v -> {
            if (mListener != null) mListener.onNextClick();
            scheduleAutoHide();
        });
        mFullscreenButton.setOnClickListener(v -> {
            if (mListener != null) mListener.onFullscreenClick(!mFullscreen);
            scheduleAutoHide();
        });
        mSpeedButton.setOnClickListener(v -> {
            if (mListener != null) mListener.onSpeedClick();
            scheduleAutoHide();
        });

        updateTimeText(0);
    }

    public void setListener(@Nullable Listener listener) {
        mListener = listener;
    }

    /** 设置总时长（毫秒）。时长未知（如直播）时传 0，此时进度条不可拖动 */
    public void setDuration(long durationMs) {
        mDurationMs = Math.max(0, durationMs);
        mSeekBar.setMax((int) Math.min(mDurationMs, Integer.MAX_VALUE));
        mSeekBar.setEnabled(mDurationMs > 0);
        if (!mDragging) {
            mSeekBar.setProgress((int) Math.min(mPositionMs, mDurationMs));
            updateTimeText(mPositionMs);
        }
    }

    /** 更新当前播放位置（毫秒）。拖动进度条期间、以及刚 seek 完播放器还在上报旧进度时会被忽略 */
    public void setPosition(long positionMs) {
        if (mDragging) return;
        if (mPendingSeekMs >= 0) {
            boolean settled = Math.abs(positionMs - mPendingSeekMs) <= SEEK_SETTLE_TOLERANCE_MS;
            boolean timeout = SystemClock.uptimeMillis() - mPendingSeekTime > SEEK_SETTLE_TIMEOUT_MS;
            if (!settled && !timeout) return;
            mPendingSeekMs = -1;
        }
        mPositionMs = Math.max(0, positionMs);
        mSeekBar.setProgress((int) Math.min(mPositionMs, Integer.MAX_VALUE));
        updateTimeText(mPositionMs);
    }

    public long getPosition() {
        return mPositionMs;
    }

    /** 播放状态，决定播放/暂停按钮的图标，以及是否自动隐藏 */
    public void setPlaying(boolean playing) {
        mPlaying = playing;
        mPlayPauseButton.setImageResource(playing ? R.drawable.av_ic_pause : R.drawable.av_ic_play);
        mPlayPauseButton.setContentDescription(playing ? "暂停" : "播放");
        if (playing) {
            scheduleAutoHide();
        } else {
            // 暂停或播完时一直显示，方便用户继续操作
            removeCallbacks(mHideAction);
            show();
        }
    }

    public boolean isPlaying() {
        return mPlaying;
    }

    /** 没有上一个/下一个时禁用对应按钮 */
    public void setPreviousEnabled(boolean enabled) {
        setButtonEnabled(mPreviousButton, enabled);
    }

    public void setNextEnabled(boolean enabled) {
        setButtonEnabled(mNextButton, enabled);
    }

    /** 当前是否处于横屏全屏，只影响按钮图标；真正的横竖屏切换由页面完成 */
    public void setFullscreen(boolean fullscreen) {
        mFullscreen = fullscreen;
        mFullscreenButton.setImageResource(fullscreen ? R.drawable.av_ic_fullscreen_exit : R.drawable.av_ic_fullscreen);
        mFullscreenButton.setContentDescription(fullscreen ? "退出全屏" : "全屏");
    }

    public boolean isFullscreen() {
        return mFullscreen;
    }

    /** 设置倍速按钮的文字并显示它；传 null 隐藏倍速按钮 */
    public void setSpeedText(@Nullable CharSequence text) {
        mSpeedButton.setVisibility(text == null ? GONE : VISIBLE);
        if (text != null) mSpeedButton.setText(text);
    }

    /** 切换视频源时调用：进度和时长清零 */
    public void reset() {
        mDragging = false;
        mPendingSeekMs = -1;
        mPositionMs = 0;
        setDuration(0);
        setPlaying(false);
    }

    public void show() {
        setVisibility(VISIBLE);
        scheduleAutoHide();
    }

    public void hide() {
        removeCallbacks(mHideAction);
        if (mDragging) return;
        setVisibility(GONE);
    }

    /** 显示 ↔ 隐藏，一般在点击视频区域时调用 */
    public void toggleVisibility() {
        if (getVisibility() == VISIBLE) hide();
        else show();
    }

    private void scheduleAutoHide() {
        removeCallbacks(mHideAction);
        if (mPlaying && !mDragging && getVisibility() == VISIBLE) {
            postDelayed(mHideAction, AUTO_HIDE_DELAY_MS);
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        removeCallbacks(mHideAction);
        super.onDetachedFromWindow();
    }

    private void updateTimeText(long positionMs) {
        mTimeText.setText(formatTime(positionMs) + " / " + formatTime(mDurationMs));
    }

    private static void setButtonEnabled(@NonNull View button, boolean enabled) {
        button.setEnabled(enabled);
        button.setAlpha(enabled ? 1f : 0.4f);
    }

    /** 1:05 / 12:05 / 1:02:05 */
    public static String formatTime(long ms) {
        long totalSec = Math.max(0, ms) / 1000;
        long h = totalSec / 3600;
        long m = (totalSec % 3600) / 60;
        long s = totalSec % 60;
        return h > 0
                ? String.format(Locale.US, "%d:%02d:%02d", h, m, s)
                : String.format(Locale.US, "%d:%02d", m, s);
    }
}
