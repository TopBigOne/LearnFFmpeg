//
// Created by 字节流动 on 2020/6/19.
//

#ifndef LEARNFFMPEG_AUDIORENDER_H
#define LEARNFFMPEG_AUDIORENDER_H

#include <cstdint>
#include <cstdlib>
#include <cstring>

class AudioFrame {
public:
    AudioFrame(uint8_t * data, int dataSize, bool hardCopy = true, int64_t pts = -1) {
        this->dataSize = dataSize;
        this->data = data;
        this->hardCopy = hardCopy;
        this->pts = pts;
        if(hardCopy) {
            this->data = static_cast<uint8_t *>(malloc(this->dataSize));
            memcpy(this->data, data, dataSize);
        }
    }

    ~AudioFrame() {
        if(hardCopy && this->data)
            free(this->data);
        this->data = nullptr;
    }

    uint8_t * data = nullptr;
    int dataSize = 0;
    bool hardCopy = true;
    int64_t pts = -1;   // 该帧的显示时间戳（毫秒），-1 表示未知
};


class AudioRender {
public:
    virtual ~AudioRender()
    {}
    virtual void Init() = 0;
    virtual void ClearAudioCache() = 0;
    /**
     * @param pts 该帧的时间戳（毫秒），用于维护音频时钟
     * 队列满时会阻塞，以此控制解码节奏（反压）
     */
    virtual void RenderAudioFrame(uint8_t *pData, int dataSize, int64_t pts) = 0;
    virtual void UnInit() = 0;

    /** 暂停/恢复声音输出 */
    virtual void Pause() {}
    virtual void Resume() {}

    /** 让阻塞在 RenderAudioFrame 里的生产者立即返回（停止解码线程前调用） */
    virtual void Abort() {}

    /**
     * @brief 获取音频时钟：当前正从扬声器播放的位置（毫秒）
     * @return -1 表示当前不可用（未开始、正在播放静音、刚 seek）
     */
    virtual int64_t GetAudioClock() { return -1; }
};


#endif //LEARNFFMPEG_AUDIORENDER_H
