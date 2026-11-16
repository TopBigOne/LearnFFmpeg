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
	// 先停掉可能还在进行的录制（写完文件尾）
	StopRecord();
	// 不删除相机渲染器单例：GL 线程可能仍在调用它（会导致释放后使用），只清理它持有的数据
	GLCameraRender::GetInstance()->SetRenderCallback(nullptr, nullptr);
	GLCameraRender::GetInstance()->UnInit();
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
	int result = -1;
	// 创建和启动录制器（打开编码器、写文件头）比较耗时，放在锁外进行，成功后才挂到成员上
	switch (recorderType) {
		case RECORDER_TYPE_SINGLE_VIDEO: {  // 单视频录制
			if(GetVideoRecorder() != nullptr) return 0;
			// 相机图像是横向的，预览/录制时旋转了 90 度，所以宽高互换
			auto recorder = std::make_shared<SingleVideoRecorder>(outUrl, frameHeight, frameWidth, videoBitRate, fps);
			result = recorder->StartRecord();
			if(result == 0) {
				std::lock_guard<std::mutex> lock(m_mutex);
				m_pVideoRecorder = recorder;
			}
			break;
		}
		case RECORDER_TYPE_SINGLE_AUDIO: {  // 单音频录制
			if(GetAudioRecorder() != nullptr) return 0;
			auto recorder = std::make_shared<SingleAudioRecorder>(outUrl, DEFAULT_SAMPLE_RATE, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16);
			result = recorder->StartRecord();
			if(result == 0) {
				std::lock_guard<std::mutex> lock(m_mutex);
				m_pAudioRecorder = recorder;
			}
			break;
		}
		case RECORDER_TYPE_AV: {  // 音视频同时录制
			if(GetAVRecorder() != nullptr) return 0;
			RecorderParam param = {0};
			param.frameWidth      = frameHeight;
			param.frameHeight     = frameWidth;
			param.videoBitRate    = videoBitRate;
			param.fps             = fps;
			param.audioSampleRate = DEFAULT_SAMPLE_RATE;
			param.channelLayout   = AV_CH_LAYOUT_STEREO;
			param.sampleFormat    = AV_SAMPLE_FMT_S16;
			auto recorder = std::make_shared<MediaRecorder>(outUrl, &param);
			result = recorder->StartRecord();
			if(result == 0) {
				std::lock_guard<std::mutex> lock(m_mutex);
				m_pAVRecorder = recorder;
			}
			break;
		}
		default:
			break;
	}

	if(result != 0) {
		LOGCATE("MediaRecorderContext::StartRecord fail, result=%d", result);
	}
	// 只有录制视频时才需要从 GPU 回读画面（glReadPixels 很贵，原来不录制时每帧也在读）
	UpdateReadbackState();
	return result;
}

/**
 * @brief 停止录制
 * @return 0表示成功
 *
 * 锁内只把录制器指针取出来，等待编码线程、写文件尾这些耗时操作都在锁外完成：
 * 原来整个过程都持有锁，期间 GL 线程的录制回调被卡住，预览画面冻结
 */
int MediaRecorderContext::StopRecord() {
	std::shared_ptr<SingleVideoRecorder> videoRecorder;
	std::shared_ptr<SingleAudioRecorder> audioRecorder;
	std::shared_ptr<MediaRecorder> avRecorder;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		videoRecorder.swap(m_pVideoRecorder);
		audioRecorder.swap(m_pAudioRecorder);
		avRecorder.swap(m_pAVRecorder);
	}
	UpdateReadbackState();

	if(videoRecorder) videoRecorder->StopRecord();
	if(audioRecorder) audioRecorder->StopRecord();
	if(avRecorder) avRecorder->StopRecord();
	// 离开作用域时，若回调线程已用完，录制器在这里析构
	return 0;
}

void MediaRecorderContext::UpdateReadbackState() {
	bool needVideo;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		needVideo = m_pVideoRecorder != nullptr || m_pAVRecorder != nullptr;
	}
	GLCameraRender::GetInstance()->SetReadbackEnabled(needVideo);
}

std::shared_ptr<SingleVideoRecorder> MediaRecorderContext::GetVideoRecorder() {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_pVideoRecorder;
}

std::shared_ptr<SingleAudioRecorder> MediaRecorderContext::GetAudioRecorder() {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_pAudioRecorder;
}

std::shared_ptr<MediaRecorder> MediaRecorderContext::GetAVRecorder() {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_pAVRecorder;
}

/**
 * @brief 接收音频数据（AudioRecord 线程调用）
 * @param pData 音频数据指针
 * @param size 数据大小
 */
void MediaRecorderContext::OnAudioData(uint8_t *pData, int size, int64_t captureTimeUs) {
    LOGCATV("MediaRecorderContext::OnAudioData pData=%p, dataSize=%d", pData, size);
    // 原来这里不加锁，StopRecord 同时 delete 录制器会导致释放后使用。
    // 现在锁内复制 shared_ptr、锁外使用：即使此时 StopRecord，对象也会等这里用完才析构
    std::shared_ptr<SingleAudioRecorder> audioRecorder = GetAudioRecorder();
    std::shared_ptr<MediaRecorder> avRecorder = GetAVRecorder();
    if(!audioRecorder && !avRecorder) return;

    AudioFrame audioFrame(pData, size, false);
    if(audioRecorder) audioRecorder->OnFrame2Encode(&audioFrame);
    if(avRecorder) avRecorder->OnFrame2Encode(&audioFrame, captureTimeUs);
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
void MediaRecorderContext::OnPreviewFrame(int format, uint8_t *pBuffer, int width, int height, int64_t captureTimeUs)
{
	LOGCATV("MediaRecorderContext::UpdateFrame format=%d, width=%d, height=%d, pData=%p",
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
    GLCameraRender::GetInstance()->RenderVideoFrame(&nativeImage, captureTimeUs);
}

void MediaRecorderContext::OnPreviewImage(const Android420Planes &planes, int64_t captureTimeUs)
{
	GLCameraRender::GetInstance()->RenderAndroid420Frame(planes, captureTimeUs);
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
void MediaRecorderContext::OnGLRenderFrame(void *ctx, NativeImage *pImage, int64_t captureTimeUs) {
	LOGCATV("MediaRecorderContext::OnGLRenderFrame ctx=%p, pImage=%p", ctx, pImage);
	MediaRecorderContext *context = static_cast<MediaRecorderContext *>(ctx);
	// 锁内复制指针、锁外入队，GL 线程不会被 StartRecord/StopRecord 卡住
	std::shared_ptr<SingleVideoRecorder> videoRecorder = context->GetVideoRecorder();
	std::shared_ptr<MediaRecorder> avRecorder = context->GetAVRecorder();
	// 传递给单视频录制器
	if(videoRecorder) videoRecorder->OnFrame2Encode(pImage, captureTimeUs);
	// 传递给音视频录制器
	if(avRecorder) avRecorder->OnFrame2Encode(pImage, captureTimeUs);
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


