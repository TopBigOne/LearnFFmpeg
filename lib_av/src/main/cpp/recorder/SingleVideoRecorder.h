/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#ifndef LEARNFFMPEG_SINGLEVIDEORECORDER_H
#define LEARNFFMPEG_SINGLEVIDEORECORDER_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
}

#include "ThreadSafeQueue.h"
#include "RecorderClock.h"
#include "YuvConvert.h"
#include "ImageDef.h"
#include <thread>
#include <atomic>
#include "LogUtil.h"

using namespace std;

/**
 * @brief 单独视频录制器类
 *
 * 专门用于视频录制，将原始视频帧编码为H264格式
 * 支持多线程编码和队列缓冲
 */
class SingleVideoRecorder {
public:
    /**
     * @brief 构造函数
     * @param outUrl 输出文件路径
     * @param frameWidth 视频帧宽度
     * @param frameHeight 视频帧高度
     * @param bitRate 视频比特率
     * @param fps 视频帧率
     */
    SingleVideoRecorder(const char* outUrl, int frameWidth, int frameHeight, long bitRate, int fps);

    /**
     * @brief 析构函数
     */
    ~SingleVideoRecorder();

    /**
     * @brief 开始录制
     * 初始化编码器并启动编码线程
     * @return 0表示成功，负数表示失败
     */
    int StartRecord();

    /**
     * @brief 接收视频帧数据
     * 将视频帧添加到编码队列
     * @param inputFrame 输入的视频帧
     * @param captureTimeUs 画面的采集时刻（CLOCK_BOOTTIME 微秒），CAPTURE_TIME_UNKNOWN 表示未知
     * @return 0表示成功，负数表示失败
     */
    int OnFrame2Encode(NativeImage *inputFrame, int64_t captureTimeUs);

    /**
     * @brief 停止录制
     * 停止编码线程并写入文件尾
     * @return 0表示成功，负数表示失败
     */
    int StopRecord();

private:
    /**
     * @brief H264编码线程函数（静态函数）
     * @param context SingleVideoRecorder实例指针
     */
    static void StartH264EncoderThread(SingleVideoRecorder *context);

    /**
     * @brief 编码一帧视频
     * @param pFrame 待编码的视频帧
     * @return 0表示成功，负数表示失败
     */
    int EncodeFrame(AVFrame *pFrame);

private:
    /** 待编码的视频帧：图像 + 采集时间戳 */
    struct TimedImage : public NativeImage {
        int64_t captureTimeUs = 0;                  // 采集时刻（CLOCK_BOOTTIME 微秒）
    };

    /** 释放所有资源，可重复调用 */
    void Release();

    ThreadSafeQueue<TimedImage *> m_frameQueue{15}; // 视频帧队列，线程安全，满了丢弃最旧的帧
    char m_outUrl[1024] = {0};                      // 输出文件路径
    int m_frameWidth;                               // 视频帧宽度
    int m_frameHeight;                              // 视频帧高度
    int64_t m_lastPts = -1;                         // 上一帧 pts，保证严格递增
    int64_t m_startTimeUs = 0;                      // 录制起点（CLOCK_BOOTTIME 微秒），pts 的 0 点
    long m_bitRate;                                 // 视频比特率
    int m_frameRate;                                // 视频帧率
    AVFrame  *m_pFrame = nullptr;                   // 编码帧缓冲（YUV420P）
    AVCodec  *m_pCodec = nullptr;                   // 视频编码器（优先 H.264）
    AVStream *m_pStream = nullptr;                  // 视频流
    AVCodecContext *m_pCodecCtx = nullptr;          // 编码器上下文
    AVFormatContext *m_pFormatCtx = nullptr;        // 格式上下文
    thread *m_encodeThread = nullptr;               // 编码线程
    EncoderFrameConverter m_converter;              // 画面转成编码器的 YUV420P（libyuv，仅编码线程使用）
    std::atomic<bool> m_started{false};             // 已成功开始录制
    std::atomic<bool> m_exit{false};                // 退出标志
    bool m_headerWritten = false;
};


#endif //LEARNFFMPEG_SINGLEVIDEORECORDER_H
