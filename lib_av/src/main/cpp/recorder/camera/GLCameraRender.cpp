/**
 *
 * Created by 公众号：字节流动 on 2021/3/12.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#include "camera/GLCameraRender.h"
#include <GLUtils.h>
#include <gtc/matrix_transform.hpp>

GLCameraRender* GLCameraRender::s_Instance = nullptr;
std::mutex GLCameraRender::m_Mutex;

static char vShaderStr[] =
        "#version 300 es\n"
        "layout(location = 0) in vec4 a_position;\n"
        "layout(location = 1) in vec2 a_texCoord;\n"
        "uniform mat4 u_MVPMatrix;\n"
        "out vec2 v_texCoord;\n"
        "void main()\n"
        "{\n"
        "    gl_Position = u_MVPMatrix * a_position;\n"
        "    v_texCoord = a_texCoord;\n"
        "}";

static char fShaderStr[] =
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec2 v_texCoord;\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform sampler2D s_texture0;\n"
        "uniform sampler2D s_texture1;\n"
        "uniform sampler2D s_texture2;\n"
        "uniform int u_nImgType;// 1:RGBA, 2:NV21, 3:NV12, 4:I420\n"
        "\n"
        "void main()\n"
        "{\n"
        "\n"
        "    if(u_nImgType == 1) //RGBA\n"
        "    {\n"
        "        outColor = texture(s_texture0, v_texCoord);\n"
        "    }\n"
        "    else if(u_nImgType == 2) //NV21\n"
        "    {\n"
        "        vec3 yuv;\n"
        "        yuv.x = texture(s_texture0, v_texCoord).r;\n"
        "        yuv.y = texture(s_texture1, v_texCoord).a - 0.5;\n"
        "        yuv.z = texture(s_texture1, v_texCoord).r - 0.5;\n"
        "        highp vec3 rgb = mat3(1.0,       1.0,     1.0,\n"
        "        0.0, \t-0.344, \t1.770,\n"
        "        1.403,  -0.714,     0.0) * yuv;\n"
        "        outColor = vec4(rgb, 1.0);\n"
        "\n"
        "    }\n"
        "    else if(u_nImgType == 3) //NV12\n"
        "    {\n"
        "        vec3 yuv;\n"
        "        yuv.x = texture(s_texture0, v_texCoord).r;\n"
        "        yuv.y = texture(s_texture1, v_texCoord).r - 0.5;\n"
        "        yuv.z = texture(s_texture1, v_texCoord).a - 0.5;\n"
        "        highp vec3 rgb = mat3(1.0,       1.0,     1.0,\n"
        "        0.0, \t-0.344, \t1.770,\n"
        "        1.403,  -0.714,     0.0) * yuv;\n"
        "        outColor = vec4(rgb, 1.0);\n"
        "    }\n"
        "    else if(u_nImgType == 4) //I420\n"
        "    {\n"
        "        vec3 yuv;\n"
        "        yuv.x = texture(s_texture0, v_texCoord).r;\n"
        "        yuv.y = texture(s_texture1, v_texCoord).r - 0.5;\n"
        "        yuv.z = texture(s_texture2, v_texCoord).r - 0.5;\n"
        "        highp vec3 rgb = mat3(1.0,       1.0,     1.0,\n"
        "                              0.0, \t-0.344, \t1.770,\n"
        "                              1.403,  -0.714,     0.0) * yuv;\n"
        "        outColor = vec4(rgb, 1.0);\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        outColor = vec4(1.0);\n"
        "    }\n"
        "}";

static GLfloat verticesCoords[] = {
        -1.0f,  1.0f, 0.0f,  // Position 0
        -1.0f, -1.0f, 0.0f,  // Position 1
        1.0f,  -1.0f, 0.0f,  // Position 2
        1.0f,   1.0f, 0.0f,  // Position 3
};

//static GLfloat textureCoords[] = {
//        0.0f,  1.0f,        // TexCoord 0
//        0.0f,  0.0f,        // TexCoord 1
//        1.0f,  0.0f,        // TexCoord 2
//        1.0f,  1.0f         // TexCoord 3
//};
static GLfloat textureCoords[] = {
        0.0f,  0.0f,        // TexCoord 0
        0.0f,  1.0f,        // TexCoord 1
        1.0f,  1.0f,        // TexCoord 2
        1.0f,  0.0f         // TexCoord 3
};

static GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

/**
 * @brief 构造函数
 * 初始化GLCameraRender实例
 */
GLCameraRender::GLCameraRender():VideoRender(VIDEO_RENDER_OPENGL) {

}

/**
 * @brief 析构函数
 * 释放渲染图像资源
 */
GLCameraRender::~GLCameraRender() {
    DetachEncoder();
    NativeImageUtil::FreeNativeImage(&m_RenderImage);

}

/**
 * @brief 初始化渲染器
 * @param videoWidth 视频宽度
 * @param videoHeight 视频高度
 * @param dstSize 输出尺寸数组（可选）
 *
 * 设置视频尺寸并初始化MVP矩阵
 */
void GLCameraRender::Init(int videoWidth, int videoHeight, int *dstSize) {
    LOGCATE("GLCameraRender::InitRender video[w, h]=[%d, %d]", videoWidth, videoHeight);
    if(dstSize != nullptr) {
        dstSize[0] = videoWidth;
        dstSize[1] = videoHeight;
    }
    m_FrameIndex = 0;
    UpdateMVPMatrix(0, 0, 1.0f, 1.0f);
}

