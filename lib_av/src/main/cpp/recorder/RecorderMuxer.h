/**
 * 录制模块写文件头的公共逻辑
 */

#ifndef LEARNFFMPEG_RECORDERMUXER_H
#define LEARNFFMPEG_RECORDERMUXER_H

#include <cstring>
#include <LogUtil.h>

extern "C" {
#include <libavformat/avformat.h>
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

#endif //LEARNFFMPEG_RECORDERMUXER_H
