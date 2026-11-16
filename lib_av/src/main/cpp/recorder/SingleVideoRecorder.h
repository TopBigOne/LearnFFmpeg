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
#include "HwVideoEncoder.h"
#include "VideoEncoderSink.h"
#include "ImageDef.h"
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include "LogUtil.h"

using namespace std;

/**
 * @brief 单独视频录制器类：只录视频，编码为 H.264
 *
 * 编码方式（构造时选择偏好，StartRecord 时按设备能力决定，硬件不可用时自动回退到 x264）：
 *  - Software：x264。画面经 OnFrame2Encode 入队，编码线程用 libyuv 转 YUV420P 后编码；
 *  - HardwareSurface：MediaCodec + 输入 Surface。画面由 GL 线程直接画到编码器 Surface
 *    （调用方 StartRecord 后把 GetVideoInputWindow() 交给 GLCameraRender::AttachEncoder），不经过 OnFrame2Encode；
 *  - HardwareBuffer：MediaCodec + 输入缓冲。画面经 OnFrame2Encode 入队，编码线程转换后写入编码器输入缓冲。
 * 硬件编码时 MP4 头要等编码器给出 SPS/PPS 才写。
 */
class SingleVideoRecorder : public VideoEncoderSink, private HwVideoEncoder::Callback {
public:
    enum class EncodeMode {
        Software,
        HardwareSurface,
        HardwareBuffer,
    };

    /**
     * @brief 构造函数
     * @param outUrl 输出文件路径
     * @param frameWidth 视频帧宽度
     * @param frameHeight 视频帧高度
     * @param bitRate 视频比特率
     * @param fps 视频帧率
     * @param preferredMode 偏好的编码方式，硬件不可用时回退到 Software
     */
    SingleVideoRecorder(const char* outUrl, int frameWidth, int frameHeight, long bitRate, int fps,
                        EncodeMode preferredMode = EncodeMode::Software);

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

    /** 实际使用的编码方式（StartRecord 成功后有效） */
    EncodeMode GetEncodeMode() const { return m_mode; }

    /** HardwareSurface 模式下编码器的输入 Surface，其他模式为 nullptr */
    ANativeWindow *GetVideoInputWindow() const;

    int GetVideoWidth() const { return m_frameWidth; }

    int GetVideoHeight() const { return m_frameHeight; }

    /** VideoEncoderSink：HardwareSurface 模式下 GL 线程为每帧取 pts（微秒） */
    int64_t NextVideoPtsUs(int64_t captureTimeUs) override;

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

    /** 打开 x264 编码器并创建视频流 */
    int OpenSoftwareEncoder();

    /** 打开 MediaCodec 并创建视频流，失败返回 false（已清理） */
    bool OpenHardwareEncoder(HwVideoEncoder::InputMode inputMode);

    /** 计算一帧的 pts（微秒）：起点前的帧返回 -1，第一帧为 0，之后严格递增 */
    int64_t AssignPtsUs(int64_t captureTimeUs);

    /** HwVideoEncoder::Callback：SPS/PPS 到达，写文件头 */
    void OnVideoConfig(const uint8_t *data, size_t size) override;

    /** HwVideoEncoder::Callback：一帧 H.264 码流 */
    void OnVideoPacket(const uint8_t *data, size_t size, int64_t ptsUs, bool keyFrame) override;

    ThreadSafeQueue<TimedImage *> m_frameQueue{15}; // 视频帧队列，线程安全，满了丢弃最旧的帧
    char m_outUrl[1024] = {0};                      // 输出文件路径
    int m_frameWidth;                               // 视频帧宽度
    int m_frameHeight;                              // 视频帧高度
    int64_t m_lastPtsUs = -1;                       // 上一帧 pts（微秒），保证严格递增
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
    bool m_headerWritten = false;                   // 受 m_writeMutex 保护

    EncodeMode m_preferredMode = EncodeMode::Software;
    EncodeMode m_mode = EncodeMode::Software;
    std::unique_ptr<HwVideoEncoder> m_hwEncoder;
    int64_t m_hwLastDtsUs = -1;                     // 硬件编码输出线程：上一包 dts
    std::mutex m_writeMutex;                        // 硬件编码时写头（输出线程）与写尾（停止线程）互斥
};


#endif //LEARNFFMPEG_SINGLEVIDEORECORDER_H
