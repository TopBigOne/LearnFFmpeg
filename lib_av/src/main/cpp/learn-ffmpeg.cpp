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
#include "context/MediaRecorderContext.h"
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
 * Class:     com_monday_av_media_FFMediaPlayer
 * Method:    native_GetFFmpegVersion
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_com_monday_av_media_FFMediaPlayer_native_1GetFFmpegVersion
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

    return env->NewStringUTF(strBuffer);
}

/*
 * Class:     com_monday_av_media_FFMediaPlayer
 * Method:    native_Init
 * Signature: (JLjava/lang/String;Ljava/lang/Object;)J
 */
JNIEXPORT jlong JNICALL Java_com_monday_av_media_FFMediaPlayer_native_1Init
        (JNIEnv *env, jobject obj, jstring jurl, int playerType, jint renderType, jobject surface)
{
    const char* url = env->GetStringUTFChars(jurl, nullptr);
    PlayerWrapper *player = new PlayerWrapper();
    player->Init(env, obj, const_cast<char *>(url), playerType, renderType, surface);
    env->ReleaseStringUTFChars(jurl, url);
    return reinterpret_cast<jlong>(player);
}

/*
 * Class:     com_monday_av_media_FFMediaPlayer
 * Method:    native_Play
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_monday_av_media_FFMediaPlayer_native_1Play
(JNIEnv *env, jobject obj, jlong player_handle)
{
    if(player_handle != 0)
    {
        PlayerWrapper *pPlayerWrapper = reinterpret_cast<PlayerWrapper *>(player_handle);
        pPlayerWrapper->Play();
    }

}

JNIEXPORT void JNICALL
Java_com_monday_av_media_FFMediaPlayer_native_1SeekToPosition(JNIEnv *env, jobject thiz,
                                                                      jlong player_handle, jfloat position) {
    if(player_handle != 0)
    {
        PlayerWrapper *ffMediaPlayer = reinterpret_cast<PlayerWrapper *>(player_handle);
        ffMediaPlayer->SeekToPosition(position);
    }
}

JNIEXPORT jlong JNICALL
Java_com_monday_av_media_FFMediaPlayer_native_1GetMediaParams(JNIEnv *env, jobject thiz,
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
Java_com_monday_av_media_FFMediaPlayer_native_1SetMediaParams(JNIEnv *env, jobject thiz,
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
Java_com_monday_av_media_FFMediaPlayer_native_1SetPlaybackSpeed(JNIEnv *env, jobject thiz,
                                                                           jlong player_handle,
                                                                           jfloat speed) {
    if (player_handle == 0) return JNI_FALSE;
    PlayerWrapper *ffMediaPlayer = reinterpret_cast<PlayerWrapper *>(player_handle);
    return ffMediaPlayer->SetPlaybackSpeed(speed) ? JNI_TRUE : JNI_FALSE;
}

/*
 * Class:     com_monday_av_media_FFMediaPlayer
 * Method:    native_Pause
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_monday_av_media_FFMediaPlayer_native_1Pause
(JNIEnv *env, jobject obj, jlong player_handle)
{
    if(player_handle != 0)
    {
        PlayerWrapper *ffMediaPlayer = reinterpret_cast<PlayerWrapper *>(player_handle);
        ffMediaPlayer->Pause();
    }
}

/*
 * Class:     com_monday_av_media_FFMediaPlayer
 * Method:    native_Stop
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_monday_av_media_FFMediaPlayer_native_1Stop
(JNIEnv *env, jobject obj, jlong player_handle)
{
    if(player_handle != 0)
    {
        PlayerWrapper *ffMediaPlayer = reinterpret_cast<PlayerWrapper *>(player_handle);
        ffMediaPlayer->Stop();
    }
}

/*
 * Class:     com_monday_av_media_FFMediaPlayer
 * Method:    native_UnInit
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_com_monday_av_media_FFMediaPlayer_native_1UnInit
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
Java_com_monday_av_media_FFMediaPlayer_native_1OnSurfaceCreated(JNIEnv *env,
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
Java_com_monday_av_media_FFMediaPlayer_native_1OnSurfaceChanged(JNIEnv *env, jclass clazz,
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
Java_com_monday_av_media_FFMediaPlayer_native_1OnDrawFrame(JNIEnv *env, jclass clazz, jint render_type) {
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
Java_com_monday_av_media_FFMediaPlayer_native_1SetGesture(JNIEnv *env, jclass clazz,
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
Java_com_monday_av_media_FFMediaPlayer_native_1SetTouchLoc(JNIEnv *env, jclass clazz,
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
Java_com_monday_av_media_MediaRecorderContext_native_1CreateContext(JNIEnv *env,
                                                                                jobject thiz) {
    MediaRecorderContext::CreateContext(env, thiz);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_monday_av_media_MediaRecorderContext_native_1DestroyContext(JNIEnv *env,
                                                                                 jobject thiz) {
    MediaRecorderContext::DeleteContext(env, thiz);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_monday_av_media_MediaRecorderContext_native_1Init(JNIEnv *env, jobject thiz) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) return pContext->Init();
    return 0;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_monday_av_media_MediaRecorderContext_native_1UnInit(JNIEnv *env, jobject thiz) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) return pContext->UnInit();
    return 0;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_monday_av_media_MediaRecorderContext_native_1StartRecord(JNIEnv *env,
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
Java_com_monday_av_media_MediaRecorderContext_native_1OnAudioData(JNIEnv *env,
                                                                             jobject thiz,
                                                                             jbyteArray data,
                                                                             jint size,
                                                                             jlong capture_time_ns) {
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
    // 采集时刻：Java 层给的是 CLOCK_BOOTTIME 纳秒，录制模块统一用微秒；<=0 表示未知
    int64_t captureTimeUs = capture_time_ns > 0 ? capture_time_ns / 1000 : 0;
    pContext->OnAudioData(reinterpret_cast<uint8_t *>(buf), size, captureTimeUs);
    env->ReleaseByteArrayElements(data, buf, JNI_ABORT);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_monday_av_media_MediaRecorderContext_native_1StopRecord(JNIEnv *env,
                                                                            jobject thiz) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) return pContext->StopRecord();
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_monday_av_media_MediaRecorderContext_native_1OnPreviewFrame(JNIEnv *env,
                                                                                 jobject thiz,
                                                                                 jint format,
                                                                                 jbyteArray data,
                                                                                 jint width,
                                                                                 jint height,
                                                                                 jlong capture_time_ns) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext == nullptr) return;
    int64_t captureTimeUs = capture_time_ns > 0 ? capture_time_ns / 1000 : 0;
    // 直接访问 Java 数组内存，省掉一次 new[] + 整帧拷贝（渲染器内部会再拷贝一份）
    jbyte *buf = env->GetByteArrayElements(data, nullptr);
    if(buf == nullptr) return;
    pContext->OnPreviewFrame(format, reinterpret_cast<uint8_t *>(buf), width, height, captureTimeUs);
    env->ReleaseByteArrayElements(data, buf, JNI_ABORT);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_monday_av_media_MediaRecorderContext_native_1SetTransformMatrix(JNIEnv *env,
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
Java_com_monday_av_media_MediaRecorderContext_native_1OnSurfaceCreated(JNIEnv *env,
                                                                                   jobject thiz) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) pContext->OnSurfaceCreated();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_monday_av_media_MediaRecorderContext_native_1OnSurfaceChanged(JNIEnv *env,
                                                                                   jobject thiz,
                                                                                   jint width,
                                                                                   jint height) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) pContext->OnSurfaceChanged(width, height);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_monday_av_media_MediaRecorderContext_native_1OnDrawFrame(JNIEnv *env, jobject thiz) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if(pContext) pContext->OnDrawFrame();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_monday_av_media_MediaRecorderContext_native_1SetFilterData(JNIEnv *env,
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
Java_com_monday_av_media_MediaRecorderContext_native_1SetFragShader(JNIEnv *env,
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
/**
 * 从 Java 的 Image.Plane（direct ByteBuffer + stride）取出 YUV_420_888 三个平面并校验。
 *
 * 只读取内存，不拷贝；ByteBuffer 必须是 direct 的（Image.Plane.getBuffer() 就是），
 * 地址只在 Image 关闭前有效，调用方必须在 JNI 返回前用完。
 * 校验每个平面的容量够不够按 stride 读完整幅图，防止参数异常时越界读。
 */