/**
 * @brief 接收视频帧用于渲染
 * @param pImage 输入的视频帧
 *
 * 将输入的视频帧复制到内部缓冲区
 * 如果尺寸变化，重新分配缓冲区
 */
void GLCameraRender::RenderVideoFrame(NativeImage *pImage) {
    RenderVideoFrame(pImage, 0);
}

void GLCameraRender::RenderVideoFrame(NativeImage *pImage, int64_t captureTimeUs) {
    LOGCATV("GLCameraRender::RenderVideoFrame pImage=%p, captureTimeUs=%lld", pImage, (long long) captureTimeUs);
    if(pImage == nullptr || pImage->ppPlane[0] == nullptr)
        return;
    std::unique_lock<std::mutex> lock(m_Mutex);
    // 检测尺寸或格式变化，重新分配缓冲区
    if (pImage->width != m_RenderImage.width || pImage->height != m_RenderImage.height
        || pImage->format != m_RenderImage.format) {
        if (m_RenderImage.ppPlane[0] != nullptr) {
            NativeImageUtil::FreeNativeImage(&m_RenderImage);
        }
        memset(&m_RenderImage, 0, sizeof(NativeImage));
        m_RenderImage.format = pImage->format;
        m_RenderImage.width = pImage->width;
        m_RenderImage.height = pImage->height;
        NativeImageUtil::AllocNativeImage(&m_RenderImage);
    }

    NativeImageUtil::CopyNativeImage(pImage, &m_RenderImage);
    // 采集时刻跟着画面一起保存，回读给录制模块时原样带出去
    m_RenderImageTimeUs = captureTimeUs;
    m_HasNewFrame = true;
    //NativeImageUtil::DumpNativeImage(&m_RenderImage, "/sdcard", "camera");
}

void GLCameraRender::RenderAndroid420Frame(const Android420Planes &planes, int64_t captureTimeUs) {
    if(planes.y == nullptr || planes.width <= 0 || planes.height <= 0) return;
    std::unique_lock<std::mutex> lock(m_Mutex);
    // 渲染缓冲统一为 I420，尺寸或格式变化时重新分配
    if (m_RenderImage.width != planes.width || m_RenderImage.height != planes.height
        || m_RenderImage.format != IMAGE_FORMAT_I420 || m_RenderImage.ppPlane[0] == nullptr) {
        NativeImageUtil::FreeNativeImage(&m_RenderImage);
        memset(&m_RenderImage, 0, sizeof(NativeImage));
        m_RenderImage.format = IMAGE_FORMAT_I420;
        m_RenderImage.width = planes.width;
        m_RenderImage.height = planes.height;
        NativeImageUtil::AllocNativeImage(&m_RenderImage);
        if (m_RenderImage.ppPlane[0] == nullptr) return;
    }

    if (!YuvConvert::Android420ToI420(planes,
                                      m_RenderImage.ppPlane[0], m_RenderImage.pLineSize[0],
                                      m_RenderImage.ppPlane[1], m_RenderImage.pLineSize[1],
                                      m_RenderImage.ppPlane[2], m_RenderImage.pLineSize[2])) {
        return;
    }
    m_RenderImageTimeUs = captureTimeUs;
    m_HasNewFrame = true;
}

/**
 * @brief 反初始化渲染器
 * 释放扩展图像和着色器缓冲区资源
 */
void GLCameraRender::UnInit() {
    DetachEncoder();
    {
        // 图像数据会被 GL 线程读取，在锁内释放（原来不加锁）
        std::lock_guard<std::mutex> lock(m_Mutex);
        NativeImageUtil::FreeNativeImage(&m_ExtImage);
        memset(&m_ExtImage, 0, sizeof(NativeImage));
        NativeImageUtil::FreeNativeImage(&m_RenderImage);
        memset(&m_RenderImage, 0, sizeof(NativeImage));
        m_RenderImageTimeUs = 0;
        m_HasNewFrame = false;
        m_ExtImageChanged = false;
    }

    {
        std::lock_guard<std::mutex> lock(m_ShaderMutex);
        if(m_pFragShaderBuffer != nullptr) {
            free(m_pFragShaderBuffer);
            m_pFragShaderBuffer = nullptr;
        }
        m_IsShaderChanged = false;
        m_ShaderIndex = 0;
    }
    m_ReadbackBuffer.clear();
    m_ReadbackBuffer.shrink_to_fit();
}

/**
 * @brief 更新MVP矩阵（简化版本）
 * @param angleX X轴旋转角度
 * @param angleY Y轴旋转角度
 * @param scaleX X轴缩放比例
 * @param scaleY Y轴缩放比例
 *
 * 根据旋转角度和缩放参数计算MVP变换矩阵
 */
