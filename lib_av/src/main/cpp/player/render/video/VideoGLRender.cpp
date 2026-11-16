/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#include "VideoGLRender.h"
#include <GLUtils.h>
#include <gtc/matrix_transform.hpp>

VideoGLRender* VideoGLRender::s_Instance = nullptr;
std::mutex VideoGLRender::m_Mutex;

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

static char fMeshShaderStr[] =
        "//dynimic mesh 动态网格\n"
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec2 v_texCoord;\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform sampler2D s_texture0;\n"
        "uniform sampler2D s_texture1;\n"
        "uniform sampler2D s_texture2;\n"
        "uniform int u_nImgType;// 1:RGBA, 2:NV21, 3:NV12, 4:I420\n"
        "uniform float u_Offset;\n"
        "uniform vec2 u_TexSize;\n"
        "\n"
        "vec4 sampleImage(vec2 texCoord) {\n"
        "    vec4 outColor;\n"
        "    if(u_nImgType == 1) //RGBA\n"
        "    {\n"
        "        outColor = texture(s_texture0, texCoord);\n"
        "    }\n"
        "    else if(u_nImgType == 2) //NV21\n"
        "    {\n"
        "        vec3 yuv;\n"
        "        yuv.x = texture(s_texture0, texCoord).r;\n"
        "        yuv.y = texture(s_texture1, texCoord).a - 0.5;\n"
        "        yuv.z = texture(s_texture1, texCoord).r - 0.5;\n"
        "        highp vec3 rgb = mat3(1.0,       1.0,     1.0,\n"
        "        0.0,  -0.344,  1.770,\n"
        "        1.403,  -0.714,     0.0) * yuv;\n"
        "        outColor = vec4(rgb, 1.0);\n"
        "\n"
        "    }\n"
        "    else if(u_nImgType == 3) //NV12\n"
        "    {\n"
        "        vec3 yuv;\n"
        "        yuv.x = texture(s_texture0, texCoord).r;\n"
        "        yuv.y = texture(s_texture1, texCoord).r - 0.5;\n"
        "        yuv.z = texture(s_texture1, texCoord).a - 0.5;\n"
        "        highp vec3 rgb = mat3(1.0,       1.0,     1.0,\n"
        "        0.0,  -0.344,  1.770,\n"
        "        1.403,  -0.714,     0.0) * yuv;\n"
        "        outColor = vec4(rgb, 1.0);\n"
        "    }\n"
        "    else if(u_nImgType == 4) //I420\n"
        "    {\n"
        "        vec3 yuv;\n"
        "        yuv.x = texture(s_texture0, texCoord).r;\n"
        "        yuv.y = texture(s_texture1, texCoord).r - 0.5;\n"
        "        yuv.z = texture(s_texture2, texCoord).r - 0.5;\n"
        "        highp vec3 rgb = mat3(1.0,       1.0,     1.0,\n"
        "        0.0,  -0.344,  1.770,\n"
        "        1.403,  -0.714,     0.0) * yuv;\n"
        "        outColor = vec4(rgb, 1.0);\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        outColor = vec4(1.0);\n"
        "    }\n"
        "    return outColor;\n"
        "}\n"
        "void main()\n"
        "{\n"
        "    vec2 imgTexCoord = v_texCoord * u_TexSize;\n"
        "    float sideLength = u_TexSize.y / 6.0;\n"
        "    float maxOffset = 0.08 * sideLength;\n"
        "    float x = mod(imgTexCoord.x, floor(sideLength));\n"
        "    float y = mod(imgTexCoord.y, floor(sideLength));\n"
        "\n"
        "    float offset = u_Offset * maxOffset;\n"
        "\n"
        "    if(offset <= x\n"
        "    && x <= sideLength - offset\n"
        "    && offset <= y\n"
        "    && y <= sideLength - offset)\n"
        "    {\n"
        "        outColor = sampleImage(v_texCoord);\n"
        "    }\n"
        "    else\n"
        "    {\n"
        "        outColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
        "    }\n"
        "}";

