/**
 *
 * Created by 公众号：字节流动 on 2021/3/12.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#ifndef LEARNFFMPEG_MASTER_GLCAMERARENDER_H
#define LEARNFFMPEG_MASTER_GLCAMERARENDER_H
#include <thread>
#include <ImageDef.h>
#include "YuvConvert.h"
#include "VideoEncoderSink.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/native_window.h>
#include "VideoRender.h"
#include <GLES3/gl3.h>
#include <detail/type_mat.hpp>
#include <detail/type_mat4x4.hpp>
#include <vec2.hpp>
#include <render/BaseGLRender.h>
#include <vector>
#include <atomic>
#include <mutex>
using namespace glm;
using namespace std;

#define MATH_PI 3.1415926535897932384626433832802

#define TEXTURE_NUM 3                          // 纹理数量

// 着色器索引定义
#define SHADER_INDEX_ORIGIN  0                 // 原始着色器
#define SHADER_INDEX_DMESH   1                 // 网格变形着色器
#define SHADER_INDEX_GHOST   2                 // 幻影效果着色器
#define SHADER_INDEX_CIRCLE  3                 // 圆形效果着色器
#define SHADER_INDEX_ASCII   4                 // ASCII效果着色器
#define SHADER_INDEX_SPLIT   5                 // 分屏效果着色器
#define SHADER_INDEX_MATTE   6                 // 遮罩效果着色器
#define SHADER_INDEX_LUT_A   7                 // LUT颜色表A
#define SHADER_INDEX_LUT_B   8                 // LUT颜色表B
#define SHADER_INDEX_LUT_C   9                 // LUT颜色表C
#define SHADER_INDEX_NE      10                // 负片效果(Negative effect)

/**
 * @brief 渲染帧回调函数类型定义
 * @param context 上下文指针
 * @param image 渲染后的图像数据
 */
// 回调参数：上下文、渲染结果、这帧画面的采集时刻（CLOCK_BOOTTIME 微秒，0 表示未知）
typedef void (*OnRenderFrameCallback)(void*, NativeImage*, int64_t);

/**
 * @brief OpenGL相机渲染器类
 *
 * 继承自VideoRender和BaseGLRender，负责相机预览的渲染和滤镜处理
 * 支持多种滤镜效果和实时渲染
 */
class GLCameraRender: public VideoRender, public BaseGLRender{
public:
    /**
     * @brief 初始化预览帧的宽高
     * @param videoWidth 视频宽度
     * @param videoHeight 视频高度
     * @param dstSize 输出尺寸数组
     */
    virtual void Init(int videoWidth, int videoHeight, int *dstSize);

    /**
     * @brief 渲染一帧视频
     * @param pImage 视频帧图像数据
     */
    virtual void RenderVideoFrame(NativeImage *pImage);

    /**
     * @brief 渲染一帧视频，并记录它的采集时刻
     * @param captureTimeUs 采集时刻（CLOCK_BOOTTIME 微秒），0 表示未知
     */
    void RenderVideoFrame(NativeImage *pImage, int64_t captureTimeUs);

    /**
     * @brief 渲染一帧相机原始图像（Android YUV_420_888），用 libyuv 直接转成 I420 写入渲染缓冲
     *
     * 相比在 Java 层先转成 byte[] 再传进来，少一次整帧拷贝，Java 层也不再分配内存
     * @param captureTimeUs 采集时刻（CLOCK_BOOTTIME 微秒），0 表示未知
     */
    void RenderAndroid420Frame(const Android420Planes &planes, int64_t captureTimeUs);

    /**
     * @brief 绑定硬件编码器的输入 Surface：之后每个新画面除了显示，还会直接画到编码器上
     *
     * 在调用线程上同步创建 EGLSurface（使用当前 GL 上下文的 display 和 config），
     * 失败时返回 false，调用方应回退到软件编码（回读 + x264）
     * @param sink 为每帧分配 pts，返回负数表示该帧不录；在 DetachEncoder 之前必须保持有效
     */
    bool AttachEncoder(ANativeWindow *window, int width, int height, VideoEncoderSink *sink);

    /**
     * @brief 解绑编码器：返回后 GL 线程不会再往编码器 Surface 上画，可以安全地结束编码器
     */
    void DetachEncoder();

