#include "VideoOutput.h"
#include <algorithm>
#include <chrono>
#include <LogUtil.h>
#include <SingleVideoRecorder.h>

extern "C" {
#include <libavutil/imgutils.h>
};

// 以下阈值与 ffplay 相同（秒）
// 同步阈值下限：帧间隔小于它时也至少按它判断是否需要修正
static const double AV_SYNC_THRESHOLD_MIN = 0.04;
// 同步阈值上限；同时也是 frame_timer 落后系统时间多少就直接重置的阈值
static const double AV_SYNC_THRESHOLD_MAX = 0.1;
// 帧间隔大于它的帧（低帧率视频）超前时不再 "加倍等待"，而是直接等到对齐
static const double AV_SYNC_FRAMEDUP_THRESHOLD = 0.1;
// 刷新线程两轮之间最多等待的时间
static const double REFRESH_RATE = 0.01;

VideoOutput::VideoOutput(VideoRender *render, FrameQueue *pictureQueue, PacketQueue *packetQueue,
                         AVSync *sync, double maxFrameDuration, Listener *listener)
        : m_Render(render),
          m_PictureQueue(pictureQueue),
          m_PacketQueue(packetQueue),
          m_Sync(sync),
          m_MaxFrameDuration(maxFrameDuration),
          m_Listener(listener) {
}

VideoOutput::~VideoOutput() {
    Stop();
}

bool VideoOutput::Init(int videoWidth, int videoHeight) {
    if (m_Render == nullptr || videoWidth <= 0 || videoHeight <= 0) return false;
    int dstSize[2] = {0, 0};
    m_Render->Init(videoWidth, videoHeight, dstSize);
    m_Initialized = true;
    m_RenderWidth = dstSize[0];
    m_RenderHeight = dstSize[1];
    if (m_RenderWidth <= 0 || m_RenderHeight <= 0) {
        LOGCATE("VideoOutput::Init invalid render size [%d, %d]", m_RenderWidth, m_RenderHeight);
        return false;
    }
    LOGCATI("VideoOutput::Init video [%d, %d], render [%d, %d], type=%d",
            videoWidth, videoHeight, m_RenderWidth, m_RenderHeight, m_Render->GetRenderType());
    return true;
}

void VideoOutput::Start() {
    m_Thread = std::thread(&VideoOutput::RefreshLoop, this);
}

void VideoOutput::SetPaused(bool paused) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Paused == paused) return;
    double now = Clock::Now();
    if (paused) {
        m_PauseStartTime = now;
    } else {
        // 暂停期间 frame_timer 不应前进，否则恢复后会把暂停时长当成 "落后" 而疯狂丢帧
        m_FrameTimer += now - m_PauseStartTime;
    }
    m_Paused = paused;
    m_Wake = true;
    m_Cond.notify_all();
}

void VideoOutput::Stop() {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Quit = true;
        m_Cond.notify_all();
    }
    if (m_Thread.joinable()) m_Thread.join();

    {
        std::lock_guard<std::mutex> lock(m_RecorderMutex);
        if (m_Recorder) {
            m_Recorder->StopRecord();
            m_Recorder.reset();
        }
    }

    if (m_Initialized) {
        // GL 渲染器是单例，只清掉本次播放的画面，不销毁
        m_Render->UnInit();
        m_Initialized = false;
    }
}

void VideoOutput::StartRecording(const std::string &path) {
    if (path.empty() || !m_Initialized || m_RenderWidth <= 0 || m_RenderHeight <= 0) return;
    if (m_Render->GetRenderType() != VIDEO_RENDER_ANWINDOW) {
        LOGCATI("VideoOutput::StartRecording only supported with ANativeWindow render");
        return;
    }
    std::lock_guard<std::mutex> lock(m_RecorderMutex);
    if (m_Recorder) return;
    // 每帧都要软编码，CPU 开销很大，所以只在显式设置了路径时开启
    const int fps = 25;
    long bitRate = static_cast<long>(m_RenderWidth * m_RenderHeight * fps * 0.2);
    std::unique_ptr<SingleVideoRecorder> recorder(
            new SingleVideoRecorder(path.c_str(), m_RenderWidth, m_RenderHeight, bitRate, fps));
    if (recorder->StartRecord() != 0) {
        LOGCATE("VideoOutput::StartRecording StartRecord fail, path=%s", path.c_str());
        return;
    }
    m_Recorder = std::move(recorder);
    LOGCATI("VideoOutput::StartRecording %s", path.c_str());
}

