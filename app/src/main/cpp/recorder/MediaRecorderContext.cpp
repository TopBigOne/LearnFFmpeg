/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#include <LogUtil.h>
#include <ImageDef.h>
#include "MediaRecorderContext.h"

jfieldID MediaRecorderContext::s_ContextHandle = 0L;

/**
 * @brief 构造函数
 * 初始化相机渲染器单例
 */
MediaRecorderContext::MediaRecorderContext() {
	GLCameraRender::GetInstance();
}

/**
 * @brief 析构函数
 * 释放相机渲染器单例
 */
MediaRecorderContext::~MediaRecorderContext()
{
	GLCameraRender::ReleaseInstance();
}

/**
 * @brief 创建上下文实例（JNI接口）
 * @param env JNI环境指针
 * @param instance Java对象实例
 *
 * 创建Native层上下文并存储到Java对象中
 */
void MediaRecorderContext::CreateContext(JNIEnv *env, jobject instance)
{
	LOGCATE("MediaRecorderContext::CreateContext");
	MediaRecorderContext *pContext = new MediaRecorderContext();
	StoreContext(env, instance, pContext);
}

/**
 * @brief 将上下文指针存储到Java对象
 * @param env JNI环境指针
 * @param instance Java对象实例
 * @param pContext 上下文指针
 *
 * 通过反射获取Java对象的字段并存储Native指针
 */
void MediaRecorderContext::StoreContext(JNIEnv *env, jobject instance, MediaRecorderContext *pContext)
{
	LOGCATE("MediaRecorderContext::StoreContext");
	jclass cls = env->GetObjectClass(instance);
	if (cls == NULL)
	{
		LOGCATE("MediaRecorderContext::StoreContext cls == NULL");
		return;
	}

	s_ContextHandle = env->GetFieldID(cls, "mNativeContextHandle", "J");
	if (s_ContextHandle == NULL)
	{
		LOGCATE("MediaRecorderContext::StoreContext s_ContextHandle == NULL");
		return;
	}

	env->SetLongField(instance, s_ContextHandle, reinterpret_cast<jlong>(pContext));

}


/**
 * @brief 删除上下文实例（JNI接口）
 * @param env JNI环境指针
 * @param instance Java对象实例
 *
 * 从Java对象中获取并删除Native层上下文
 */
void MediaRecorderContext::DeleteContext(JNIEnv *env, jobject instance)
{
	LOGCATE("MediaRecorderContext::DeleteContext");
	if (s_ContextHandle == NULL)
	{
		LOGCATE("MediaRecorderContext::DeleteContext Could not find render context.");
		return;
	}

	MediaRecorderContext *pContext = reinterpret_cast<MediaRecorderContext *>(env->GetLongField(
			instance, s_ContextHandle));
	if (pContext)
	{
		delete pContext;
	}
	env->SetLongField(instance, s_ContextHandle, 0L);
}

/**
 * @brief 从Java对象获取上下文指针
 * @param env JNI环境指针
 * @param instance Java对象实例
 * @return 上下文指针
 *
 * 通过反射从Java对象获取Native层上下文指针
 */
MediaRecorderContext *MediaRecorderContext::GetContext(JNIEnv *env, jobject instance)
{
	LOGCATE("MediaRecorderContext::GetContext");

	if (s_ContextHandle == NULL)
	{
		LOGCATE("MediaRecorderContext::GetContext Could not find render context.");
		return NULL;
	}

	MediaRecorderContext *pContext = reinterpret_cast<MediaRecorderContext *>(env->GetLongField(
			instance, s_ContextHandle));
	return pContext;
}

/**
 * @brief 初始化上下文
 * @return 0表示成功
 *
 * 初始化相机渲染器并设置渲染回调
 */
int MediaRecorderContext::Init()
{
	GLCameraRender::GetInstance()->Init(0, 0, nullptr);
	GLCameraRender::GetInstance()->SetRenderCallback(this, OnGLRenderFrame);
	return 0;
}

/**
 * @brief 反初始化上下文
 * @return 0表示成功
 *
 * 清理相机渲染器资源
 */
int MediaRecorderContext::UnInit()
{
	GLCameraRender::GetInstance()->UnInit();

	return 0;
}

/**
 * @brief 开始录制
 * @param recorderType 录制类型（单视频、单音频或音视频）
 * @param outUrl 输出文件路径
 * @param frameWidth 视频帧宽度
 * @param frameHeight 视频帧高度
 * @param videoBitRate 视频比特率
 * @param fps 帧率
 * @return 0表示成功
 *
 * 根据录制类型创建对应的录制器并开始录制
 */
