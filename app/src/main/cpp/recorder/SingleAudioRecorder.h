/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#ifndef LEARNFFMPEG_SINGLEAUDIORECORDER_H
#define LEARNFFMPEG_SINGLEAUDIORECORDER_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

#include "ThreadSafeQueue.h"
#include "thread"
#include "LogUtil.h"
#include "AudioRender.h"

using namespace std;

#define DEFAULT_SAMPLE_RATE    44100                 // 默认采样率：44.1kHz
#define DEFAULT_CHANNEL_LAYOUT AV_CH_LAYOUT_STEREO  // 默认声道布局：立体声

/**
 * @brief 单独音频录制器类
 *
 * 专门用于音频录制，将PCM音频数据编码为AAC格式
 * 支持多线程编码和队列缓冲
 */
class SingleAudioRecorder {
public:
    /**
     * @brief 构造函数
     * @param outUrl 输出文件路径
     * @param sampleRate 采样率
     * @param channelLayout 声道布局
     * @param sampleFormat 采样格式
     */
    SingleAudioRecorder(const char *outUrl, int sampleRate, int channelLayout, int sampleFormat);

    /**
     * @brief 析构函数
     */
    ~SingleAudioRecorder();

    /**
     * @brief 开始录制
     * 初始化编码器并启动编码线程
     * @return 0表示成功，负数表示失败
     */
    int StartRecord();

    /**
     * @brief 接收音频数据
     * 将音频帧添加到编码队列
     * @param inputFrame 输入的音频帧
     * @return 0表示成功，负数表示失败
     */
    int OnFrame2Encode(AudioFrame *inputFrame);

    /**
     * @brief 停止录制
     * 停止编码线程并写入文件尾
     * @return 0表示成功，负数表示失败
     */
    int StopRecord();

private:
    /**
     * @brief AAC编码线程函数（静态函数）
     * @param context SingleAudioRecorder实例指针
     */
    static void StartAACEncoderThread(SingleAudioRecorder *context);

    /**
     * @brief 编码一帧音频
     * @param pFrame 待编码的音频帧
     * @return 0表示成功，负数表示失败
     */
    int EncodeFrame(AVFrame *pFrame);

private:
    ThreadSafeQueue<AudioFrame *> m_frameQueue;     // 音频帧队列，线程安全
    char m_outUrl[1024] = {0};                      // 输出文件路径
    int m_frameIndex = 0;                           // 帧索引计数器
    int m_sampleRate;                               // 音频采样率
    int m_channelLayout;                            // 声道布局
    int m_sampleFormat;                             // 采样格式
    AVPacket m_avPacket;                            // 编码后的数据包
    AVFrame  *m_pFrame = nullptr;                   // 编码帧缓冲
    uint8_t *m_pFrameBuffer = nullptr;              // 帧数据缓冲区
    int m_frameBufferSize;                          // 帧缓冲区大小
    AVCodec  *m_pCodec = nullptr;                   // AAC编码器
    AVStream *m_pStream = nullptr;                  // 音频流
    AVCodecContext *m_pCodecCtx = nullptr;          // 编码器上下文
    AVFormatContext *m_pFormatCtx = nullptr;        // 格式上下文
    SwrContext *m_swrCtx = nullptr;                 // 音频重采样上下文
    thread *m_encodeThread = nullptr;               // 编码线程
    volatile int m_exit = 0;                        // 退出标志
};


#endif //LEARNFFMPEG_SINGLEAUDIORECORDER_H
