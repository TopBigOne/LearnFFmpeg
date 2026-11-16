/**
 * 录制模块的统一时钟
 *
 * 音视频同步的前提是两路时间戳来自同一个时钟。录制模块统一使用 CLOCK_BOOTTIME（微秒）：
 *  - Java 层 SystemClock.elapsedRealtimeNanos() 就是 CLOCK_BOOTTIME；
 *  - Camera2 在 SENSOR_INFO_TIMESTAMP_SOURCE == REALTIME 时，Image.getTimestamp() 也是它；
 *  - AudioRecord.getTimestamp(..., AudioTimestamp.TIMEBASE_BOOTTIME) 同样是它。
 * 这样 Java 层给出的"采集时刻"可以直接和 Native 层的"开始录制时刻"比较。
 */

#ifndef LEARNFFMPEG_RECORDERCLOCK_H
#define LEARNFFMPEG_RECORDERCLOCK_H

#include <cstdint>
#include <ctime>

/** 采集时间未知（调用方没有提供），由录制模块用到达时间近似 */
#define CAPTURE_TIME_UNKNOWN 0

/** 当前 CLOCK_BOOTTIME 时间（微秒） */
static inline int64_t GetBootTimeUs() {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000 + ts.tv_nsec / 1000;
}

/** 采样数与微秒互相换算（四舍五入，避免长时间累积截断误差） */
static inline int64_t SamplesToUs(int64_t samples, int sampleRate) {
    return (samples * 1000000 + sampleRate / 2) / sampleRate;
}

static inline int64_t UsToSamples(int64_t us, int sampleRate) {
    return (us * sampleRate + 500000) / 1000000;
}

#endif //LEARNFFMPEG_RECORDERCLOCK_H
