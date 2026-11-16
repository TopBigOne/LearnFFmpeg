/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#ifndef OPENGLCAMERA2_BYTEFLOWRENDERCONTEXT_H
#define OPENGLCAMERA2_BYTEFLOWRENDERCONTEXT_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <jni.h>
#include "pipeline/SingleVideoRecorder.h"
#include "VideoGLRender.h"
#include "camera/GLCameraRender.h"
#include "pipeline/SingleAudioRecorder.h"
#include "pipeline/MediaRecorder.h"

#define RECORDER_TYPE_SINGLE_VIDEO  0 // 仅录制视频
#define RECORDER_TYPE_SINGLE_AUDIO  1 // 仅录制音频
#define RECORDER_TYPE_AV            2 // 同时录制音频和视频，打包成 MP4 文件

/**
 * @brief 媒体录制器上下文类
 *
 * 管理媒体录制的整个生命周期，包括音频、视频和音视频混合录制
 * 提供与JNI层的交互接口，管理OpenGL渲染和录制器实例
 */
class MediaRecorderContext
{
public:
	/**
	 * @brief 构造函数
	 */
	MediaRecorderContext();

	/**
	 * @brief 析构函数
	 */
	~MediaRecorderContext();

	/**
	 * @brief 创建上下文（静态方法）
	 * @param env JNI环境指针
	 * @param instance Java对象实例
	 */
	static void CreateContext(JNIEnv *env, jobject instance);

	/**
	 * @brief 存储上下文到Java对象（静态方法）
	 * @param env JNI环境指针
	 * @param instance Java对象实例
	 * @param pContext 上下文指针
	 */
	static void StoreContext(JNIEnv *env, jobject instance, MediaRecorderContext *pContext);

	/**
	 * @brief 删除上下文（静态方法）
	 * @param env JNI环境指针
	 * @param instance Java对象实例
	 */
	static void DeleteContext(JNIEnv *env, jobject instance);

	/**
	 * @brief 获取上下文（静态方法）
	 * @param env JNI环境指针
	 * @param instance Java对象实例
	 * @return 上下文指针
	 */
	static MediaRecorderContext* GetContext(JNIEnv *env, jobject instance);

	/**
	 * @brief OpenGL渲染帧回调（静态方法）
	 * @param ctx 上下文指针
	 * @param pImage 渲染的图像数据
	 * @param captureTimeUs 画面的采集时刻（CLOCK_BOOTTIME 微秒）
	 */
	static void OnGLRenderFrame(void *ctx, NativeImage * pImage, int64_t captureTimeUs);

	/**
	 * @brief 初始化
	 * @return 0表示成功，负数表示失败
	 */
	int Init();

	/**
	 * @brief 反初始化，释放资源
	 * @return 0表示成功，负数表示失败
	 */
	int UnInit();

	/**
	 * @brief 开始录制
	 * @param recorderType 录制类型（视频/音频/音视频）
	 * @param outUrl 输出文件路径
	 * @param frameWidth 视频帧宽度
	 * @param frameHeight 视频帧高度
	 * @param videoBitRate 视频比特率
	 * @param fps 帧率
	 * @return 0表示成功，负数表示失败
	 */
    int StartRecord(int recorderType, const char* outUrl, int frameWidth, int frameHeight, long videoBitRate, int fps);

	/**
	 * @brief 处理音频数据
	 * @param pData 音频数据指针
	 * @param size 数据大小
	 * @param captureTimeUs 第一个采样的采集时刻（CLOCK_BOOTTIME 微秒），0 表示未知
	 */
    void OnAudioData(uint8_t *pData, int size, int64_t captureTimeUs);

	/**
	 * @brief 处理预览帧数据
	 * @param format 图像格式
	 * @param pBuffer 图像数据缓冲区
	 * @param width 图像宽度
	 * @param height 图像高度
	 * @param captureTimeUs 画面的采集时刻（CLOCK_BOOTTIME 微秒），0 表示未知
	 */
	void OnPreviewFrame(int format, uint8_t *pBuffer, int width, int height, int64_t captureTimeUs);

	/**
	 * @brief 处理相机原始图像（Android YUV_420_888 的三个平面），直接转换进渲染缓冲
	 * @param captureTimeUs 画面的采集时刻（CLOCK_BOOTTIME 微秒），0 表示未知
	 */
	void OnPreviewImage(const Android420Planes &planes, int64_t captureTimeUs);

	/**
	 * @brief 停止录制
	 * @return 0表示成功，负数表示失败
	 */
    int StopRecord();

	/**
	 * @brief 设置变换矩阵
	 * @param translateX X轴平移
	 * @param translateY Y轴平移
	 * @param scaleX X轴缩放
	 * @param scaleY Y轴缩放
	 * @param degree 旋转角度
	 * @param mirror 镜像标志
	 */
	void SetTransformMatrix(float translateX, float translateY, float scaleX, float scaleY, int degree, int mirror);

	/**
	 * @brief OpenGL Surface创建回调
	 */
	void OnSurfaceCreated();

	/**
	 * @brief OpenGL Surface大小变化回调
	 * @param width 新宽度
	 * @param height 新高度
	 */
	void OnSurfaceChanged(int width, int height);

	/**
	 * @brief OpenGL 绘制帧回调
	 */
	void OnDrawFrame();

	/**
	 * @brief 加载LUT（查找表）滤镜素材图像
	 * @param index 索引
	 * @param format 图像格式
	 * @param width 图像宽度
	 * @param height 图像高度
	 * @param pData 图像数据
	 */
	void SetLUTImage(int index, int format, int width, int height, uint8_t *pData);

	/**
	 * @brief 加载片段着色器脚本
	 * @param index 索引
	 * @param pShaderStr 着色器字符串
	 * @param strSize 字符串大小
	 */
	void SetFragShader(int index, char *pShaderStr, int strSize);

private:
	/** 只有在录制视频时才开启 GPU 画面回读 */
	void UpdateReadbackState();

	std::shared_ptr<SingleVideoRecorder> GetVideoRecorder();
	std::shared_ptr<SingleAudioRecorder> GetAudioRecorder();
	std::shared_ptr<MediaRecorder> GetAVRecorder();

	static jfieldID s_ContextHandle;            // JNI字段ID，用于存储Native上下文
	TransformMatrix m_transformMatrix;          // 变换矩阵
	// 录制器用 shared_ptr 管理：回调线程在锁内复制一份指针、锁外使用，
	// 停止录制时即使回调线程还在用，对象也要等它用完才会真正析构，不会出现释放后使用
	std::shared_ptr<SingleVideoRecorder> m_pVideoRecorder;  // 单独视频录制器
	std::shared_ptr<SingleAudioRecorder> m_pAudioRecorder;  // 单独音频录制器
	std::shared_ptr<MediaRecorder>       m_pAVRecorder;     // 音视频录制器
	mutex m_mutex;                              // 互斥锁，只保护上面三个指针本身，耗时操作都在锁外

};


#endif //OPENGLCAMERA2_BYTEFLOWRENDERCONTEXT_H
