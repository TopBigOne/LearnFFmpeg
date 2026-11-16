/**
 * FFmpeg 对象的 RAII 封装。
 *
 * FFmpeg 的 C 接口要求每种对象用对应的函数释放（avformat_close_input、avcodec_free_context……），
 * 手写释放代码在提前 return / break 的错误分支里极易漏掉或重复释放。
 * 这里统一用 std::unique_ptr + 自定义 deleter 表达"所有权"，播放器内核的新代码不再手动 free。
 *
 * 约定：
 *  - deleter 都能接受空指针；
 *  - 需要 "二级指针" 的 FFmpeg API（如 avformat_open_input 失败时会释放并置空），
 *    先用裸指针调用，成功后再交给智能指针接管。
 */

#ifndef LEARNFFMPEG_FFPTR_H
#define LEARNFFMPEG_FFPTR_H

#include <memory>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/dict.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
};

struct AVFormatInputDeleter {
    // 输入上下文必须用 avformat_close_input 释放：它会关闭 demuxer 和 AVIOContext，
    // 对 "已分配但还没打开" 的上下文同样适用
    void operator()(AVFormatContext *ctx) const { avformat_close_input(&ctx); }
};

struct AVCodecContextDeleter {
    // avcodec_free_context 内部已经包含 close，不需要再调用已废弃的 avcodec_close
    void operator()(AVCodecContext *ctx) const { avcodec_free_context(&ctx); }
};

struct AVFrameDeleter {
    void operator()(AVFrame *frame) const { av_frame_free(&frame); }
};

struct AVPacketDeleter {
    void operator()(AVPacket *pkt) const { av_packet_free(&pkt); }
};

struct SwrContextDeleter {
    void operator()(SwrContext *ctx) const { swr_free(&ctx); }
};

struct SwsContextDeleter {
    void operator()(SwsContext *ctx) const { sws_freeContext(ctx); }
};

struct AVDictionaryDeleter {
    void operator()(AVDictionary *dict) const { av_dict_free(&dict); }
};

/** av_malloc 系列分配的内存必须用 av_free 释放（可能有对齐要求，不能用 free） */
struct AVMallocDeleter {
    void operator()(void *ptr) const { av_free(ptr); }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, AVFormatInputDeleter>;
using CodecContextPtr  = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using FramePtr         = std::unique_ptr<AVFrame, AVFrameDeleter>;
using PacketPtr        = std::unique_ptr<AVPacket, AVPacketDeleter>;
using SwrContextPtr    = std::unique_ptr<SwrContext, SwrContextDeleter>;
using SwsContextPtr    = std::unique_ptr<SwsContext, SwsContextDeleter>;
using DictionaryPtr    = std::unique_ptr<AVDictionary, AVDictionaryDeleter>;
using AVBufferPtr      = std::unique_ptr<uint8_t, AVMallocDeleter>;

inline FramePtr MakeFrame() { return FramePtr(av_frame_alloc()); }

inline PacketPtr MakePacket() { return PacketPtr(av_packet_alloc()); }

#endif //LEARNFFMPEG_FFPTR_H
