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

#ifndef LEARNFFMPEG_DECODERBASE_H
#define LEARNFFMPEG_DECODERBASE_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/time.h>
#include <libavcodec/jni.h>
};

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "Decoder.h"

#define MAX_PATH   2048                        // 最大路径长度
#define DELAY_THRESHOLD 100                   // 单次同步等待的上限（ms），避免时间戳异常时长时间睡眠
#define DROP_THRESHOLD  100                   // 视频落后主时钟超过该值（ms）则丢帧追赶

using namespace std;

/**
 * @brief 解码器状态枚举
 */
enum DecoderState {
    STATE_UNKNOWN,                            // 未知状态
    STATE_DECODING,                           // 解码中
    STATE_PAUSE,                              // 暂停
    STATE_STOP                                // 停止
};

/**
 * @brief 解码器消息类型枚举
 */
enum DecoderMsg {
    MSG_DECODER_INIT_ERROR,                   // 解码器初始化错误
    MSG_DECODER_READY,                        // 解码器准备就绪
    MSG_DECODER_DONE,                         // 解码完成
    MSG_REQUEST_RENDER,                       // 请求渲染
    MSG_DECODING_TIME                         // 解码时间
};

/**
 * @brief 解码器基类
 *
 * 继承自Decoder接口，实现基于FFmpeg的音视频解码功能
 * 提供解码线程管理、音视频同步等基础功能
 *
 * 音视频同步策略（音频为主时钟）：
 *  - 音频解码线程不主动睡眠，由音频渲染器的有界队列"反压"控制节奏，
 *    音频渲染器根据真正送进声卡的帧维护音频时钟；
 *  - 视频解码线程通过 AVSyncCallback 读取音频时钟，早了就等、晚太多就丢帧；
 *  - 拿不到音频时钟（没有音轨、音频还没开始播）时，退回到以系统单调时钟为参考。
 */
class DecoderBase : public Decoder {
public:
    /**
     * @brief 构造函数
     */
    DecoderBase()
    {};

    /**
     * @brief 虚析构函数
     */
    virtual~ DecoderBase()
    {};

    /**
     * @brief 开始播放
     * 首次调用时启动解码线程，之后调用用于从暂停中恢复
     */
    virtual void Start();

    /**
     * @brief 暂停播放
     * 解码线程阻塞在条件变量上（不轮询），保持当前位置
     */
    virtual void Pause();

    /**
     * @brief 停止播放
     * 通知解码线程退出
     */
    virtual void Stop();

    /**
     * @brief 获取媒体时长
     * @return 时长（秒）
     */
    virtual float GetDuration()
    {
        // 毫秒转秒
        return m_Duration * 1.0f / 1000;
    }

    /**
     * @brief 跳转到指定播放位置
     * @param position 目标播放位置（秒），允许为 0
     */
    virtual void SeekToPosition(float position);

    /**
     * @brief 获取当前播放位置
     * @return 当前播放位置（毫秒）
     */
    virtual float GetCurrentPosition();

    /**
     * @brief 清空缓存
     * 派生类可重写此方法以实现特定的缓存清理逻辑（seek 时调用）
     */
    virtual void ClearCache()
    {};

    /**
     * @brief 设置消息回调
     * @param context 上下文
     * @param callback 回调函数
     */
    virtual void SetMessageCallback(void* context, MessageCallback callback)
    {
        m_MsgContext = context;
        m_MsgCallback = callback;
    }

    /**
     * @brief 设置音视频同步回调（获取主时钟）
     * @param context 上下文
     * @param callback 返回主时钟（毫秒），无效时返回 -1
     */
    virtual void SetAVSyncCallback(void* context, AVSyncCallback callback)
    {
        m_AVDecoderContext = context;
        m_AVSyncCallback = callback;
    }

protected:
    void * m_MsgContext = nullptr;                      // 消息回调的上下文指针
    MessageCallback m_MsgCallback = nullptr;           // 消息回调函数指针

    /**
     * @brief 初始化解码器
     * @param url 媒体文件URL或路径
     * @param mediaType 媒体类型（AVMEDIA_TYPE_AUDIO 或 AVMEDIA_TYPE_VIDEO）
     * @return 0表示成功，负数表示失败
     */
    virtual int Init(const char *url, AVMediaType mediaType);

    /**
     * @brief 释放解码器资源
     * 停止并回收解码线程
     */
    virtual void UnInit();

    /**
     * @brief 解码器准备就绪回调（纯虚函数）
     * 当解码器初始化成功并准备好开始解码时调用
     */
    virtual void OnDecoderReady() = 0;

