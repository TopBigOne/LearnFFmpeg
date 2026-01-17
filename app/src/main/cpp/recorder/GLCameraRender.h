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
#include "VideoRender.h"
#include <GLES3/gl3.h>
#include <detail/type_mat.hpp>
#include <detail/type_mat4x4.hpp>
#include <vec2.hpp>
#include <render/BaseGLRender.h>
#include <vector>
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
typedef void (*OnRenderFrameCallback)(void*, NativeImage*);

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
        m_CallbackContext = ctx;
        m_RenderFrameCallback = callback;
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

    /**
     * @brief 从FBO获取渲染帧
     * 读取FBO中的像素数据用于编码
     */
    void GetRenderFrameFromFBO();

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
    GLuint m_TextureIds[TEXTURE_NUM];              // 纹理ID数组，用于存储YUV纹理
    GLuint m_VaoId = GL_NONE;                      // VAO ID（顶点数组对象）
    GLuint m_VboIds[3];                            // VBO ID数组（顶点缓冲对象）
    GLuint m_SrcFboTextureId = GL_NONE;            // 源FBO纹理ID，用于离屏渲染
    GLuint m_SrcFboId = GL_NONE;                   // 源FBO ID
    GLuint m_DstFboTextureId = GL_NONE;            // 目标FBO纹理ID
    GLuint m_DstFboId = GL_NONE;                   // 目标FBO ID

    NativeImage m_RenderImage;                      // 渲染图像数据缓冲
    glm::mat4 m_MVPMatrix;                         // MVP变换矩阵（模型-视图-投影）
    TransformMatrix m_transformMatrix;              // 变换矩阵

    int m_FrameIndex;                              // 帧索引，用于时间相关的特效
    vec2 m_TouchXY;                                // 触摸坐标（归一化）
    vec2 m_ScreenSize;                             // 屏幕尺寸

    OnRenderFrameCallback m_RenderFrameCallback = nullptr;  // 渲染帧回调函数
    void *m_CallbackContext = nullptr;                     // 回调上下文

    // 支持滑动选择滤镜功能
    volatile bool m_IsShaderChanged = false;        // 着色器变更标志
    volatile bool m_ExtImageChanged = false;        // 外部图像变更标志（用于LUT纹理更新）
    char * m_pFragShaderBuffer = nullptr;           // 片段着色器缓冲区
    NativeImage m_ExtImage;                         // 外部图像（LUT纹理等）
    GLuint m_ExtTextureId = GL_NONE;               // 外部纹理ID
    int m_ShaderIndex = 0;                         // 当前着色器索引
    mutex m_ShaderMutex;                           // 着色器互斥锁，保护着色器切换

};


#endif //LEARNFFMPEG_MASTER_GLCAMERARENDER_H
