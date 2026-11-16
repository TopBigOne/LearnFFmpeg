/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#include <cstdio>
#include <cstring>
#include <PlayerWrapper.h>
#include <render/video/VideoGLRender.h>
#include <render/video/VRGLRender.h>
#include <render/audio/AudioGLRender.h>
#include <MediaRecorderContext.h>
#include "util/LogUtil.h"
#include "jni.h"
#include "ASanTestCase.h"

extern "C" {
#include <libavcodec/version.h>
#include <libavcodec/avcodec.h>
#include <libavformat/version.h>
#include <libavutil/version.h>
#include <libavfilter/version.h>
#include <libswresample/version.h>
#include <libswscale/version.h>
};

#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     com_byteflow_learnffmpeg_media_FFMediaPlayer
 * Method:    native_GetFFmpegVersion
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1GetFFmpegVersion
        (JNIEnv *env, jclass cls)
{
    char strBuffer[1024 * 4] = {0};
    strcat(strBuffer, "libavcodec : ");
    strcat(strBuffer, AV_STRINGIFY(LIBAVCODEC_VERSION));
    strcat(strBuffer, "\nlibavformat : ");
    strcat(strBuffer, AV_STRINGIFY(LIBAVFORMAT_VERSION));
    strcat(strBuffer, "\nlibavutil : ");
    strcat(strBuffer, AV_STRINGIFY(LIBAVUTIL_VERSION));
    strcat(strBuffer, "\nlibavfilter : ");
    strcat(strBuffer, AV_STRINGIFY(LIBAVFILTER_VERSION));
    strcat(strBuffer, "\nlibswresample : ");
    strcat(strBuffer, AV_STRINGIFY(LIBSWRESAMPLE_VERSION));
    strcat(strBuffer, "\nlibswscale : ");
    strcat(strBuffer, AV_STRINGIFY(LIBSWSCALE_VERSION));
    strcat(strBuffer, "\navcodec_configure : \n");
    strcat(strBuffer, avcodec_configuration());
    strcat(strBuffer, "\navcodec_license : ");
    strcat(strBuffer, avcodec_license());
    LOGCATE("GetFFmpegVersion\n%s", strBuffer);

    //ASanTestCase::MainTest();

    return env->NewStringUTF(strBuffer);
}

/*
 * Class:     com_byteflow_learnffmpeg_media_FFMediaPlayer
 * Method:    native_Init
 * Signature: (JLjava/lang/String;Ljava/lang/Object;)J
 */
JNIEXPORT jlong JNICALL Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1Init
        (JNIEnv *env, jobject obj, jstring jurl, int playerType, jint renderType, jobject surface)
{
    const char* url = env->GetStringUTFChars(jurl, nullptr);
    PlayerWrapper *player = new PlayerWrapper();
    player->Init(env, obj, const_cast<char *>(url), playerType, renderType, surface);
    env->ReleaseStringUTFChars(jurl, url);
    return reinterpret_cast<jlong>(player);
}