int
MediaRecorderContext::StartRecord(int recorderType, const char *outUrl, int frameWidth, int frameHeight, long videoBitRate,
                                  int fps) {
	LOGCATE("MediaRecorderContext::StartRecord recorderType=%d, outUrl=%s, [w,h]=[%d,%d], videoBitRate=%ld, fps=%d", recorderType, outUrl, frameWidth, frameHeight, videoBitRate, fps);
	std::unique_lock<std::mutex> lock(m_mutex);
	switch (recorderType) {
		case RECORDER_TYPE_SINGLE_VIDEO:  // 单视频录制
			if(m_pVideoRecorder == nullptr) {
				m_pVideoRecorder = new SingleVideoRecorder(outUrl, frameHeight, frameWidth, videoBitRate, fps);
				m_pVideoRecorder->StartRecord();
			}
			break;
		case RECORDER_TYPE_SINGLE_AUDIO:  // 单音频录制
			if(m_pAudioRecorder == nullptr) {
				m_pAudioRecorder = new SingleAudioRecorder(outUrl, DEFAULT_SAMPLE_RATE, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16);
				m_pAudioRecorder->StartRecord();
			}
			break;
		case RECORDER_TYPE_AV:  // 音视频同时录制
			if(m_pAVRecorder == nullptr) {
				RecorderParam param = {0};
				param.frameWidth      = frameHeight;
				param.frameHeight     = frameWidth;
				param.videoBitRate    = videoBitRate;
				param.fps             = fps;
				param.audioSampleRate = DEFAULT_SAMPLE_RATE;
				param.channelLayout   = AV_CH_LAYOUT_STEREO;
				param.sampleFormat    = AV_SAMPLE_FMT_S16;
				m_pAVRecorder = new MediaRecorder(outUrl, &param);
				m_pAVRecorder->StartRecord();
			}
			break;
		default:
			break;
	}


    return 0;
}

/**
 * @brief 停止录制
 * @return 0表示成功
 *
 * 停止所有活动的录制器并释放资源
 */
int MediaRecorderContext::StopRecord() {
	std::unique_lock<std::mutex> lock(m_mutex);
	// 停止单视频录制
	if(m_pVideoRecorder != nullptr) {
        m_pVideoRecorder->StopRecord();
        delete m_pVideoRecorder;
        m_pVideoRecorder = nullptr;
    }

	// 停止单音频录制
	if(m_pAudioRecorder != nullptr) {
		m_pAudioRecorder->StopRecord();
		delete m_pAudioRecorder;
		m_pAudioRecorder = nullptr;
	}

	// 停止音视频录制
	if(m_pAVRecorder != nullptr) {
		m_pAVRecorder->StopRecord();
		delete m_pAVRecorder;
		m_pAVRecorder = nullptr;
	}
    return 0;
}

/**
 * @brief 接收音频数据
 * @param pData 音频数据指针
 * @param size 数据大小
 *
 * 将音频数据传递给活动的音频录制器
 */
void MediaRecorderContext::OnAudioData(uint8_t *pData, int size) {
    LOGCATE("MediaRecorderContext::OnAudioData pData=%p, dataSize=%d", pData, size);
    AudioFrame audioFrame(pData, size, false);
    // 传递给单音频录制器
    if(m_pAudioRecorder != nullptr)
		m_pAudioRecorder->OnFrame2Encode(&audioFrame);

    // 传递给音视频录制器
    if(m_pAVRecorder != nullptr)
    	m_pAVRecorder->OnFrame2Encode(&audioFrame);
}

/**
 * @brief 接收预览帧
 * @param format 图像格式
 * @param pBuffer 图像数据指针
 * @param width 图像宽度
 * @param height 图像高度
 *
 * 将相机预览帧传递给渲染器进行显示
 */
void MediaRecorderContext::OnPreviewFrame(int format, uint8_t *pBuffer, int width, int height)
{
	LOGCATE("MediaRecorderContext::UpdateFrame format=%d, width=%d, height=%d, pData=%p",
			format, width, height, pBuffer);
	// 构造NativeImage结构
	NativeImage nativeImage;
	nativeImage.format = format;
	nativeImage.width = width;
	nativeImage.height = height;
	nativeImage.ppPlane[0] = pBuffer;

    // 根据图像格式设置平面指针
    switch (format)
	{
		case IMAGE_FORMAT_NV12:
		case IMAGE_FORMAT_NV21:
			nativeImage.ppPlane[1] = nativeImage.ppPlane[0] + width * height;
			nativeImage.pLineSize[0] = width;
            nativeImage.pLineSize[1] = width;
			break;
		case IMAGE_FORMAT_I420:
			nativeImage.ppPlane[1] = nativeImage.ppPlane[0] + width * height;
			nativeImage.ppPlane[2] = nativeImage.ppPlane[1] + width * height / 4;
            nativeImage.pLineSize[0] = width;
            nativeImage.pLineSize[1] = width / 2;
            nativeImage.pLineSize[2] = width / 2;
			break;
		default:
			break;
	}

//	std::unique_lock<std::mutex> lock(m_mutex);
//	if(m_pVideoRecorder!= nullptr) {
//        m_pVideoRecorder->OnFrame2Encode(&nativeImage);
//    }
//	lock.unlock();

    //NativeImageUtil::DumpNativeImage(&nativeImage, "/sdcard", "camera");
    // 传递给渲染器
    GLCameraRender::GetInstance()->RenderVideoFrame(&nativeImage);
}