static bool GetAndroid420Planes(JNIEnv *env,
                                jobject yBuf, jint yRowStride,
                                jobject uBuf, jint uRowStride,
                                jobject vBuf, jint vRowStride,
                                jint uvPixelStride, jint width, jint height,
                                Android420Planes *out) {
    if (yBuf == nullptr || uBuf == nullptr || vBuf == nullptr || width <= 0 || height <= 0
        || uvPixelStride < 1 || yRowStride < width) {
        LOGCATE("GetAndroid420Planes invalid args [w,h]=[%d,%d] yRowStride=%d uvPixelStride=%d",
                width, height, yRowStride, uvPixelStride);
        return false;
    }
    auto *y = static_cast<const uint8_t *>(env->GetDirectBufferAddress(yBuf));
    auto *u = static_cast<const uint8_t *>(env->GetDirectBufferAddress(uBuf));
    auto *v = static_cast<const uint8_t *>(env->GetDirectBufferAddress(vBuf));
    if (y == nullptr || u == nullptr || v == nullptr) {
        LOGCATE("GetAndroid420Planes buffer is not direct");
        return false;
    }
    const int64_t halfW = (width + 1) / 2, halfH = (height + 1) / 2;
    // 最后一行可能不足 rowStride 字节，只要求覆盖到最后一个有效像素
    const int64_t needY = (int64_t) (height - 1) * yRowStride + width;
    const int64_t needU = (halfH - 1) * uRowStride + (halfW - 1) * uvPixelStride + 1;
    const int64_t needV = (halfH - 1) * vRowStride + (halfW - 1) * uvPixelStride + 1;
    if (env->GetDirectBufferCapacity(yBuf) < needY || env->GetDirectBufferCapacity(uBuf) < needU
        || env->GetDirectBufferCapacity(vBuf) < needV) {
        LOGCATE("GetAndroid420Planes buffer too small for [w,h]=[%d,%d]", width, height);
        return false;
    }
    out->y = y; out->yRowStride = yRowStride;
    out->u = u; out->uRowStride = uRowStride;
    out->v = v; out->vRowStride = vRowStride;
    out->uvPixelStride = uvPixelStride;
    out->width = width;
    out->height = height;
    return true;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_monday_av_media_MediaRecorderContext_native_1OnPreviewImage(JNIEnv *env, jobject thiz,
                                                                     jobject y_buf, jint y_row_stride,
                                                                     jobject u_buf, jint u_row_stride,
                                                                     jobject v_buf, jint v_row_stride,
                                                                     jint uv_pixel_stride,
                                                                     jint width, jint height,
                                                                     jlong capture_time_ns) {
    MediaRecorderContext *pContext = MediaRecorderContext::GetContext(env, thiz);
    if (pContext == nullptr) return;
    Android420Planes planes;
    if (!GetAndroid420Planes(env, y_buf, y_row_stride, u_buf, u_row_stride, v_buf, v_row_stride,
                             uv_pixel_stride, width, height, &planes)) {
        return;
    }
    int64_t captureTimeUs = capture_time_ns > 0 ? capture_time_ns / 1000 : 0;
    pContext->OnPreviewImage(planes, captureTimeUs);
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_monday_av_media_YuvUtil_nativeAndroid420ToI420(JNIEnv *env, jclass clazz,
                                                        jobject y_buf, jint y_row_stride,
                                                        jobject u_buf, jint u_row_stride,
                                                        jobject v_buf, jint v_row_stride,
                                                        jint uv_pixel_stride,
                                                        jint width, jint height,
                                                        jbyteArray dst) {
    Android420Planes planes;
    if (dst == nullptr || !GetAndroid420Planes(env, y_buf, y_row_stride, u_buf, u_row_stride,
                                               v_buf, v_row_stride, uv_pixel_stride, width, height, &planes)) {
        return JNI_FALSE;
    }
    const int halfW = (width + 1) / 2, halfH = (height + 1) / 2;
    const int64_t need = (int64_t) width * height + (int64_t) halfW * halfH * 2;
    if (env->GetArrayLength(dst) < need) {
        LOGCATE("YuvUtil_nativeAndroid420ToI420 dst too small");
        return JNI_FALSE;
    }
    // Critical 区间内只做一次 libyuv 转换，不调用其他 JNI，GC 只被短暂阻塞
    auto *out = static_cast<uint8_t *>(env->GetPrimitiveArrayCritical(dst, nullptr));
    if (out == nullptr) return JNI_FALSE;
    uint8_t *outU = out + (size_t) width * height;
    uint8_t *outV = outU + (size_t) halfW * halfH;
    bool ok = YuvConvert::Android420ToI420(planes, out, width, outU, halfW, outV, halfW);
    env->ReleasePrimitiveArrayCritical(dst, out, 0);
    return ok ? JNI_TRUE : JNI_FALSE;
}

/**
 * ASan 测试入口（com.monday.av.debug.ASanTester.runCase）。
 * 只有 asan 构建类型（CMake 定义了 LEARNFFMPEG_ASAN）才执行，其他构建只打日志，避免在没有 ASan 时制造真正的内存破坏。
 */
extern "C"
JNIEXPORT void JNICALL
Java_com_monday_av_debug_ASanTester_nativeRunCase(JNIEnv *env, jclass clazz, jint case_id) {
#ifdef LEARNFFMPEG_ASAN
    ASanTestCase::Run(case_id);
#else
    LOGCATE("ASanTester: not an ASan build, ignore case %d (build with ./gradlew :app:installAsan)", case_id);
#endif
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_monday_av_debug_ASanTester_nativeIsAsanBuild(JNIEnv *env, jclass clazz) {
#ifdef LEARNFFMPEG_ASAN
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}
