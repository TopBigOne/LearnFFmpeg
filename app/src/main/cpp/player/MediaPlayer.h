/**
 *
 * Created by 公众号：字节流动 on 2021/12/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#ifndef LEARNFFMPEG_MEDIAPLAYER_H
#define LEARNFFMPEG_MEDIAPLAYER_H

#include <jni.h>
#include <decoder/VideoDecoder.h>
#include <decoder/AudioDecoder.h>
#include <render/audio/AudioRender.h>

// Java层播放器事件回调API名称
#define JAVA_PLAYER_EVENT_CALLBACK_API_NAME "playerEventCallback"

// 媒体参数类型定义
#define MEDIA_PARAM_VIDEO_WIDTH         0x0001    // 视频宽度
#define MEDIA_PARAM_VIDEO_HEIGHT        0x0002    // 视频高度
#define MEDIA_PARAM_VIDEO_DURATION      0x0003    // 视频时长

#define MEDIA_PARAM_ASSET_MANAGER       0x0020    // 资源管理器


/**
 * @brief 媒体播放器抽象基类
 *
 * 定义媒体播放器的通用接口，包括播放控制、参数获取等功能
 * 子类需要实现具体的播放逻辑
 */
class MediaPlayer {
public:
    MediaPlayer(){};
    virtual ~MediaPlayer(){};

    /**
     * @brief 初始化播放器（纯虚函数）
     * @param jniEnv JNI环境指针
     * @param obj Java对象
     * @param url 媒体文件URL
     * @param renderType 渲染类型
     * @param surface Android Surface对象
     */
    virtual void Init(JNIEnv *jniEnv, jobject obj, char *url, int renderType, jobject surface) = 0;

    /**
     * @brief 释放播放器资源（纯虚函数）
     */
    virtual void UnInit() = 0;

    /**
     * @brief 开始播放（纯虚函数）
     */
    virtual void Play() = 0;

    /**
     * @brief 暂停播放（纯虚函数）
     */
    virtual void Pause() = 0;

    /**
     * @brief 停止播放（纯虚函数）
     */
    virtual void Stop() = 0;

    /**
     * @brief 跳转到指定位置（纯虚函数）
     * @param position 目标位置（0.0-1.0）
     */
    virtual void SeekToPosition(float position) = 0;

    /**
     * @brief 获取媒体参数（纯虚函数）
     * @param paramType 参数类型
     * @return 参数值
     */
    virtual long GetMediaParams(int paramType) = 0;

    /**
     * @brief 设置媒体参数（虚函数，默认空实现）
     * @param paramType 参数类型
     * @param obj 参数对象
     */
    virtual void SetMediaParams(int paramType, jobject obj){}

    /**
     * @brief 获取JNI环境（纯虚函数）
     * @param isAttach 是否需要附加到当前线程
     * @return JNI环境指针
     */
    virtual JNIEnv *GetJNIEnv(bool *isAttach) = 0;

    /**
     * @brief 获取Java对象（纯虚函数）
     * @return Java对象引用
     */
    virtual jobject GetJavaObj() = 0;

    /**
     * @brief 获取Java虚拟机（纯虚函数）
     * @return JavaVM指针
     */
    virtual JavaVM *GetJavaVM() = 0;

    JavaVM *m_JavaVM = nullptr;                    // Java虚拟机指针
    jobject m_JavaObj = nullptr;                   // Java对象引用
};

#endif //LEARNFFMPEG_MEDIAPLAYER_H