/**
 * @brief 设置变换矩阵
 * @param translateX X轴平移
 * @param translateY Y轴平移
 * @param scaleX X轴缩放
 * @param scaleY Y轴缩放
 * @param degree 旋转角度
 * @param mirror 镜像模式
 *
 * 更新渲染器的变换矩阵参数
 */
void MediaRecorderContext::SetTransformMatrix(float translateX, float translateY, float scaleX, float scaleY, int degree, int mirror)
{
	m_transformMatrix.translateX = translateX;
	m_transformMatrix.translateY = translateY;
	m_transformMatrix.scaleX = scaleX;
	m_transformMatrix.scaleY = scaleY;
	m_transformMatrix.degree = degree;
	m_transformMatrix.mirror = mirror;
	GLCameraRender::GetInstance()->UpdateMVPMatrix(&m_transformMatrix);
}

/**
 * @brief Surface创建时回调
 * 初始化OpenGL渲染环境
 */
void MediaRecorderContext::OnSurfaceCreated()
{
	GLCameraRender::GetInstance()->OnSurfaceCreated();
}

/**
 * @brief Surface尺寸变化时回调
 * @param width 新的宽度
 * @param height 新的高度
 *
 * 更新渲染器视口尺寸
 */
void MediaRecorderContext::OnSurfaceChanged(int width, int height)
{
	GLCameraRender::GetInstance()->OnSurfaceChanged(width, height);
}

/**
 * @brief 绘制一帧
 * 触发渲染器绘制当前帧
 */
void MediaRecorderContext::OnDrawFrame()
{
	GLCameraRender::GetInstance()->OnDrawFrame();
}

/**
 * @brief OpenGL渲染帧回调（静态函数）
 * @param ctx 上下文指针
 * @param pImage 渲染后的图像数据
 *
 * 将渲染后的帧传递给录制器进行编码
 */
void MediaRecorderContext::OnGLRenderFrame(void *ctx, NativeImage *pImage) {
	LOGCATE("MediaRecorderContext::OnGLRenderFrame ctx=%p, pImage=%p", ctx, pImage);
	MediaRecorderContext *context = static_cast<MediaRecorderContext *>(ctx);
	std::unique_lock<std::mutex> lock(context->m_mutex);
	// 传递给单视频录制器
	if(context->m_pVideoRecorder != nullptr)
		context->m_pVideoRecorder->OnFrame2Encode(pImage);

	// 传递给音视频录制器
	if(context->m_pAVRecorder != nullptr)
		context->m_pAVRecorder->OnFrame2Encode(pImage);
}

/**
 * @brief 设置LUT滤镜图像
 * @param index LUT索引
 * @param format 图像格式
 * @param width 图像宽度
 * @param height 图像高度
 * @param pData 图像数据
 *
 * 设置颜色查找表图像用于滤镜效果
 */
void
MediaRecorderContext::SetLUTImage(int index, int format, int width, int height, uint8_t *pData) {
	LOGCATE("MediaRecorderContext::SetLUTImage index=%d, format=%d, width=%d, height=%d, pData=%p",
			index, format, width, height, pData);
	NativeImage nativeImage;
	nativeImage.format = format;
	nativeImage.width = width;
	nativeImage.height = height;
	nativeImage.ppPlane[0] = pData;
	nativeImage.pLineSize[0] = width * 4; //RGBA

	GLCameraRender::GetInstance()->SetLUTImage(index, &nativeImage);
}

/**
 * @brief 设置片段着色器
 * @param index 着色器索引
 * @param pShaderStr 着色器源码
 * @param strSize 源码大小
 *
 * 动态设置片段着色器实现不同的渲染效果
 */
void MediaRecorderContext::SetFragShader(int index, char *pShaderStr, int strSize) {
	GLCameraRender::GetInstance()->SetFragShaderStr(index, pShaderStr, strSize);
}