static char fGrayShaderStr[] =
        "//黑白滤镜\n"
        "#version 300 es\n"
        "precision highp float;\n"
        "in vec2 v_texCoord;\n"
        "layout(location = 0) out vec4 outColor;\n"
        "uniform sampler2D s_TextureMap;//采样器\n"
        "void main()\n"
        "{\n"
        "    outColor = texture(s_TextureMap, v_texCoord);\n"
        "    if(v_texCoord.x > 0.5)\n"
        "        outColor = vec4(vec3(outColor.r*0.299 + outColor.g*0.587 + outColor.b*0.114), outColor.a);\n"
        "}";

GLfloat verticesCoords[] = {
        -1.0f,  1.0f, 0.0f,  // Position 0
        -1.0f, -1.0f, 0.0f,  // Position 1
        1.0f,  -1.0f, 0.0f,  // Position 2
        1.0f,   1.0f, 0.0f,  // Position 3
};

GLfloat textureCoords[] = {
        0.0f,  0.0f,        // TexCoord 0
        0.0f,  1.0f,        // TexCoord 1
        1.0f,  1.0f,        // TexCoord 2
        1.0f,  0.0f         // TexCoord 3
};

GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

VideoGLRender::VideoGLRender():VideoRender(VIDEO_RENDER_OPENGL) {
    m_MVPMatrix = glm::mat4(1.0f);
}

VideoGLRender::~VideoGLRender() {
    NativeImageUtil::FreeNativeImage(&m_RenderImage);

}

void VideoGLRender::Init(int videoWidth, int videoHeight, int *dstSize) {
    LOGCATE("VideoGLRender::InitRender video[w, h]=[%d, %d]", videoWidth, videoHeight);
    if(dstSize != nullptr) {
        dstSize[0] = videoWidth;
        dstSize[1] = videoHeight;
    }
    m_FrameIndex = 0;
    UpdateMVPMatrix(0, 0, 1.0f, 1.0f);
}

void VideoGLRender::RenderVideoFrame(NativeImage *pImage) {
    LOGCATV("VideoGLRender::RenderVideoFrame pImage=%p", pImage);
    if(pImage == nullptr || pImage->ppPlane[0] == nullptr)
        return;
    std::unique_lock<std::mutex> lock(m_Mutex);
    // 宽高或格式任一变化都要重新分配（只比较宽高时，格式变化会导致 CopyNativeImage 直接放弃拷贝）
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
    m_FrameDirty = true;
    //NativeImageUtil::DumpNativeImage(&m_RenderImage, "/sdcard", "camera");
}

void VideoGLRender::UnInit() {
    // 单例不在这里销毁：GL 线程可能正在 OnDrawFrame 中使用它（原来 ReleaseInstance 会造成释放后使用）。
    // 只清掉上一次播放残留的画面，下次播放会重新分配
    std::unique_lock<std::mutex> lock(m_Mutex);
    NativeImageUtil::FreeNativeImage(&m_RenderImage);
    memset(&m_RenderImage, 0, sizeof(NativeImage));
    m_FrameDirty = false;
}

void VideoGLRender::UpdateMVPMatrix(int angleX, int angleY, float scaleX, float scaleY)
{
    angleX = angleX % 360;
    angleY = angleY % 360;

    //转化为弧度角
    float radiansX = static_cast<float>(MATH_PI / 180.0f * angleX);
    float radiansY = static_cast<float>(MATH_PI / 180.0f * angleY);
    // Projection matrix
    glm::mat4 Projection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 100.0f);
    //glm::mat4 Projection = glm::frustum(-ratio, ratio, -1.0f, 1.0f, 4.0f, 100.0f);
    //glm::mat4 Projection = glm::perspective(45.0f,ratio, 0.1f,100.f);

    // View matrix
    glm::mat4 View = glm::lookAt(
            glm::vec3(0, 0, 4), // Camera is at (0,0,1), in World Space
            glm::vec3(0, 0, 0), // and looks at the origin
            glm::vec3(0, 1, 0)  // Head is up (set to 0,-1,0 to look upside-down)
    );

    // Model matrix
    glm::mat4 Model = glm::mat4(1.0f);
    Model = glm::scale(Model, glm::vec3(scaleX, scaleY, 1.0f));
    Model = glm::rotate(Model, radiansX, glm::vec3(1.0f, 0.0f, 0.0f));
    Model = glm::rotate(Model, radiansY, glm::vec3(0.0f, 1.0f, 0.0f));
    Model = glm::translate(Model, glm::vec3(0.0f, 0.0f, 0.0f));

    // 矩阵由手势线程/解码线程写、GL 线程读，加锁避免读到写了一半的矩阵
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_MVPMatrix = Projection * View * Model;

}