    /**
     * @brief 反初始化，释放资源
     */
    virtual void UnInit();

    /**
     * @brief OpenGL Surface创建回调
     */
    virtual void OnSurfaceCreated();

    /**
     * @brief OpenGL Surface大小变化回调
     * @param w 新宽度
     * @param h 新高度
     */
    virtual void OnSurfaceChanged(int w, int h);

    /**
     * @brief OpenGL 绘制帧回调
     */
    virtual void OnDrawFrame();

    /**
     * @brief 获取GLCameraRender单例实例
     * @return GLCameraRender实例指针
     */
    static GLCameraRender *GetInstance();

    /**
     * @brief 释放GLCameraRender单例实例
     */
    static void ReleaseInstance();

    /**
     * @brief 更新MVP变换矩阵
     * Camera预览帧需要进行旋转和缩放
     * @param angleX X轴旋转角度
     * @param angleY Y轴旋转角度
     * @param scaleX X轴缩放
     * @param scaleY Y轴缩放
     */
    virtual void UpdateMVPMatrix(int angleX, int angleY, float scaleX, float scaleY);

    /**
     * @brief 更新MVP变换矩阵
     * @param pTransformMatrix 变换矩阵指针
     */
    virtual void UpdateMVPMatrix(TransformMatrix * pTransformMatrix);

    /**
     * @brief 设置触摸位置
     * @param touchX X坐标
     * @param touchY Y坐标
     */
    virtual void SetTouchLoc(float touchX, float touchY) {
        m_TouchXY.x = touchX / m_ScreenSize.x;
        m_TouchXY.y = touchY / m_ScreenSize.y;
    }

    /**
     * @brief 设置渲染帧回调
     * @param ctx 回调上下文
     * @param callback 回调函数
     *
     * 添加好滤镜之后，视频帧的回调，然后将带有滤镜的视频帧放入编码队列
     */
    void SetRenderCallback(void *ctx, OnRenderFrameCallback callback) {
        // 回调期间持有同一把锁：这里返回后，旧的回调一定已经执行完，调用方可以安全释放 ctx
        std::lock_guard<std::mutex> lock(m_CallbackMutex);
        m_CallbackContext = ctx;
        m_RenderFrameCallback = callback;
    }

    /**
     * @brief 是否从 FBO 回读画面并回调给录制模块
     * glReadPixels 会同步等待 GPU、代价很高，只在录制视频时开启
     */
    void SetReadbackEnabled(bool enabled) {
        m_ReadbackEnabled = enabled;
    }

    /**
     * @brief 加载LUT滤镜素材图像
     * @param index LUT索引
     * @param pLUTImg LUT图像数据
     */
    void SetLUTImage(int index, NativeImage *pLUTImg);

    /**
     * @brief 加载Java层着色器脚本
     * @param index 着色器索引
     * @param pShaderStr 着色器字符串
     * @param strSize 字符串大小
     */
    void SetFragShaderStr(int index, char *pShaderStr, int strSize);

private:
    /**
     * @brief 私有构造函数（单例模式）
     */
    GLCameraRender();

    /**
     * @brief 虚析构函数
     */
    virtual ~GLCameraRender();

    /**
     * @brief 创建帧缓冲对象
     * @return 创建成功返回true
     */
    bool CreateFrameBufferObj();

    /** FBO 纹理尺寸变化时才重新分配存储（原来每帧都 glTexImage2D 重新分配） */
    void UpdateFboTextureSize(int width, int height);

    /** 上传相机图像到纹理（调用方持有 m_Mutex） */
    void UploadTextures();

    /** 根据变换参数计算 MVP 矩阵（GL 线程调用） */
    void ApplyTransformMatrix(const TransformMatrix &transform);

    /**
     * @brief 从FBO获取渲染帧
     * 读取FBO中的像素数据用于编码
     */
    void GetRenderFrameFromFBO(int width, int height, int64_t captureTimeUs);

    /** 把目标 FBO 的画面画到编码器 Surface 并送出一帧（GL 线程，仅新画面） */
    void RenderToEncoder(bool isNewFrame, int64_t captureTimeUs);

    /** 调用方持有 m_EncoderMutex */
    void DetachEncoderLocked();

