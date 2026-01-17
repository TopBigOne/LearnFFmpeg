/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#ifndef LEARNFFMPEG_FFMEDIAPLAYER_H
#define LEARNFFMPEG_FFMEDIAPLAYER_H

#include <MediaPlayer.h>

/**
 * @brief FFmpeg媒体播放器类
 *
 * 继承自MediaPlayer基类，实现基于FFmpeg的音视频播放功能
 * 支持多种媒体格式的解码和播放
 */
class FFMediaPlayer : public MediaPlayer {
public:
    FFMediaPlayer(){};
    virtual ~FFMediaPlayer(){};

    /**
     * @brief 初始化播放器
     * @param jniEnv JNI环境指针
     * @param obj Java对象
     * @param url 媒体文件URL
     * @param renderType 渲染类型
     * @param surface Android Surface对象
     */
    virtual void Init(JNIEnv *jniEnv, jobject obj, char *url, int renderType, jobject surface);

    /**
     * @brief 释放播放器资源
     */
    virtual void UnInit();

    /**
     * @brief 开始播放
     */
    virtual void Play();

    /**
     * @brief 暂停播放
     */
    virtual void Pause();

    /**
     * @brief 停止播放
     */
    virtual void Stop();

    /**
     * @brief 跳转到指定位置
     * @param position 目标位置（0.0-1.0）
     */
    virtual void SeekToPosition(float position);

    /**
     * @brief 获取媒体参数
     * @param paramType 参数类型
     * @return 参数值
     */
    virtual long GetMediaParams(int paramType);

private:
    /**
     * @brief 获取JNI环境
     * @param isAttach 是否需要附加到当前线程
     * @return JNI环境指针
     */
    virtual JNIEnv *GetJNIEnv(bool *isAttach);

    /**
     * @brief 获取Java对象
     * @return Java对象引用
     */
    virtual jobject GetJavaObj();

    /**
     * @brief 获取Java虚拟机
     * @return JavaVM指针
     */
    virtual JavaVM *GetJavaVM();

    /**
     * @brief 向Java层发送消息（静态函数）
     * @param context 上下文
     * @param msgType 消息类型
     * @param msgCode 消息代码
     */
    static void PostMessage(void *context, int msgType, float msgCode);

private:
    VideoDecoder *m_VideoDecoder = nullptr;        // 视频解码器
    AudioDecoder *m_AudioDecoder = nullptr;        // 音频解码器

    VideoRender *m_VideoRender = nullptr;          // 视频渲染器
    AudioRender *m_AudioRender = nullptr;          // 音频渲染器
};


#endif //LEARNFFMPEG_FFMEDIAPLAYER_H
