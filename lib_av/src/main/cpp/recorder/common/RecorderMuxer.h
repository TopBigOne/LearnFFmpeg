/**
 * 录制模块写文件头的公共逻辑
 */

#ifndef LEARNFFMPEG_RECORDERMUXER_H
#define LEARNFFMPEG_RECORDERMUXER_H

#include <cstring>
#include <LogUtil.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
}

/**
 * 写文件头。输出为 MP4/MOV 时使用分段 MP4（fragmented MP4）：
 *
 * 普通 MP4 把所有帧的索引（moov）留在内存里，直到 av_write_trailer 才写出：
 *  - 录得越久内存越大（1 小时约 50MB）；
 *  - 中途崩溃、被系统杀掉、没电时 moov 没写，整个文件无法播放。
 * 分段 MP4 开头只写不含逐帧索引的 moov，之后每个关键帧（约 1 秒）切一段，
 * 每段带自己的索引（moof）随数据一起落盘，中断时只丢最后不到一段。
 *
 *  - frag_keyframe：在视频关键帧处切段；
 *  - empty_moov：开头的 moov 不含逐帧索引；
 *  - default_base_moof：段内偏移相对 moof 计算，兼容性更好（MSE/浏览器、ExoPlayer 等）；
 *  - AVFMT_FLAG_FLUSH_PACKETS：每次写包后把 AVIO 缓冲刷到文件，
 *    段写完立刻进入系统缓存，进程被杀也不会丢在用户态缓冲里。
 *    分段模式下数据按段整块写出，刷新次数等于段数，开销可以忽略。
 */
static inline int WriteRecorderHeader(AVFormatContext *ctx) {
    AVDictionary *opts = nullptr;
    const char *name = ctx->oformat ? ctx->oformat->name : nullptr;
    bool isMp4 = name != nullptr && (strcmp(name, "mp4") == 0 || strcmp(name, "mov") == 0);
    if (isMp4) {
        av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
        ctx->flags |= AVFMT_FLAG_FLUSH_PACKETS;
    }
    int ret = avformat_write_header(ctx, &opts);
    // 剩下的条目是 muxer 不认识的选项，说明这个 FFmpeg 版本不支持，打日志便于排查
    AVDictionaryEntry *unused = nullptr;
    while ((unused = av_dict_get(opts, "", unused, AV_DICT_IGNORE_SUFFIX)) != nullptr) {
        LOGCATE("WriteRecorderHeader option not used: %s=%s", unused->key, unused->value);
    }
    av_dict_free(&opts);
    return ret;
}

// ---------------- 硬件编码（MediaCodec）视频流的封装辅助 ----------------

/** MediaCodec 输出的时间戳单位（微秒） */
static const AVRational kRecorderMicrosecondTimeBase = {1, 1000000};

/**
 * 为硬件编码器创建 H.264 视频流。编码器在 MediaCodec 里，这里只手工描述码流参数，
 * SPS/PPS（extradata）等编码器输出后用 SetHwVideoExtradata 填。
 * 时间基取 90kHz：视频常用时间基，1 小时的时长也不会让 32 位的字段溢出。
 */
static inline AVStream *AddHwH264Stream(AVFormatContext *ctx, int width, int height, long bitRate, int fps) {
    AVStream *st = avformat_new_stream(ctx, nullptr);
    if (st == nullptr) return nullptr;
    st->id = ctx->nb_streams - 1;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_H264;
    st->codecpar->width = width;
    st->codecpar->height = height;
    st->codecpar->format = AV_PIX_FMT_YUV420P;
    st->codecpar->bit_rate = bitRate;
    st->time_base = AVRational{1, 90000};
    st->avg_frame_rate = AVRational{fps, 1};
    return st;
}

/**
 * 填入 SPS/PPS。MediaCodec 给出的是 Annex B 格式（00 00 00 01 起始码），
 * mp4 muxer 会把它转成 avcC，并把每帧码流转成长度前缀格式。
 */
static inline bool SetHwVideoExtradata(AVStream *st, const uint8_t *data, size_t size) {
    AVCodecParameters *par = st->codecpar;
    av_freep(&par->extradata);
    par->extradata = static_cast<uint8_t *>(av_mallocz(size + AV_INPUT_BUFFER_PADDING_SIZE));
    if (par->extradata == nullptr) {
        par->extradata_size = 0;
        return false;
    }
    memcpy(par->extradata, data, size);
    par->extradata_size = static_cast<int>(size);
    return true;
}

/**
 * 把编码器输出的一帧码流打包成 AVPacket，时间戳单位为微秒（kRecorderMicrosecondTimeBase）。
 * 编码器已关闭 B 帧，dts == pts；仍保证 dts 严格递增，防止个别编码器输出的时间戳重复。
 * @param lastDtsUs 上一包的 dts，调用方保存，初始为 -1
 * @return 新分配的包，调用方用 av_packet_free 释放；失败返回 nullptr
 */
static inline AVPacket *MakeHwVideoPacket(const uint8_t *data, size_t size, int64_t ptsUs, bool keyFrame,
                                          int64_t *lastDtsUs) {
    AVPacket *pkt = av_packet_alloc();
    if (pkt == nullptr || av_new_packet(pkt, static_cast<int>(size)) < 0) {
        av_packet_free(&pkt);
        return nullptr;
    }
    memcpy(pkt->data, data, size);
    int64_t dts = ptsUs;
    if (dts <= *lastDtsUs) dts = *lastDtsUs + 1;
    *lastDtsUs = dts;
    pkt->pts = ptsUs < dts ? dts : ptsUs;
    pkt->dts = dts;
    if (keyFrame) pkt->flags |= AV_PKT_FLAG_KEY;
    return pkt;
}

#endif //LEARNFFMPEG_RECORDERMUXER_H