void GLCameraRender::UpdateMVPMatrix(int angleX, int angleY, float scaleX, float scaleY)
{
    angleX = angleX % 360;
    angleY = angleY % 360;

    //转化为弧度角
    float radiansX = static_cast<float>(MATH_PI / 180.0f * angleX);
    float radiansY = static_cast<float>(MATH_PI / 180.0f * angleY);
    // Projection matrix - 投影矩阵（正交投影）
    glm::mat4 Projection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f);
    //glm::mat4 Projection = glm::frustum(-ratio, ratio, -1.0f, 1.0f, 4.0f, 100.0f);
    //glm::mat4 Projection = glm::perspective(45.0f,ratio, 0.1f,100.f);

    // View matrix - 视图矩阵
    glm::mat4 View = glm::lookAt(
            glm::vec3(0, 0, 4), // Camera is at (0,0,1), in World Space
            glm::vec3(0, 0, 0), // and looks at the origin
            glm::vec3(0, 1, 0)  // Head is up (set to 0,-1,0 to look upside-down)
    );

    // Model matrix - 模型矩阵（缩放、旋转、平移）
    glm::mat4 Model = glm::mat4(1.0f);
    Model = glm::scale(Model, glm::vec3(scaleX, scaleY, 1.0f));
    Model = glm::rotate(Model, radiansX, glm::vec3(1.0f, 0.0f, 0.0f));
    Model = glm::rotate(Model, radiansY, glm::vec3(0.0f, 1.0f, 0.0f));
    Model = glm::translate(Model, glm::vec3(0.0f, 0.0f, 0.0f));

    m_MVPMatrix = Projection * View * Model;

}

/**
 * @brief 更新MVP矩阵（完整版本）
 * @param pTransformMatrix 变换矩阵参数（包含旋转、缩放、平移、镜像等）
 *
 * 根据完整的变换参数计算MVP矩阵，支持镜像和任意角度旋转
 */
void GLCameraRender::UpdateMVPMatrix(TransformMatrix *pTransformMatrix) {
    // JNI 线程调用：只保存参数（加锁），矩阵在 GL 线程绘制时计算，避免两个线程同时读写
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_transformMatrix = *pTransformMatrix;
}

void GLCameraRender::ApplyTransformMatrix(const TransformMatrix &transform) {
    const TransformMatrix *pTransformMatrix = &transform;

    //转化为弧度角
    float radiansX = static_cast<float>(MATH_PI / 180.0f * pTransformMatrix->angleX);
    float radiansY = static_cast<float>(MATH_PI / 180.0f * pTransformMatrix->angleY);

    // 根据镜像参数设置缩放因子
    float fFactorX = 1.0f;
    float fFactorY = 1.0f;

    if (pTransformMatrix->mirror == 1) {
        fFactorX = -1.0f;  // 水平镜像
    } else if (pTransformMatrix->mirror == 2) {
        fFactorY = -1.0f;  // 垂直镜像
    }

    // 计算旋转角度（弧度），针对不同镜像模式调整旋转方向
    float fRotate = MATH_PI * pTransformMatrix->degree * 1.0f / 180;
    if (pTransformMatrix->mirror == 0) {
        if (pTransformMatrix->degree == 270) {
            fRotate = MATH_PI * 0.5;
        } else if (pTransformMatrix->degree == 180) {
            fRotate = MATH_PI;
        } else if (pTransformMatrix->degree == 90) {
            fRotate = MATH_PI * 1.5;
        }
    } else if (pTransformMatrix->mirror == 1) {
        if (pTransformMatrix->degree == 90) {
            fRotate = MATH_PI * 0.5;
        } else if (pTransformMatrix->degree == 180) {
            fRotate = MATH_PI;
        } else if (pTransformMatrix->degree == 270) {
            fRotate = MATH_PI * 1.5;
        }
    }

    // 投影矩阵
    glm::mat4 Projection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f);
    // 视图矩阵
    glm::mat4 View = glm::lookAt(
            glm::vec3(0, 0, 1), // Camera is at (0,0,1), in World Space
            glm::vec3(0, 0, 0), // and looks at the origin
            glm::vec3(0, 1, 0) // Head is up (set to 0,-1,0 to look upside-down)
    );

    // 模型矩阵：应用缩放、旋转和平移变换
    glm::mat4 Model = glm::mat4(1.0f);
    Model = glm::scale(Model, glm::vec3(fFactorX * pTransformMatrix->scaleX,
                                        fFactorY * pTransformMatrix->scaleY, 1.0f));
    Model = glm::rotate(Model, fRotate, glm::vec3(0.0f, 0.0f, 1.0f));  // Z轴旋转
    Model = glm::rotate(Model, radiansX, glm::vec3(1.0f, 0.0f, 0.0f));  // X轴旋转
    Model = glm::rotate(Model, radiansY, glm::vec3(0.0f, 1.0f, 0.0f));  // Y轴旋转
    Model = glm::translate(Model,
                           glm::vec3(pTransformMatrix->translateX, pTransformMatrix->translateY, 0.0f));

    LOGCATE("GLCameraRender::UpdateMVPMatrix rotate %d,%.2f,%0.5f,%0.5f,%0.5f,%0.5f,", pTransformMatrix->degree, fRotate,
            pTransformMatrix->translateX, pTransformMatrix->translateY,
            fFactorX * pTransformMatrix->scaleX, fFactorY * pTransformMatrix->scaleY);

    m_MVPMatrix = Projection * View * Model;
}

/**
 * @brief OpenGL Surface创建时回调
 * 初始化OpenGL程序、纹理、VBO、VAO等资源
 */
