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
#include <pthread.h>

/**
 * 解码线程是 native 线程，回调 Java 前必须 AttachCurrentThread。
 * 原来每发一条消息（每个音频帧、每个视频帧）都 Attach + Detach 一次，开销很大。
 * 现在每个线程只 Attach 一次，并借助 pthread_key 的析构函数在线程退出时自动 Detach。
 */
static pthread_key_t s_JNIThreadKey;
static pthread_once_t s_JNIThreadKeyOnce = PTHREAD_ONCE_INIT;

static void DetachThreadOnExit(void *javaVM) {
    if(javaVM != nullptr)
        static_cast<JavaVM *>(javaVM)->DetachCurrentThread();
}

static void CreateJNIThreadKey() {
    pthread_key_create(&s_JNIThreadKey, DetachThreadOnExit);
}

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
    // 方法 ID 在类的生命周期内不变，查一次缓存起来；GetObjectClass 返回的局部引用及时释放
    jclass clazz = jniEnv->GetObjectClass(obj);
    m_EventCallbackMid = jniEnv->GetMethodID(clazz, JAVA_PLAYER_EVENT_CALLBACK_API_NAME, "(IF)V");
    jniEnv->DeleteLocalRef(clazz);

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

    // 音视频同步：以音频时钟为主时钟，视频解码器向音频解码器查询当前播放位置
    m_VideoDecoder->SetAVSyncCallback(m_AudioDecoder, AudioDecoder::GetAudioClock);
}

/**
 * @brief 释放播放器资源
 * 按顺序释放解码器、渲染器和JNI相关资源
 */
void FFMediaPlayer::UnInit() {
    LOGCATE("FFMediaPlayer::UnInit");
    // 释放顺序很重要：视频解码线程会通过同步回调访问音频解码器，所以先停掉视频
    // 释放视频解码器（析构时会停止并 join 解码线程）
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

    // 不再删除 OpenGL 渲染器单例：GL 线程可能仍在调用它的 OnDrawFrame，删除会导致释放后使用。
    // 它在解码结束时（VideoDecoder::OnDecoderDone -> UnInit）已经清理了画面数据

    // 删除Java对象的全局引用
    bool isAttach = false;
    JNIEnv *env = GetJNIEnv(&isAttach);
    if(env != nullptr && m_JavaObj != nullptr) {
        env->DeleteGlobalRef(m_JavaObj);
        m_JavaObj = nullptr;
    }

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
    // 线程退出时会自动分离，调用方不需要 Detach
    *isAttach = false;
    // 尝试获取当前线程的JNI环境
    status = m_JavaVM->GetEnv((void **)&env, JNI_VERSION_1_4);
    if (status != JNI_OK) {
        // 当前线程未附加到JVM：附加一次，并登记线程退出时自动分离
        status = m_JavaVM->AttachCurrentThread(&env, nullptr);
        if (status != JNI_OK) {
            LOGCATE("FFMediaPlayer::GetJNIEnv failed to attach current thread");
            return nullptr;
        }
        pthread_once(&s_JNIThreadKeyOnce, CreateJNIThreadKey);
        pthread_setspecific(s_JNIThreadKey, m_JavaVM);
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
        // 获取JNI环境：解码线程第一次调用时附加到JVM，之后直接复用
        JNIEnv *env = player->GetJNIEnv(&isAttach);
        LOGCATV("FFMediaPlayer::PostMessage env=%p", env);
        if(env == nullptr || player->m_JavaObj == nullptr || player->m_EventCallbackMid == nullptr)
            return;
        // 调用Java层的回调方法（方法签名：void playerEventCallback(int msgType, float msgCode)）
        env->CallVoidMethod(player->m_JavaObj, player->m_EventCallbackMid, msgType, msgCode);
        // 线程会长期附加在 JVM 上，Java 层抛出的异常必须清掉，否则后续任何 JNI 调用都会出错
        if(env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    }
}

/**
 * @brief 设置媒体参数
 * @param paramType 参数类型
 * @param obj       参数值
 */
void FFMediaPlayer::SetMediaParams(int paramType, jobject obj) {
    LOGCATE("FFMediaPlayer::SetMediaParams paramType=%d", paramType);
    switch(paramType)
    {
        case MEDIA_PARAM_RECORD_PATH:
        {
            if(m_VideoDecoder == nullptr || obj == nullptr) break;
            bool isAttach = false;
            JNIEnv *env = GetJNIEnv(&isAttach);
            if(env == nullptr) break;
            jstring jPath = static_cast<jstring>(obj);
            const char *path = env->GetStringUTFChars(jPath, nullptr);
            m_VideoDecoder->SetRecordPath(path);
            env->ReleaseStringUTFChars(jPath, path);
        }
            break;
        default:
            break;
    }
}
