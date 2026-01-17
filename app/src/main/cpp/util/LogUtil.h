//
// Created by 公众号：字节流动 on 2019/1/14.
//

#ifndef BYTEFLOW_LOGUTIL_H
#define BYTEFLOW_LOGUTIL_H

#include<android/log.h>
#include <sys/time.h>

#define  LOG_TAG "ByteFlow"  // 日志标签

// Android 日志宏定义
#define  LOGCATE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)    // 错误级别日志
#define  LOGCATV(...)  __android_log_print(ANDROID_LOG_VERBOSE,LOG_TAG,__VA_ARGS__)  // 详细级别日志
#define  LOGCATD(...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)    // 调试级别日志
#define  LOGCATI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)     // 信息级别日志

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
 * @brief 获取系统当前时间（毫秒）
 * @return 当前时间戳（毫秒）
 */
static long long GetSysCurrentTime()
{
	struct timeval time;
	gettimeofday(&time, NULL);
	long long curTime = ((long long)(time.tv_sec))*1000+time.tv_usec/1000;
	return curTime;
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