void GLCameraRender::OnSurfaceCreated() {
    LOGCATE("GLCameraRender::OnSurfaceCreated");

    // 单例会跨越多个 GLSurfaceView 存活，每次 OnSurfaceCreated 都是新的 EGL 上下文：
    // 旧上下文的对象已随上下文销毁，ID 必须清零，否则 FBO 等不会重建，画面黑屏
    m_SrcFboId = m_DstFboId = GL_NONE;
    m_SrcFboTextureId = m_DstFboTextureId = GL_NONE;
    m_ExtTextureId = GL_NONE;
    m_FboWidth = m_FboHeight = 0;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ExtImageChanged = m_ExtImage.ppPlane[0] != nullptr;   // LUT 纹理需要重新上传
    }
    {
        // 记录当前上下文的 display 和 config：硬件编码器的输入 Surface 要用同一个 config 创建，
        // 才能和这个上下文一起 make current。GLSurfaceView 已选择带 EGL_RECORDABLE_ANDROID 的 config
        std::lock_guard<std::mutex> lock(m_EncoderMutex);
        m_EglDisplay = eglGetCurrentDisplay();
        m_EglConfig = nullptr;
        EGLContext context = eglGetCurrentContext();
        EGLint configId = 0;
        if (m_EglDisplay != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT
            && eglQueryContext(m_EglDisplay, context, EGL_CONFIG_ID, &configId)) {
            EGLint attribs[] = {EGL_CONFIG_ID, configId, EGL_NONE};
            EGLint count = 0;
            if (!eglChooseConfig(m_EglDisplay, attribs, &m_EglConfig, 1, &count) || count < 1) {
                m_EglConfig = nullptr;
            }
        }
        EGLint recordable = 0;
        if (m_EglConfig != nullptr) eglGetConfigAttrib(m_EglDisplay, m_EglConfig, EGL_RECORDABLE_ANDROID, &recordable);
        LOGCATE("GLCameraRender::OnSurfaceCreated EGL configId=%d recordable=%d", configId, recordable);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);   // 色度平面行宽不一定是 4 的倍数
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // 创建两个着色器程序：一个用于屏幕显示，一个用于FBO离屏渲染
    m_ProgramObj = GLUtils::CreateProgram(vShaderStr, fShaderStr);
    m_FboProgramObj = GLUtils::CreateProgram(vShaderStr, fShaderStr);
    {
        // 之前选过滤镜：在新的上下文里重新编译滤镜程序
        std::lock_guard<std::mutex> lock(m_ShaderMutex);
        if(m_pFragShaderBuffer != nullptr) m_IsShaderChanged = true;
    }
    if (!m_ProgramObj || !m_FboProgramObj)
    {
        LOGCATE("GLCameraRender::OnSurfaceCreated create program fail");
        return;
    }

    // 创建并配置纹理（用于YUV各个平面）
    glGenTextures(TEXTURE_NUM, m_TextureIds);
    for (int i = 0; i < TEXTURE_NUM ; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, m_TextureIds[i]);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, GL_NONE);
    }

    // 创建VBO并加载顶点数据
    glGenBuffers(3, m_VboIds);
    glBindBuffer(GL_ARRAY_BUFFER, m_VboIds[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verticesCoords), verticesCoords, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, m_VboIds[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoords), textureCoords, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_VboIds[2]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // 创建VAO并绑定顶点属性
    glGenVertexArrays(1, &m_VaoId);
    glBindVertexArray(m_VaoId);

    glBindBuffer(GL_ARRAY_BUFFER, m_VboIds[0]);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (const void *)0);
    glBindBuffer(GL_ARRAY_BUFFER, GL_NONE);

    glBindBuffer(GL_ARRAY_BUFFER, m_VboIds[1]);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (const void *)0);
    glBindBuffer(GL_ARRAY_BUFFER, GL_NONE);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_VboIds[2]);

    glBindVertexArray(GL_NONE);

    m_TouchXY = vec2(0.5f, 0.5f);
}

/**
 * @brief Surface尺寸变化时回调
 * @param w 新的宽度
 * @param h 新的高度
 *
 * 更新视口尺寸和清屏颜色
 */
void GLCameraRender::OnSurfaceChanged(int w, int h) {
    LOGCATE("GLCameraRender::OnSurfaceChanged [w, h]=[%d, %d]", w, h);
    m_ScreenSize.x = w;
    m_ScreenSize.y = h;
    glViewport(0, 0, w, h);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
}

/**
 * @brief 绘制一帧
 *
 * 核心渲染函数，分三个阶段：
 * 1. 渲染到FBO1（应用滤镜效果）
 * 2. 渲染到FBO2（调整方向）
 * 3. 渲染到屏幕
 */
static void UploadPlane(GLuint texId, GLenum unit, GLenum format, int w, int h, const void *data) {
    glActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, GL_NONE);
}

