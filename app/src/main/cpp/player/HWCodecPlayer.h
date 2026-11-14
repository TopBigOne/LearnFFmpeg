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
#include <SyncClock.h>
#include <atomic>
// FFmpeg 4.2 中 av_bsf_* 声明在 avcodec.h 里（4.4 起才拆到 bsf.h），已由 MediaPlayer.h 间接包含

// 视频缓冲区最大时长（秒）- 防止缓冲过多数据包
#define BUFF_MAX_VIDEO_DURATION   0.5
// 音频缓冲区最大时长（秒）
#define BUFF_MAX_AUDIO_DURATION   1.0
// 音视频同步单次最大等待时间（毫秒）
#define MAX_SYNC_SLEEP_TIME       200
// 视频落后主时钟超过该值（毫秒）则丢帧追赶
#define AV_SYNC_DROP_THRESHOLD    100
// MediaCodec 输入/输出缓冲区的等待超时（微秒）
#define CODEC_DEQUEUE_TIMEOUT_US  10000

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
 * - 解封装线程(DeMux)：从媒体文件读取音视频数据包，放入两个队列
 * - 视频解码线程(VideoDecode)：使用MediaCodec硬解视频，直接渲染到 Surface
 * - 音频解码线程(AudioDecode)：使用FFmpeg软解音频，经 Java AudioTrack 播放
 *
 * 线程协作约定：
 * - 每个解码器只被它自己的解码线程操作。seek 时解封装线程只清空队列并插入 FLUSH 标记，
 *   解码线程收到标记后自行 flush 解码器，避免跨线程并发访问 MediaCodec/AVCodecContext；
 * - 文件读完时插入 EOS 标记，解码线程据此通知解码器输入结束，把缓存的帧全部取出；
 * - 停止时 Abort 两个队列，唤醒所有阻塞的线程，保证 join 不会卡住。
 *
 * 音视频同步：以音频时钟为主时钟；没有音轨（或音频时钟暂不可用）时以系统时钟为参考。
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
     * @brief 视频帧的音视频同步
     * @param videoPts 视频帧时间戳（毫秒）
     * @param sysBase  系统时钟参考（now - pts），音频时钟不可用时使用；-1 表示需要重建
     * @return true 渲染；false 严重落后，丢弃
     */
    bool AVSync(int64_t videoPts, int64_t &sysBase);

    /**
     * @brief 初始化解码器（打开文件、音频软解码器、视频 MediaCodec、码流过滤器、重采样）
     * @return 0成功，-1失败
     */
    int InitDecoder();

    /** 根据视频流参数创建并启动 MediaCodec */
    int InitVideoCodec(AVStream *videoStream);

    /**
     * @brief 解封装循环：读包分发到队列，处理暂停/停止/seek/EOF
     */
    int DoMuxLoop();

    /**
     * @brief 释放解码器资源（会先等待解码线程退出）
     */
    int UnInitDecoder();

    /** 暂停时阻塞等待，返回 false 表示需要退出 */
    bool WaitWhilePaused();

    /** 音频和视频都播放完时，上报一次播放完成 */
    void CheckPlaybackDone();

    /** 把 AV_TIME_BASE / 流时间戳换算为相对文件起点的毫秒 */
    int64_t ToMs(int64_t ts, AVRational timeBase);

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
    AVBSFContext*                 m_Bsfc = nullptr;  // 码流过滤器：mp4(AVCC/HVCC) -> Annex-B，MediaCodec 需要后者
    AVCodecContext*      m_AudioCodecCtx = nullptr;  // 音频解码器上下文
    AVRational           m_VideoTimeBase = {0, 1};   // 视频时间基
    AVRational           m_AudioTimeBase = {0, 1};   // 音频时间基
    SwrContext*                 m_SwrCtx = nullptr;  // 音频重采样上下文
    int64_t                   m_Duration = 0;        // 媒体总时长（毫秒）
    int64_t                  m_StartTime = 0;        // 文件起始时间（毫秒），时间戳都减去它

    // Android MediaCodec相关
    AMediaCodec*            m_MediaCodec = nullptr;     // MediaCodec硬件解码器
    ANativeWindow*       m_ANativeWindow = nullptr;     // Native窗口（用于视频渲染）

    // 线程
    thread*              m_DeMuxThread   = nullptr;  // 解封装线程
    thread*              m_ADecodeThread = nullptr;  // 音频解码线程
    thread*              m_VDecodeThread = nullptr;  // 视频解码线程

    // 播放状态（多线程读写，使用原子变量）
    std::atomic<int>       m_PlayerState{PLAYER_STATE_UNKNOWN}; // 播放器状态
    std::atomic<float>    m_SeekPosition{-1.0f};     // Seek目标位置（秒，-1表示无Seek请求）
    int                 m_VideoStreamIdx = -1;       // 视频流索引
    int                 m_AudioStreamIdx = -1;       // 音频流索引（-1 表示没有音轨）
    std::atomic<int>       m_VideoWidth{0};          // 缓存的视频宽高，供 GetMediaParams 查询
    std::atomic<int>       m_VideoHeight{0};
    std::atomic<bool>      m_VideoDone{false};       // 视频已播放到结尾
    std::atomic<bool>      m_AudioDone{false};       // 音频已播放到结尾
    std::atomic<bool>      m_DonePosted{false};      // 已上报过播放完成

    // 线程同步：锁和条件变量
    mutex               m_Mutex;          // 播放状态互斥锁
    condition_variable  m_Cond;           // 条件变量（暂停/恢复/停止/seek 时唤醒）

    // 音视频同步时钟
    SyncClock           m_AudioClock;     // 音频时钟（作为主时钟）

    jmethodID           m_EventCallbackMid = nullptr;  // 缓存的 Java 回调方法 ID
};


#endif //LEARNFFMPEG_HWCODECPLAYER_H
