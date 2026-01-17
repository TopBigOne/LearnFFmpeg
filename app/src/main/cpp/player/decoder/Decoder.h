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

#ifndef LEARNFFMPEG_DECODER_H
#define LEARNFFMPEG_DECODER_H

/**
 * @brief 消息回调函数类型定义
 * @param void* 上下文指针
 * @param int 消息类型
 * @param float 消息代码/数据
 */
typedef void (*MessageCallback)(void*, int, float);

/**
 * @brief 音视频同步回调函数类型定义
 * @param void* 上下文指针
 * @return long 同步延迟时间
 */
typedef long (*AVSyncCallback)(void*);

/**
 * @brief 解码器抽象接口类
 *
 * 定义解码器的基本接口，所有具体解码器都需要实现这些纯虚函数
 * 包括播放控制、进度管理、回调设置等功能
 */
class Decoder {
public:
    /**
     * @brief 开始解码/播放
     */
    virtual void Start() = 0;

    /**
     * @brief 暂停解码/播放
     */
    virtual void Pause() = 0;

    /**
     * @brief 停止解码/播放
     */
    virtual void Stop() = 0;

    /**
     * @brief 获取媒体总时长
     * @return 时长（秒）
     */
    virtual float GetDuration() = 0;

    /**
     * @brief 跳转到指定位置
     * @param position 目标位置（0.0-1.0之间的浮点数）
     */
    virtual void SeekToPosition(float position) = 0;

    /**
     * @brief 获取当前播放位置
     * @return 当前位置（秒）
     */
    virtual float GetCurrentPosition() = 0;

    /**
     * @brief 设置消息回调函数
     * @param context 上下文指针
     * @param callback 回调函数指针
     */
    virtual void SetMessageCallback(void* context, MessageCallback callback) = 0;
};

#endif //LEARNFFMPEG_DECODER_H
