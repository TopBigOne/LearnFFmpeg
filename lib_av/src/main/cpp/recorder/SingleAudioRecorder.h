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
#include <libavutil/audio_fifo.h>
}

#include "ThreadSafeQueue.h"
#include "AudioChunk.h"
#include <vector>
#include <thread>
#include <atomic>
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
    /** 从 FIFO 取 nbSamples 个采样，转换格式后编码 */
    int EncodeSamples(int nbSamples);

    /** 采样写入 FIFO，满一帧就编码 */
    void FeedFifo(const uint8_t *data, int samples);

    /** 释放所有资源，可重复调用 */
    void Release();

    ThreadSafeQueue<AudioChunk *> m_frameQueue{200}; // 音频帧队列，线程安全，满了丢弃最旧的
    char m_outUrl[1024] = {0};                      // 输出文件路径
    int64_t m_samplesCount = 0;                     // 已编码的采样数，用于计算 pts
    int m_sampleRate;                               // 输入音频采样率
    int m_channelLayout;                            // 输入声道布局
    int m_sampleFormat;                             // 输入采样格式
    AVFrame  *m_pFrame = nullptr;                   // 编码帧（编码器格式）
    AVFrame  *m_pInFrame = nullptr;                 // 从 FIFO 读出的原始数据（输入格式）
    AVAudioFifo *m_pFifo = nullptr;                 // 采样 FIFO：凑满编码器要求的帧长再编码
    AVCodec  *m_pCodec = nullptr;                   // AAC编码器
    AVStream *m_pStream = nullptr;                  // 音频流
    AVCodecContext *m_pCodecCtx = nullptr;          // 编码器上下文（自己创建并持有）
    AVFormatContext *m_pFormatCtx = nullptr;        // 格式上下文
    SwrContext *m_swrCtx = nullptr;                 // 音频重采样上下文
    thread *m_encodeThread = nullptr;               // 编码线程
    std::atomic<bool> m_started{false};             // 已成功开始录制
    std::atomic<bool> m_exit{false};                // 退出标志
    bool m_headerWritten = false;
    int m_bytesPerSample = 0;                       // 输入 PCM 每个采样（含所有声道）的字节数
    std::atomic<int64_t> m_pushedSamples{0};        // 生产端：已入队的采样总数，用于给每块编号
    int64_t m_nextSamplePos = 0;                    // 消费端：期望的下一块编号，出现缺口说明队列丢了块
    std::vector<uint8_t> m_silenceBuffer;           // 一帧长度的静音，用于填补被丢弃的块
};


#endif //LEARNFFMPEG_SINGLEAUDIORECORDER_H
