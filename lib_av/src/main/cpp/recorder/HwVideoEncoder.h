/**
 * 基于 MediaCodec（NDK AMediaCodec）的 H.264 硬件编码器，输入为 Surface
 */

#ifndef LEARNFFMPEG_HWVIDEOENCODER_H
#define LEARNFFMPEG_HWVIDEOENCODER_H

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <string>
#include <thread>
#include <media/NdkMediaCodec.h>
#include <android/native_window.h>

/**
 * H.264 硬件编码器。
 *
 * 输入：createInputSurface 得到的 ANativeWindow，由 GL 线程把画面直接画上去（eglSwapBuffers 即送一帧），
 *       不需要 glReadPixels 回读，也不需要 RGBA→YUV 转换；帧的 pts 由 eglPresentationTimeANDROID 指定。
 * 输出：后台线程从编码器取出码流（Annex B 格式），通过回调交给封装模块。
 *
 * Surface 输入需要 Android 8.0（API 26），工程 minSdk 是 23，相关函数运行时用 dlsym 查找。
 * Open 返回 false 时（系统版本不够、没有硬件编码器、配置失败等）调用方应回退到软件编码。
 */
class HwVideoEncoder {
public:
    /** 编码输出回调（在编码器输出线程上调用） */
    class Callback {
    public:
        virtual ~Callback() = default;

        /** SPS/PPS（Annex B），在第一帧码流之前到达，只回调一次 */
        virtual void OnVideoConfig(const uint8_t *data, size_t size) = 0;

        /** 一帧码流（Annex B），ptsUs 为 eglPresentationTimeANDROID 指定的时间（微秒） */
        virtual void OnVideoPacket(const uint8_t *data, size_t size, int64_t ptsUs, bool keyFrame) = 0;
    };

    HwVideoEncoder() = default;

    ~HwVideoEncoder();

    HwVideoEncoder(const HwVideoEncoder &) = delete;
    HwVideoEncoder &operator=(const HwVideoEncoder &) = delete;

    /**
     * 创建并启动编码器。只接受硬件编码器：系统默认给出的是软件实现（OMX.google.* / c2.android.*）时返回 false，
     * 由调用方回退到 x264。
     * @return true 成功；false 不支持或失败（已释放全部资源）
     */
    bool Open(int width, int height, int bitRate, int fps);

    /** 编码器的输入 Surface，Open 成功后有效，Stop 后失效 */
    ANativeWindow *GetInputWindow() const { return m_InputWindow; }

    /** 编码器名称（Android 9 以下拿不到时为空） */
    const std::string &GetName() const { return m_Name; }

    /** 启动输出线程 */
    void Start(Callback *callback);

    /**
     * 停止：通知输入结束，取出剩余码流直到 EOS（最多等待 2 秒），然后释放编码器。可重复调用。
     * 调用前必须保证已没有线程再往输入 Surface 上画（GL 侧已解绑）。
     */
    void Stop();

private:
    void DrainLoop();

    void Release();

    AMediaCodec *m_Codec = nullptr;
    ANativeWindow *m_InputWindow = nullptr;
    std::string m_Name;
    Callback *m_Callback = nullptr;
    std::thread *m_DrainThread = nullptr;
    std::atomic<bool> m_EosSignaled{false};
    std::atomic<int64_t> m_EosDeadlineUs{0};
    bool m_ConfigSent = false;
};

#endif //LEARNFFMPEG_HWVIDEOENCODER_H
