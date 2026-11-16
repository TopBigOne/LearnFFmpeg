/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#ifndef LEARNFFMPEG_AUDIOGLRENDER_H
#define LEARNFFMPEG_AUDIOGLRENDER_H

#include <thread>
#include <mutex>
#include "AudioRender.h"
#include <GLES3/gl3.h>
#include <detail/type_mat.hpp>
#include <detail/type_mat4x4.hpp>
#include <render/BaseGLRender.h>

using namespace glm;

#define MAX_AUDIO_LEVEL 5000
#define RESAMPLE_LEVEL  40
#define VISUAL_BUFFER_SIZE 4096     // 可视化使用的固定数据长度：1024 个双声道 S16 采样

class AudioGLRender : public BaseGLRender {
public:
    static AudioGLRender* GetInstance();
    static void ReleaseInstance();

    virtual void OnSurfaceCreated();
    virtual void OnSurfaceChanged(int w, int h);
    virtual void OnDrawFrame();
    virtual void UpdateMVPMatrix(int angleX, int angleY, float scaleX, float scaleY){};
    virtual void SetTouchLoc(float touchX, float touchY) {}

    void UpdateAudioFrame(AudioFrame *audioFrame);

    /** 清掉上一次播放的可视化数据（不销毁单例，GL 线程可能仍在使用它） */
    void ClearAudioData();

private:
    void Init();
    void UnInit();
    AudioGLRender(){
        Init();
    }
    ~AudioGLRender(){
        UnInit();
    }

    void UpdateMesh();

    static AudioGLRender *m_pInstance;
    static std::mutex m_Mutex;
    AudioFrame *m_pAudioBuffer = nullptr;

    GLuint m_ProgramObj = 0;
    GLuint m_VaoId;
    GLuint m_VboIds[2];
    glm::mat4 m_MVPMatrix;

    vec3 *m_pVerticesCoords = nullptr;
    vec2 *m_pTextureCoords = nullptr;

    int m_RenderDataSize;

};


#endif //LEARNFFMPEG_AUDIOGLRENDER_H