/*
 * Class:     com_byteflow_learnffmpeg_media_FFMediaPlayer
 * Method:    native_Play
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1Play
(JNIEnv *env, jobject obj, jlong player_handle)
{
    if(player_handle != 0)
    {
        PlayerWrapper *pPlayerWrapper = reinterpret_cast<PlayerWrapper *>(player_handle);
        pPlayerWrapper->Play();
    }

}

JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1SeekToPosition(JNIEnv *env, jobject thiz,
                                                                      jlong player_handle, jfloat position) {
    if(player_handle != 0)
    {
        PlayerWrapper *ffMediaPlayer = reinterpret_cast<PlayerWrapper *>(player_handle);
        ffMediaPlayer->SeekToPosition(position);
    }
}

JNIEXPORT jlong JNICALL
Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1GetMediaParams(JNIEnv *env, jobject thiz,
                                                                         jlong player_handle,
                                                                         jint param_type) {
    long value = 0;
    if(player_handle != 0)
    {
        PlayerWrapper *ffMediaPlayer = reinterpret_cast<PlayerWrapper *>(player_handle);
        value = ffMediaPlayer->GetMediaParams(param_type);
    }
    return value;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1SetMediaParams(JNIEnv *env, jobject thiz,
                                                                         jlong player_handle,
                                                                         jint param_type,
                                                                         jobject param) {
    if(player_handle != 0)
    {
        PlayerWrapper *ffMediaPlayer = reinterpret_cast<PlayerWrapper *>(player_handle);
        ffMediaPlayer->SetMediaParams(param_type, param);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1SetPlaybackSpeed(JNIEnv *env, jobject thiz,
                                                                           jlong player_handle,
                                                                           jfloat speed) {
    if (player_handle == 0) return JNI_FALSE;
    PlayerWrapper *ffMediaPlayer = reinterpret_cast<PlayerWrapper *>(player_handle);
    return ffMediaPlayer->SetPlaybackSpeed(speed) ? JNI_TRUE : JNI_FALSE;
}

/*
 * Class:     com_byteflow_learnffmpeg_media_FFMediaPlayer
 * Method:    native_Pause
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1Pause
(JNIEnv *env, jobject obj, jlong player_handle)
{
    if(player_handle != 0)
    {
        PlayerWrapper *ffMediaPlayer = reinterpret_cast<PlayerWrapper *>(player_handle);
        ffMediaPlayer->Pause();
    }
}

/*
 * Class:     com_byteflow_learnffmpeg_media_FFMediaPlayer
 * Method:    native_Stop
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1Stop
(JNIEnv *env, jobject obj, jlong player_handle)
{
    if(player_handle != 0)
    {
        PlayerWrapper *ffMediaPlayer = reinterpret_cast<PlayerWrapper *>(player_handle);
        ffMediaPlayer->Stop();
    }
}

/*
 * Class:     com_byteflow_learnffmpeg_media_FFMediaPlayer
 * Method:    native_UnInit
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1UnInit
(JNIEnv *env, jobject obj, jlong player_handle)
{
    if(player_handle != 0)
    {
        PlayerWrapper *ffMediaPlayer = reinterpret_cast<PlayerWrapper *>(player_handle);
        ffMediaPlayer->UnInit();
        delete ffMediaPlayer;
    }
}

JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1OnSurfaceCreated(JNIEnv *env,
                                                                           jclass clazz,
                                                                           jint render_type) {
    switch (render_type)
    {
        case VIDEO_GL_RENDER:
            VideoGLRender::GetInstance()->OnSurfaceCreated();
            break;
        case AUDIO_GL_RENDER:
            AudioGLRender::GetInstance()->OnSurfaceCreated();
            break;
        case VR_3D_GL_RENDER:
            VRGLRender::GetInstance()->OnSurfaceCreated();
            break;
        default:
            break;
    }
}

JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1OnSurfaceChanged(JNIEnv *env, jclass clazz,
                                                                           jint render_type,
                                                                           jint width,
                                                                           jint height) {
    switch (render_type)
    {
        case VIDEO_GL_RENDER:
            VideoGLRender::GetInstance()->OnSurfaceChanged(width, height);
            break;
        case AUDIO_GL_RENDER:
            AudioGLRender::GetInstance()->OnSurfaceChanged(width, height);
            break;
        case VR_3D_GL_RENDER:
            VRGLRender::GetInstance()->OnSurfaceChanged(width, height);
            break;
        default:
            break;
    }
}

JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1OnDrawFrame(JNIEnv *env, jclass clazz, jint render_type) {
    switch (render_type)
    {
        case VIDEO_GL_RENDER:
            VideoGLRender::GetInstance()->OnDrawFrame();
            break;
        case AUDIO_GL_RENDER:
            AudioGLRender::GetInstance()->OnDrawFrame();
            break;
        case VR_3D_GL_RENDER:
            VRGLRender::GetInstance()->OnDrawFrame();
            break;
        default:
            break;
    }
}

JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1SetGesture(JNIEnv *env, jclass clazz,
                                                                     jint   render_type,
                                                                     jfloat x_rotate_angle,
                                                                     jfloat y_rotate_angle,
                                                                     jfloat scale) {
    switch (render_type)
    {
        case VIDEO_GL_RENDER:
            VideoGLRender::GetInstance()->UpdateMVPMatrix(x_rotate_angle, y_rotate_angle, scale, scale);
            break;
        case AUDIO_GL_RENDER:
            AudioGLRender::GetInstance()->UpdateMVPMatrix(x_rotate_angle, y_rotate_angle, scale, scale);
            break;
        case VR_3D_GL_RENDER:
            VRGLRender::GetInstance()->UpdateMVPMatrix(x_rotate_angle, y_rotate_angle, scale, scale);
            break;
        default:
            break;
    }
}

JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_FFMediaPlayer_native_1SetTouchLoc(JNIEnv *env, jclass clazz,
                                                                      jint   render_type,
                                                                      jfloat touch_x,
                                                                      jfloat touch_y) {
    switch (render_type)
    {
        case VIDEO_GL_RENDER:
            VideoGLRender::GetInstance()->SetTouchLoc(touch_x, touch_y);
            break;
        case AUDIO_GL_RENDER:
            AudioGLRender::GetInstance()->SetTouchLoc(touch_x, touch_y);
            break;
        case VR_3D_GL_RENDER:
            VRGLRender::GetInstance()->SetTouchLoc(touch_x, touch_y);
            break;
        default:
            break;
    }
}

#ifdef __cplusplus
}
#endif

extern "C"
JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1CreateContext(JNIEnv *env,
                                                                                jobject thiz) {
    MediaRecorderContext::CreateContext(env, thiz);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1DestroyContext(JNIEnv *env,
                                                                                 jobject thiz) {
    MediaRecorderContext::DeleteContext(env, thiz);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1Init(JNIEnv *env, jobject thiz) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) return pContext->Init();
    return 0;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1UnInit(JNIEnv *env, jobject thiz) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) return pContext->UnInit();
    return 0;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1StartRecord(JNIEnv *env,
                                                                             jobject thiz,
                                                                             jint recorder_type,
                                                                             jstring out_url,
                                                                             jint frame_width,
                                                                             jint frame_height,
                                                                             jlong video_bit_rate,
                                                                             jint fps) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext == nullptr) return -1;
    const char* url = env->GetStringUTFChars(out_url, nullptr);
    // 必须等 StartRecord 用完 url 再释放（原来先释放后使用，路径可能变成乱码）
    int result = pContext->StartRecord(recorder_type, url, frame_width, frame_height, video_bit_rate, fps);
    env->ReleaseStringUTFChars(out_url, url);
    return result;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1OnAudioData(JNIEnv *env,
                                                                             jobject thiz,
                                                                             jbyteArray data,
                                                                             jint size) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext == nullptr) return;
    // 只处理本次真正读到的 size 字节：AudioRecord.read 可能读不满整个数组，
    // 原来按数组长度处理，会把上一次残留的旧数据也编码进去，产生杂音
    int len = env->GetArrayLength(data);
    if(size <= 0) return;
    if(size > len) size = len;
    // 直接访问 Java 数组内存，录制模块入队时会拷贝一份；JNI_ABORT 表示无需写回
    jbyte *buf = env->GetByteArrayElements(data, nullptr);
    if(buf == nullptr) return;
    pContext->OnAudioData(reinterpret_cast<uint8_t *>(buf), size);
    env->ReleaseByteArrayElements(data, buf, JNI_ABORT);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1StopRecord(JNIEnv *env,
                                                                            jobject thiz) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) return pContext->StopRecord();
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1OnPreviewFrame(JNIEnv *env,
                                                                                 jobject thiz,
                                                                                 jint format,
                                                                                 jbyteArray data,
                                                                                 jint width,
                                                                                 jint height) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext == nullptr) return;
    // 直接访问 Java 数组内存，省掉一次 new[] + 整帧拷贝（渲染器内部会再拷贝一份）
    jbyte *buf = env->GetByteArrayElements(data, nullptr);
    if(buf == nullptr) return;
    pContext->OnPreviewFrame(format, reinterpret_cast<uint8_t *>(buf), width, height);
    env->ReleaseByteArrayElements(data, buf, JNI_ABORT);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1SetTransformMatrix(JNIEnv *env,
                                                                                     jobject thiz,
                                                                                     jfloat translate_x,
                                                                                     jfloat translate_y,
                                                                                     jfloat scale_x,
                                                                                     jfloat scale_y,
                                                                                     jint degree,
                                                                                     jint mirror) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) pContext->SetTransformMatrix(translate_x, translate_y, scale_x, scale_y, degree, mirror);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1OnSurfaceCreated(JNIEnv *env,
                                                                                   jobject thiz) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) pContext->OnSurfaceCreated();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1OnSurfaceChanged(JNIEnv *env,
                                                                                   jobject thiz,
                                                                                   jint width,
                                                                                   jint height) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) pContext->OnSurfaceChanged(width, height);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1OnDrawFrame(JNIEnv *env, jobject thiz) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) pContext->OnDrawFrame();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1SetFilterData(JNIEnv *env,
                                                                               jobject thiz,
                                                                               jint index,
                                                                               jint format,
                                                                               jint width,
                                                                               jint height,
                                                                               jbyteArray bytes) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext == nullptr) return;
    jbyte *buf = env->GetByteArrayElements(bytes, nullptr);
    if(buf == nullptr) return;
    pContext->SetLUTImage(index, format, width, height, reinterpret_cast<uint8_t *>(buf));
    env->ReleaseByteArrayElements(bytes, buf, JNI_ABORT);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_byteflow_learnffmpeg_media_MediaRecorderContext_native_1SetFragShader(JNIEnv *env,
                                                                               jobject thiz,
                                                                               jint index,
                                                                               jstring str) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext == nullptr) return;
    int length = env->GetStringUTFLength(str);
    const char* cStr = env->GetStringUTFChars(str, nullptr);
    // 渲染器内部会拷贝一份，这里直接传入，无需再 malloc 中转
    pContext->SetFragShader(index, const_cast<char *>(cStr), length + 1);
    env->ReleaseStringUTFChars(str, cStr);
}