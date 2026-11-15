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


// 原 AudioRender 推模式接口（OpenSLRender 实现）已由 player/output/AudioOutput 拉模式接口取代，
// 这里只保留 AudioFrame：音频可视化和录制模块仍在使用它。

#endif //LEARNFFMPEG_AUDIORENDER_H
