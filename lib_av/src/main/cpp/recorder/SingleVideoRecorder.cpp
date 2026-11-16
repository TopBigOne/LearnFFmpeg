/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#include "SingleVideoRecorder.h"
#include "RecorderMuxer.h"

extern "C" {
#include <libavutil/opt.h>
}

/**
 * @brief 构造函数
 * @param outUrl 输出文件路径
 * @param frameWidth 视频帧宽度
 * @param frameHeight 视频帧高度
 * @param bitRate 视频比特率
 * @param fps 帧率
 */
SingleVideoRecorder::SingleVideoRecorder(const char *outUrl, int frameWidth, int frameHeight,
                                         long bitRate, int fps) {
    LOGCATE("SingleVideoRecorder::SingleVideoRecorder outUrl=%s, [w,h]=[%d,%d], bitRate=%ld, fps=%d", outUrl, frameWidth, frameHeight, bitRate, fps);
    strncpy(m_outUrl, outUrl, sizeof(m_outUrl) - 1);
    m_frameWidth = frameWidth;
    m_frameHeight = frameHeight;
    m_bitRate = bitRate;
    m_frameRate = fps;
}

/**
 * @brief 析构函数：确保编码线程已停止、资源已释放
 */
SingleVideoRecorder::~SingleVideoRecorder() {
    StopRecord();
}

/**
 * @brief 开始录制
 * @return 0表示成功，负数表示失败（失败时已释放所有资源）
 */
