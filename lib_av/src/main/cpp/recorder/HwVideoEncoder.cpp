/**
 * 基于 MediaCodec（NDK AMediaCodec）的 H.264 硬件编码器，输入为 Surface
 */

#include "HwVideoEncoder.h"
#include "RecorderClock.h"
#include <LogUtil.h>
#include <dlfcn.h>
#include <cstring>
#include <vector>
#include <media/NdkMediaFormat.h>

namespace {

const char *kMimeAvc = "video/avc";
const int32_t kColorFormatSurface = 0x7F000789;   // MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface
const uint32_t kBufferFlagKeyFrame = 1;            // AMEDIACODEC_BUFFER_FLAG_KEY_FRAME（头文件里标注 API 34，值从 API 21 起就是 1）
const int64_t kDequeueTimeoutUs = 10000;
const int64_t kEosWaitUs = 2000000;                // 停止时最多等 2 秒拿到 EOS

/** API 26+ / 28+ 的函数，minSdk 23 不能直接链接，运行时查找 */
struct MediaCodecApi {
    media_status_t (*createInputSurface)(AMediaCodec *, ANativeWindow **) = nullptr;   // API 26
    media_status_t (*signalEndOfInputStream)(AMediaCodec *) = nullptr;                 // API 26
    media_status_t (*getName)(AMediaCodec *, char **) = nullptr;                       // API 28
    void (*releaseName)(AMediaCodec *, char *) = nullptr;                              // API 28
};

const MediaCodecApi &GetApi() {
    static MediaCodecApi api = [] {
        MediaCodecApi a;
        void *lib = dlopen("libmediandk.so", RTLD_NOW);
        if (lib != nullptr) {
            a.createInputSurface = reinterpret_cast<decltype(a.createInputSurface)>(dlsym(lib, "AMediaCodec_createInputSurface"));
            a.signalEndOfInputStream = reinterpret_cast<decltype(a.signalEndOfInputStream)>(dlsym(lib, "AMediaCodec_signalEndOfInputStream"));
            a.getName = reinterpret_cast<decltype(a.getName)>(dlsym(lib, "AMediaCodec_getName"));
            a.releaseName = reinterpret_cast<decltype(a.releaseName)>(dlsym(lib, "AMediaCodec_releaseName"));
            // 不 dlclose：libmediandk 本来就被本库链接，常驻进程
        }
        return a;
    }();
    return api;
}

/** Google 自带的软件编码器，名字有固定前缀 */
bool IsSoftwareCodec(const std::string &name) {
    return name.rfind("OMX.google.", 0) == 0 || name.rfind("c2.android.", 0) == 0;
}

}

HwVideoEncoder::~HwVideoEncoder() {
    Stop();
}

