/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#ifndef LEARNFFMPEG_MASTER_GLRENDER_H
#define LEARNFFMPEG_MASTER_GLRENDER_H
#include <thread>
#include <ImageDef.h>
#include "VideoRender.h"
#include <GLES3/gl3.h>
#include <detail/type_mat.hpp>
#include <detail/type_mat4x4.hpp>
#include <vec2.hpp>
#include <render/BaseGLRender.h>

using namespace glm;

#define MATH_PI 3.1415926535897932384626433832802
#define TEXTURE_NUM 3

class VideoGLRender: public VideoRender, public BaseGLRender{
public:
    virtual void Init(int videoWidth, int videoHeight, int *dstSize);
    virtual void RenderVideoFrame(NativeImage *pImage);
    virtual void UnInit();

    virtual void OnSurfaceCreated();
    virtual void OnSurfaceChanged(int w, int h);
    virtual void OnDrawFrame();

    static VideoGLRender *GetInstance();
    static void ReleaseInstance();

    virtual void UpdateMVPMatrix(int angleX, int angleY, float scaleX, float scaleY);
    virtual void UpdateMVPMatrix(TransformMatrix * pTransformMatrix);
    virtual void SetTouchLoc(float touchX, float touchY) {
        m_TouchXY.x = touchX / m_ScreenSize.x;
        m_TouchXY.y = touchY / m_ScreenSize.y;
    }

private:
    VideoGLRender();
    virtual ~VideoGLRender();

    /** 把 m_RenderImage 上传到纹理：尺寸/格式不变时只更新数据（glTexSubImage2D），不重新分配存储 */
    void UploadTextures();

    static std::mutex m_Mutex;
    static VideoGLRender* s_Instance;
    GLuint m_ProgramObj = GL_NONE;
    GLuint m_TextureIds[TEXTURE_NUM] = {0};
    GLuint m_VaoId = GL_NONE;
    GLuint m_VboIds[3] = {0};
    GLint  m_MVPMatLoc = -1;                // 缓存 uniform 位置，避免每帧按名字查找
    GLint  m_ImgTypeLoc = -1;
    NativeImage m_RenderImage;
    bool   m_FrameDirty = false;            // 有新帧还没上传到纹理
    int    m_TexWidth = 0;                  // 当前纹理存储对应的图像尺寸/格式
    int    m_TexHeight = 0;
    int    m_TexFormat = 0;
    glm::mat4 m_MVPMatrix;

    int m_FrameIndex = 0;
    vec2 m_TouchXY;
    vec2 m_ScreenSize;
};


#endif //LEARNFFMPEG_MASTER_GLRENDER_H