void GLCameraRender::UploadTextures() {
    int w = m_RenderImage.width;
    int h = m_RenderImage.height;
    switch (m_RenderImage.format)
    {
        case IMAGE_FORMAT_RGBA:
            UploadPlane(m_TextureIds[0], GL_TEXTURE0, GL_RGBA, w, h, m_RenderImage.ppPlane[0]);
            break;
        case IMAGE_FORMAT_NV21:
        case IMAGE_FORMAT_NV12:
            //upload Y plane data
            UploadPlane(m_TextureIds[0], GL_TEXTURE0, GL_LUMINANCE, w, h, m_RenderImage.ppPlane[0]);
            //update UV plane data
            UploadPlane(m_TextureIds[1], GL_TEXTURE1, GL_LUMINANCE_ALPHA, w >> 1, h >> 1, m_RenderImage.ppPlane[1]);
            break;
        case IMAGE_FORMAT_I420:
            //upload Y plane data
            UploadPlane(m_TextureIds[0], GL_TEXTURE0, GL_LUMINANCE, w, h, m_RenderImage.ppPlane[0]);
            //update U plane data
            UploadPlane(m_TextureIds[1], GL_TEXTURE1, GL_LUMINANCE, w >> 1, h >> 1, m_RenderImage.ppPlane[1]);
            //update V plane data
            UploadPlane(m_TextureIds[2], GL_TEXTURE2, GL_LUMINANCE, w >> 1, h >> 1, m_RenderImage.ppPlane[2]);
            break;
        default:
            break;
    }
}

void GLCameraRender::OnDrawFrame() {
    // 如果着色器发生变化，重新创建FBO程序
    if(m_IsShaderChanged) {
        unique_lock<mutex> lock(m_ShaderMutex);
        if(m_pFragShaderBuffer != nullptr) {
            GLUtils::DeleteProgram(m_FboProgramObj);
            m_FboProgramObj = GLUtils::CreateProgram(vShaderStr, m_pFragShaderBuffer);
        }
        m_IsShaderChanged = false;
    }

    glClear(GL_COLOR_BUFFER_BIT);
    if(m_ProgramObj == GL_NONE || m_FboProgramObj == GL_NONE) return;

    // ---- 锁内：读取共享数据、上传纹理（这些数据会被相机线程/JNI 线程改写）----
    std::unique_lock<std::mutex> lock(m_Mutex);
    if(m_RenderImage.ppPlane[0] == nullptr) return;
    int imageWidth = m_RenderImage.width;
    int imageHeight = m_RenderImage.height;
    // 相机图像是横向的，经过旋转后输出尺寸宽高互换
    int outWidth = imageHeight;
    int outHeight = imageWidth;

    // 原来写成 `== GL_NONE && CreateFrameBufferObj()`：创建成功反而 return，失败却继续画，条件反了
    if(m_SrcFboId == GL_NONE && !CreateFrameBufferObj()) {
        LOGCATE("GLCameraRender::OnDrawFrame CreateFrameBufferObj fail");
        return;
    }
    // FBO 纹理只在尺寸变化时重新分配
    UpdateFboTextureSize(outWidth, outHeight);

    LOGCATV("GLCameraRender::OnDrawFrame [w, h]=[%d, %d], format=%d", imageWidth, imageHeight, m_RenderImage.format);
    m_FrameIndex++;

    // 更新扩展纹理（如LUT滤镜纹理）：在锁内进行，SetLUTImage 可能同时在释放/重新分配它（原来在锁外，会读到已释放的内存）
    UpdateExtTexture();
    int extWidth = m_ExtImage.width;
    int extHeight = m_ExtImage.height;

    // 上传相机图像到纹理
    UploadTextures();
    int imageFormat = m_RenderImage.format;
    TransformMatrix transform = m_transformMatrix;
    // 同一帧相机画面可能被重绘多次（Surface 变化、切换滤镜等都会触发 onDrawFrame），
    // 只有新画面才回读给录制模块，否则同一画面会以相同采集时刻被编码两次
    bool isNewFrame = m_HasNewFrame;
    int64_t captureTimeUs = m_RenderImageTimeUs;
    m_HasNewFrame = false;
    lock.unlock();
    // ---- 以下只使用局部副本和 GL 对象，不再持有锁 ----

    // 第一步：渲染到 FBO（应用滤镜效果）
    glBindFramebuffer(GL_FRAMEBUFFER, m_SrcFboId);
    glViewport(0, 0, outWidth, outHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_FboProgramObj);

    glBindVertexArray(m_VaoId);
    ApplyTransformMatrix(transform);
    GLUtils::setMat4(m_FboProgramObj, "u_MVPMatrix", m_MVPMatrix);
    for (int i = 0; i < TEXTURE_NUM; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, m_TextureIds[i]);
        char samplerName[64] = {0};
        sprintf(samplerName, "s_texture%d", i);
        GLUtils::setInt(m_FboProgramObj, samplerName, i);
    }
    float offset = (sin(m_FrameIndex * MATH_PI / 40) + 1.0f) / 2.0f;
    GLUtils::setFloat(m_FboProgramObj, "u_Offset", offset);
    GLUtils::setVec2(m_FboProgramObj, "u_TexSize", vec2(imageWidth, imageHeight));
    GLUtils::setInt(m_FboProgramObj, "u_nImgType", imageFormat);

    switch (m_ShaderIndex.load()) {
        case SHADER_INDEX_ORIGIN:
            break;
        case SHADER_INDEX_DMESH:
            break;
        case SHADER_INDEX_GHOST:
            offset = m_FrameIndex % 60 / 60.0f - 0.2f;
            if(offset < 0) offset = 0;
            GLUtils::setFloat(m_FboProgramObj, "u_Offset", offset);
            break;
        case SHADER_INDEX_CIRCLE:
            break;
        case SHADER_INDEX_ASCII:
            glActiveTexture(GL_TEXTURE0 + TEXTURE_NUM);
            glBindTexture(GL_TEXTURE_2D, m_ExtTextureId);
            GLUtils::setInt(m_FboProgramObj, "s_textureMapping", TEXTURE_NUM);
            GLUtils::setVec2(m_FboProgramObj, "asciiTexSize", vec2(extWidth, extHeight));
            break;
        case SHADER_INDEX_LUT_A:
        case SHADER_INDEX_LUT_B:
        case SHADER_INDEX_LUT_C:
            glActiveTexture(GL_TEXTURE0 + TEXTURE_NUM);
            glBindTexture(GL_TEXTURE_2D, m_ExtTextureId);
            GLUtils::setInt(m_FboProgramObj, "s_LutTexture", TEXTURE_NUM);
            break;
        case SHADER_INDEX_NE:
            offset = (sin(m_FrameIndex * MATH_PI / 60) + 1.0f) / 2.0f;
            GLUtils::setFloat(m_FboProgramObj, "u_Offset", offset);
            break;
        default:
            break;
    }

    // 绘制到第一个FBO
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void *)0);

    // 第二步：再绘制一次到第二个FBO，调整方向
    glBindFramebuffer(GL_FRAMEBUFFER, m_DstFboId);
    glViewport(0, 0, outWidth, outHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram (m_ProgramObj);
    glBindVertexArray(m_VaoId);

    UpdateMVPMatrix(0, 0, 1.0, 1.0);
    GLUtils::setMat4(m_ProgramObj, "u_MVPMatrix", m_MVPMatrix);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_SrcFboTextureId);
    GLUtils::setInt(m_ProgramObj, "s_texture0", 0);
    GLUtils::setInt(m_ProgramObj, "u_nImgType", IMAGE_FORMAT_RGBA);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void *)0);

    // 从FBO读取渲染结果，用于录制（仅录制时）。此时不持有图像锁，录制模块的回调不会卡住相机线程
    if (isNewFrame) GetRenderFrameFromFBO(outWidth, outHeight, captureTimeUs);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // 硬件编码：把同一个画面直接画到编码器的输入 Surface（不回读、不转 YUV）
    RenderToEncoder(isNewFrame, captureTimeUs);

    // 第三步：渲染到屏幕
    glViewport(0, 0, m_ScreenSize.x, m_ScreenSize.y);
    glClear(GL_COLOR_BUFFER_BIT);

    GLUtils::setMat4(m_ProgramObj, "u_MVPMatrix", m_MVPMatrix);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_DstFboTextureId);
    GLUtils::setInt(m_ProgramObj, "s_texture0", 0);

    GLUtils::setInt(m_ProgramObj, "u_nImgType", IMAGE_FORMAT_RGBA);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void *)0);
}

