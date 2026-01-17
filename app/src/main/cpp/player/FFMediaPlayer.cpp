/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#include <render/video/NativeRender.h>
#include <render/audio/OpenSLRender.h>
#include <render/video/VideoGLRender.h>
#include <render/video/VRGLRender.h>
#include "FFMediaPlayer.h"

/**
 * @brief 初始化FFmpeg播放器
 * @param jniEnv JNI环境指针
 * @param obj Java层播放器对象
 * @param url 媒体文件路径
 * @param videoRenderType 视频渲染类型（OpenGL/NativeWindow/VR）
 * @param surface Android Surface对象
 */
void FFMediaPlayer::Init(JNIEnv *jniEnv, jobject obj, char *url, int videoRenderType, jobject surface) {
    // 获取Java虚拟机指针，用于后续跨线程JNI调用
    jniEnv->GetJavaVM(&m_JavaVM);
    // 创建Java对象的全局引用，避免被GC回收
    m_JavaObj = jniEnv->NewGlobalRef(obj);

    // 创建视频解码器和音频解码器
    m_VideoDecoder = new VideoDecoder(url);
    m_AudioDecoder = new AudioDecoder(url);

    // 根据渲染类型设置视频渲染器
    if(videoRenderType == VIDEO_RENDER_OPENGL) {
        // OpenGL渲染方式（单例模式）
        m_VideoDecoder->SetVideoRender(VideoGLRender::GetInstance());
    } else if (videoRenderType == VIDEO_RENDER_ANWINDOW) {
        // NativeWindow渲染方式
        m_VideoRender = new NativeRender(jniEnv, surface);
        m_VideoDecoder->SetVideoRender(m_VideoRender);
    } else if (videoRenderType == VIDEO_RENDER_3D_VR) {
        // VR 3D渲染方式（单例模式）
        m_VideoDecoder->SetVideoRender(VRGLRender::GetInstance());
    }

    // 创建音频渲染器（使用OpenSL ES）
    m_AudioRender = new OpenSLRender();
    m_AudioDecoder->SetAudioRender(m_AudioRender);

    // 设置解码器的消息回调，用于向Java层发送事件
    m_VideoDecoder->SetMessageCallback(this, PostMessage);
    m_AudioDecoder->SetMessageCallback(this, PostMessage);
}

/**
 * @brief 释放播放器资源
 * 按顺序释放解码器、渲染器和JNI相关资源
 */
void FFMediaPlayer::UnInit() {
    LOGCATE("FFMediaPlayer::UnInit");
    // 释放视频解码器
    if(m_VideoDecoder) {
        delete m_VideoDecoder;
        m_VideoDecoder = nullptr;
    }

    // 释放视频渲染器
    if(m_VideoRender) {
        delete m_VideoRender;
        m_VideoRender = nullptr;
    }

    // 释放音频解码器
    if(m_AudioDecoder) {
        delete m_AudioDecoder;
        m_AudioDecoder = nullptr;
    }

    // 释放音频渲染器
    if(m_AudioRender) {
        delete m_AudioRender;
        m_AudioRender = nullptr;
    }

    // 释放OpenGL渲染器单例
    VideoGLRender::ReleaseInstance();

    // 删除Java对象的全局引用，并在需要时从线程分离
    bool isAttach = false;
    GetJNIEnv(&isAttach)->DeleteGlobalRef(m_JavaObj);
    if(isAttach)
        GetJavaVM()->DetachCurrentThread();

}

/**
 * @brief 开始播放
 * 启动视频解码器和音频解码器的解码线程
 */
void FFMediaPlayer::Play() {
    LOGCATE("FFMediaPlayer::Play");
    if(m_VideoDecoder)
        m_VideoDecoder->Start();

    if(m_AudioDecoder)
        m_AudioDecoder->Start();
}

/**
 * @brief 暂停播放
 * 暂停视频和音频解码器，线程继续运行但不解码
 */
void FFMediaPlayer::Pause() {
    LOGCATE("FFMediaPlayer::Pause");
    if(m_VideoDecoder)
        m_VideoDecoder->Pause();

    if(m_AudioDecoder)
        m_AudioDecoder->Pause();

}

/**
 * @brief 停止播放
 * 停止视频和音频解码器，解码线程将退出
 */
void FFMediaPlayer::Stop() {
    LOGCATE("FFMediaPlayer::Stop");
    if(m_VideoDecoder)
        m_VideoDecoder->Stop();

    if(m_AudioDecoder)
        m_AudioDecoder->Stop();
}

