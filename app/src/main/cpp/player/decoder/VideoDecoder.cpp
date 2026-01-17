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
 * 5. 可选：初始化视频录制器（仅在NativeWindow渲染模式下）
 */
void VideoDecoder::OnDecoderReady() {
    LOGCATE("VideoDecoder::OnDecoderReady");
    // 获取视频原始宽高
    m_VideoWidth = GetCodecContext()->width;
    m_VideoHeight = GetCodecContext()->height;

    // 通知播放器解码器已就绪
    if(m_MsgContext && m_MsgCallback)
        m_MsgCallback(m_MsgContext, MSG_DECODER_READY, 0);

    if(m_VideoRender != nullptr) {
        // 初始化渲染器，获取渲染目标尺寸
        int dstSize[2] = {0};
        m_VideoRender->Init(m_VideoWidth, m_VideoHeight, dstSize);
        m_RenderWidth = dstSize[0];
        m_RenderHeight = dstSize[1];

        // 如果是NativeWindow渲染模式，初始化视频录制器
        if(m_VideoRender->GetRenderType() == VIDEO_RENDER_ANWINDOW) {
            int fps = 25;
            long videoBitRate = m_RenderWidth * m_RenderHeight * fps * 0.2;
            m_pVideoRecorder = new SingleVideoRecorder("/sdcard/learnffmpeg_output.mp4", m_RenderWidth, m_RenderHeight, videoBitRate, fps);
            m_pVideoRecorder->StartRecord();
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
    } else {
        LOGCATE("VideoDecoder::OnDecoderReady m_VideoRender == null");
    }
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

    // 释放RGBA帧
    if(m_RGBAFrame != nullptr) {
        av_frame_free(&m_RGBAFrame);
        m_RGBAFrame = nullptr;
    }

    // 释放帧缓冲区
    if(m_FrameBuffer != nullptr) {
        free(m_FrameBuffer);
        m_FrameBuffer = nullptr;
    }

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
 * 当解码器解码出一帧视频数据时被调用，负责：
 * 1. 根据渲染类型进行格式转换（如果需要）
 * 2. 将视频帧数据封装为NativeImage
 * 3. 送给渲染器进行渲染
 * 4. 可选：送给录制器进行录制
 */
void VideoDecoder::OnFrameAvailable(AVFrame *frame) {
    LOGCATE("VideoDecoder::OnFrameAvailable frame=%p", frame);
    if(m_VideoRender != nullptr && frame != nullptr) {
        NativeImage image;
        LOGCATE("VideoDecoder::OnFrameAvailable frame[w,h]=[%d, %d],format=%d,[line0,line1,line2]=[%d, %d, %d]", frame->width, frame->height, GetCodecContext()->pix_fmt, frame->linesize[0], frame->linesize[1],frame->linesize[2]);
        if(m_VideoRender->GetRenderType() == VIDEO_RENDER_ANWINDOW)
        {
            sws_scale(m_SwsContext, frame->data, frame->linesize, 0,
                      m_VideoHeight, m_RGBAFrame->data, m_RGBAFrame->linesize);

            image.format = IMAGE_FORMAT_RGBA;
            image.width = m_RenderWidth;
            image.height = m_RenderHeight;
            image.ppPlane[0] = m_RGBAFrame->data[0];
            image.pLineSize[0] = image.width * 4;
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
            sws_scale(m_SwsContext, frame->data, frame->linesize, 0,
                      m_VideoHeight, m_RGBAFrame->data, m_RGBAFrame->linesize);
            image.format = IMAGE_FORMAT_RGBA;
            image.width = m_RenderWidth;
            image.height = m_RenderHeight;
            image.ppPlane[0] = m_RGBAFrame->data[0];
            image.pLineSize[0] = image.width * 4;
        }

        m_VideoRender->RenderVideoFrame(&image);

        if(m_pVideoRecorder != nullptr) {
            m_pVideoRecorder->OnFrame2Encode(&image);
        }
    }

    if(m_MsgContext && m_MsgCallback)
        m_MsgCallback(m_MsgContext, MSG_REQUEST_RENDER, 0);
}