bool GLCameraRender::AttachEncoder(ANativeWindow *window, int width, int height, VideoEncoderSink *sink) {
    std::lock_guard<std::mutex> lock(m_EncoderMutex);
    if (window == nullptr || sink == nullptr || width <= 0 || height <= 0) return false;
    if (m_EncoderSurface != EGL_NO_SURFACE) {
        LOGCATE("GLCameraRender::AttachEncoder another encoder is attached");
        return false;
    }
    if (m_EglDisplay == EGL_NO_DISPLAY || m_EglConfig == nullptr) {
        LOGCATE("GLCameraRender::AttachEncoder GL context not ready");
        return false;
    }
    EGLint attribs[] = {EGL_NONE};
    EGLSurface surface = eglCreateWindowSurface(m_EglDisplay, m_EglConfig, window, attribs);
    if (surface == EGL_NO_SURFACE) {
        LOGCATE("GLCameraRender::AttachEncoder eglCreateWindowSurface fail, error=0x%x", eglGetError());
        return false;
    }
    ANativeWindow_acquire(window);
    m_EncoderWindow = window;
    m_EncoderSurface = surface;
    m_EncoderWidth = width;
    m_EncoderHeight = height;
    m_EncoderSink = sink;
    m_EncoderErrorLogged = false;
    LOGCATE("GLCameraRender::AttachEncoder [w,h]=[%d,%d]", width, height);
    return true;
}

void GLCameraRender::DetachEncoder(VideoEncoderSink *sink) {
    std::lock_guard<std::mutex> lock(m_EncoderMutex);
    if (sink != nullptr && sink != m_EncoderSink) return;
    DetachEncoderLocked();
}

void GLCameraRender::DetachEncoderLocked() {
    if (m_EncoderSurface != EGL_NO_SURFACE) {
        eglDestroySurface(m_EglDisplay, m_EncoderSurface);
        m_EncoderSurface = EGL_NO_SURFACE;
    }
    if (m_EncoderWindow != nullptr) {
        ANativeWindow_release(m_EncoderWindow);
        m_EncoderWindow = nullptr;
    }
    m_EncoderSink = nullptr;
}

