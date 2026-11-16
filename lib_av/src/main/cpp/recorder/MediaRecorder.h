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
#include "RecorderClock.h"
#include "AudioChunk.h"
#include "YuvConvert.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include <cstdlib>

extern "C" {
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavutil/audio_fifo.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

using namespace std;

#define MAX_VIDEO_QUEUE_SIZE   15    // 视频帧队列上限（720p RGBA 每帧约 3.7MB），编码跟不上时丢弃最旧的帧
#define MAX_AUDIO_QUEUE_SIZE   200   // 音频帧队列上限
#define MAX_VIDEO_FRAME_POOL   4     // 视频帧缓冲复用池大小

// 音频时钟纠偏：每个窗口统计一次"采集时刻 - 音频时间轴位置"的最小偏差，超过阈值就补静音/丢采样。
// 取窗口内最小值：交付延迟只会让采集时刻看起来更晚（正向抖动），最小值最接近真实偏差。
#define AUDIO_DRIFT_WINDOW_US     1000000   // 统计窗口 1 秒
#define AUDIO_DRIFT_THRESHOLD_US  40000     // 偏差超过 40ms 才纠正（低于人耳对口型不同步的感知门限）

/**
 * @brief 待编码的视频帧：图像数据 + 采集时间戳
 */
struct VideoFrame : public NativeImage {
    int64_t captureTimeUs = 0;   // 采集时刻（CLOCK_BOOTTIME 微秒）
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
 * 时间戳（两路都以 StartRecord 时刻为 0 点，时钟统一为 CLOCK_BOOTTIME）：
 *  - 视频 pts = 采集时刻 - 起点（毫秒）。用相机传感器时间戳而不是到达时间，消除渲染/回读延迟；
 *    起点之前采集的帧丢弃，第一帧 pts 固定为 0，保证文件从 0 开始；
 *  - 音频 pts = 时间轴上的采样序号（time_base 1/sample_rate），声卡时钟连续、无抖动。
 *    时间轴与采集时刻保持一致：
 *      1. 开头：起点到第一个采样之间补静音（起点前采集的采样裁掉）；
 *      2. 队列丢块：按丢失的采样数原位补静音，后续音频不会整体提前；
 *      3. 时钟漂移：按窗口统计偏差，超过阈值补静音或丢采样，以系统时钟为准把音频拉回。
 */
class MediaRecorder {
public:
    MediaRecorder(const char *url, RecorderParam *param);

    ~MediaRecorder();

    /** @return 0 成功；负数失败（失败时已释放所有资源） */
    int StartRecord();

    /**
     * @param captureTimeUs 第一个采样的采集时刻（CLOCK_BOOTTIME 微秒），CAPTURE_TIME_UNKNOWN 表示未知
     */
    int OnFrame2Encode(AudioFrame *inputFrame, int64_t captureTimeUs);

    /**
     * @param captureTimeUs 画面的采集时刻（CLOCK_BOOTTIME 微秒），CAPTURE_TIME_UNKNOWN 表示未知
     */
    int OnFrame2Encode(NativeImage *inputFrame, int64_t captureTimeUs);

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

    /** 一块 PCM 进入音频时间轴：先按采集时间做对齐/补洞/纠偏，再写入 FIFO 并编码 */
    void ProcessAudioChunk(AudioChunk *chunk);

    /** 按偏差修正音频时间轴：正数补静音，负数记为待丢弃的采样 */
    void CorrectAudioTimeline(int64_t deviationUs);

    /** 向时间轴追加静音（分块写入并编码，内存占用有上限） */
    void AppendAudioSilence(int64_t samples);

    /** 写入一块 PCM：先扣掉待丢弃的采样，余下的进入时间轴 */
    void WriteAudioSamples(const uint8_t *data, int64_t samples);

    /** 采样写入 FIFO 并计入时间轴，满一帧就编码 */
    void FeedAudioFifo(const uint8_t *data, int samples);

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
    EncoderFrameConverter m_VideoConverter;        // 画面转成编码器的 YUV420P（libyuv，仅视频编码线程使用）

    ThreadSafeQueue<VideoFrame *> m_VideoFrameQueue{MAX_VIDEO_QUEUE_SIZE};
    ThreadSafeQueue<AudioChunk *> m_AudioFrameQueue{MAX_AUDIO_QUEUE_SIZE};

    std::mutex       m_FramePoolMutex;
    std::vector<VideoFrame *> m_FramePool;         // 视频帧缓冲复用池，避免每帧 malloc 一大块内存

    std::mutex       m_WriteMutex;                 // 两路编码线程写同一个文件，需要串行

    bool             m_EnableVideo = false;
    bool             m_EnableAudio = false;
    bool             m_HeaderWritten = false;
    std::atomic<bool> m_Started{false};            // 已成功开始录制
    std::atomic<bool> m_Exit{false};               // 通知编码线程：不会再有新数据
    int64_t          m_StartTimeUs = 0;            // 录制起点（CLOCK_BOOTTIME 微秒），两路时间戳的 0 点

    // ---- 音频时间轴（仅音频编码线程访问）----
    int              m_AudioBytesPerSample = 0;    // 输入 PCM 每个采样（含所有声道）的字节数
    std::atomic<int64_t> m_AudioPushedSamples{0};  // 生产端：已入队的采样总数，用于给每块编号
    int64_t          m_AudioNextSamplePos = 0;     // 消费端：期望的下一块编号
    int64_t          m_AudioTimelineSamples = 0;   // 已进入时间轴的采样数（含补的静音，扣除丢弃的）
    int64_t          m_AudioPendingSkip = 0;       // 待丢弃的采样数（音频超前时用）
    bool             m_AudioAnchored = false;      // 是否已完成开头对齐
    int64_t          m_DriftWindowStartUs = -1;    // 当前纠偏窗口的起始采集时刻
    int64_t          m_DriftWindowMinUs = 0;       // 当前窗口内的最小偏差
    std::vector<uint8_t> m_SilenceBuffer;          // 一帧长度的静音数据

    thread          *m_pAudioThread = nullptr;
    thread          *m_pVideoThread = nullptr;
};


#endif //LEARNFFMPEG_MEDIARECORDER_H
