/**
 * 基于 MediaCodec（NDK AMediaCodec）的 H.264 硬件编码器
 */

#include "HwVideoEncoder.h"
#include "RecorderClock.h"
#include <LogUtil.h>
#include <dlfcn.h>
#include <cstring>
#include <libyuv.h>
#include <media/NdkMediaFormat.h>
#include <sys/system_properties.h>

namespace {

const char *kMimeAvc = "video/avc";
const int32_t kColorFormatSurface = 0x7F000789;          // COLOR_FormatSurface
const int32_t kColorFormatYUV420Planar = 19;             // COLOR_FormatYUV420Planar（I420）
const int32_t kColorFormatYUV420SemiPlanar = 21;         // COLOR_FormatYUV420SemiPlanar（NV12）
const uint32_t kBufferFlagKeyFrame = 1;                  // AMEDIACODEC_BUFFER_FLAG_KEY_FRAME（头文件标注 API 34，值从 API 21 起就是 1）
const int64_t kDequeueTimeoutUs = 10000;
const int64_t kInputTimeoutUs = 20000;                   // Buffer 模式等待空闲输入缓冲的时间
const int64_t kEosWaitUs = 2000000;                      // 停止时最多等 2 秒拿到 EOS

/** API 26+ / 28+ 的函数，minSdk 23 不能直接链接，运行时查找 */
struct MediaCodecApi {
    media_status_t (*createInputSurface)(AMediaCodec *, ANativeWindow **) = nullptr;   // API 26
    media_status_t (*signalEndOfInputStream)(AMediaCodec *) = nullptr;                 // API 26
    media_status_t (*getName)(AMediaCodec *, char **) = nullptr;                       // API 28
    void (*releaseName)(AMediaCodec *, char *) = nullptr;                              // API 28
    AMediaFormat *(*getInputFormat)(AMediaCodec *) = nullptr;                          // API 28
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
            a.getInputFormat = reinterpret_cast<decltype(a.getInputFormat)>(dlsym(lib, "AMediaCodec_getInputFormat"));
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

bool HwVideoEncoder::IsDisabledByDebugProperty() {
    char value[PROP_VALUE_MAX] = {0};
    return __system_property_get("debug.learnffmpeg.sw_encode", value) > 0 && strcmp(value, "1") == 0;
}

HwVideoEncoder::~HwVideoEncoder() {
    Stop();
}

bool HwVideoEncoder::Open(InputMode mode, int width, int height, int bitRate, int fps) {
    const MediaCodecApi &api = GetApi();
    if (mode == InputMode::Surface && (api.createInputSurface == nullptr || api.signalEndOfInputStream == nullptr)) {
        LOGCATE("HwVideoEncoder::Open Surface input requires Android 8.0+, fall back to software");
        return false;
    }
    m_Mode = mode;
    m_Width = width;
    m_Height = height;

    // Surface 输入固定用 COLOR_FormatSurface；Buffer 输入先试 NV12（几乎所有硬件编码器都支持），再试 I420
    const int32_t surfaceFormats[] = {kColorFormatSurface};
    const int32_t bufferFormats[] = {kColorFormatYUV420SemiPlanar, kColorFormatYUV420Planar};
    const int32_t *formats = mode == InputMode::Surface ? surfaceFormats : bufferFormats;
    const int formatCount = mode == InputMode::Surface ? 1 : 2;
    bool configured = false;
    for (int i = 0; i < formatCount && !configured; ++i) {
        configured = Configure(width, height, bitRate, fps, formats[i]);
        if (!configured && m_Codec == nullptr && !m_Name.empty() && IsSoftwareCodec(m_Name)) return false;
    }
    if (!configured) return false;

    if (mode == InputMode::Surface) {
        media_status_t status = api.createInputSurface(m_Codec, &m_InputWindow);
        if (status != AMEDIA_OK || m_InputWindow == nullptr) {
            LOGCATE("HwVideoEncoder::Open createInputSurface fail status=%d", status);
            Release();
            return false;
        }
    }

    media_status_t status = AMediaCodec_start(m_Codec);
    if (status != AMEDIA_OK) {
        LOGCATE("HwVideoEncoder::Open start fail status=%d", status);
        Release();
        return false;
    }

    if (mode == InputMode::Buffer) {
        // 输入缓冲的行宽、Y 平面行数：编码器可能要求对齐（例如 16 或 64），以编码器给出的为准
        m_Stride = width;
        m_SliceHeight = height;
        if (api.getInputFormat != nullptr) {
            AMediaFormat *inputFormat = api.getInputFormat(m_Codec);
            if (inputFormat != nullptr) {
                int32_t stride = 0, sliceHeight = 0;
                if (AMediaFormat_getInt32(inputFormat, "stride", &stride) && stride >= width) m_Stride = stride;
                if (AMediaFormat_getInt32(inputFormat, "slice-height", &sliceHeight) && sliceHeight >= height) m_SliceHeight = sliceHeight;
                AMediaFormat_delete(inputFormat);
            }
        }
        LOGCATE("HwVideoEncoder::Open buffer input colorFormat=%d stride=%d sliceHeight=%d",
                m_ColorFormat, m_Stride, m_SliceHeight);
    }
    return true;
}

bool HwVideoEncoder::Configure(int width, int height, int bitRate, int fps, int32_t colorFormat) {
    // 每次尝试都新建编码器：configure 失败后编码器状态不确定
    Release();
    m_Codec = AMediaCodec_createEncoderByType(kMimeAvc);
    if (m_Codec == nullptr) {
        LOGCATE("HwVideoEncoder::Configure no H.264 encoder");
        return false;
    }

    const MediaCodecApi &api = GetApi();
    m_Name.clear();
    if (api.getName != nullptr) {
        char *name = nullptr;
        if (api.getName(m_Codec, &name) == AMEDIA_OK && name != nullptr) {
            m_Name = name;
            if (api.releaseName != nullptr) api.releaseName(m_Codec, name);
        }
    }
    LOGCATE("HwVideoEncoder::Configure encoder=%s [w,h]=[%d,%d] bitRate=%d fps=%d colorFormat=0x%x",
            m_Name.empty() ? "(unknown)" : m_Name.c_str(), width, height, bitRate, fps, colorFormat);
    if (IsSoftwareCodec(m_Name)) {
        LOGCATE("HwVideoEncoder::Configure %s is a software encoder, fall back to x264", m_Name.c_str());
        Release();
        return false;
    }

    AMediaFormat *format = AMediaFormat_new();
    AMediaFormat_setString(format, "mime", kMimeAvc);
    AMediaFormat_setInt32(format, "width", width);
    AMediaFormat_setInt32(format, "height", height);
    AMediaFormat_setInt32(format, "color-format", colorFormat);
    AMediaFormat_setInt32(format, "bitrate", bitRate);
    AMediaFormat_setInt32(format, "frame-rate", fps);
    AMediaFormat_setInt32(format, "i-frame-interval", 1);   // 约 1 秒一个关键帧，与分段 MP4 的切段间隔一致
    // 不要 B 帧：MediaCodec 只输出 pts 不输出 dts，没有 B 帧时 dts == pts（不支持该键的系统会忽略）
    AMediaFormat_setInt32(format, "max-bframes", 0);
    media_status_t status = AMediaCodec_configure(m_Codec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    AMediaFormat_delete(format);
    if (status != AMEDIA_OK) {
        LOGCATE("HwVideoEncoder::Configure configure fail status=%d colorFormat=0x%x", status, colorFormat);
        Release();
        return false;
    }
    m_ColorFormat = colorFormat;
    return true;
}

void HwVideoEncoder::Start(Callback *callback) {
    if (m_Codec == nullptr || m_DrainThread != nullptr) return;
    m_Callback = callback;
    m_DrainThread = new std::thread(&HwVideoEncoder::DrainLoop, this);
}

bool HwVideoEncoder::EncodeImage(const NativeImage &image, int64_t ptsUs) {
    if (m_Codec == nullptr || m_Mode != InputMode::Buffer || m_EosSignaled) return false;
    ssize_t index = AMediaCodec_dequeueInputBuffer(m_Codec, kInputTimeoutUs);
    if (index < 0) {
        LOGCATV("HwVideoEncoder::EncodeImage no input buffer, drop frame");
        return false;
    }
    size_t capacity = 0;
    uint8_t *buffer = AMediaCodec_getInputBuffer(m_Codec, static_cast<size_t>(index), &capacity);
    const size_t ySize = static_cast<size_t>(m_Stride) * m_SliceHeight;
    // 色度部分只写到最后一行有效数据：最后一个平面不要求补齐到 sliceHeight
    const size_t needed = m_ColorFormat == kColorFormatYUV420SemiPlanar
                          ? ySize + static_cast<size_t>(m_Stride) * ((m_Height + 1) / 2)
                          : ySize + static_cast<size_t>(m_Stride / 2) * (m_SliceHeight / 2) + static_cast<size_t>(m_Stride / 2) * ((m_Height + 1) / 2);
    if (buffer == nullptr || capacity < needed) {
        LOGCATE("HwVideoEncoder::EncodeImage input buffer too small capacity=%zu needed=%zu", capacity, needed);
        AMediaCodec_queueInputBuffer(m_Codec, static_cast<size_t>(index), 0, 0, ptsUs, 0);
        return false;
    }

    int ret;
    if (m_ColorFormat == kColorFormatYUV420SemiPlanar) {
        // 先转成编码尺寸的 I420，再交错成 NV12 写入输入缓冲（libyuv 两步都很快）
        const int halfW = (m_Width + 1) / 2, halfH = (m_Height + 1) / 2;
        const size_t i420Size = static_cast<size_t>(m_Width) * m_Height + static_cast<size_t>(halfW) * halfH * 2;
        if (m_I420.size() != i420Size) m_I420.resize(i420Size);
        uint8_t *y = m_I420.data();
        uint8_t *u = y + static_cast<size_t>(m_Width) * m_Height;
        uint8_t *v = u + static_cast<size_t>(halfW) * halfH;
        ret = m_Converter.ConvertToI420(image, y, m_Width, u, halfW, v, halfW, m_Width, m_Height);
        if (ret == 0) {
            ret = libyuv::I420ToNV12(y, m_Width, u, halfW, v, halfW,
                                     buffer, m_Stride, buffer + ySize, m_Stride, m_Width, m_Height);
        }
    } else {
        uint8_t *u = buffer + ySize;
        uint8_t *v = u + static_cast<size_t>(m_Stride / 2) * (m_SliceHeight / 2);
        ret = m_Converter.ConvertToI420(image, buffer, m_Stride, u, m_Stride / 2, v, m_Stride / 2, m_Width, m_Height);
    }
    if (ret != 0) {
        AMediaCodec_queueInputBuffer(m_Codec, static_cast<size_t>(index), 0, 0, ptsUs, 0);
        return false;
    }
    media_status_t status = AMediaCodec_queueInputBuffer(m_Codec, static_cast<size_t>(index), 0, needed, ptsUs, 0);
    if (status != AMEDIA_OK) {
        LOGCATE("HwVideoEncoder::EncodeImage queueInputBuffer fail status=%d", status);
        return false;
    }
    return true;
}

void HwVideoEncoder::SignalEndOfStream() {
    if (m_Mode == InputMode::Surface) {
        media_status_t status = GetApi().signalEndOfInputStream(m_Codec);
        if (status != AMEDIA_OK) {
            LOGCATE("HwVideoEncoder::SignalEndOfStream signalEndOfInputStream fail status=%d", status);
        }
        return;
    }
    // Buffer 模式：送一个带 EOS 标志的空输入缓冲
    ssize_t index = AMediaCodec_dequeueInputBuffer(m_Codec, kEosWaitUs / 4);
    if (index >= 0) {
        AMediaCodec_queueInputBuffer(m_Codec, static_cast<size_t>(index), 0, 0, 0,
                                     AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
    } else {
        LOGCATE("HwVideoEncoder::SignalEndOfStream no input buffer for EOS");
    }
}

void HwVideoEncoder::Stop() {
    if (m_Codec == nullptr) return;
    if (m_DrainThread != nullptr) {
        // 输入结束：编码器输出完缓存的帧后给出 EOS，输出线程据此退出（超时也会退出）
        m_EosDeadlineUs = GetBootTimeUs() + kEosWaitUs;
        SignalEndOfStream();
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