bool HwVideoEncoder::Open(int width, int height, int bitRate, int fps) {
    const MediaCodecApi &api = GetApi();
    if (api.createInputSurface == nullptr || api.signalEndOfInputStream == nullptr) {
        LOGCATE("HwVideoEncoder::Open Surface input requires Android 8.0+, fall back to software");
        return false;
    }

    m_Codec = AMediaCodec_createEncoderByType(kMimeAvc);
    if (m_Codec == nullptr) {
        LOGCATE("HwVideoEncoder::Open no H.264 encoder");
        return false;
    }

    if (api.getName != nullptr) {
        char *name = nullptr;
        if (api.getName(m_Codec, &name) == AMEDIA_OK && name != nullptr) {
            m_Name = name;
            if (api.releaseName != nullptr) api.releaseName(m_Codec, name);
        }
    }
    LOGCATE("HwVideoEncoder::Open encoder=%s [w,h]=[%d,%d] bitRate=%d fps=%d",
            m_Name.empty() ? "(unknown)" : m_Name.c_str(), width, height, bitRate, fps);
    if (IsSoftwareCodec(m_Name)) {
        LOGCATE("HwVideoEncoder::Open %s is a software encoder, fall back to x264", m_Name.c_str());
        Release();
        return false;
    }

    AMediaFormat *format = AMediaFormat_new();
    AMediaFormat_setString(format, "mime", kMimeAvc);
    AMediaFormat_setInt32(format, "width", width);
    AMediaFormat_setInt32(format, "height", height);
    AMediaFormat_setInt32(format, "color-format", kColorFormatSurface);
    AMediaFormat_setInt32(format, "bitrate", bitRate);
    AMediaFormat_setInt32(format, "frame-rate", fps);
    AMediaFormat_setInt32(format, "i-frame-interval", 1);   // 约 1 秒一个关键帧，与分段 MP4 的切段间隔一致
    // 不要 B 帧：MediaCodec 只输出 pts 不输出 dts，没有 B 帧时 dts == pts（不支持该键的系统会忽略）
    AMediaFormat_setInt32(format, "max-bframes", 0);
    media_status_t status = AMediaCodec_configure(m_Codec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    AMediaFormat_delete(format);
    if (status != AMEDIA_OK) {
        LOGCATE("HwVideoEncoder::Open configure fail status=%d", status);
        Release();
        return false;
    }

    status = api.createInputSurface(m_Codec, &m_InputWindow);
    if (status != AMEDIA_OK || m_InputWindow == nullptr) {
        LOGCATE("HwVideoEncoder::Open createInputSurface fail status=%d", status);
        Release();
        return false;
    }

    status = AMediaCodec_start(m_Codec);
    if (status != AMEDIA_OK) {
        LOGCATE("HwVideoEncoder::Open start fail status=%d", status);
        Release();
        return false;
    }
    return true;
}

void HwVideoEncoder::Start(Callback *callback) {
    if (m_Codec == nullptr || m_DrainThread != nullptr) return;
    m_Callback = callback;
    m_DrainThread = new std::thread(&HwVideoEncoder::DrainLoop, this);
}

void HwVideoEncoder::Stop() {
    if (m_Codec == nullptr) return;
    if (m_DrainThread != nullptr) {
        // 输入结束：编码器输出完缓存的帧后给出 EOS，输出线程据此退出
        m_EosDeadlineUs = GetBootTimeUs() + kEosWaitUs;
        media_status_t status = GetApi().signalEndOfInputStream(m_Codec);
        if (status != AMEDIA_OK) {
            LOGCATE("HwVideoEncoder::Stop signalEndOfInputStream fail status=%d", status);
        }
        m_EosSignaled = true;
        m_DrainThread->join();
        delete m_DrainThread;
        m_DrainThread = nullptr;
    }
    Release();
}

void HwVideoEncoder::Release() {
    if (m_Codec != nullptr) {
        AMediaCodec_stop(m_Codec);
        AMediaCodec_delete(m_Codec);
        m_Codec = nullptr;
    }
    if (m_InputWindow != nullptr) {
        ANativeWindow_release(m_InputWindow);
        m_InputWindow = nullptr;
    }
}

void HwVideoEncoder::DrainLoop() {
    LOGCATE("HwVideoEncoder::DrainLoop start");
    std::vector<uint8_t> config;
    for (;;) {
        AMediaCodecBufferInfo info;
        ssize_t index = AMediaCodec_dequeueOutputBuffer(m_Codec, &info, kDequeueTimeoutUs);
        if (index >= 0) {
            size_t capacity = 0;
            uint8_t *buffer = AMediaCodec_getOutputBuffer(m_Codec, static_cast<size_t>(index), &capacity);
            bool eos = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            if (buffer != nullptr && info.size > 0
                && static_cast<size_t>(info.offset) + static_cast<size_t>(info.size) <= capacity) {
                const uint8_t *data = buffer + info.offset;
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) {
                    // SPS/PPS 以单独的 codec config 缓冲给出（有的设备只走 FORMAT_CHANGED，二者取先到的）
                    if (!m_ConfigSent) {
                        m_ConfigSent = true;
                        m_Callback->OnVideoConfig(data, info.size);
                    }
                } else {
                    m_Callback->OnVideoPacket(data, info.size, info.presentationTimeUs,
                                              (info.flags & kBufferFlagKeyFrame) != 0);
                }
            }
            AMediaCodec_releaseOutputBuffer(m_Codec, static_cast<size_t>(index), false);
            if (eos) {
                LOGCATE("HwVideoEncoder::DrainLoop got EOS");
                break;
            }
        } else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat *format = AMediaCodec_getOutputFormat(m_Codec);
            if (format != nullptr) {
                LOGCATE("HwVideoEncoder::DrainLoop output format: %s", AMediaFormat_toString(format));
                void *sps = nullptr, *pps = nullptr;
                size_t spsSize = 0, ppsSize = 0;
                if (!m_ConfigSent && AMediaFormat_getBuffer(format, "csd-0", &sps, &spsSize) && spsSize > 0) {
                    config.assign(static_cast<uint8_t *>(sps), static_cast<uint8_t *>(sps) + spsSize);
                    if (AMediaFormat_getBuffer(format, "csd-1", &pps, &ppsSize) && ppsSize > 0) {
                        config.insert(config.end(), static_cast<uint8_t *>(pps), static_cast<uint8_t *>(pps) + ppsSize);
                    }
                    m_ConfigSent = true;
                    m_Callback->OnVideoConfig(config.data(), config.size());
                }
                AMediaFormat_delete(format);
            }
        } else if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            if (m_EosSignaled && GetBootTimeUs() > m_EosDeadlineUs) {
                LOGCATE("HwVideoEncoder::DrainLoop wait EOS timeout");
                break;
            }
        } else if (index != AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            LOGCATE("HwVideoEncoder::DrainLoop dequeueOutputBuffer error %zd", index);
            break;
        }
    }
    LOGCATE("HwVideoEncoder::DrainLoop end");
}