void VideoOutput::RefreshLoop() {
    LOGCATI("VideoOutput::RefreshLoop start");
    std::unique_lock<std::mutex> lock(m_Mutex);
    while (!m_Quit) {
        double remainingTime = REFRESH_RATE;
        if (RefreshLocked(&remainingTime)) {
            // 显示可能耗时（格式转换、录制拷贝），期间不持锁，SetPaused/Stop 不会被卡住；
            // PeekLast 的那一帧只有本线程会释放，锁外访问是安全的
            lock.unlock();
            Present(m_PictureQueue->PeekLast()->frame.get());
            m_Listener->OnVideoFrameRendered();
            lock.lock();
        }
        if (m_Quit) break;
        if (remainingTime > 0) {
            m_Cond.wait_for(lock, std::chrono::microseconds(static_cast<int64_t>(remainingTime * 1000000)),
                            [this] { return m_Quit || m_Wake; });
        }
        m_Wake = false;
    }
    LOGCATI("VideoOutput::RefreshLoop exit, late dropped %d frames", m_LateDrops.load());
}

bool VideoOutput::RefreshLocked(double *remainingTime) {
    bool display = false;
    for (;;) {
        if (m_PictureQueue->NbRemaining() == 0) break;

        DecodedFrame *vp = m_PictureQueue->Peek();
        if (vp->serial != m_PacketQueue->Serial()) {
            m_PictureQueue->Next();
            continue;
        }

        DecodedFrame *lastvp = m_PictureQueue->PeekLast();
        bool newSegment = !m_PictureQueue->HasShownFrame() || lastvp->serial != vp->serial;
        // seek 之后的第一帧：以当前时刻为起点重新计时
        if (lastvp->serial != vp->serial) m_FrameTimer = Clock::Now();

        if (m_Paused) {
            // 暂停时只预览新一段的首帧（prepare 完成、暂停中 seek），其余保持不动
            if (newSegment) {
                m_FrameTimer = Clock::Now();
                // 预览时刻视为新的暂停起点，恢复时 frame_timer 只补偿此后的暂停时长
                m_PauseStartTime = m_FrameTimer;
                if (!std::isnan(vp->pts)) UpdateVideoPts(vp->pts, vp->serial);
                m_PictureQueue->Next();
                display = true;
            }
            break;
        }

        // 帧间隔是媒体时间，按倍速换算成真实时间
        const double speed = m_Sync->Speed();
        double lastDuration = FrameDuration(lastvp, vp) / speed;
        double delay = ComputeTargetDelay(lastDuration, speed);
        double time = Clock::Now();
        if (time < m_FrameTimer + delay) {
            // 还没到显示时间
            *remainingTime = std::min(m_FrameTimer + delay - time, *remainingTime);
            break;
        }

        m_FrameTimer += delay;
        // 落后太多（卡顿、暂停恢复、首帧）时不再试图追回历史，直接以当前时刻为准
        if (delay > 0 && time - m_FrameTimer > AV_SYNC_THRESHOLD_MAX) m_FrameTimer = time;

        if (!std::isnan(vp->pts)) UpdateVideoPts(vp->pts, vp->serial);

        if (m_PictureQueue->NbRemaining() > 1) {
            DecodedFrame *nextvp = m_PictureQueue->PeekNext();
            double duration = FrameDuration(vp, nextvp) / speed;
            // 连下一帧的显示时间都已经过了：丢掉这一帧追赶主时钟
            if (m_Sync->Master() != SyncMaster::Video && time > m_FrameTimer + duration) {
                m_LateDrops++;
                LOGCATV("VideoOutput::RefreshLocked late drop, pts=%.3f", vp->pts);
                m_PictureQueue->Next();
                continue;
            }
        }

        m_PictureQueue->Next();
        display = true;
        break;
    }
    return display && m_PictureQueue->HasShownFrame();
}

double VideoOutput::ComputeTargetDelay(double delay, double speed) {
    if (m_Sync->Master() == SyncMaster::Video) return delay;

    // 视频时钟相对主时钟的偏差（媒体时间）：正数表示视频超前
    double mediaDiff = m_Sync->videoClock.Get() - m_Sync->GetMasterClock();
    // 换算成真实时间：2 倍速下落后 0.1s 媒体时间，只需少等 0.05s 就能追上
    double diff = mediaDiff / speed;
    // 偏差小于一帧（且在 [MIN, MAX] 内）时不修正，避免来回抖动
    double syncThreshold = std::max(AV_SYNC_THRESHOLD_MIN, std::min(AV_SYNC_THRESHOLD_MAX, delay));
    // 偏差大到超过最大帧间隔（时间戳跳变）时不修正，这个判断针对的是时间戳本身，用媒体时间
    if (!std::isnan(diff) && std::fabs(mediaDiff) < m_MaxFrameDuration) {
        if (diff <= -syncThreshold) {
            delay = std::max(0.0, delay + diff);
        } else if (diff >= syncThreshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD) {
            delay = delay + diff;
        } else if (diff >= syncThreshold) {
            delay = 2 * delay;
        }
    }
    LOGCATV("VideoOutput::ComputeTargetDelay delay=%.3f diff=%.3f", delay, diff);
    return delay;
}