    /**
     * @brief 解码完成回调（纯虚函数）
     * 解码线程退出前调用，派生类在这里释放自己的资源
     */
    virtual void OnDecoderDone() = 0;

    /**
     * @brief 解码数据的回调（纯虚函数）
     * @param frame 解码后的帧数据（音频帧或视频帧）
     */
    virtual void OnFrameAvailable(AVFrame *frame) = 0;

    /**
     * @brief 获取编解码器上下文
     * @return AVCodecContext指针，包含编解码器的配置信息
     */
    AVCodecContext *GetCodecContext() {
        return m_AVCodecContext;
    }

    /**
     * @brief 当前帧的显示时间戳（毫秒），在 OnFrameAvailable 中可用
     */
    int64_t GetCurrentTimeStamp() {
        return m_CurTimeStamp;
    }

private:
    /**
     * @brief 初始化FFmpeg解码器
     * 打开媒体文件、查找流信息、初始化解码器上下文
     * @return 0表示成功，负数表示失败
     */
    int InitFFDecoder();

    /**
     * @brief 释放解码器
     * 关闭解码器、释放上下文等资源
     */
    void UnInitDecoder();

    /**
     * @brief 启动解码线程
     */
    void StartDecodingThread();

    /**
     * @brief 音视频解码循环
     * 处理暂停、停止、seek等状态变化
     */
    void DecodingLoop();

    /**
     * @brief 更新显示时间戳
     * 根据当前解码帧的 best_effort_timestamp 更新播放时间戳
     */
    void UpdateTimeStamp();

    /**
     * @brief 音视频同步
     * @return true 渲染该帧；false 该帧已经严重落后，丢弃
     */
    bool AVSync();

    /**
     * @brief 执行一次待处理的 seek（如果有）
     */
    void DoSeekIfNeeded();

    /**
     * @brief 把解码器里已经可以输出的帧全部取出并处理
     * @return 取出的帧数
     */
    int ReceiveFrames();

    /**
     * @brief 读取并解码一个packet，直到至少输出一帧
     * @return 0 成功；-1 文件读完且解码器已排空
     */
    int DecodeOnePacket();

    /**
     * @brief 解码线程函数（静态函数）
     * @param decoder 解码器实例指针
     */
    static void DoAVDecoding(DecoderBase *decoder);

private:
    // FFmpeg 核心组件
    AVFormatContext *m_AVFormatContext = nullptr;      // 封装格式上下文，用于读取媒体文件
    AVCodecContext  *m_AVCodecContext = nullptr;       // 解码器上下文，包含解码参数
    AVCodec         *m_AVCodec = nullptr;              // 解码器实例
    AVPacket        *m_Packet = nullptr;               // 编码的数据包，从媒体文件读取的压缩数据
    AVFrame         *m_Frame = nullptr;                // 解码后的帧数据

    // 媒体信息
    AVMediaType      m_MediaType = AVMEDIA_TYPE_UNKNOWN;  // 数据流的类型（音频/视频）
    char             m_Url[MAX_PATH] = {0};               // 媒体文件地址
    int              m_StreamIndex = -1;                  // 当前处理的数据流索引

    // 时间相关（统一用 int64_t：armeabi-v7a 上 long 只有 32 位）
    std::atomic<int64_t> m_CurTimeStamp{0};            // 当前帧显示时间戳（毫秒）
    int64_t          m_StartTimeStamp = -1;            // 系统时钟参考下的播放起点（毫秒）
    int64_t          m_PauseTimeStamp = -1;            // 进入暂停时的系统时间（毫秒）
    int64_t          m_LastProgressTime = -1;          // 上次上报进度的时间戳，用于节流
    int64_t          m_Duration = 0;                   // 总时长（毫秒）
    bool             m_InputEOF = false;               // 已读到文件末尾并送入了 flush 包

    // 线程同步
    mutex               m_Mutex;                       // 互斥锁，保护状态切换
    condition_variable  m_Cond;                        // 条件变量，用于暂停/恢复/退出唤醒
    thread             *m_Thread = nullptr;            // 解码线程指针

    // Seek 相关：-1 表示没有待处理的 seek
    std::atomic<float>  m_SeekPosition{-1.0f};         // 目标 seek 位置（秒）
    bool                m_ResetClockOnNextFrame = false;// seek 成功后，下一帧需要重建系统时钟参考

    // 状态和回调
    std::atomic<int>    m_DecoderState{STATE_UNKNOWN};    // 解码器当前状态
    void*               m_AVDecoderContext = nullptr;     // 音视频同步回调的上下文指针
    AVSyncCallback      m_AVSyncCallback = nullptr;       // 获取主时钟的回调
};


#endif //LEARNFFMPEG_DECODERBASE_H