    /**
     * @brief 创建或更新滤镜素材纹理
     * 用于LUT滤镜等需要额外纹理的效果
     */
    void UpdateExtTexture();

private:
    static std::mutex m_Mutex;                      // 单例保护锁
    static GLCameraRender* s_Instance;              // 单例实例

    GLuint m_ProgramObj = GL_NONE;                  // 着色器程序对象
    GLuint m_FboProgramObj = GL_NONE;              // FBO着色器程序对象
    GLuint m_TextureIds[TEXTURE_NUM] = {0};        // 纹理ID数组，用于存储YUV纹理
    GLuint m_VaoId = GL_NONE;                      // VAO ID（顶点数组对象）
    GLuint m_VboIds[3] = {0};                      // VBO ID数组（顶点缓冲对象）
    int    m_FboWidth = 0;                         // FBO 纹理当前尺寸
    int    m_FboHeight = 0;
    std::vector<uint8_t> m_ReadbackBuffer;         // 回读缓冲，复用，避免每帧 new/delete
    std::atomic<bool> m_ReadbackEnabled{false};    // 是否回读画面给录制模块
    std::mutex m_CallbackMutex;                    // 保护回调函数及其上下文
    GLuint m_SrcFboTextureId = GL_NONE;            // 源FBO纹理ID，用于离屏渲染
    GLuint m_SrcFboId = GL_NONE;                   // 源FBO ID
    GLuint m_DstFboTextureId = GL_NONE;            // 目标FBO纹理ID
    GLuint m_DstFboId = GL_NONE;                   // 目标FBO ID

    NativeImage m_RenderImage;                      // 渲染图像数据缓冲
    int64_t m_RenderImageTimeUs = 0;                // m_RenderImage 的采集时刻（受 m_Mutex 保护）

    // ---- 硬件编码器输入 Surface（均受 m_EncoderMutex 保护）----
    // GL 线程画编码帧的整个过程都持有这把锁，所以别的线程拿到锁时，编码器的 EGLSurface 一定不是 current，
    // 可以直接在本线程创建/销毁（EGL 允许跨线程操作非 current 的 Surface）
    std::mutex m_EncoderMutex;
    EGLDisplay m_EglDisplay = EGL_NO_DISPLAY;       // GL 上下文所在的 display（OnSurfaceCreated 时记录）
    EGLConfig m_EglConfig = nullptr;                // GL 上下文的 config，编码器 Surface 必须与之兼容
    EGLSurface m_EncoderSurface = EGL_NO_SURFACE;
    ANativeWindow *m_EncoderWindow = nullptr;
    int m_EncoderWidth = 0;
    int m_EncoderHeight = 0;
    VideoEncoderSink *m_EncoderSink = nullptr;
    bool m_EncoderErrorLogged = false;
    bool m_HasNewFrame = false;                     // 是否有尚未回读给录制模块的新画面（受 m_Mutex 保护）
    glm::mat4 m_MVPMatrix;                         // MVP变换矩阵（模型-视图-投影）
    TransformMatrix m_transformMatrix;              // 变换矩阵

    int m_FrameIndex = 0;                          // 帧索引，用于时间相关的特效
    vec2 m_TouchXY;                                // 触摸坐标（归一化）
    vec2 m_ScreenSize;                             // 屏幕尺寸

    OnRenderFrameCallback m_RenderFrameCallback = nullptr;  // 渲染帧回调函数
    void *m_CallbackContext = nullptr;                     // 回调上下文

    // 支持滑动选择滤镜功能
    std::atomic<bool> m_IsShaderChanged{false};     // 着色器变更标志
    bool m_ExtImageChanged = false;                 // 外部图像变更标志（受 m_Mutex 保护）
    char * m_pFragShaderBuffer = nullptr;           // 片段着色器缓冲区
    NativeImage m_ExtImage;                         // 外部图像（LUT纹理等）
    GLuint m_ExtTextureId = GL_NONE;               // 外部纹理ID
    std::atomic<int> m_ShaderIndex{0};             // 当前着色器索引
    mutex m_ShaderMutex;                           // 着色器互斥锁，保护着色器切换

};


#endif //LEARNFFMPEG_MASTER_GLCAMERARENDER_H
