package com.monday.av.widget;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.SystemClock;
import android.util.AttributeSet;
import android.util.TypedValue;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import java.util.Locale;

/**
 * 视频画面上的手势层：单击显示/隐藏控制栏，双击左右两侧快退/快进（YouTube 的交互）。
 *
 * <pre>
 *  ┌──────────┬────────┬──────────┐
 *  │  左 40%   │ 中 20% │  右 40%   │   左侧双击：后退一个步长；右侧双击：快进一个步长
 *  │  后退     │ 不跳转 │  快进     │   中间双击：不跳转
 *  └──────────┴────────┴──────────┘
 * </pre>
 *
 * 判定规则：
 *  - 第一下点击后等待 {@link #DOUBLE_TAP_TIMEOUT_MS}，没有第二下才算单击（所以单击会有这么一点延迟）；
 *  - 双击进入"连点跳转"状态：之后在同一侧每点一下再累加一个步长，不需要再双击；
 *  - 停止点击 {@link #SEEK_COMMIT_DELAY_MS} 后才真正跳转一次，连点多少下都只 seek 一次，
 *    避免播放器反复清空缓冲；期间通过 {@link Callback#onSeekPreview} 预览目标位置；
 *  - 连点过程中点了另一侧：先把当前累计的跳转提交，再从另一侧重新开始；点了中间：提交并结束；
 *  - 目标位置限制在 [0, 总时长]；总时长未知（如直播）时双击不跳转，按单击处理。
 */
public class DoubleTapSeekView extends View {

    public interface Callback {
        /** 当前播放位置（毫秒），开始连点时作为基准 */
        long getCurrentPosition();

        /** 总时长（毫秒），≤ 0 表示未知，不能跳转 */
        long getDuration();

        /** 单击 */
        void onSingleTap();

        /** 连点过程中每累加一次调用一次，用于预览目标位置（还没有真正 seek） */
        void onSeekPreview(long targetMs);

        /** 停止连点后调用一次，此时应真正 seek 到 targetMs */
        void onSeekCommit(long targetMs);
    }

    public static final long DEFAULT_SEEK_STEP_MS = 5000;
    private static final long DOUBLE_TAP_TIMEOUT_MS = 300;
    private static final long SEEK_COMMIT_DELAY_MS = 700;
    private static final float SIDE_RATIO = 0.4f;          // 左右两侧各占宽度的比例

    private static final long RIPPLE_DURATION_MS = 400;
    private static final long FADE_DURATION_MS = 300;

    private static final int SIDE_NONE = 0;
    private static final int SIDE_LEFT = -1;
    private static final int SIDE_RIGHT = 1;

    @Nullable
    private Callback mCallback;
    private long mSeekStepMs = DEFAULT_SEEK_STEP_MS;
    @Nullable
    private View mPassThroughTarget;

    // ---- 手势状态
    private final int mTouchSlop;
    private float mDownX, mDownY;
    private long mDownTime;
    private boolean mTapCanceled;
    private boolean mPendingSingleTap = false;

    // ---- 连点跳转状态：mSide != SIDE_NONE 表示正在连点
    private int mSide = SIDE_NONE;
    private int mTapCount = 0;
    private long mBasePositionMs = 0;

    // ---- 绘制状态
    private int mHintSide = SIDE_NONE;          // 正在显示（或淡出中）的提示在哪一侧
    private int mHintTapCount = 0;
    private long mFadeStartTime = -1;           // ≥ 0 表示提示正在淡出
    private float mRippleX, mRippleY;
    private long mRippleStartTime = -1;

    private final Paint mAreaPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mRipplePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mTextPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mArrowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF mAreaRect = new RectF();
    private final Path mArrowPath = new Path();

    private final Runnable mSingleTapAction = () -> {
        mPendingSingleTap = false;
        if (mCallback != null) mCallback.onSingleTap();
    };
    private final Runnable mCommitAction = this::commitSeek;

    public DoubleTapSeekView(Context context) {
        this(context, null);
    }

