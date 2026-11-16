/**
 * 音频解码线程（ffplay audio_thread）：解出的 AVFrame 原样（引用转移）放入音频帧队列，
 * 重采样留给音频输出在取数据时做，解码线程只负责 "解码 + 打时间戳"。
 */

#ifndef LEARNFFMPEG_AUDIODECODER_H
#define LEARNFFMPEG_AUDIODECODER_H

#include "Decoder.h"

class AudioDecoder : public Decoder {
public:
    using Decoder::Decoder;

    ~AudioDecoder() override { Stop(); }

protected:
    void Run() override;
};

#endif //LEARNFFMPEG_AUDIODECODER_H
