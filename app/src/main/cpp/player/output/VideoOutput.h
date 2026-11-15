/**
 * 视频输出：独立的视频刷新线程 + 帧呈现（参照 ffplay video_refresh / compute_target_delay / video_display）。
 *
 * 刷新线程每轮：
 *  1. 丢掉 serial 过期（seek 之前）的帧；
 *  2. 用上一帧到这一帧的间隔，经 ComputeTargetDelay 按主时钟修正后得到 delay：
 *     视频落后 → 缩短等待，超前 → 加倍等待；
 *  3. 没到显示时间就睡到那个时刻（最多 REFRESH_RATE），到了就更新视频时钟并显示；
 *     如果已经晚到连下一帧的显示时间都过了，直接丢弃这一帧追赶。
 * 显示（格式转换 + 送渲染器 + 录制）在刷新线程中完成，不持有刷新状态锁，Stop/SetPaused 不会被它卡住。
 *
 * 暂停时不推进；但 prepare 后的首帧、暂停中 seek 后的首帧会显示一次作为预览。
 *
 * 渲染器（VideoRender）由外部持有，可以是 GL 单例或 NativeRender；本类只在 Init/Release 时初始化/清理它。
 */

#ifndef LEARNFFMPEG_VIDEOOUTPUT_H
#define LEARNFFMPEG_VIDEOOUTPUT_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <core/AVSync.h>
#include <core/FFPtr.h>
#include <core/FrameQueue.h>
#include <core/PacketQueue.h>
#include <render/video/VideoRender.h>

class SingleVideoRecorder;

class VideoOutput {
public:
    class Listener {
    public:
        virtual ~Listener() = default;

        /** 刷新线程：一帧已交给渲染器（GL 渲染需要据此 requestRender） */
        virtual void OnVideoFrameRendered() = 0;
    };

    VideoOutput(VideoRender *render, FrameQueue *pictureQueue, PacketQueue *packetQueue, AVSync *sync,
                double maxFrameDuration, Listener *listener);

    ~VideoOutput();

    VideoOutput(const VideoOutput &) = delete;
    VideoOutput &operator=(const VideoOutput &) = delete;

    /** 初始化渲染器，失败（渲染目标无效）返回 false */
    bool Init(int videoWidth, int videoHeight);

    /** 启动刷新线程（初始为暂停状态） */
    void Start();

    void SetPaused(bool paused);

    /** 停止刷新线程、结束录制、清理渲染器（可重复调用） */
    void Stop();

    /** 开始 "边播边录"（仅 ANativeWindow 渲染支持），控制线程调用 */
    void StartRecording(const std::string &path);

    /** 显示阶段因落后而丢弃的帧数 */
    int LateDroppedFrames() const { return m_LateDrops.load(); }

private:
    void RefreshLoop();

    /**
     * 一轮刷新（持有 m_Mutex）
     * @param remainingTime 输入/输出：下一轮最多等待的时间（秒）
     * @return 是否需要显示 PeekLast() 那一帧
     */
    bool RefreshLocked(double *remainingTime);

    double ComputeTargetDelay(double delay);

    double FrameDuration(const DecodedFrame *vp, const DecodedFrame *nextvp) const;

    void UpdateVideoPts(double pts, int serial);

    /** 把一帧交给渲染器（以及录制器），只在刷新线程调用 */
    void Present(const AVFrame *frame);

    VideoRender *m_Render;
    FrameQueue *m_PictureQueue;
    PacketQueue *m_PacketQueue;
    AVSync *m_Sync;
    const double m_MaxFrameDuration;
    Listener *m_Listener;

    std::thread m_Thread;
    std::mutex m_Mutex;
    std::condition_variable m_Cond;
    // 以下由 m_Mutex 保护
    bool m_Quit = false;
    bool m_Wake = false;
    bool m_Paused = true;
    double m_PauseStartTime = 0;
    double m_FrameTimer = 0;                // 上一帧 "应该" 显示的系统时刻（秒）

    // 渲染目标尺寸（Init 后不变）
    int m_RenderWidth = 0;
    int m_RenderHeight = 0;
    bool m_Initialized = false;

    // 以下只在刷新线程访问
    SwsContextPtr m_SwsCtx;
    AVBufferPtr m_RGBABuffer;
    int m_RGBALineSize = 0;

    // 录制器：控制线程创建、刷新线程使用，由 m_RecorderMutex 保护
    std::mutex m_RecorderMutex;
    std::unique_ptr<SingleVideoRecorder> m_Recorder;

    std::atomic<int> m_LateDrops{0};
};

#endif //LEARNFFMPEG_VIDEOOUTPUT_H