void GLCameraRender::RenderToEncoder(bool isNewFrame, int64_t captureTimeUs) {
    std::lock_guard<std::mutex> lock(m_EncoderMutex);
    if (!isNewFrame || m_EncoderSurface == EGL_NO_SURFACE || m_EncoderSink == nullptr) return;
    int64_t ptsUs = m_EncoderSink->NextVideoPtsUs(captureTimeUs);
    if (ptsUs < 0) return;   // 录制开始前采集的画面

    EGLDisplay display = eglGetCurrentDisplay();
    EGLContext context = eglGetCurrentContext();
    EGLSurface drawSurface = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface readSurface = eglGetCurrentSurface(EGL_READ);
    if (display != m_EglDisplay || context == EGL_NO_CONTEXT) return;
    if (!eglMakeCurrent(display, m_EncoderSurface, m_EncoderSurface, context)) {
        if (!m_EncoderErrorLogged) {
            m_EncoderErrorLogged = true;
            LOGCATE("GLCameraRender::RenderToEncoder eglMakeCurrent fail, error=0x%x", eglGetError());
        }
        return;
    }

    // 与上屏（第三步）用同一个程序和纹理：编码画面与预览一致
    glViewport(0, 0, m_EncoderWidth, m_EncoderHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_ProgramObj);
    glBindVertexArray(m_VaoId);
    GLUtils::setMat4(m_ProgramObj, "u_MVPMatrix", m_MVPMatrix);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_DstFboTextureId);
    GLUtils::setInt(m_ProgramObj, "s_texture0", 0);
    GLUtils::setInt(m_ProgramObj, "u_nImgType", IMAGE_FORMAT_RGBA);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void *)0);

    // 这一帧在码流里的时间戳：编码器输出的 presentationTimeUs 就是这里给的值
    static PFNEGLPRESENTATIONTIMEANDROIDPROC presentationTime =
            reinterpret_cast<PFNEGLPRESENTATIONTIMEANDROIDPROC>(eglGetProcAddress("eglPresentationTimeANDROID"));
    if (presentationTime != nullptr) {
        presentationTime(display, m_EncoderSurface, static_cast<EGLnsecsANDROID>(ptsUs) * 1000);
    }
    // 送出一帧。编码器来不及处理时这里会阻塞，GL 线程（预览）随之降速，由编码器自然限流
    if (!eglSwapBuffers(display, m_EncoderSurface) && !m_EncoderErrorLogged) {
        m_EncoderErrorLogged = true;
        LOGCATE("GLCameraRender::RenderToEncoder eglSwapBuffers fail, error=0x%x", eglGetError());
    }
    eglMakeCurrent(display, drawSurface, readSurface, context);
}

/**
 * @brief 获取单例实例
 * @return GLCameraRender单例指针
 *
 * 线程安全的单例模式实现（双重检查锁定）
 */
GLCameraRender *GLCameraRender::GetInstance() {
    // 每次都加锁：无锁的"双重检查"在 C++ 内存模型下是数据竞争
    std::lock_guard<std::mutex> lock(m_Mutex);
    if(s_Instance == nullptr)
    {
        s_Instance = new GLCameraRender();
    }
    return s_Instance;
}

/**
 * @brief 释放单例实例
 * 线程安全地删除单例对象
 */
void GLCameraRender::ReleaseInstance() {
    if(s_Instance != nullptr)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if(s_Instance != nullptr)
        {
            delete s_Instance;
            s_Instance = nullptr;
        }

    }
}

/**
 * @brief 创建帧缓冲对象（FBO）
 * @return true表示成功，false表示失败
 *
 * 创建两个FBO及其对应的纹理，用于离屏渲染
 */
bool GLCameraRender::CreateFrameBufferObj() {
    // 创建源FBO纹理
    if(m_SrcFboTextureId == GL_NONE) {
        glGenTextures(1, &m_SrcFboTextureId);
        glBindTexture(GL_TEXTURE_2D, m_SrcFboTextureId);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, GL_NONE);
    }

    if(m_DstFboTextureId == GL_NONE) {
        glGenTextures(1, &m_DstFboTextureId);
        glBindTexture(GL_TEXTURE_2D, m_DstFboTextureId);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, GL_NONE);
    }

    // 创建并初始化 FBO
    if(m_SrcFboId == GL_NONE) {
        glGenFramebuffers(1, &m_SrcFboId);
        glBindFramebuffer(GL_FRAMEBUFFER, m_SrcFboId);
        glBindTexture(GL_TEXTURE_2D, m_SrcFboTextureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_RenderImage.height, m_RenderImage.width, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_SrcFboTextureId, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!= GL_FRAMEBUFFER_COMPLETE) {
            LOGCATE("GLCameraRender::CreateFrameBufferObj glCheckFramebufferStatus status != GL_FRAMEBUFFER_COMPLETE");
            if(m_SrcFboTextureId != GL_NONE) {
                glDeleteTextures(1, &m_SrcFboTextureId);
                m_SrcFboTextureId = GL_NONE;
            }

            if(m_SrcFboId != GL_NONE) {
                glDeleteFramebuffers(1, &m_SrcFboId);
                m_SrcFboId = GL_NONE;
            }
            glBindTexture(GL_TEXTURE_2D, GL_NONE);
            glBindFramebuffer(GL_FRAMEBUFFER, GL_NONE);
            return false;
        }
    }

    if(m_DstFboId == GL_NONE) {
        glGenFramebuffers(1, &m_DstFboId);
        glBindFramebuffer(GL_FRAMEBUFFER, m_DstFboId);
        glBindTexture(GL_TEXTURE_2D, m_DstFboTextureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_RenderImage.height, m_RenderImage.width, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_DstFboTextureId, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER)!= GL_FRAMEBUFFER_COMPLETE) {
            LOGCATE("GLCameraRender::CreateFrameBufferObj glCheckFramebufferStatus status != GL_FRAMEBUFFER_COMPLETE");
            if(m_DstFboTextureId != GL_NONE) {
                glDeleteTextures(1, &m_DstFboTextureId);
                m_DstFboTextureId = GL_NONE;
            }

            if(m_DstFboId != GL_NONE) {
                glDeleteFramebuffers(1, &m_DstFboId);
                m_DstFboId = GL_NONE;
            }
            glBindTexture(GL_TEXTURE_2D, GL_NONE);
            glBindFramebuffer(GL_FRAMEBUFFER, GL_NONE);
            return false;
        }
    }

    glBindTexture(GL_TEXTURE_2D, GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, GL_NONE);
    m_FboWidth = m_RenderImage.height;
    m_FboHeight = m_RenderImage.width;
    return true;
}

