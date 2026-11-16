/**
  ____        _             _____ _
 | __ ) _   _| |_ ___      |  ___| | _____      __
 |  _ \| | | | __/ _ \_____| |_  | |/ _ \ \ /\ / /
 | |_) | |_| | ||  __/_____|  _| | | (_) \ V  V /
 |____/ \__, |\__\___|     |_|   |_|\___/ \_/\_/
        |___/
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#ifndef LEARNFFMPEG_MEDIARECORDER_H
#define LEARNFFMPEG_MEDIARECORDER_H

#include <ImageDef.h>
#include <render/audio/AudioRender.h>
#include "ThreadSafeQueue.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

extern "C" {
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavutil/audio_fifo.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

using namespace std;

#define MAX_VIDEO_QUEUE_SIZE   15    // 视频帧队列上限（720p RGBA 每帧约 3.7MB），编码跟不上时丢弃最旧的帧
#define MAX_AUDIO_QUEUE_SIZE   200   // 音频帧队列上限
#define MAX_VIDEO_FRAME_POOL   4     // 视频帧缓冲复用池大小

/**
 * @brief 待编码的视频帧：图像数据 + 采集时间戳
 */
struct VideoFrame : public NativeImage {
    int64_t timestamp = 0;   // 相对录制开始的采集时间（毫秒），用作视频 pts
};

/**
 * @brief 输出流封装类
 *
 * 封装一路输出流（音频或视频）编码所需的全部对象
 */
class AVOutputStream {
public:
    AVOutputStream() {}
    ~AVOutputStream() {}
public:
    AVStream *m_pStream = nullptr;           // 输出文件中的流
    AVCodecContext *m_pCodecCtx = nullptr;   // 编码器上下文
    int64_t m_SamplesCount = 0;              // 音频：已编码的采样数（用于计算 pts）
    int64_t m_LastPts = -1;                  // 视频：上一帧 pts，保证单调递增
    AVFrame *m_pFrame = nullptr;             // 送入编码器的帧（编码器格式）
    AVFrame *m_pTmpFrame = nullptr;          // 音频：从 FIFO 读出的原始 S16 数据
    SwsContext *m_pSwsCtx = nullptr;         // 视频：图像格式/尺寸转换
    SwrContext *m_pSwrCtx = nullptr;         // 音频：采样格式转换
};

/**
 * @brief 录制参数结构体
 */
struct RecorderParam {
    //video
    int frameWidth;                        // 视频帧宽度（像素）
    int frameHeight;                       // 视频帧高度（像素）
    long videoBitRate;                     // 视频比特率（bps）
    int fps;                               // 视频帧率（帧/秒）

    //audio
    int audioSampleRate;                   // 音频采样率（Hz）
    int channelLayout;                     // 声道布局（单声道/立体声等）
    int sampleFormat;                      // 采样格式（如AV_SAMPLE_FMT_S16）
};

/**
 * @brief 音视频录制器
 *
 * 线程模型：
 *  - 生产者：GL 线程（视频帧）、AudioRecord 线程（音频数据），只负责入队；
 *  - 消费者：音频、视频各一个编码线程，用阻塞队列等待数据（不再轮询），互不等待；
 *  - 两路编码线程写同一个文件，写包由 m_WriteMutex 串行，交错排序由 av_interleaved_write_frame 完成。
 *
 * 时间戳：
 *  - 视频 pts = 采集时间（相对录制开始的毫秒），相机掉帧时画面时长依然正确；
 *  - 音频 pts = 已编码采样数（time_base 1/sample_rate），起点按第一块音频数据的到达时间对齐。
 */
class MediaRecorder {
public:
    MediaRecorder(const char *url, RecorderParam *param);

    ~MediaRecorder();

    /** @return 0 成功；负数失败（失败时已释放所有资源） */
    int StartRecord();

    int OnFrame2Encode(AudioFrame *inputFrame);

    int OnFrame2Encode(NativeImage *inputFrame);

    /** 停止录制：把队列里剩余的数据编码完、刷新编码器、写文件尾。可重复调用 */
    int StopRecord();

private:
    static void AudioEncodeThread(MediaRecorder *recorder);

    static void VideoEncodeThread(MediaRecorder *recorder);

    AVFrame *AllocAudioFrame(AVSampleFormat sample_fmt, uint64_t channel_layout, int sample_rate, int nb_samples);

    AVFrame *AllocVideoFrame(AVPixelFormat pix_fmt, int width, int height);

    int WritePacket(AVRational *time_base, AVStream *st, AVPacket *pkt);

    int AddStream(AVOutputStream *ost, AVCodec **codec, AVCodecID codec_id);

    int OpenAudio(AVCodec *codec, AVOutputStream *ost);

    int OpenVideo(AVCodec *codec, AVOutputStream *ost);

    /** 从 FIFO 取 nbSamples 个采样编码 */
    int EncodeAudioSamples(int nbSamples);

    int EncodeVideoFrame(VideoFrame *videoFrame);

    /** 把一帧（nullptr 表示刷新）送入编码器，并把产出的包全部写入文件 */
    int EncodeAndWrite(AVOutputStream *ost, AVFrame *frame);

    VideoFrame *ObtainVideoFrame(int width, int height, int format);

    void RecycleVideoFrame(VideoFrame *frame);

    void CloseStream(AVOutputStream *ost);

    /** 释放所有资源，可重复调用 */
    void Release();

private:
    RecorderParam    m_RecorderParam = {0};        // 录制参数配置
    AVOutputStream   m_VideoStream;                // 视频输出流对象
    AVOutputStream   m_AudioStream;                // 音频输出流对象
    char             m_OutUrl[1024] = {0};         // 输出文件路径
    AVOutputFormat  *m_OutputFormat = nullptr;     // 输出格式（如MP4、FLV等）
    AVFormatContext *m_FormatCtx = nullptr;        // 格式上下文，管理输出文件
    AVCodec         *m_AudioCodec = nullptr;       // 音频编码器（如AAC）
    AVCodec         *m_VideoCodec = nullptr;       // 视频编码器（如H264）
    AVAudioFifo     *m_AudioFifo = nullptr;        // 音频采样 FIFO：凑满编码器要求的帧长再编码

    ThreadSafeQueue<VideoFrame *> m_VideoFrameQueue{MAX_VIDEO_QUEUE_SIZE};
    ThreadSafeQueue<AudioFrame *> m_AudioFrameQueue{MAX_AUDIO_QUEUE_SIZE};

    std::mutex       m_FramePoolMutex;
    std::vector<VideoFrame *> m_FramePool;         // 视频帧缓冲复用池，避免每帧 malloc 一大块内存

    std::mutex       m_WriteMutex;                 // 两路编码线程写同一个文件，需要串行

    bool             m_EnableVideo = false;
    bool             m_EnableAudio = false;
    bool             m_HeaderWritten = false;
    std::atomic<bool> m_Started{false};            // 已成功开始录制
    std::atomic<bool> m_Exit{false};               // 通知编码线程：不会再有新数据
    int64_t          m_StartTime = 0;              // 录制开始的系统时间（毫秒）

    thread          *m_pAudioThread = nullptr;
    thread          *m_pVideoThread = nullptr;
};


#endif //LEARNFFMPEG_MEDIARECORDER_H
