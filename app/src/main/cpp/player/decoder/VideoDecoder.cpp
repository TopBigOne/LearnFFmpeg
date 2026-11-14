/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#include "VideoDecoder.h"

/**
 * @brief 视频解码器准备就绪回调
 *
 * 在解码器初始化完成后被调用，用于：
 * 1. 获取视频宽高信息
 * 2. 初始化视频渲染器，获取渲染目标尺寸
 * 3. 创建SwsContext用于视频格式转换（YUV->RGBA等）
 * 4. 分配RGBA帧缓冲区
 * 5. 可选：初始化视频录制器（设置了录制路径且为NativeWindow渲染模式）
 */
void VideoDecoder::OnDecoderReady() {
    LOGCATE("VideoDecoder::OnDecoderReady");
    // 获取视频原始宽高
    m_VideoWidth = GetCodecContext()->width;
    m_VideoHeight = GetCodecContext()->height;

    // 通知播放器解码器已就绪
    if(m_MsgContext && m_MsgCallback)
        m_MsgCallback(m_MsgContext, MSG_DECODER_READY, 0);

    if(m_VideoRender == nullptr) {
        LOGCATE("VideoDecoder::OnDecoderReady m_VideoRender == null");
        return;
    }

    // 初始化渲染器，获取渲染目标尺寸
    int dstSize[2] = {0};
    m_VideoRender->Init(m_VideoWidth, m_VideoHeight, dstSize);
    m_RenderWidth = dstSize[0];
    m_RenderHeight = dstSize[1];
    if(m_RenderWidth <= 0 || m_RenderHeight <= 0) {
        LOGCATE("VideoDecoder::OnDecoderReady invalid render size [%d, %d]", m_RenderWidth, m_RenderHeight);
        return;
    }

    // "边播边录"：只有显式设置了输出路径才开启（每帧都要 x264 软编码，CPU 开销很大）
    if(m_VideoRender->GetRenderType() == VIDEO_RENDER_ANWINDOW && m_RecordPath[0] != '\0') {
        int fps = 25;
        long videoBitRate = m_RenderWidth * m_RenderHeight * fps * 0.2;
        m_pVideoRecorder = new SingleVideoRecorder(m_RecordPath, m_RenderWidth, m_RenderHeight, videoBitRate, fps);
        if(m_pVideoRecorder->StartRecord() != 0) {
            LOGCATE("VideoDecoder::OnDecoderReady StartRecord fail, path=%s", m_RecordPath);
            delete m_pVideoRecorder;
            m_pVideoRecorder = nullptr;
        }
    }

    // 分配RGBA帧和缓冲区
    m_RGBAFrame = av_frame_alloc();
    int bufferSize = av_image_get_buffer_size(DST_PIXEL_FORMAT, m_RenderWidth, m_RenderHeight, 1);
    m_FrameBuffer = (uint8_t *) av_malloc(bufferSize * sizeof(uint8_t));
    // 为RGBA帧填充数据指针和行大小信息
    av_image_fill_arrays(m_RGBAFrame->data, m_RGBAFrame->linesize,
                         m_FrameBuffer, DST_PIXEL_FORMAT, m_RenderWidth, m_RenderHeight, 1);

    // 创建视频格式转换上下文（Sws: Software Scale）
    // 用于将解码后的视频帧转换为RGBA格式
    m_SwsContext = sws_getContext(m_VideoWidth, m_VideoHeight, GetCodecContext()->pix_fmt,
                                  m_RenderWidth, m_RenderHeight, DST_PIXEL_FORMAT,
                                  SWS_FAST_BILINEAR, NULL, NULL, NULL);
}

/**
 * @brief 视频解码器完成回调
 *
 * 在解码器停止时被调用，负责释放所有资源
 */
void VideoDecoder::OnDecoderDone() {
    LOGCATE("VideoDecoder::OnDecoderDone");

    // 通知播放器解码已完成
    if(m_MsgContext && m_MsgCallback)
        m_MsgCallback(m_MsgContext, MSG_DECODER_DONE, 0);

    // 释放渲染器
    if(m_VideoRender)
        m_VideoRender->UnInit();

    // 释放RGBA帧（会把指针置空）
    av_frame_free(&m_RGBAFrame);

    // av_malloc 分配的内存必须用 av_free/av_freep 释放，不能用 free
    av_freep(&m_FrameBuffer);

    // 释放格式转换上下文
    if(m_SwsContext != nullptr) {
        sws_freeContext(m_SwsContext);
        m_SwsContext = nullptr;
    }

    // 释放视频录制器
    if(m_pVideoRecorder != nullptr) {
        m_pVideoRecorder->StopRecord();
        delete m_pVideoRecorder;
        m_pVideoRecorder = nullptr;
    }

}

/**
 * @brief 视频帧可用回调
 * @param frame 解码后的视频帧
 *
 * 1. 根据渲染类型进行格式转换（如果需要）
 * 2. 将视频帧数据封装为NativeImage
 * 3. 送给渲染器进行渲染
 * 4. 可选：送给录制器进行录制
 */