int SingleVideoRecorder::StartRecord() {
    LOGCATE("SingleVideoRecorder::StartRecord");
    int result = -1;
    AVDictionary *codecOpts = nullptr;
    do{
        // 分配输出格式上下文
        result = avformat_alloc_output_context2(&m_pFormatCtx, nullptr, nullptr, m_outUrl);
        if(result < 0) {
            LOGCATE("SingleVideoRecorder::StartRecord avformat_alloc_output_context2 ret=%d", result);
            break;
        }

        // 创建视频流
        m_pStream = avformat_new_stream(m_pFormatCtx, nullptr);
        if (m_pStream == nullptr) {
            result = -1;
            LOGCATE("SingleVideoRecorder::StartRecord avformat_new_stream fail.");
            break;
        }

        // 优先使用 H.264（x264，体积小、兼容性好），找不到再退回 MPEG4
        m_pCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (m_pCodec == nullptr) m_pCodec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
        if (m_pCodec == nullptr) {
            result = -1;
            LOGCATE("SingleVideoRecorder::StartRecord avcodec_find_encoder fail.");
            break;
        }

        // 配置编码器参数
        m_pCodecCtx = avcodec_alloc_context3(m_pCodec);
        if (m_pCodecCtx == nullptr) {
            result = -1;
            break;
        }
        m_pCodecCtx->codec_id = m_pCodec->id;
        m_pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
        m_pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;  // 输出YUV420P格式
        m_pCodecCtx->width = m_frameWidth;
        m_pCodecCtx->height = m_frameHeight;
        // 时间基取毫秒：pts 直接使用采集时间，输入帧率波动时画面时长仍然正确
        m_pCodecCtx->time_base = AVRational{1, 1000};
        m_pCodecCtx->framerate = AVRational{m_frameRate, 1};
        m_pCodecCtx->bit_rate = m_bitRate;
        m_pCodecCtx->gop_size = m_frameRate;  // 约 1 秒一个关键帧
        // mp4 要求编码器头信息（SPS/PPS）放在全局 extradata 中，否则文件缺少解码配置
        if (m_pFormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
            m_pCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (m_pCodecCtx->codec_id == AV_CODEC_ID_H264) {
            // 编码器私有参数要传给 avcodec_open2（原来误传给了 avformat_write_header）
            // 默认 medium 在手机上无法实时编码，录制优先保证速度
            av_dict_set(&codecOpts, "preset", "ultrafast", 0);
            av_dict_set(&codecOpts, "tune", "zerolatency", 0);
        }

        result = avcodec_open2(m_pCodecCtx, m_pCodec, &codecOpts);
        if(result < 0) {
            LOGCATE("SingleVideoRecorder::StartRecord avcodec_open2 ret=%d", result);
            break;
        }

        // 必须在编码器打开之后拷贝参数：此时 extradata 才已生成（原来顺序反了）
        result = avcodec_parameters_from_context(m_pStream->codecpar, m_pCodecCtx);
        if(result < 0) {
            LOGCATE("SingleVideoRecorder::StartRecord avcodec_parameters_from_context ret=%d", result);
            break;
        }
        m_pStream->time_base = m_pCodecCtx->time_base;
        // 帧率是 {fps, 1}（原来写成了 {1, fps}，分子分母反了）
        m_pStream->avg_frame_rate = AVRational{m_frameRate, 1};
        m_pStream->r_frame_rate = AVRational{m_frameRate, 1};

        // 打印格式信息
        av_dump_format(m_pFormatCtx, 0, m_outUrl, 1);

        // 分配编码帧（YUV420P，可复用）
        m_pFrame = av_frame_alloc();
        if (m_pFrame == nullptr) {
            result = -1;
            break;
        }
        m_pFrame->width = m_pCodecCtx->width;
        m_pFrame->height = m_pCodecCtx->height;
        m_pFrame->format = m_pCodecCtx->pix_fmt;
        result = av_frame_get_buffer(m_pFrame, 0);
        if (result < 0) break;

        // 打开输出文件
        result = avio_open(&m_pFormatCtx->pb, m_outUrl, AVIO_FLAG_WRITE);
        if(result < 0) {
            LOGCATE("SingleVideoRecorder::StartRecord avio_open ret=%d", result);
            break;
        }

        result = WriteRecorderHeader(m_pFormatCtx);   // mp4 使用分段格式，见 RecorderMuxer.h
        if(result < 0) {
            LOGCATE("SingleVideoRecorder::StartRecord avformat_write_header ret=%s", av_err2str(result));
            break;
        }
        m_headerWritten = true;
        result = 0;
    } while(false);

    av_dict_free(&codecOpts);

    if(result < 0) {
        Release();
        return result;
    }

    // 初始化成功，启动编码线程
    // pts 的 0 点。必须在 m_started 置位前确定：生产线程看到 m_started 后就会读它
    m_startTimeUs = GetBootTimeUs();
    m_started = true;
    m_encodeThread = new thread(StartH264EncoderThread, this);
    return 0;
}

/**
 * @brief 停止录制：编码完队列里剩余的帧、刷新编码器、写文件尾并释放资源。可重复调用
 */
int SingleVideoRecorder::StopRecord() {
    m_exit = true;
    m_frameQueue.WakeUpAll();
    if(m_encodeThread != nullptr) {
        // 等待编码线程把剩余帧编码完并退出
        m_encodeThread->join();
        delete m_encodeThread;
        m_encodeThread = nullptr;

        // 刷新编码器（传入nullptr）
        EncodeFrame(nullptr);
        if(m_headerWritten) {
            av_write_trailer(m_pFormatCtx);
        }
    }
    m_started = false;
    Release();
    return 0;
}

void SingleVideoRecorder::Release() {
    // 清理队列中剩余的帧
    while (TimedImage *pImage = m_frameQueue.Pop()) {
        NativeImageUtil::FreeNativeImage(pImage);
        delete pImage;
    }

    // avcodec_free_context 已包含关闭编码器
    avcodec_free_context(&m_pCodecCtx);
    av_frame_free(&m_pFrame);
    if (m_pFormatCtx != nullptr) {
        avio_closep(&m_pFormatCtx->pb);
        avformat_free_context(m_pFormatCtx);
        m_pFormatCtx = nullptr;
    }

    m_headerWritten = false;
}

/**
 * @brief 编码线程函数（静态函数）
 *
 * 阻塞等待队列中的帧（不再 usleep 轮询），转换为 YUV420P 后编码；停止且队列为空时退出
 */
void SingleVideoRecorder::StartH264EncoderThread(SingleVideoRecorder *recorder) {
    LOGCATE("SingleVideoRecorder::StartH264EncoderThread start");
    for(;;)
    {
        TimedImage *pImage = recorder->m_frameQueue.WaitPop(50);
        if(pImage == nullptr) {
            if(recorder->m_exit) break;   // 已停止且队列为空
            continue;
        }

        AVFrame *pFrame = recorder->m_pFrame;
        if(av_frame_make_writable(pFrame) >= 0) {
            // 用 libyuv 转成编码器的 YUV420P（尺寸不同时顺带缩放），代替 sws_scale
            if(recorder->m_converter.Convert(*pImage, pFrame) == 0) {
                // pts = 采集时刻相对录制起点的毫秒数（time_base 1/1000）。
                // 第一帧固定为 0，文件从 0 开始；之后保证严格递增
                int64_t pts = (pImage->captureTimeUs - recorder->m_startTimeUs) / 1000;
                if(recorder->m_lastPts < 0) pts = 0;
                else if(pts <= recorder->m_lastPts) pts = recorder->m_lastPts + 1;
                recorder->m_lastPts = pts;
                pFrame->pts = pts;
                recorder->EncodeFrame(pFrame);
            }
        }
        // 释放帧资源
        NativeImageUtil::FreeNativeImage(pImage);
        delete pImage;
    }

    LOGCATE("SingleVideoRecorder::StartH264EncoderThread end");
}

/**
 * @brief 接收待编码的视频帧：复制一份并加入编码队列
 */
int SingleVideoRecorder::OnFrame2Encode(NativeImage *inputFrame, int64_t captureTimeUs) {
    if(!m_started || m_exit) return 0;
    if(captureTimeUs == CAPTURE_TIME_UNKNOWN) captureTimeUs = GetBootTimeUs();
    // 点击开始之前就已采集的画面不属于本次录制
    if(captureTimeUs < m_startTimeUs) return 0;
    LOGCATV("SingleVideoRecorder::OnFrame2Encode [w,h,format]=[%d,%d,%d]", inputFrame->width, inputFrame->height, inputFrame->format);
    // 分配新的图像并复制数据
    TimedImage *pImage = new TimedImage();
    pImage->width = inputFrame->width;
    pImage->height = inputFrame->height;
    pImage->format = inputFrame->format;
    NativeImageUtil::AllocNativeImage(pImage);
    NativeImageUtil::CopyNativeImage(inputFrame, pImage);
    pImage->captureTimeUs = captureTimeUs;
    // 加入编码队列；编码跟不上时丢弃最旧的帧，避免内存无限增长
    TimedImage *dropped = m_frameQueue.Push(pImage);
    if(dropped) {
        LOGCATE("SingleVideoRecorder::OnFrame2Encode queue full, drop oldest frame");
        NativeImageUtil::FreeNativeImage(dropped);
        delete dropped;
    }
    return 0;
}

/**
 * @brief 编码单个视频帧
 * @param pFrame 待编码的帧（nullptr表示刷新编码器）
 * @return 0表示成功，负数表示失败
 */
int SingleVideoRecorder::EncodeFrame(AVFrame *pFrame) {
    if(m_pCodecCtx == nullptr) return -1;
    // 发送帧到编码器
    int result = avcodec_send_frame(m_pCodecCtx, pFrame);
    if(result < 0)
    {
        LOGCATE("SingleVideoRecorder::EncodeFrame avcodec_send_frame fail. ret=%d", result);
        return result;
    }
    // 从编码器接收数据包
    AVPacket *pkt = av_packet_alloc();
    for(;;) {
        result = avcodec_receive_packet(m_pCodecCtx, pkt);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
            result = 0;
            break;
        } else if (result < 0) {
            LOGCATE("SingleVideoRecorder::EncodeFrame avcodec_receive_packet fail. ret=%d", result);
            break;
        }
        LOGCATV("SingleVideoRecorder::EncodeFrame frame pts=%lld, size=%d", (long long)pkt->pts, pkt->size);
        // 调整时间戳并写入文件
        pkt->stream_index = m_pStream->index;
        av_packet_rescale_ts(pkt, m_pCodecCtx->time_base, m_pStream->time_base);
        pkt->pos = -1;
        av_interleaved_write_frame(m_pFormatCtx, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return result;
}
