//
// Created by 字节流动 on 2020/6/19.
//

#ifndef LEARNFFMPEG_VIDEORENDER_H
#define LEARNFFMPEG_VIDEORENDER_H

#define VIDEO_RENDER_OPENGL             0
#define VIDEO_RENDER_ANWINDOW           1
#define VIDEO_RENDER_3D_VR              2

#include "ImageDef.h"

class VideoRender {
public:
    VideoRender(int type){
        m_RenderType = type;
    }
    virtual ~VideoRender(){}
    virtual void Init(int videoWidth, int videoHeight, int *dstSize) = 0;
    /**
     * 纯虚函数
     * @param pImage
     */
    virtual void RenderVideoFrame(NativeImage *pImage) = 0;
    virtual void UnInit() = 0;

    /**
     * @brief 直接获取渲染目标的可写缓冲区（零拷贝路径）
     * 支持的渲染器（如 ANativeWindow）返回 true，调用方可以把像素直接写进去，
     * 写完后必须调用 UnlockDirectBuffer() 提交显示。
     * @param ppBits    输出：缓冲区首地址（RGBA）
     * @param pLineSize 输出：每行字节数（含对齐）
     */
    virtual bool LockDirectBuffer(uint8_t **ppBits, int *pLineSize) { return false; }
    virtual void UnlockDirectBuffer() {}

    int GetRenderType() {
        return m_RenderType;
    }
private:
    int m_RenderType = VIDEO_RENDER_ANWINDOW;
};

#endif //LEARNFFMPEG_VIDEORENDER_H
