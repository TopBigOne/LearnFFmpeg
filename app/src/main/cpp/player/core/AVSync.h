/**
 * 一次播放会话中的三个时钟与主时钟选择（参照 ffplay VideoState 中的 audclk/vidclk/extclk）。
 *
 *  - audioClock：由音频输出在每块数据开始播放时设置，serial 跟随音频包队列；
 *  - videoClock：由视频刷新线程在每帧显示时设置，serial 跟随视频包队列；
 *  - externalClock：系统时钟，跟随（SyncToSlave）音频或视频时钟，没有音轨时作为主时钟。
 *
 * 主时钟在 prepare 时确定：有可用的音频输出就以音频为主，否则以外部时钟为主。
 *
 * 倍速：SetSpeed 立即作用于视频时钟和外部时钟；音频时钟的速度由音频输出随每块数据设置，
 * 因为设备缓冲里按旧速度处理过的数据还要再播一会儿，新速度要等它们播完才真正生效。
 */

#ifndef LEARNFFMPEG_AVSYNC_H
#define LEARNFFMPEG_AVSYNC_H

#include <atomic>
#include "Clock.h"
#include "PacketQueue.h"

enum class SyncMaster {
    Audio,
    Video,
    External
};

class AVSync {
public:
    AVSync(const PacketQueue &audioQueue, const PacketQueue &videoQueue)
            : audioClock(audioQueue.SerialPtr()),
              videoClock(videoQueue.SerialPtr()),
              externalClock(nullptr) {
    }

    AVSync(const AVSync &) = delete;
    AVSync &operator=(const AVSync &) = delete;

    /** 只在 prepare 阶段（各线程启动前）设置一次 */
    void SetMaster(SyncMaster master) { m_Master = master; }

    SyncMaster Master() const { return m_Master; }

    /** 主时钟当前值（秒），不可用时为 NAN */
    double GetMasterClock() {
        switch (m_Master) {
            case SyncMaster::Audio:
                return audioClock.Get();
            case SyncMaster::Video:
                return videoClock.Get();
            default:
                return externalClock.Get();
        }
    }

    /** 视频刷新线程据此把帧间隔（媒体时间）换算成真实等待时间 */
    double Speed() const { return m_Speed.load(); }

    void SetSpeed(double speed) {
        m_Speed.store(speed);
        videoClock.SetSpeed(speed);
        externalClock.SetSpeed(speed);
    }

    void SetPaused(bool paused) {
        audioClock.SetPaused(paused);
        videoClock.SetPaused(paused);
        externalClock.SetPaused(paused);
    }

    Clock audioClock;
    Clock videoClock;
    Clock externalClock;

private:
    SyncMaster m_Master = SyncMaster::External;
    std::atomic<double> m_Speed{1.0};
};

#endif //LEARNFFMPEG_AVSYNC_H
