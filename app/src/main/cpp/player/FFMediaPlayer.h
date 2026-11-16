/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#ifndef LEARNFFMPEG_FFMEDIAPLAYER_H
#define LEARNFFMPEG_FFMEDIAPLAYER_H

#include <memory>
#include <MediaPlayer.h>
#include <render/video/VideoRender.h>
#include "JniEventDispatcher.h"
#include "Player.h"

/**
 * FFmpeg 软解播放器的 JNI 适配层。
 *
 * 播放逻辑全部在 Player（状态机 + 控制线程）里，这里只负责：
 *  - 按渲染类型创建视频渲染器（GL 单例 / NativeRender）；
 *  - 把 MediaPlayer 接口转成 Player 的异步命令；
 *  - 把 Player 的事件交给 JniEventDispatcher，由它在唯一的分发线程里回调 Java。
 *
 * Init 之后立即开始异步 prepare（打开输入、启动解码），完成时上报 MSG_DECODER_READY；
 * Play 可以在 prepare 完成之前调用，完成后自动开始播放。
 */
class FFMediaPlayer : public MediaPlayer, private Player::Listener {
public:
    FFMediaPlayer() {}

    ~FFMediaPlayer() override { UnInit(); }

    void Init(JNIEnv *jniEnv, jobject obj, char *url, int renderType, jobject surface) override;

    /** 同步释放，保证有限时间内返回 */
    void UnInit() override;

    /** 开始 / 从暂停恢复 / 播完后从头播放 */
    void Play() override;

    void Pause() override;

    void Stop() override;

    /** @param position 目标位置（秒） */
    void SeekToPosition(float position) override;

    /** 宽高（像素）、时长（秒），任意线程可调用 */
    long GetMediaParams(int paramType) override;

    /** 目前支持 MEDIA_PARAM_RECORD_PATH（obj 为 jstring），需在 Play 之前调用 */
    void SetMediaParams(int paramType, jobject obj) override;

    bool SetPlaybackSpeed(float speed) override;

private:
    /** 返回当前线程（必须已附加到 JVM，例如 Java 调用线程）的 JNIEnv，不会主动 Attach */
    JNIEnv *GetJNIEnv(bool *isAttach) override;

    /** Java 对象的全局引用由 JniEventDispatcher 持有，这里返回 nullptr */
    jobject GetJavaObj() override;

    JavaVM *GetJavaVM() override;

    /** Player::Listener，在播放器内部线程调用，只做投递 */
    void OnPlayerEvent(int msg, float value) override;

    std::unique_ptr<Player> m_Player;
    std::shared_ptr<JniEventDispatcher> m_Dispatcher;
    VideoRender *m_VideoRender = nullptr;            // GL 渲染器是单例，不持有
    std::unique_ptr<VideoRender> m_OwnedVideoRender;  // NativeRender 由本对象持有
};


#endif //LEARNFFMPEG_FFMEDIAPLAYER_H
