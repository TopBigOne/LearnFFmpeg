/**
 * 基于 MediaCodec（NDK AMediaCodec）的 H.264 硬件编码器
 */

#ifndef LEARNFFMPEG_HWVIDEOENCODER_H
#define LEARNFFMPEG_HWVIDEOENCODER_H

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <media/NdkMediaCodec.h>
#include <android/native_window.h>
#include <ImageDef.h>
#include "codec/YuvConvert.h"

/**
 * H.264 硬件编码器，两种输入方式：
 *
 *  - Surface：createInputSurface 得到 ANativeWindow，由 GL 线程把画面直接画上去（eglSwapBuffers 即送一帧），
 *    不需要 glReadPixels 回读和 RGBA→YUV 转换，帧的 pts 由 eglPresentationTimeANDROID 指定。
 *    适合画面本来就在 GL 里的场景（相机录制）。需要 Android 8.0（API 26）。
 *  - Buffer：调用方用 EncodeImage 送入 CPU 内存里的图像，转换成编码器要求的 NV12 / I420 后写入输入缓冲。
 *    适合画面在 CPU 内存里的场景（边播边录的解码帧）。Android 5.0 起可用。
 *
 * 输出：后台线程从编码器取出码流（Annex B 格式），通过回调交给封装模块。
 * 工程 minSdk 是 23，API 26/28 才有的函数运行时用 dlsym 查找。
 * Open 返回 false 时（系统版本不够、没有硬件编码器、配置失败等）调用方应回退到软件编码。
 */
class HwVideoEncoder {
public:
    enum class InputMode {
        Surface,
        Buffer,
    };

    /** 编码输出回调（在编码器输出线程上调用） */
    class Callback {
    public:
        virtual ~Callback() = default;

        /** SPS/PPS（Annex B），在第一帧码流之前到达，只回调一次 */
        virtual void OnVideoConfig(const uint8_t *data, size_t size) = 0;

        /** 一帧码流（Annex B），ptsUs 为送入时指定的时间（微秒） */
        virtual void OnVideoPacket(const uint8_t *data, size_t size, int64_t ptsUs, bool keyFrame) = 0;
    };

    HwVideoEncoder() = default;

    ~HwVideoEncoder();

    HwVideoEncoder(const HwVideoEncoder &) = delete;
    HwVideoEncoder &operator=(const HwVideoEncoder &) = delete;

    /**
     * 调试开关：adb shell setprop debug.learnffmpeg.sw_encode 1 时所有录制都不用硬件编码，用于验证回退路径
     */
    static bool IsDisabledByDebugProperty();

    /**
     * 创建并启动编码器。只接受硬件编码器：系统默认给出的是软件实现（OMX.google.* / c2.android.*）时返回 false，
     * 由调用方回退到 x264。
     * @return true 成功；false 不支持或失败（已释放全部资源）
     */
    bool Open(InputMode mode, int width, int height, int bitRate, int fps);

    /** Surface 模式下编码器的输入 Surface，Open 成功后有效，Stop 后失效 */
    ANativeWindow *GetInputWindow() const { return m_InputWindow; }

    /** 编码器名称（Android 9 以下拿不到时为空） */
    const std::string &GetName() const { return m_Name; }

    /** 启动输出线程 */
    void Start(Callback *callback);

    /**
     * Buffer 模式：送入一帧（任意尺寸，RGBA / NV21 / NV12 / I420），按编码器尺寸缩放并转换格式。
     * 只能在同一个线程上调用。编码器暂时没有空闲输入缓冲时等待最多 kInputTimeoutUs，仍没有则丢弃本帧。
     * @return true 已送入
     */
    bool EncodeImage(const NativeImage &image, int64_t ptsUs);

    /**
     * 停止：通知输入结束，取出剩余码流直到 EOS（最多等待 2 秒），然后释放编码器。可重复调用。
     * Surface 模式下调用前必须保证已没有线程再往输入 Surface 上画（GL 侧已解绑）；
     * Buffer 模式下调用前必须保证不再调用 EncodeImage。
     */
    void Stop();

private:
    bool Configure(int width, int height, int bitRate, int fps, int32_t colorFormat);

    void SignalEndOfStream();

    void DrainLoop();

    void Release();

    InputMode m_Mode = InputMode::Surface;
    AMediaCodec *m_Codec = nullptr;
    ANativeWindow *m_InputWindow = nullptr;
    std::string m_Name;
    Callback *m_Callback = nullptr;
    std::thread *m_DrainThread = nullptr;
    std::atomic<bool> m_EosSignaled{false};
    std::atomic<int64_t> m_EosDeadlineUs{0};
    bool m_ConfigSent = false;

    // ---- Buffer 模式的输入布局 ----
    int m_Width = 0;
    int m_Height = 0;
    int32_t m_ColorFormat = 0;      // COLOR_FormatYUV420SemiPlanar(NV12) 或 COLOR_FormatYUV420Planar(I420)
    int m_Stride = 0;               // 每行字节数（编码器可能要求对齐）
    int m_SliceHeight = 0;          // Y 平面的行数（编码器可能要求对齐），色度平面紧随其后
    EncoderFrameConverter m_Converter;
    std::vector<uint8_t> m_I420;    // NV12 编码器：先转成 I420 再交错成 NV12
};

#endif //LEARNFFMPEG_HWVIDEOENCODER_H
