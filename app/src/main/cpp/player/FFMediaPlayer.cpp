/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#include "FFMediaPlayer.h"
#include <string>
#include <render/video/NativeRender.h>
#include <render/video/VideoGLRender.h>
#include <render/video/VRGLRender.h>

// 等待 Java 回调返回的上限：超过后放弃等待（见 JniEventDispatcher::Shutdown），保证 release 不会卡死
static const int DISPATCHER_SHUTDOWN_TIMEOUT_MS = 300;

void FFMediaPlayer::Init(JNIEnv *jniEnv, jobject obj, char *url, int videoRenderType, jobject surface) {
    LOGCATI("FFMediaPlayer::Init url=%s, renderType=%d", url, videoRenderType);
    jniEnv->GetJavaVM(&m_JavaVM);
    m_Dispatcher = JniEventDispatcher::Create(jniEnv, obj);

    switch (videoRenderType) {
        case VIDEO_RENDER_OPENGL:
            m_VideoRender = VideoGLRender::GetInstance();
            break;
        case VIDEO_RENDER_ANWINDOW:
            if (surface != nullptr) {
                m_OwnedVideoRender.reset(new NativeRender(jniEnv, surface));
                m_VideoRender = m_OwnedVideoRender.get();
            }
            break;
        case VIDEO_RENDER_3D_VR:
            m_VideoRender = VRGLRender::GetInstance();
            break;
        default:
            LOGCATE("FFMediaPlayer::Init unknown render type %d", videoRenderType);
            break;
    }

    m_Player.reset(new Player(this, m_VideoRender));
    m_Player->SetDataSource(url != nullptr ? url : "");
    m_Player->PrepareAsync();
}

void FFMediaPlayer::UnInit() {
    if (!m_Player && !m_Dispatcher) return;
    int64_t startMs = GetSysCurrentTime();
    // 顺序：先停播放器（之后不会再产生事件），再停分发线程，最后释放渲染器（播放器线程已不再使用它）
    if (m_Player) {
        m_Player->Release();
        m_Player.reset();
    }
    if (m_Dispatcher) {
        m_Dispatcher->Shutdown(DISPATCHER_SHUTDOWN_TIMEOUT_MS);
        m_Dispatcher.reset();
    }
    m_OwnedVideoRender.reset();
    m_VideoRender = nullptr;
    LOGCATI("FFMediaPlayer::UnInit done in %lldms", (long long) (GetSysCurrentTime() - startMs));
}

void FFMediaPlayer::Play() {
    if (m_Player) m_Player->Start();
}

void FFMediaPlayer::Pause() {
    if (m_Player) m_Player->Pause();
}

void FFMediaPlayer::Stop() {
    if (m_Player) m_Player->Stop();
}

void FFMediaPlayer::SeekToPosition(float position) {
    if (m_Player) m_Player->SeekTo(position);
}

long FFMediaPlayer::GetMediaParams(int paramType) {
    if (!m_Player) return 0;
    switch (paramType) {
        case MEDIA_PARAM_VIDEO_WIDTH:
            return m_Player->VideoWidth();
        case MEDIA_PARAM_VIDEO_HEIGHT:
            return m_Player->VideoHeight();
        case MEDIA_PARAM_VIDEO_DURATION:
            // 与旧版一致：单位为秒（Java 层用作进度条最大值）
            return static_cast<long>(m_Player->DurationMs() / 1000);
        default:
            return 0;
    }
}

void FFMediaPlayer::SetMediaParams(int paramType, jobject obj) {
    if (!m_Player) return;
    switch (paramType) {
        case MEDIA_PARAM_RECORD_PATH: {
            bool isAttach = false;
            JNIEnv *env = GetJNIEnv(&isAttach);
            if (env == nullptr) break;
            if (obj == nullptr) {
                m_Player->SetRecordPath("");
                break;
            }
            jstring jPath = static_cast<jstring>(obj);
            const char *path = env->GetStringUTFChars(jPath, nullptr);
            if (path != nullptr) {
                m_Player->SetRecordPath(path);
                env->ReleaseStringUTFChars(jPath, path);
            }
            break;
        }
        default:
            break;
    }
}

JNIEnv *FFMediaPlayer::GetJNIEnv(bool *isAttach) {
    *isAttach = false;
    if (m_JavaVM == nullptr) return nullptr;
    JNIEnv *env = nullptr;
    if (m_JavaVM->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) return nullptr;
    return env;
}

jobject FFMediaPlayer::GetJavaObj() {
    return nullptr;
}

JavaVM *FFMediaPlayer::GetJavaVM() {
    return m_JavaVM;
}

void FFMediaPlayer::OnPlayerEvent(int msg, float value) {
    // Player 的线程在 Release 中被回收之后才会 reset m_Dispatcher，这里不会读到被释放的对象
    if (m_Dispatcher) m_Dispatcher->Post(msg, value);
}