void VideoGLRender::UpdateMVPMatrix(TransformMatrix *pTransformMatrix) {
    //BaseGLRender::UpdateMVPMatrix(pTransformMatrix);
    float fFactorX = 1.0f;
    float fFactorY = 1.0f;

    if (pTransformMatrix->mirror == 1) {
        fFactorX = -1.0f;
    } else if (pTransformMatrix->mirror == 2) {
        fFactorY = -1.0f;
    }

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

    glm::mat4 Projection = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f);
    glm::mat4 View = glm::lookAt(
            glm::vec3(0, 0, 1), // Camera is at (0,0,1), in World Space
            glm::vec3(0, 0, 0), // and looks at the origin
            glm::vec3(0, 1, 0) // Head is up (set to 0,-1,0 to look upside-down)
    );

    // Model matrix : an identity matrix (model will be at the origin)
    glm::mat4 Model = glm::mat4(1.0f);
    Model = glm::scale(Model, glm::vec3(fFactorX * pTransformMatrix->scaleX,
                                        fFactorY * pTransformMatrix->scaleY, 1.0f));
    Model = glm::rotate(Model, fRotate, glm::vec3(0.0f, 0.0f, 1.0f));
    Model = glm::translate(Model,
                           glm::vec3(pTransformMatrix->translateX, pTransformMatrix->translateY, 0.0f));

    LOGCATE("VideoGLRender::UpdateMVPMatrix rotate %d,%.2f,%0.5f,%0.5f,%0.5f,%0.5f,", pTransformMatrix->degree, fRotate,
            pTransformMatrix->translateX, pTransformMatrix->translateY,
            fFactorX * pTransformMatrix->scaleX, fFactorY * pTransformMatrix->scaleY);

    std::unique_lock<std::mutex> lock(m_Mutex);
    m_MVPMatrix = Projection * View * Model;
}

void VideoGLRender::OnSurfaceCreated() {
    LOGCATE("VideoGLRender::OnSurfaceCreated");

    // 单例会跨越多个 GLSurfaceView 存活。每次 OnSurfaceCreated 都是新的 EGL 上下文，
    // 旧上下文里的纹理/缓冲已随上下文销毁，这里重新创建，并让纹理在下一帧重新分配存储
    m_TexWidth = m_TexHeight = m_TexFormat = 0;
    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_FrameDirty = m_RenderImage.ppPlane[0] != nullptr;
    }

    m_ProgramObj = GLUtils::CreateProgram(vShaderStr, fShaderStr);
    if (!m_ProgramObj)
    {
        LOGCATE("VideoGLRender::OnSurfaceCreated create program fail");
        return;
    }

    // 采样器绑定的纹理单元不会变化，创建程序后设置一次即可；uniform 位置也只查一次
    glUseProgram(m_ProgramObj);
    for (int i = 0; i < TEXTURE_NUM; ++i) {
        char samplerName[64] = {0};
        sprintf(samplerName, "s_texture%d", i);
        glUniform1i(glGetUniformLocation(m_ProgramObj, samplerName), i);
    }
    m_MVPMatLoc = glGetUniformLocation(m_ProgramObj, "u_MVPMatrix");
    m_ImgTypeLoc = glGetUniformLocation(m_ProgramObj, "u_nImgType");

    // Y/U/V 平面每行字节数不一定是 4 的倍数（如宽 270 的色度平面），
    // 默认 4 字节对齐会导致读错行，画面倾斜错位
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

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

    // Generate VBO Ids and load the VBOs with data
    glGenBuffers(3, m_VboIds);
    glBindBuffer(GL_ARRAY_BUFFER, m_VboIds[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verticesCoords), verticesCoords, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, m_VboIds[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(textureCoords), textureCoords, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_VboIds[2]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Generate VAO Id
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

void VideoGLRender::OnSurfaceChanged(int w, int h) {
    LOGCATE("VideoGLRender::OnSurfaceChanged [w, h]=[%d, %d]", w, h);
    m_ScreenSize.x = w;
    m_ScreenSize.y = h;
    glViewport(0, 0, w, h);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
}

/**
 * 按平面上传一张纹理：存储尺寸/格式变化时用 glTexImage2D 重新分配，否则只用 glTexSubImage2D 更新数据。
 * 原来每帧都调用 glTexImage2D，相当于每帧重新分配一次显存。
 */
static void UploadPlane(GLuint texId, GLenum unit, GLenum format, int w, int h, const void *data, bool realloc) {
    glActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, texId);
    if (realloc) {
        glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, data);
    }
    glBindTexture(GL_TEXTURE_2D, GL_NONE);
}

