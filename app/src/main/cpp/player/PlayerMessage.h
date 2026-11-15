/**
 * 播放器上报给 Java 层的消息类型，数值与 FFMediaPlayer.java 中的 MSG_* 常量一一对应。
 */

#ifndef LEARNFFMPEG_PLAYERMESSAGE_H
#define LEARNFFMPEG_PLAYERMESSAGE_H

enum PlayerMessage {
    MSG_DECODER_INIT_ERROR = 0,     // prepare 失败（打开输入 / 解码器 / 输出失败），value 为 MEDIA_ERROR_*
    MSG_DECODER_READY      = 1,     // prepare 完成，可以查询宽高、时长
    MSG_DECODER_DONE       = 2,     // 播放完成
    MSG_REQUEST_RENDER     = 3,     // 有新的一帧交给了渲染器（GLSurfaceView 需要 requestRender）
    MSG_DECODING_TIME      = 4,     // 播放进度（秒）
    MSG_PLAYER_ERROR       = 5      // 播放过程中出错（IO 错误、网络超时），value 为 MEDIA_ERROR_*
};

/**
 * 上报给 Java 的错误类别（取值与 android.media.MediaPlayer 的 MEDIA_ERROR_* 一致）。
 * 不直接上报 FFmpeg 错误码：消息值是 float，FFERRTAG 类错误码超出 float 的精确整数范围；
 * 原始错误码会打印在日志里。
 */
enum PlayerError {
    MEDIA_ERROR_UNKNOWN     = 1,
    MEDIA_ERROR_IO          = -1004,
    MEDIA_ERROR_MALFORMED   = -1007,
    MEDIA_ERROR_UNSUPPORTED = -1010,
    MEDIA_ERROR_TIMED_OUT   = -110
};

#endif //LEARNFFMPEG_PLAYERMESSAGE_H
