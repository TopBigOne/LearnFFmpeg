/**
 *
 * Created by 公众号：字节流动 on 2021/12/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#include "PlayerWrapper.h"

/**
 * @brief 初始化播放器包装器
 * @param jniEnv JNI环境指针
 * @param obj Java对象
 * @param url 媒体文件路径
 * @param playerType 播放器类型（FFmpeg软解/硬件解码）
 * @param renderType 渲染类型
 * @param surface Android Surface对象
 *
 * 根据playerType创建不同的播放器实例：
 * - FFMEDIA_PLAYER: FFmpeg软解播放器
 * - HWCODEC_PLAYER: 硬件解码播放器
 */
void PlayerWrapper::Init(JNIEnv *jniEnv, jobject obj, char *url, int playerType, int renderType,
                         jobject surface) {
    // 根据播放器类型创建对应的播放器实例
    switch (playerType) {
        case FFMEDIA_PLAYER:
            m_MediaPlayer = new FFMediaPlayer();
            break;
        case HWCODEC_PLAYER:
            m_MediaPlayer = new HWCodecPlayer();
            break;
        default:
            break;
    }

    // 初始化播放器
    if(m_MediaPlayer)
        m_MediaPlayer->Init(jniEnv, obj, url, renderType, surface);
}

/**
 * @brief 释放播放器包装器资源
 */
void PlayerWrapper::UnInit() {
    if(m_MediaPlayer) {
        m_MediaPlayer->UnInit();
        delete m_MediaPlayer;
        m_MediaPlayer = nullptr;
    }
}

/**
 * @brief 开始播放
 */
void PlayerWrapper::Play() {
    if(m_MediaPlayer) {
        m_MediaPlayer->Play();
    }
}

/**
 * @brief 暂停播放
 */
void PlayerWrapper::Pause() {
    if(m_MediaPlayer) {
        m_MediaPlayer->Pause();
    }
}

/**
 * @brief 停止播放
 */
void PlayerWrapper::Stop() {
    if(m_MediaPlayer) {
        m_MediaPlayer->Stop();
    }
}

/**
 * @brief 跳转到指定位置
 * @param position 跳转位置（0.0-1.0之间）
 */
void PlayerWrapper::SeekToPosition(float position) {
    if(m_MediaPlayer) {
        m_MediaPlayer->SeekToPosition(position);
    }

}

/**
 * @brief 获取媒体参数
 * @param paramType 参数类型
 * @return 参数值
 */
long PlayerWrapper::GetMediaParams(int paramType) {
    if(m_MediaPlayer) {
        return m_MediaPlayer->GetMediaParams(paramType);
    }

    return 0;
}

/**
 * @brief 设置媒体参数
 * @param paramType 参数类型
 * @param obj 参数对象
 */
void PlayerWrapper::SetMediaParams(int paramType, jobject obj) {
    if(m_MediaPlayer) {
        m_MediaPlayer->SetMediaParams(paramType, obj);
    }

}
