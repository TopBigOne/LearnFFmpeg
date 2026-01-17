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
#include "Decoder.h"

#define MAX_PATH   2048                        // 最大路径长度
#define DELAY_THRESHOLD 100                   // 延迟阈值（100ms）

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
     * 启动解码线程，开始解码并播放媒体数据
     */
    virtual void Start();

    /**
     * @brief 暂停播放
     * 暂停解码线程，保持当前位置
     */
    virtual void Pause();

    /**
     * @brief 停止播放
     * 停止解码线程并释放资源
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
     * @param position 目标播放位置（秒）
     */
    virtual void SeekToPosition(float position);

    /**
     * @brief 获取当前播放位置
     * 用于更新进度条和音视频同步
     * @return 当前播放位置（秒）
     */
    virtual float GetCurrentPosition();

    /**
     * @brief 清空缓存
     * 派生类可重写此方法以实现特定的缓存清理逻辑
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
     * @brief 设置音视频同步回调
     * @param context 上下文
     * @param callback 同步回调函数
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
     * 释放FFmpeg相关的所有资源，包括上下文、packet、frame等
     */
    virtual void UnInit();

    /**
     * @brief 解码器准备就绪回调（纯虚函数）
     * 当解码器初始化成功并准备好开始解码时调用
     * 派生类必须实现此方法
     */
    virtual void OnDecoderReady() = 0;

    /**
     * @brief 解码完成回调（纯虚函数）
     * 当所有数据解码完成时调用
     * 派生类必须实现此方法
     */
    virtual void OnDecoderDone() = 0;

    /**
     * @brief 解码数据的回调（纯虚函数）
     * @param frame 解码后的帧数据（音频帧或视频帧）
     * 派生类必须实现此方法来处理解码后的帧
     */
    virtual void OnFrameAvailable(AVFrame *frame) = 0;

    /**
     * @brief 获取编解码器上下文
     * @return AVCodecContext指针，包含编解码器的配置信息
     */
    AVCodecContext *GetCodecContext() {
        return m_AVCodecContext;
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
     * 创建并启动后台解码线程
     */
    void StartDecodingThread();

    /**
     * @brief 音视频解码循环
     * 在解码线程中持续读取packet、解码并处理帧数据
     * 处理暂停、停止、seek等状态变化
     */
    void DecodingLoop();

    /**
     * @brief 更新显示时间戳
     * 根据当前解码帧的PTS更新播放时间戳
     */
    void UpdateTimeStamp();

    /**
     * @brief 音视频同步
     * 通过回调获取音视频同步时间差，计算需要延迟的时间
     * @return 同步延迟时间（毫秒），用于控制播放速度
     */
    long AVSync();

    /**
     * @brief 解码一个packet编码数据
     * 发送packet到解码器并接收解码后的frame
     * @return 解码结果，0表示成功，负数表示错误
     */
    int DecodeOnePacket();

    /**
     * @brief 解码线程函数（静态函数）
     * @param decoder 解码器实例指针
     * 线程入口函数，调用DecodingLoop执行实际解码工作
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

    // 时间相关
    long             m_CurTimeStamp = 0;               // 当前播放时间戳（毫秒）
    long             m_StartTimeStamp = -1;            // 播放的起始时间戳（毫秒）
    long             m_Duration = 0;                   // 总时长（毫秒）

    // 线程同步
    mutex               m_Mutex;                       // 互斥锁，保护共享数据
    condition_variable  m_Cond;                        // 条件变量，用于线程间通信
    thread             *m_Thread = nullptr;            // 解码线程指针

    // Seek 相关
    volatile float      m_SeekPosition = 0;            // 目标 seek 位置（秒）
    volatile bool       m_SeekSuccess = false;         // seek 操作是否成功

    // 状态和回调
    volatile int        m_DecoderState = STATE_UNKNOWN;   // 解码器当前状态
    void*               m_AVDecoderContext = nullptr;     // 音视频同步回调的上下文指针
    AVSyncCallback      m_AVSyncCallback = nullptr;       // 音视频同步回调函数
};


#endif //LEARNFFMPEG_DECODERBASE_H
