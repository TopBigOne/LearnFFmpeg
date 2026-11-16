/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#ifndef LEARNFFMPEG_NATIVERENDER_H
#define LEARNFFMPEG_NATIVERENDER_H

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include "VideoRender.h"

class NativeRender : public VideoRender{

public:
    NativeRender(JNIEnv *env, jobject surface);
    virtual ~NativeRender();
    virtual void Init(int videoWidth, int videoHeight, int *dstSize);
    /**
     * 虚函数
     * @param pImage
     */
    virtual void RenderVideoFrame(NativeImage *pImage);
    virtual void UnInit();
    virtual bool LockDirectBuffer(uint8_t **ppBits, int *pLineSize);
    virtual void UnlockDirectBuffer();

private:
    ANativeWindow_Buffer m_NativeWindowBuffer;
    ANativeWindow *m_NativeWindow = nullptr;
    int m_DstWidth = 0;
    int m_DstHeight = 0;
};


#endif //LEARNFFMPEG_NATIVERENDER_H
