/**
 *
 * Created by 公众号：字节流动 on 2021/12/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#ifndef LEARNFFMPEG_HWCODECPLAYER_H
#define LEARNFFMPEG_HWCODECPLAYER_H

#include <MediaPlayer.h>
#include <AVPacketQueue.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <SyncClock.h>

// 视频缓冲区最大时长（秒）- 防止缓冲过多数据包
#define BUFF_MAX_VIDEO_DURATION   0.5
// 音视频同步最大休眠时间（毫秒）
#define MAX_SYNC_SLEEP_TIME       200
// 视频帧默认延迟时间（毫秒）
#define VIDEO_FRAME_DEFAULT_DELAY 25
// 视频帧最大延迟时间（毫秒）
#define VIDEO_FRAME_MAX_DELAY     250

// 音视频同步阈值最小值（毫秒）
#define AV_SYNC_THRESHOLD_MIN   40
// 音视频同步阈值最大值（毫秒）
#define AV_SYNC_THRESHOLD_MAX   100
// 音视频同步帧重复阈值
#define AV_SYNC_FRAMEDUP_THRESHOLD AV_SYNC_THRESHOLD_MAX

/**
 * @brief 播放器状态枚举
 */
enum PlayerState {
    PLAYER_STATE_UNKNOWN,   // 未知状态
    PLAYER_STATE_PLAYING,   // 播放中
    PLAYER_STATE_PAUSE,     // 暂停
    PLAYER_STATE_STOP       // 停止
};

/**
 * @brief 硬件解码播放器类
 *
 * 使用Android MediaCodec进行硬件解码的播放器实现
 * 采用三线程架构：
 * - 解封装线程(DeMux)：从媒体文件读取音视频数据包
 * - 视频解码线程(VideoDecode)：使用MediaCodec硬解视频
 * - 音频解码线程(AudioDecode)：使用FFmpeg软解音频
 *
 * 特点：
 * - 硬件加速视频解码，性能优异
 * - 支持音视频同步
 * - 支持Seek操作
 * - 使用PacketQueue缓冲音视频数据包
 */
class HWCodecPlayer : public MediaPlayer {
public:
    HWCodecPlayer(){};
    virtual ~HWCodecPlayer(){};

    // MediaPlayer接口实现
    virtual void Init(JNIEnv *jniEnv, jobject obj, char *url, int renderType, jobject surface);
    virtual void UnInit();

    virtual void Play();
    virtual void Pause();
    virtual void Stop();
    virtual void SeekToPosition(float position);
    virtual long GetMediaParams(int paramType);
    virtual void SetMediaParams(int paramType, jobject obj);

private:
    // JNI相关方法
    virtual JNIEnv *GetJNIEnv(bool *isAttach);
    virtual jobject GetJavaObj();
    virtual JavaVM *GetJavaVM();

    /**
     * @brief 音视频同步
     * 计算视频帧显示延迟，实现视频向音频同步
     */
    void AVSync();

    /**
     * @brief 初始化解码器
     * @return 0成功，-1失败
     *
     * 初始化流程：
     * 1. 打开媒体文件
     * 2. 查找音视频流
     * 3. 创建FFmpeg解码器上下文（用于音频和解封装）
     * 4. 创建MediaCodec硬件解码器（用于视频）
     * 5. 初始化音频重采样器
     * 6. 启动音视频解码线程
     */
    int InitDecoder();

    /**
     * @brief 解封装循环
     * @return 0成功
     *
     * 从媒体文件中读取音视频数据包，分发到对应的队列
     * 处理暂停、停止、Seek等控制逻辑
     */
    int DoMuxLoop();

    /**
     * @brief 释放解码器资源
     * @return 0成功
     */
    int UnInitDecoder();

    // 静态线程函数
    static void PostMessage(void *context, int msgType, float msgCode);
    static void DeMuxThreadProc(HWCodecPlayer* player);        // 解封装线程
    static void AudioDecodeThreadProc(HWCodecPlayer* player);  // 音频解码线程
    static void VideoDecodeThreadProc(HWCodecPlayer* player);  // 视频解码线程

private:
    // 音视频数据包队列
    AVPacketQueue*    m_VideoPacketQueue = nullptr;  // 视频包队列
    AVPacketQueue*    m_AudioPacketQueue = nullptr;  // 音频包队列

    // FFmpeg相关
    AVFormatContext*   m_AVFormatContext = nullptr;  // 封装格式上下文
    char                 m_Url[MAX_PATH] = {0};      // 媒体文件路径
    AVBitStreamFilterContext*     m_Bsfc = nullptr;  // H264比特流过滤器（mp4toannexb）
    AVCodecContext*      m_AudioCodecCtx = nullptr;  // 音频解码器上下文
    AVCodecContext*      m_VideoCodecCtx = nullptr;  // 视频解码器上下文（用于解封装，不用于解码）
    AVRational           m_VideoTimeBase = {0};      // 视频时间基
    AVRational           m_AudioTimeBase = {0};      // 音频时间基
    SwrContext*                 m_SwrCtx = nullptr;  // 音频重采样上下文
    long                      m_Duration = 0;        // 媒体总时长（毫秒）

    // Android MediaCodec相关
    AMediaCodec*            m_MediaCodec = nullptr;     // MediaCodec硬件解码器
    AMediaExtractor*    m_MediaExtractor = nullptr;     // MediaExtractor（用于获取视频格式）
    ANativeWindow*       m_ANativeWindow = nullptr;     // Native窗口（用于视频渲染）

    // 线程
    thread*              m_DeMuxThread   = nullptr;  // 解封装线程
    thread*              m_ADecodeThread = nullptr;  // 音频解码线程
    thread*              m_VDecodeThread = nullptr;  // 视频解码线程
    jobject              m_AssetMgr      = nullptr;  // Asset管理器（用于读取assets资源）

    // 播放状态
    PlayerState            m_PlayerState = PLAYER_STATE_UNKNOWN;  // 播放器状态
    volatile float        m_SeekPosition = -1;       // Seek目标位置（-1表示无Seek请求）
    volatile bool          m_SeekSuccess = false;    // Seek是否成功
    int                 m_VideoStreamIdx = -1;       // 视频流索引
    int                 m_AudioStreamIdx = -1;       // 音频流索引

    // 音视频同步相关
    double              m_VideoStartBase = -1.0;     // 视频起始时间基准
    double              m_AudioStartBase = -1.0;     // 音频起始时间基准
    double             m_CommonStartBase = -1.0;     // 公共起始时间基准


    // 线程同步：锁和条件变量
    mutex               m_Mutex;          // 播放状态互斥锁
    mutex               m_VideoBufMutex;  // 视频缓冲区互斥锁
    mutex               m_AudioBufMutex;  // 音频缓冲区互斥锁
    condition_variable  m_Cond;           // 条件变量（用于暂停/恢复）

    // 音视频同步时钟
    SyncClock           m_VideoClock;     // 视频时钟
    SyncClock           m_AudioClock;     // 音频时钟（作为主时钟）
    AVRational          m_FrameRate;      // 视频帧率
};


#endif //LEARNFFMPEG_HWCODECPLAYER_H
