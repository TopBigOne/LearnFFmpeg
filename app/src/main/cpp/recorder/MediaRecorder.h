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
#include "thread"

extern "C" {
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

using namespace std;

/**
 * @brief 视频帧类型别名
 *
 * 使用NativeImage作为VideoFrame的类型定义，用于表示视频帧数据
 */
typedef NativeImage VideoFrame;

/**
 * @brief AVOutputStream类
 *
 * 封装FFmpeg的音视频输出流相关的上下文和状态信息
 * 包含编码器上下文、帧缓冲、转换上下文等
 */
class AVOutputStream {
public:
    /**
     * @brief AVOutputStream构造函数
     *
     * 初始化所有成员变量为默认值
     */
    AVOutputStream() {
        m_pStream = nullptr;
        m_pCodecCtx = nullptr;
        m_pFrame = nullptr;
        m_pTmpFrame = nullptr;
        m_pSwrCtx = nullptr;
        m_pSwsCtx = nullptr;
        m_NextPts = 0;
        m_SamplesCount = 0;
        m_EncodeEnd = 0;
    }

    /**
     * @brief AVOutputStream析构函数
     */
    ~AVOutputStream(){}
public:
    AVStream *m_pStream;                    // AVStream流指针，指向媒体文件中的流
    AVCodecContext *m_pCodecCtx;           // 编码器上下文，包含编码参数
    volatile int64_t m_NextPts;            // 下一个PTS时间戳，用于帧的时间标记
    volatile int m_EncodeEnd;              // 编码结束标志，1表示编码结束
    int m_SamplesCount;                    // 音频样本计数，记录已编码的样本数
    AVFrame *m_pFrame;                     // 编码帧缓冲，用于存储待编码的帧
    AVFrame *m_pTmpFrame;                  // 临时帧缓冲，用于格式转换
    SwsContext *m_pSwsCtx;                 // 图像格式转换上下文，用于视频格式转换
    SwrContext *m_pSwrCtx;                 // 音频重采样上下文，用于音频格式转换
};

/**
 * @brief 录制参数结构体
 *
 * 包含音视频编码所需的参数配置
 */
struct RecorderParam {
    // 视频参数
    int frameWidth;                        // 视频帧宽度（像素）
    int frameHeight;                       // 视频帧高度（像素）
    long videoBitRate;                     // 视频比特率（bps）
    int fps;                              // 视频帧率（帧/秒）

    // 音频参数
    int audioSampleRate;                   // 音频采样率（Hz）
    int channelLayout;                     // 声道布局（单声道/立体声等）
    int sampleFormat;                      // 采样格式（如AV_SAMPLE_FMT_S16）
};

/**
 * @brief 媒体录制器类
 *
 * 负责音视频数据的编码和录制，支持多线程编码
 * 使用FFmpeg库进行媒体文件的写入和编码操作
 */
class MediaRecorder {
public:
    /**
     * @brief MediaRecorder构造函数
     * @param url 输出文件路径
     * @param param 录制参数
     */
    MediaRecorder(const char *url, RecorderParam *param);

    /**
     * @brief MediaRecorder析构函数
     */
    ~MediaRecorder();

    /**
     * @brief 开始录制
     * 初始化编码器并启动编码线程
     * @return 0表示成功，负数表示失败
     */
    int StartRecord();

    /**
     * @brief 添加音频数据到音频队列
     * @param inputFrame 输入的音频帧数据
     * @return 0表示成功，负数表示失败
     */
    int OnFrame2Encode(AudioFrame *inputFrame);

    /**
     * @brief 添加视频数据到视频队列
     * @param inputFrame 输入的视频帧数据
     * @return 0表示成功，负数表示失败
     */
    int OnFrame2Encode(VideoFrame *inputFrame);

    /**
     * @brief 停止录制
     * 停止编码线程并写入文件尾
     * @return 0表示成功，负数表示失败
     */
    int StopRecord();

private:
    /**
     * @brief 启动音频编码线程（静态函数）
     * @param recorder MediaRecorder实例指针
     */
    static void StartAudioEncodeThread(MediaRecorder *recorder);

    /**
     * @brief 启动视频编码线程（静态函数）
     * @param recorder MediaRecorder实例指针
     */
    static void StartVideoEncodeThread(MediaRecorder *recorder);

    /**
     * @brief 启动统一媒体编码线程（静态函数）
     * 在单个线程中处理音视频编码
     * @param recorder MediaRecorder实例指针
     */
    static void StartMediaEncodeThread(MediaRecorder *recorder);

    /**
     * @brief 分配音频缓冲帧
     * @param sample_fmt 采样格式
     * @param channel_layout 声道布局
     * @param sample_rate 采样率
     * @param nb_samples 样本数量
     * @return 分配的AVFrame指针
     */
    AVFrame *AllocAudioFrame(AVSampleFormat sample_fmt, uint64_t channel_layout, int sample_rate, int nb_samples);

    /**
     * @brief 分配视频缓冲帧
     * @param pix_fmt 像素格式
     * @param width 视频宽度
     * @param height 视频高度
     * @return 分配的AVFrame指针
     */
    AVFrame *AllocVideoFrame(AVPixelFormat pix_fmt, int width, int height);

    /**
     * @brief 写编码包到媒体文件
     * @param fmt_ctx 格式上下文
     * @param time_base 时间基准
     * @param st 流指针
     * @param pkt 数据包指针
     * @return 0表示成功，负数表示失败
     */
    int WritePacket(AVFormatContext *fmt_ctx, AVRational *time_base, AVStream *st, AVPacket *pkt);

    /**
     * @brief 添加媒体流
     * @param ost 输出流对象
     * @param oc 输出格式上下文
     * @param codec 编解码器指针的指针
     * @param codec_id 编解码器ID
     */
    void AddStream(AVOutputStream *ost, AVFormatContext *oc, AVCodec **codec, AVCodecID codec_id);

    /**
     * @brief 打印packet信息（用于调试）
     * @param fmt_ctx 格式上下文
     * @param pkt 数据包指针
     */
    void PrintfPacket(AVFormatContext *fmt_ctx, AVPacket *pkt);

    /**
     * @brief 打开音频编码器
     * @param oc 输出格式上下文
     * @param codec 编解码器
     * @param ost 输出流对象
     * @return 0表示成功，负数表示失败
     */
    int OpenAudio(AVFormatContext *oc, AVCodec *codec, AVOutputStream *ost);

    /**
     * @brief 打开视频编码器
     * @param oc 输出格式上下文
     * @param codec 编解码器
     * @param ost 输出流对象
     * @return 0表示成功，负数表示失败
     */
    int OpenVideo(AVFormatContext *oc, AVCodec *codec, AVOutputStream *ost);

    /**
     * @brief 编码一帧音频
     * @param ost 音频输出流对象
     * @return 0表示成功，负数表示失败
     */
    int EncodeAudioFrame(AVOutputStream *ost);

    /**
     * @brief 编码一帧视频
     * @param ost 视频输出流对象
     * @return 0表示成功，负数表示失败
     */
    int EncodeVideoFrame(AVOutputStream *ost);

    /**
     * @brief 释放编码器上下文
     * 关闭编码器并释放相关资源
     * @param ost 输出流对象
     */
    void CloseStream(AVOutputStream *ost);

private:
    RecorderParam    m_RecorderParam = {0};        // 录制参数配置
    AVOutputStream   m_VideoStream;                // 视频输出流对象
    AVOutputStream   m_AudioStream;                // 音频输出流对象
    char             m_OutUrl[1024] = {0};         // 输出文件路径
    AVOutputFormat  *m_OutputFormat = nullptr;    // 输出格式（如MP4、FLV等）
    AVFormatContext *m_FormatCtx = nullptr;        // 格式上下文，管理输出文件
    AVCodec         *m_AudioCodec = nullptr;       // 音频编码器（如AAC）
    AVCodec         *m_VideoCodec = nullptr;       // 视频编码器（如H264）

    // 视频帧队列，缓存待编码的视频帧
    ThreadSafeQueue<VideoFrame *>
                     m_VideoFrameQueue;

    // 音频帧队列，缓存待编码的音频帧
    ThreadSafeQueue<AudioFrame *>
                     m_AudioFrameQueue;

    int              m_EnableVideo = 0;            // 视频启用标志，1表示启用
    int              m_EnableAudio = 0;            // 音频启用标志，1表示启用
    volatile bool    m_Exit = false;               // 退出标志，用于通知线程退出

    // 音频编码线程
    thread          *m_pAudioThread = nullptr;

    // 视频编码线程
    thread          *m_pVideoThread = nullptr;

    // 统一媒体编码线程（音视频在同一线程中编码）
    thread          *m_pMediaThread = nullptr;
};


#endif //LEARNFFMPEG_MEDIARECORDER_H