double VideoOutput::FrameDuration(const DecodedFrame *vp, const DecodedFrame *nextvp) const {
    if (vp->serial != nextvp->serial) return 0.0;
    double duration = nextvp->pts - vp->pts;
    // 时间戳缺失、乱序或跳变时退回到按帧率估计的时长
    if (std::isnan(duration) || duration <= 0 || duration > m_MaxFrameDuration) return vp->duration;
    return duration;
}

void VideoOutput::UpdateVideoPts(double pts, int serial) {
    m_Sync->videoClock.Set(pts, serial);
    m_Sync->externalClock.SyncToSlave(m_Sync->videoClock);
}

void VideoOutput::Present(const AVFrame *frame) {
    if (frame == nullptr || frame->data[0] == nullptr || !m_Initialized) return;
    std::lock_guard<std::mutex> recorderLock(m_RecorderMutex);

    NativeImage image;
    const int renderType = m_Render->GetRenderType();
    const AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);

    // ANativeWindow 只接受 RGBA；GL 渲染器能直接上传的 YUV 格式不做转换
    bool needConvert = renderType == VIDEO_RENDER_ANWINDOW
                       || (format != AV_PIX_FMT_YUV420P && format != AV_PIX_FMT_YUVJ420P
                           && format != AV_PIX_FMT_NV12 && format != AV_PIX_FMT_NV21
                           && format != AV_PIX_FMT_RGBA);

    if (needConvert) {
        // 源尺寸/格式变化时（直播流分辨率切换）会自动重建
        m_SwsCtx.reset(sws_getCachedContext(m_SwsCtx.release(), frame->width, frame->height, format,
                                            m_RenderWidth, m_RenderHeight, AV_PIX_FMT_RGBA,
                                            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr));
        if (!m_SwsCtx) {
            LOGCATE("VideoOutput::Present sws_getCachedContext fail, format=%d", frame->format);
            return;
        }

        // 零拷贝：不录制时直接让 sws_scale 写进窗口缓冲区，省掉一次整帧拷贝。
        // 录制需要一份 RGBA 图像给编码器，窗口缓冲区不适合回读，所以仍走中间缓冲区
        uint8_t *windowBits = nullptr;
        int windowLineSize = 0;
        if (renderType == VIDEO_RENDER_ANWINDOW && !m_Recorder
            && m_Render->LockDirectBuffer(&windowBits, &windowLineSize)) {
            uint8_t *dstData[4] = {windowBits, nullptr, nullptr, nullptr};
            int dstLineSize[4] = {windowLineSize, 0, 0, 0};
            sws_scale(m_SwsCtx.get(), frame->data, frame->linesize, 0, frame->height, dstData, dstLineSize);
            m_Render->UnlockDirectBuffer();
            return;
        }

        if (!m_RGBABuffer) {
            int size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, m_RenderWidth, m_RenderHeight, 1);
            if (size <= 0) return;
            m_RGBABuffer.reset(static_cast<uint8_t *>(av_malloc(static_cast<size_t>(size))));
            if (!m_RGBABuffer) return;
            m_RGBALineSize = m_RenderWidth * 4;
        }
        uint8_t *dstData[4] = {m_RGBABuffer.get(), nullptr, nullptr, nullptr};
        int dstLineSize[4] = {m_RGBALineSize, 0, 0, 0};
        sws_scale(m_SwsCtx.get(), frame->data, frame->linesize, 0, frame->height, dstData, dstLineSize);

        image.format = IMAGE_FORMAT_RGBA;
        image.width = m_RenderWidth;
        image.height = m_RenderHeight;
        image.ppPlane[0] = m_RGBABuffer.get();
        image.pLineSize[0] = m_RGBALineSize;
    } else {
        image.width = frame->width;
        image.height = frame->height;
        for (int i = 0; i < 3; ++i) {
            image.ppPlane[i] = frame->data[i];
            image.pLineSize[i] = frame->linesize[i];
        }
        switch (format) {
            case AV_PIX_FMT_NV12:
                image.format = IMAGE_FORMAT_NV12;
                break;
            case AV_PIX_FMT_NV21:
                image.format = IMAGE_FORMAT_NV21;
                break;
            case AV_PIX_FMT_RGBA:
                image.format = IMAGE_FORMAT_RGBA;
                break;
            default:
                image.format = IMAGE_FORMAT_I420;
                // 部分设备上 h264_mediacodec 声明 YUV420P 实际输出 NV12（只有两个平面且行宽相同）
                if (frame->data[1] && !frame->data[2] && frame->linesize[0] == frame->linesize[1]
                    && frame->linesize[2] == 0) {
                    image.format = IMAGE_FORMAT_NV12;
                }
                break;
        }
    }

    m_Render->RenderVideoFrame(&image);
    // 录的是"屏幕上看到的画面"，画面的上屏时刻就是它在录像里的时间，传未知让录制器取当前时间
    if (m_Recorder) m_Recorder->OnFrame2Encode(&image, CAPTURE_TIME_UNKNOWN);
}