void GLCameraRender::UpdateFboTextureSize(int width, int height) {
    if(width == m_FboWidth && height == m_FboHeight) return;
    glBindTexture(GL_TEXTURE_2D, m_SrcFboTextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, m_DstFboTextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, GL_NONE);
    m_FboWidth = width;
    m_FboHeight = height;
}

/**
 * @brief 从FBO读取渲染后的帧数据
 *
 * 使用glReadPixels从FBO读取RGBA数据，并通过回调函数传递给录制模块
 */
void GLCameraRender::GetRenderFrameFromFBO(int width, int height, int64_t captureTimeUs) {
    // 不录制时不回读：glReadPixels 会让 CPU 同步等待 GPU 完成渲染，非常耗时
    if(!m_ReadbackEnabled) return;

    std::lock_guard<std::mutex> lock(m_CallbackMutex);
    if(m_RenderFrameCallback == nullptr) return;

    // 复用回读缓冲，尺寸变化时才重新分配（原来每帧 new/delete 一块 w*h*4 的内存）
    size_t bufferSize = static_cast<size_t>(width) * height * 4;
    if(m_ReadbackBuffer.size() != bufferSize) m_ReadbackBuffer.resize(bufferSize);

    NativeImage nativeImage;
    nativeImage.format = IMAGE_FORMAT_RGBA;
    nativeImage.width = width;
    nativeImage.height = height;
    nativeImage.pLineSize[0] = width * 4;
    nativeImage.ppPlane[0] = m_ReadbackBuffer.data();
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nativeImage.ppPlane[0]);
    // 通过回调传递帧数据（录制模块会拷贝一份，回调返回后缓冲即可复用）
    m_RenderFrameCallback(m_CallbackContext, &nativeImage, captureTimeUs);
}

/**
 * @brief 设置片段着色器
 * @param index 着色器索引
 * @param pShaderStr 着色器源码
 * @param strSize 源码大小
 *
 * 动态更换片段着色器以实现不同的滤镜效果
 */
void GLCameraRender::SetFragShaderStr(int index, char *pShaderStr, int strSize) {
    LOGCATE("GLByteFlowRender::LoadFragShaderScript pShaderStr = %p, shaderIndex=%d", pShaderStr,
            index);
    if(m_ShaderIndex != index) {
        unique_lock<mutex> lock(m_ShaderMutex);
        // 释放旧的着色器缓冲区
        if(m_pFragShaderBuffer != nullptr) {
            free(m_pFragShaderBuffer);
            m_pFragShaderBuffer = nullptr;
        }
        // 复制新的着色器代码
        m_ShaderIndex = index;
        m_pFragShaderBuffer = static_cast<char *>(malloc(strSize));
        memcpy(m_pFragShaderBuffer, pShaderStr, strSize);
        m_IsShaderChanged = true;
    }

}

/**
 * @brief 设置LUT滤镜图像
 * @param index LUT索引
 * @param pLUTImg LUT图像数据
 *
 * 用于实现颜色查找表（LUT）滤镜效果
 */
void GLCameraRender::SetLUTImage(int index, NativeImage *pLUTImg) {
    LOGCATE("GLCameraRender::SetLUTImage pImage = %p, index=%d", pLUTImg->ppPlane[0],
            index);
    unique_lock<mutex> lock(m_Mutex);
    // 释放旧的扩展图像
    NativeImageUtil::FreeNativeImage(&m_ExtImage);
    // 复制新的LUT图像
    m_ExtImage.width = pLUTImg->width;
    m_ExtImage.height = pLUTImg->height;
    m_ExtImage.format = pLUTImg->format;
    NativeImageUtil::AllocNativeImage(&m_ExtImage);
    NativeImageUtil::CopyNativeImage(pLUTImg, &m_ExtImage);
    m_ExtImageChanged = true;
}

/**
 * @brief 更新扩展纹理
 *
 * 当LUT图像发生变化时，更新OpenGL纹理
 */
void GLCameraRender::UpdateExtTexture() {
    // 调用方已持有 m_Mutex
    if(m_ExtImageChanged && m_ExtImage.ppPlane[0] != nullptr) {
        // 删除旧纹理
        if(m_ExtTextureId != GL_NONE) {
            glDeleteTextures(1, &m_ExtTextureId);
        }
        // 创建新纹理并上传数据
        glGenTextures(1, &m_ExtTextureId);
        glBindTexture(GL_TEXTURE_2D, m_ExtTextureId);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_ExtImage.width, m_ExtImage.height, 0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE, m_ExtImage.ppPlane[0]);
        m_ExtImageChanged = false;
    }
}