/**
 * @brief 跳转到指定位置
 * @param position 跳转位置（0.0-1.0之间的浮点数，表示总时长的百分比）
 */
void FFMediaPlayer::SeekToPosition(float position) {
    LOGCATE("FFMediaPlayer::SeekToPosition position=%f", position);
    if(m_VideoDecoder)
        m_VideoDecoder->SeekToPosition(position);

    if(m_AudioDecoder)
        m_AudioDecoder->SeekToPosition(position);

}

/**
 * @brief 获取媒体参数
 * @param paramType 参数类型（视频宽度/高度/时长等）
 * @return 参数值
 */
long FFMediaPlayer::GetMediaParams(int paramType) {
    LOGCATE("FFMediaPlayer::GetMediaParams paramType=%d", paramType);
    long value = 0;
    switch(paramType)
    {
        case MEDIA_PARAM_VIDEO_WIDTH:
            value = m_VideoDecoder != nullptr ? m_VideoDecoder->GetVideoWidth() : 0;
            break;
        case MEDIA_PARAM_VIDEO_HEIGHT:
            value = m_VideoDecoder != nullptr ? m_VideoDecoder->GetVideoHeight() : 0;
            break;
        case MEDIA_PARAM_VIDEO_DURATION:
            value = m_VideoDecoder != nullptr ? m_VideoDecoder->GetDuration() : 0;
            break;
    }
    return value;
}

/**
 * @brief 获取JNI环境指针
 * @param isAttach 输出参数，指示当前线程是否被附加到JVM
 * @return JNI环境指针
 *
 * 在非主线程中调用JNI时，需要先将线程附加到JVM
 * 如果线程被附加，使用完后需要调用DetachCurrentThread
 */
JNIEnv *FFMediaPlayer::GetJNIEnv(bool *isAttach) {
    JNIEnv *env;
    int status;
    if (nullptr == m_JavaVM) {
        LOGCATE("FFMediaPlayer::GetJNIEnv m_JavaVM == nullptr");
        return nullptr;
    }
    *isAttach = false;
    // 尝试获取当前线程的JNI环境
    status = m_JavaVM->GetEnv((void **)&env, JNI_VERSION_1_4);
    if (status != JNI_OK) {
        // 如果当前线程未附加到JVM，则附加它
        status = m_JavaVM->AttachCurrentThread(&env, nullptr);
        if (status != JNI_OK) {
            LOGCATE("FFMediaPlayer::GetJNIEnv failed to attach current thread");
            return nullptr;
        }
        *isAttach = true;
    }
    return env;
}

/**
 * @brief 获取Java对象引用
 * @return Java对象的全局引用
 */
jobject FFMediaPlayer::GetJavaObj() {
    return m_JavaObj;
}

/**
 * @brief 获取Java虚拟机指针
 * @return JavaVM指针
 */
JavaVM *FFMediaPlayer::GetJavaVM() {
    return m_JavaVM;
}

/**
 * @brief 向Java层发送消息的回调函数（静态函数）
 * @param context 上下文对象（FFMediaPlayer实例指针）
 * @param msgType 消息类型
 * @param msgCode 消息代码
 *
 * 该函数在解码器线程中被调用，通过JNI回调Java层的playerEventCallback方法
 */
void FFMediaPlayer::PostMessage(void *context, int msgType, float msgCode) {
    if(context != nullptr)
    {
        FFMediaPlayer *player = static_cast<FFMediaPlayer *>(context);
        bool isAttach = false;
        // 获取JNI环境，如果是新线程则附加到JVM
        JNIEnv *env = player->GetJNIEnv(&isAttach);
        LOGCATE("FFMediaPlayer::PostMessage env=%p", env);
        if(env == nullptr)
            return;
        jobject javaObj = player->GetJavaObj();
        // 获取Java层的回调方法ID（方法签名：void playerEventCallback(int msgType, float msgCode)）
        jmethodID mid = env->GetMethodID(env->GetObjectClass(javaObj), JAVA_PLAYER_EVENT_CALLBACK_API_NAME, "(IF)V");
        // 调用Java层的回调方法
        env->CallVoidMethod(javaObj, mid, msgType, msgCode);
        // 如果线程是新附加的，使用完后需要从JVM分离
        if(isAttach)
            player->GetJavaVM()->DetachCurrentThread();

    }
}


