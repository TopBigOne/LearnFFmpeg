//
// Created by 公众号：字节流动 on 2019/1/14.
//

#ifndef BYTEFLOW_LOGUTIL_H
#define BYTEFLOW_LOGUTIL_H

#include<android/log.h>
#include <sys/time.h>
#include <time.h>

#define  LOG_TAG "ByteFlow"  // 日志标签

// Android 日志宏定义
#define  LOGCATE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)    // 错误级别日志
#define  LOGCATI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)     // 信息级别日志

/**
 * 详细/调试级别日志：用于每帧、每包这类高频热路径。
 * Release 构建（定义了 NDEBUG）时整体编译掉，避免格式化字符串 + 写 logcat 的开销。
 */
#ifdef NDEBUG
#define  LOGCATV(...)  ((void)0)
#define  LOGCATD(...)  ((void)0)
#else
#define  LOGCATV(...)  __android_log_print(ANDROID_LOG_VERBOSE,LOG_TAG,__VA_ARGS__)  // 详细级别日志
#define  LOGCATD(...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)    // 调试级别日志
#endif

// 日志宏别名
#define ByteFlowPrintE LOGCATE
#define ByteFlowPrintV LOGCATV
#define ByteFlowPrintD LOGCATD
#define ByteFlowPrintI LOGCATI

/**
 * @brief 函数执行时间测量宏（带文件名）
 *
 * 使用方法：
 * FUN_BEGIN_TIME("FunctionName")
 *   // 函数代码
 * FUN_END_TIME("FunctionName")
 */
#define FUN_BEGIN_TIME(FUN) {\
    LOGCATE("%s:%s func start", __FILE__, FUN); \
    long long t0 = GetSysCurrentTime();

#define FUN_END_TIME(FUN) \
    long long t1 = GetSysCurrentTime(); \
    LOGCATE("%s:%s func cost time %ldms", __FILE__, FUN, (long)(t1-t0));}

/**
 * @brief 函数执行时间测量宏（不带文件名）
 *
 * 使用方法：
 * BEGIN_TIME("FunctionName")
 *   // 函数代码
 * END_TIME("FunctionName")
 */
#define BEGIN_TIME(FUN) {\
    LOGCATE("%s func start", FUN); \
    long long t0 = GetSysCurrentTime();

#define END_TIME(FUN) \
    long long t1 = GetSysCurrentTime(); \
    LOGCATE("%s func cost time %ldms", FUN, (long)(t1-t0));}

/**
 * @brief 获取单调时钟当前时间（毫秒）
 * @return 当前时间戳（毫秒）
 *
 * 使用 CLOCK_MONOTONIC 而不是 gettimeofday：
 * 后者是墙上时钟，用户改系统时间或 NTP 校时会让它跳变，音视频同步会被打乱。
 * 注意返回 long long，在 armeabi-v7a 上 long 只有 32 位，存放时间戳请用 int64_t。
 */
static inline long long GetSysCurrentTime()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((long long)ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

/**
 * @brief 检查OpenGL错误宏
 * 输出函数名、错误码和行号
 */
#define GO_CHECK_GL_ERROR(...)   LOGCATE("CHECK_GL_ERROR %s glGetError = %d, line = %d, ",  __FUNCTION__, glGetError(), __LINE__)

/**
 * @brief 调试日志宏
 * 输出函数名和行号，用于调试定位
 */
#define DEBUG_LOGCATE(...) LOGCATE("DEBUG_LOGCATE %s line = %d",  __FUNCTION__, __LINE__)

#endif //BYTEFLOW_LOGUTIL_H