    public DoubleTapSeekView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        mTouchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
        mAreaPaint.setColor(0x33FFFFFF);
        mRipplePaint.setColor(0x40FFFFFF);
        mTextPaint.setColor(0xFFFFFFFF);
        mTextPaint.setTextAlign(Paint.Align.CENTER);
        mTextPaint.setTextSize(sp(14));
        mTextPaint.setFakeBoldText(true);
        mTextPaint.setShadowLayer(dp(2), 0, 0, 0x80000000);
        mArrowPaint.setColor(0xFFFFFFFF);
        mArrowPaint.setStyle(Paint.Style.FILL);
        mArrowPaint.setShadowLayer(dp(2), 0, 0, 0x80000000);
        setClickable(true);
    }

    public void setCallback(@Nullable Callback callback) {
        mCallback = callback;
    }

    /** 每次跳转的步长（毫秒），默认 5 秒 */
    public void setSeekStepMs(long stepMs) {
        mSeekStepMs = Math.max(1000, stepMs);
    }

    public long getSeekStepMs() {
        return mSeekStepMs;
    }

    /**
     * 把所有触摸事件在识别之后原样转发给 target（坐标会换算到 target 上）。
     * 用于下面的视图自己也要处理手势的场景，例如 GL 画面的拖动旋转、双指缩放。
     * target 必须和本视图在同一个父布局里。
     */
    public void setPassThroughTarget(@Nullable View target) {
        mPassThroughTarget = target;
    }

    // ---------------------------------------------------------------- 手势

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        boolean handled = handleTouch(event);
        if (mPassThroughTarget != null) {
            MotionEvent copy = MotionEvent.obtain(event);
            copy.offsetLocation(getLeft() - mPassThroughTarget.getLeft(), getTop() - mPassThroughTarget.getTop());
            mPassThroughTarget.dispatchTouchEvent(copy);
            copy.recycle();
        }
        return handled;
    }

    private boolean handleTouch(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_POINTER_DOWN:
                mTapCanceled = true;   // 多指（缩放）不算点击
                return true;
            case MotionEvent.ACTION_DOWN:
                mDownX = event.getX();
                mDownY = event.getY();
                mDownTime = event.getEventTime();
                mTapCanceled = false;
                return true;
            case MotionEvent.ACTION_MOVE:
                if (Math.abs(event.getX() - mDownX) > mTouchSlop || Math.abs(event.getY() - mDownY) > mTouchSlop) {
                    mTapCanceled = true;   // 滑动不算点击
                }
                return true;
            case MotionEvent.ACTION_UP:
                boolean longPress = event.getEventTime() - mDownTime > ViewConfiguration.getLongPressTimeout();
                if (!mTapCanceled && !longPress) {
                    handleTap(event.getX(), event.getY());
                }
                return true;
            case MotionEvent.ACTION_CANCEL:
                mTapCanceled = true;
                return true;
            default:
                return super.onTouchEvent(event);
        }
    }

    private void handleTap(float x, float y) {
        int side = sideOf(x);

        // 正在连点跳转
        if (mSide != SIDE_NONE) {
            if (side == mSide) {
                mTapCount++;
                onSeekAccumulated(x, y);
                return;
            }
            commitSeek();                       // 换了一侧或点了中间：先提交当前的
            if (side != SIDE_NONE) startSeek(side, x, y);
            return;
        }

        // 第二下：构成双击
        if (mPendingSingleTap) {
            removeCallbacks(mSingleTapAction);
            mPendingSingleTap = false;
            if (side != SIDE_NONE && canSeek()) {
                startSeek(side, x, y);
            } else if (mCallback != null) {
                // 中间双击、或者不能跳转时，当作一次单击
                mCallback.onSingleTap();
            }
            return;
        }

        // 第一下：等一等，看是不是双击
        mPendingSingleTap = true;
        postDelayed(mSingleTapAction, DOUBLE_TAP_TIMEOUT_MS);
        performClick();
    }

    private void startSeek(int side, float x, float y) {
        mSide = side;
        mTapCount = 1;
        mBasePositionMs = mCallback != null ? mCallback.getCurrentPosition() : 0;
        onSeekAccumulated(x, y);
    }

    private void onSeekAccumulated(float x, float y) {
        if (mCallback != null) mCallback.onSeekPreview(targetPosition());
        removeCallbacks(mCommitAction);
        postDelayed(mCommitAction, SEEK_COMMIT_DELAY_MS);

        mHintSide = mSide;
        mHintTapCount = mTapCount;
        mFadeStartTime = -1;
        mRippleX = x;
        mRippleY = y;
        mRippleStartTime = SystemClock.uptimeMillis();
        postInvalidateOnAnimation();
    }

    private void commitSeek() {
        removeCallbacks(mCommitAction);
        if (mSide == SIDE_NONE) return;
        long target = targetPosition();
        mSide = SIDE_NONE;
        mTapCount = 0;
        mFadeStartTime = SystemClock.uptimeMillis();   // 提示开始淡出
        postInvalidateOnAnimation();
        if (mCallback != null) mCallback.onSeekCommit(target);
    }

    private long targetPosition() {
        long duration = mCallback != null ? mCallback.getDuration() : 0;
        long target = mBasePositionMs + mSide * mTapCount * mSeekStepMs;
        return Math.max(0, Math.min(target, duration));
    }

    private boolean canSeek() {
        return mCallback != null && mCallback.getDuration() > 0;
    }

    private int sideOf(float x) {
        float w = getWidth();
        if (x < w * SIDE_RATIO) return SIDE_LEFT;
        if (x > w * (1 - SIDE_RATIO)) return SIDE_RIGHT;
        return SIDE_NONE;
    }

    @Override
    public boolean performClick() {
        return super.performClick();
    }

    // ---------------------------------------------------------------- 绘制

    @Override
    protected void onDraw(@NonNull Canvas canvas) {
        super.onDraw(canvas);
        if (mHintSide == SIDE_NONE) return;

        long now = SystemClock.uptimeMillis();
        float alpha = 1f;
        if (mFadeStartTime >= 0) {
            alpha = 1f - (now - mFadeStartTime) / (float) FADE_DURATION_MS;
            if (alpha <= 0) {
                mHintSide = SIDE_NONE;
                mFadeStartTime = -1;
                mRippleStartTime = -1;
                return;
            }
        }

        float w = getWidth();
        float h = getHeight();
        boolean left = mHintSide == SIDE_LEFT;

        // 半圆形的高亮区域：一个大椭圆，只露出靠内侧的弧边
        if (left) mAreaRect.set(-w * 0.5f, -h * 0.25f, w * 0.38f, h * 1.25f);
        else mAreaRect.set(w * 0.62f, -h * 0.25f, w * 1.5f, h * 1.25f);
        mAreaPaint.setAlpha((int) (0x33 * alpha));
        canvas.save();
        canvas.clipRect(left ? 0 : w * 0.62f, 0, left ? w * 0.38f : w, h);
        canvas.drawOval(mAreaRect, mAreaPaint);

        // 点击位置的水波纹
        if (mRippleStartTime >= 0) {
            float p = (now - mRippleStartTime) / (float) RIPPLE_DURATION_MS;
            if (p < 1f) {
                mRipplePaint.setAlpha((int) (0x40 * (1f - p) * alpha));
                canvas.drawCircle(mRippleX, mRippleY, w * 0.18f * p, mRipplePaint);
            } else {
                mRippleStartTime = -1;
            }
        }
        canvas.restore();

        // 箭头 + 累计秒数
        float cx = left ? w * 0.18f : w * 0.82f;
        float cy = h / 2f;
        int textAlpha = (int) (255 * alpha);
        mArrowPaint.setAlpha(textAlpha);
        mTextPaint.setAlpha(textAlpha);
        drawArrows(canvas, cx, cy - dp(14), left);
        long seconds = mHintTapCount * mSeekStepMs / 1000;
        canvas.drawText(String.format(Locale.US, "%d 秒", seconds), cx, cy + sp(14) + dp(4), mTextPaint);

        // 水波纹或淡出还没结束就继续刷新
        if (mRippleStartTime >= 0 || mFadeStartTime >= 0) postInvalidateOnAnimation();
    }

    /** 三个并排的实心三角形（不用 ◀ ▶ 字符：部分机型会把它们渲染成彩色 emoji） */
    private void drawArrows(Canvas canvas, float cx, float cy, boolean pointLeft) {
        float size = dp(10);       // 三角形宽度
        float half = dp(6);        // 三角形高度的一半
        float gap = dp(2);
        float total = size * 3 + gap * 2;
        float x = cx - total / 2f;
        for (int i = 0; i < 3; i++) {
            mArrowPath.reset();
            if (pointLeft) {
                mArrowPath.moveTo(x, cy);
                mArrowPath.lineTo(x + size, cy - half);
                mArrowPath.lineTo(x + size, cy + half);
            } else {
                mArrowPath.moveTo(x + size, cy);
                mArrowPath.lineTo(x, cy - half);
                mArrowPath.lineTo(x, cy + half);
            }
            mArrowPath.close();
            canvas.drawPath(mArrowPath, mArrowPaint);
            x += size + gap;
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        removeCallbacks(mSingleTapAction);
        removeCallbacks(mCommitAction);
        mPendingSingleTap = false;
        mSide = SIDE_NONE;
        super.onDetachedFromWindow();
    }

    private float dp(float v) {
        return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics());
    }

    private float sp(float v) {
        return TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, v, getResources().getDisplayMetrics());
    }
}
