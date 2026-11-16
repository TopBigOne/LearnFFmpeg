/**
 * 录制模块的音频数据块
 */

#ifndef LEARNFFMPEG_AUDIOCHUNK_H
#define LEARNFFMPEG_AUDIOCHUNK_H

#include <render/audio/AudioRender.h>
#include <cstdint>

/**
 * @brief 待编码的一块 PCM：数据（拷贝一份）+ 采集时间 + 在采集流中的位置
 *
 * samplePos 是这块第一个采样在"入队流"中的序号，由生产端按入队顺序累加。
 * 编码队列满时会丢弃最旧的块，消费端发现编号不连续，就知道丢了多少采样，可以原位补静音。
 */
struct AudioChunk : public AudioFrame {
    AudioChunk(uint8_t *data, int dataSize, int64_t captureTimeUs, int64_t samplePos)
            : AudioFrame(data, dataSize, true), captureTimeUs(captureTimeUs), samplePos(samplePos) {}

    int64_t captureTimeUs;   // 第一个采样的采集时刻（CLOCK_BOOTTIME 微秒）
    int64_t samplePos;       // 第一个采样在入队流中的序号
};

#endif //LEARNFFMPEG_AUDIOCHUNK_H
