/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */


#include "NativeRender.h"  // 引入 NativeRender 头文件，包含类声明及依赖

// 构造函数：接收 JNI 环境和 Java 层的 Surface 对象，初始化渲染类型为 ANativeWindow
NativeRender::NativeRender(JNIEnv *env, jobject surface): VideoRender(VIDEO_RENDER_ANWINDOW)
{
    // 从 Java Surface 对象获取 ANativeWindow 句柄，后续所有渲染操作都通过它进行
    m_NativeWindow = ANativeWindow_fromSurface(env, surface);
}

// 析构函数：释放 ANativeWindow 资源，防止内存泄漏
NativeRender::~NativeRender()
{
    if(m_NativeWindow)  // 检查句柄是否有效，避免重复释放
        ANativeWindow_release(m_NativeWindow);  // 释放 ANativeWindow，减少引用计数

}

// Init：根据视频分辨率和窗口尺寸，计算目标渲染区域大小并配置 Surface 缓冲区格式
void NativeRender::Init(int videoWidth, int videoHeight, int *dstSize)
{
    // 打印日志：记录窗口句柄和视频原始宽高，便于调试
    LOGCATE("NativeRender::Init m_NativeWindow=%p, video[w,h]=[%d, %d]", m_NativeWindow, videoWidth, videoHeight);
    if(m_NativeWindow == nullptr) return;  // 窗口句柄无效则直接返回，避免空指针崩溃

    int windowWidth = ANativeWindow_getWidth(m_NativeWindow);   // 获取 Surface 实际宽度（单位：像素）
    int windowHeight = ANativeWindow_getHeight(m_NativeWindow); // 获取 Surface 实际高度（单位：像素）

    // 按比例缩放：保持视频宽高比，在窗口内取最大适配尺寸（类似 fitCenter）
    if (windowWidth < windowHeight * videoWidth / videoHeight) {
        // 窗口相对"窄"：以窗口宽为基准，高度等比缩放
        m_DstWidth = windowWidth;
        m_DstHeight = windowWidth * videoHeight / videoWidth;
    } else {
        // 窗口相对"宽"：以窗口高为基准，宽度等比缩放
        m_DstWidth = windowHeight * videoWidth / videoHeight;
        m_DstHeight = windowHeight;
    }
    // 打印日志：输出窗口尺寸和最终目标渲染尺寸
    LOGCATE("NativeRender::Init window[w,h]=[%d, %d],DstSize[w, h]=[%d, %d]", windowWidth, windowHeight, m_DstWidth, m_DstHeight);

    // 设置 Surface 缓冲区的宽高和像素格式为 RGBA_8888（每像素 4 字节）
    ANativeWindow_setBuffersGeometry(m_NativeWindow, m_DstWidth,
                                     m_DstHeight, WINDOW_FORMAT_RGBA_8888);

    dstSize[0] = m_DstWidth;   // 将目标宽度写回调用方
    dstSize[1] = m_DstHeight;  // 将目标高度写回调用方
}

// RenderVideoFrame：将一帧 RGBA 图像数据逐行拷贝到 ANativeWindow 缓冲区并提交显示
void NativeRender::RenderVideoFrame(NativeImage *pImage)
{
    if(m_NativeWindow == nullptr || pImage == nullptr) return;  // 安全检查：句柄或图像数据为空则跳过
    ANativeWindow_lock(m_NativeWindow, &m_NativeWindowBuffer, nullptr);  // 锁定缓冲区，获取可写内存地址
    uint8_t *dstBuffer = static_cast<uint8_t *>(m_NativeWindowBuffer.bits);  // 获取缓冲区起始指针，转为字节指针

    // 计算源图像每行的字节数（width × 4，因为 RGBA 每像素 4 字节）
    int srcLineSize = pImage->width * 4;//RGBA
    // 计算目标缓冲区每行的字节数：stride 是硬件对齐后的行宽（可能大于实际像素宽），× 4 转为字节
    int dstLineSize = m_NativeWindowBuffer.stride * 4;

    // 逐行将源图像数据拷贝到目标缓冲区，处理 stride 不等于 width 的情况
    for (int i = 0; i < m_DstHeight; ++i) {
        // 从源偏移  0 读16字节 → 写到目标偏移  0
        memcpy(dstBuffer + i * dstLineSize, pImage->ppPlane[0] + i * srcLineSize, srcLineSize);
    }

    ANativeWindow_unlockAndPost(m_NativeWindow);  // 解锁缓冲区并提交，触发 Surface 将该帧显示到屏幕

}

// UnInit：反初始化，当前无需清理额外资源（ANativeWindow 在析构函数中释放）
void NativeRender::UnInit()
{
   // * * * *       4x4
   // - - - - - -   6x4

}