void VideoDecoder::OnFrameAvailable(AVFrame *frame) {
    LOGCATV("VideoDecoder::OnFrameAvailable frame=%p", frame);
    if(m_VideoRender != nullptr && frame != nullptr) {
        NativeImage image;
        LOGCATV("VideoDecoder::OnFrameAvailable frame[w,h]=[%d, %d],format=%d,[line0,line1,line2]=[%d, %d, %d]", frame->width, frame->height, GetCodecContext()->pix_fmt, frame->linesize[0], frame->linesize[1],frame->linesize[2]);
        if(m_VideoRender->GetRenderType() == VIDEO_RENDER_ANWINDOW)
        {
            if(m_SwsContext == nullptr) return;

            uint8_t *windowBits = nullptr;
            int windowLineSize = 0;
            // 零拷贝路径：不录制时，直接让 sws_scale 把 RGBA 结果写进窗口缓冲区，
            // 省掉"先转到中间缓冲区，再逐行拷进窗口"这一次整帧拷贝。
            // 录制时需要一份 RGBA 图像给编码器，而窗口缓冲区通常不适合回读，所以仍走中间缓冲区。
            if(m_pVideoRecorder == nullptr && m_VideoRender->LockDirectBuffer(&windowBits, &windowLineSize)) {
                uint8_t *dstData[4] = {windowBits, nullptr, nullptr, nullptr};
                int dstLineSize[4] = {windowLineSize, 0, 0, 0};
                sws_scale(m_SwsContext, frame->data, frame->linesize, 0,
                          frame->height, dstData, dstLineSize);
                m_VideoRender->UnlockDirectBuffer();
                goto __NOTIFY;
            }

            sws_scale(m_SwsContext, frame->data, frame->linesize, 0,
                      frame->height, m_RGBAFrame->data, m_RGBAFrame->linesize);

            image.format = IMAGE_FORMAT_RGBA;
            image.width = m_RenderWidth;
            image.height = m_RenderHeight;
            image.ppPlane[0] = m_RGBAFrame->data[0];
            image.pLineSize[0] = m_RGBAFrame->linesize[0];
        } else if(GetCodecContext()->pix_fmt == AV_PIX_FMT_YUV420P || GetCodecContext()->pix_fmt == AV_PIX_FMT_YUVJ420P) {
            image.format = IMAGE_FORMAT_I420;
            image.width = frame->width;
            image.height = frame->height;
            image.pLineSize[0] = frame->linesize[0];
            image.pLineSize[1] = frame->linesize[1];
            image.pLineSize[2] = frame->linesize[2];
            image.ppPlane[0] = frame->data[0];
            image.ppPlane[1] = frame->data[1];
            image.ppPlane[2] = frame->data[2];
            if(frame->data[0] && frame->data[1] && !frame->data[2] && frame->linesize[0] == frame->linesize[1] && frame->linesize[2] == 0) {
                // on some android device, output of h264 mediacodec decoder is NV12 兼容某些设备可能出现的格式不匹配问题
                image.format = IMAGE_FORMAT_NV12;
            }
        } else if (GetCodecContext()->pix_fmt == AV_PIX_FMT_NV12) {
            image.format = IMAGE_FORMAT_NV12;
            image.width = frame->width;
            image.height = frame->height;
            image.pLineSize[0] = frame->linesize[0];
            image.pLineSize[1] = frame->linesize[1];
            image.ppPlane[0] = frame->data[0];
            image.ppPlane[1] = frame->data[1];
        } else if (GetCodecContext()->pix_fmt == AV_PIX_FMT_NV21) {
            image.format = IMAGE_FORMAT_NV21;
            image.width = frame->width;
            image.height = frame->height;
            image.pLineSize[0] = frame->linesize[0];
            image.pLineSize[1] = frame->linesize[1];
            image.ppPlane[0] = frame->data[0];
            image.ppPlane[1] = frame->data[1];
        } else if (GetCodecContext()->pix_fmt == AV_PIX_FMT_RGBA) {
            image.format = IMAGE_FORMAT_RGBA;
            image.width = frame->width;
            image.height = frame->height;
            image.pLineSize[0] = frame->linesize[0];
            image.ppPlane[0] = frame->data[0];
        } else {
            if(m_SwsContext == nullptr) return;
            sws_scale(m_SwsContext, frame->data, frame->linesize, 0,
                      frame->height, m_RGBAFrame->data, m_RGBAFrame->linesize);
            image.format = IMAGE_FORMAT_RGBA;
            image.width = m_RenderWidth;
            image.height = m_RenderHeight;
            image.ppPlane[0] = m_RGBAFrame->data[0];
            image.pLineSize[0] = m_RGBAFrame->linesize[0];
        }

        m_VideoRender->RenderVideoFrame(&image);

        if(m_pVideoRecorder != nullptr) {
            m_pVideoRecorder->OnFrame2Encode(&image);
        }
    }

__NOTIFY:
    if(m_MsgContext && m_MsgCallback)
        m_MsgCallback(m_MsgContext, MSG_REQUEST_RENDER, 0);
}