void VideoGLRender::UploadTextures() {
    // 调用方已持有 m_Mutex
    int w = m_RenderImage.width;
    int h = m_RenderImage.height;
    bool realloc = w != m_TexWidth || h != m_TexHeight || m_RenderImage.format != m_TexFormat;

    switch (m_RenderImage.format)
    {
        case IMAGE_FORMAT_RGBA:
            UploadPlane(m_TextureIds[0], GL_TEXTURE0, GL_RGBA, w, h, m_RenderImage.ppPlane[0], realloc);
            break;
        case IMAGE_FORMAT_NV21:
        case IMAGE_FORMAT_NV12:
            //upload Y plane data
            UploadPlane(m_TextureIds[0], GL_TEXTURE0, GL_LUMINANCE, w, h, m_RenderImage.ppPlane[0], realloc);
            //update UV plane data
            UploadPlane(m_TextureIds[1], GL_TEXTURE1, GL_LUMINANCE_ALPHA, w >> 1, h >> 1, m_RenderImage.ppPlane[1], realloc);
            break;
        case IMAGE_FORMAT_I420:
            //upload Y plane data
            UploadPlane(m_TextureIds[0], GL_TEXTURE0, GL_LUMINANCE, w, h, m_RenderImage.ppPlane[0], realloc);
            //update U plane data
            UploadPlane(m_TextureIds[1], GL_TEXTURE1, GL_LUMINANCE, w >> 1, h >> 1, m_RenderImage.ppPlane[1], realloc);
            //update V plane data
            UploadPlane(m_TextureIds[2], GL_TEXTURE2, GL_LUMINANCE, w >> 1, h >> 1, m_RenderImage.ppPlane[2], realloc);
            break;
        default:
            return;
    }

    m_TexWidth = w;
    m_TexHeight = h;
    m_TexFormat = m_RenderImage.format;
}

void VideoGLRender::OnDrawFrame() {
    glClear(GL_COLOR_BUFFER_BIT);
    if(m_ProgramObj == GL_NONE) return;

    // 图像数据会被解码线程改写，检查和上传都在锁内完成
    std::unique_lock<std::mutex> lock(m_Mutex);
    if(m_RenderImage.ppPlane[0] == nullptr) return;
    LOGCATV("VideoGLRender::OnDrawFrame [w, h]=[%d, %d], format=%d", m_RenderImage.width, m_RenderImage.height, m_RenderImage.format);
    m_FrameIndex++;

    // 只有新帧到来时才上传；GLSurfaceView 因其他原因重绘时直接复用纹理
    if(m_FrameDirty) {
        UploadTextures();
        m_FrameDirty = false;
    }
    int imgType = m_RenderImage.format;
    glm::mat4 mvpMatrix = m_MVPMatrix;
    lock.unlock();

    // Use the program object
    glUseProgram (m_ProgramObj);

    glBindVertexArray(m_VaoId);

    glUniformMatrix4fv(m_MVPMatLoc, 1, GL_FALSE, &mvpMatrix[0][0]);

    for (int i = 0; i < TEXTURE_NUM; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, m_TextureIds[i]);
    }

    glUniform1i(m_ImgTypeLoc, imgType);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void *)0);

}

VideoGLRender *VideoGLRender::GetInstance() {
    // 每次都加锁：无竞争时开销很小，而无锁的"双重检查"在 C++ 内存模型下是数据竞争
    std::lock_guard<std::mutex> lock(m_Mutex);
    if(s_Instance == nullptr)
    {
        s_Instance = new VideoGLRender();
    }
    return s_Instance;
}

void VideoGLRender::ReleaseInstance() {
    // 注意：只能在确定 GL 线程不再使用它时调用（例如进程退出）。播放结束请用 UnInit()
    std::lock_guard<std::mutex> lock(m_Mutex);
    if(s_Instance != nullptr)
    {
        delete s_Instance;
        s_Instance = nullptr;
    }
}
