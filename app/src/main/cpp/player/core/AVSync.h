/**
 * 一次播放会话中的三个时钟与主时钟选择（参照 ffplay VideoState 中的 audclk/vidclk/extclk）。
 *
 *  - audioClock：由音频输出在每块数据开始播放时设置，serial 跟随音频包队列；
 *  - videoClock：由视频刷新线程在每帧显示时设置，serial 跟随视频包队列；
 *  - externalClock：系统时钟，跟随（SyncToSlave）音频或视频时钟，没有音轨时作为主时钟。
 *
 * 主时钟在 prepare 时确定：有可用的音频输出就以音频为主，否则以外部时钟为主。
 */

#ifndef LEARNFFMPEG_AVSYNC_H
#define LEARNFFMPEG_AVSYNC_H

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
};

#endif //LEARNFFMPEG_AVSYNC_H
