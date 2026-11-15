# NativeRender 源码解析

> 基于 ANativeWindow 的视频帧渲染实现，是 Android 平台上不经过 OpenGL、直接操作 Surface 像素缓冲区的渲染方案。
>
> 2026-11-15 更新：代码已重构，`RenderVideoFrame` 改为按源图像实际行宽拷贝、检查 `lock` 返回值，并新增零拷贝路径（见 3.5）。下文代码均为当前版本。

---

## 一、ANativeWindow 是什么？

ANativeWindow 是 Android NDK 提供的**原生窗口句柄**，对应 Java 层的 `Surface` 对象。

通过它可以：
- 直接向 Surface 的像素缓冲区写入数据
- 不依赖 OpenGL ES，适合简单、低延迟的视频渲染场景

```
Java 层 Surface  →  ANativeWindow_fromSurface()  →  ANativeWindow*（C++ 层句柄）
```

---

## 二、整体流程

```
构造函数                     Init()                      RenderVideoFrame()          析构函数
获取 ANativeWindow  →  计算目标尺寸 & 配置缓冲格式  →  逐帧拷贝像素数据到缓冲区  →  释放窗口句柄

                                                    或（不录像时）
                                                    LockDirectBuffer() →
                                                    sws_scale 直接写入 →
                                                    UnlockDirectBuffer()
```

---

## 三、关键方法逐一解析

### 3.1 构造函数

```cpp
NativeRender::NativeRender(JNIEnv *env, jobject surface)
    : VideoRender(VIDEO_RENDER_ANWINDOW)
{
    m_NativeWindow = ANativeWindow_fromSurface(env, surface);
}
```

- `ANativeWindow_fromSurface(env, surface)`：从 Java 的 `Surface` 对象获取 native 窗口句柄。
- ANativeWindow 内部有**引用计数**，`fromSurface` 会让引用计数 +1，对应的 `ANativeWindow_release` 会让它 -1，归零后才真正销毁。
- 父类初始化列表 `VideoRender(VIDEO_RENDER_ANWINDOW)` 标记渲染类型为 ANativeWindow 渲染。

---

### 3.2 Init() — 计算目标渲染尺寸

```cpp
void NativeRender::Init(int videoWidth, int videoHeight, int *dstSize)
{
    if(m_NativeWindow == nullptr) return;
    if(videoWidth <= 0 || videoHeight <= 0) return;  // 防止下面除零

    int windowWidth  = ANativeWindow_getWidth(m_NativeWindow);
    int windowHeight = ANativeWindow_getHeight(m_NativeWindow);

    if (windowWidth < windowHeight * videoWidth / videoHeight) {
        m_DstWidth  = windowWidth;
        m_DstHeight = windowWidth * videoHeight / videoWidth;
    } else {
        m_DstWidth  = windowHeight * videoWidth / videoHeight;
        m_DstHeight = windowHeight;
    }

    ANativeWindow_setBuffersGeometry(m_NativeWindow, m_DstWidth,
                                     m_DstHeight, WINDOW_FORMAT_RGBA_8888);
    dstSize[0] = m_DstWidth;
    dstSize[1] = m_DstHeight;
}
```

**宽高比适配逻辑（fitCenter）：**

目标是在窗口内，保持视频原始宽高比，尽可能占满窗口，不裁剪、不拉伸。
判断依据是窗口的"长宽比"和视频的"长宽比"哪个更大：

```
条件：windowWidth < windowHeight * videoWidth / videoHeight
等价：windowWidth / windowHeight < videoWidth / videoHeight
即：窗口宽高比 < 视频宽高比  →  窗口相对"窄"
```

| 情况 | 限制边 | 计算方式 |
|------|--------|---------|
| 窗口比视频"窄" | 以窗口宽为基准 | 高 = 宽 × (视频高 / 视频宽) |
| 窗口比视频"宽" | 以窗口高为基准 | 宽 = 高 × (视频宽 / 视频高) |

**示例：**
```
视频：1920 × 1080（宽高比 16:9）
窗口：1080 × 1920（竖屏，宽高比 9:16）

判断：1080 < 1920 × 1920/1080 ≈ 3413  → 成立，窗口"窄"
结果：DstWidth = 1080，DstHeight = 1080 × 1080/1920 = 607
最终在窗口中渲染 1080×607 的区域，上下留黑边
```

**`ANativeWindow_setBuffersGeometry`：**

设置 Surface 缓冲区的宽、高和像素格式。
- `WINDOW_FORMAT_RGBA_8888`：每个像素用 4 字节存储（R、G、B、A 各 1 字节）。
- 这一步必须在写入像素数据之前调用，否则格式不匹配会导致花屏。

---

### 3.3 RenderVideoFrame() — 帧数据写入缓冲区

```cpp
void NativeRender::RenderVideoFrame(NativeImage *pImage)
{
    if(m_NativeWindow == nullptr || pImage == nullptr || pImage->ppPlane[0] == nullptr) return;

    uint8_t *dstBuffer = nullptr;
    int dstLineSize = 0;                       // 目标缓冲区每行字节数 = stride * 4
    if(!LockDirectBuffer(&dstBuffer, &dstLineSize)) return;   // lock 失败直接放弃这一帧

    // 源图像每行字节数：优先用图像自带的行宽（可能带对齐填充），没有才按 width * 4 计算
    int srcLineSize = pImage->pLineSize[0] > 0 ? pImage->pLineSize[0] : pImage->width * 4;
    int copyBytes = pImage->width * 4;         // 每行真正要拷贝的有效像素字节数
    if(copyBytes > dstLineSize) copyBytes = dstLineSize;
    // 行数取源图像高度和缓冲区高度的较小值，任何一边都不会越界
    int rows = pImage->height < m_NativeWindowBuffer.height ? pImage->height : m_NativeWindowBuffer.height;

    for (int i = 0; i < rows; ++i) {
        memcpy(dstBuffer + i * dstLineSize, pImage->ppPlane[0] + i * srcLineSize, copyBytes);
    }

    UnlockDirectBuffer();                      // 内部调用 ANativeWindow_unlockAndPost
}
```

**和旧版本的区别**（旧版本见 `RenderVideoFrame.md`）：

| 项目 | 旧版本 | 当前版本 | 原因 |
|------|--------|----------|------|
| 源行宽 | 固定 `width * 4` | 优先 `pLineSize[0]` | 源图像本身也可能有行对齐填充，不能假设紧密排列 |
| 拷贝长度 | `srcLineSize` | `width * 4`，且不超过目标行宽 | 只拷有效像素，不把源的填充也拷过去 |
| 行数 | `m_DstHeight` | `min(图像高, 缓冲区高)` | 图像和窗口尺寸不一致时防止越界读写 |
| lock 返回值 | 不检查 | 失败直接返回 | Surface 已销毁时 lock 会失败，继续写会崩溃 |

#### 3.3.1 锁定缓冲区

```cpp
if(ANativeWindow_lock(m_NativeWindow, &m_NativeWindowBuffer, nullptr) != 0) {
    return false;   // 失败（如 Surface 已销毁）时不能继续写
}
```

- `ANativeWindow_lock` 向系统申请一块可写的像素缓冲区，存放在 `m_NativeWindowBuffer` 中。
- 锁定期间，系统保证这块内存不会被显示合成器使用，可以安全写入。
- 第三个参数是"脏区域"（本次更新的矩形范围），传 `nullptr` 表示整个缓冲区都会更新。

锁定后，`m_NativeWindowBuffer` 包含两个关键字段：

| 字段 | 含义 |
|------|------|
| `bits` | 缓冲区起始内存地址 |
| `stride` | 缓冲区每行的**实际像素宽度**（含硬件对齐填充，单位：像素） |

---

#### 3.3.2 理解 stride（内存对齐行宽）

这是整个渲染流程中**最容易踩坑的地方**。

**为什么 stride ≠ width？**

GPU 和显示硬件对内存有**对齐要求**（如每行必须是 64 字节、128 字节的整数倍），以提高内存访问效率。当视频宽度不满足对齐要求时，系统会在每行末尾填充若干空字节，使每行总长度对齐。

```
例子：视频宽度 = 100 像素，硬件要求每行对齐到 128 像素

stride = 128（系统自动填充）

内存实际布局：
行0: [像素0 像素1 ... 像素99] [填充28个像素]
行1: [像素0 像素1 ... 像素99] [填充28个像素]
...
```

**用字节来表达：**

```
srcLineSize（源图像） = width  × 4  = 100 × 4 = 400 字节（紧密排列，无填充）
dstLineSize（目标）  = stride × 4  = 128 × 4 = 512 字节（每行末尾有 112 字节填充）
```

---

#### 3.3.3 逐行 memcpy 详解

```cpp
memcpy(dstBuffer + i * dstLineSize,          // 目标：第 i 行起始地址（按 stride 步进）
       pImage->ppPlane[0] + i * srcLineSize,  // 源：第 i 行起始地址（按源行宽步进）
       copyBytes);                             // 只拷贝有效像素（width * 4），不拷贝填充
```

**为什么不能一次性整块 memcpy？**

如果直接 `memcpy(dstBuffer, pImage->ppPlane[0], width * height * 4)`：

```
源（无填充）：
[行0: 100像素][行1: 100像素][行2: 100像素]...

目标（有填充，stride=128）：
[行0: 128像素空间][行1: 128像素空间]...

一次性拷贝后，源数据会"溢出"到目标行末尾的填充区域，
导致下一行的数据偏移，整个画面向左错位，出现花屏。
```

**逐行拷贝如何解决：**

```
循环第 i 次：
  从源第 i 行起点（偏移 i × 400 字节）
  拷贝 400 字节（100像素的有效数据）
  到目标第 i 行起点（偏移 i × 512 字节）

目标行末尾的 112 字节填充区域不写入，保持原状，
下一行从正确的偏移位置开始，画面不错位。
```

**内存示意图（i=0 和 i=1）：**

```
源内存（ppPlane[0]）:
偏移   0: [行0: R G B A  R G B A  ... ×100]
偏移 400: [行1: R G B A  R G B A  ... ×100]
偏移 800: [行2: ...]

目标内存（dstBuffer, stride=128）:
偏移   0: [行0: R G B A  R G B A  ... ×100 | 填充×28]  ← 第0次 memcpy 写入前400字节
偏移 512: [行1: R G B A  R G B A  ... ×100 | 填充×28]  ← 第1次 memcpy 写入前400字节
偏移1024: [行2: ...]
```

---

#### 3.3.4 提交缓冲区

```cpp
ANativeWindow_unlockAndPost(m_NativeWindow);
```

- 解锁缓冲区，并将其**提交给显示系统**（SurfaceFlinger）。
- 显示系统在下一个 VSync 信号到来时，将该缓冲区合成并显示到屏幕上。
- `lock` 和 `unlockAndPost` 必须成对调用，否则会导致缓冲区泄漏或画面不更新。

---

### 3.4 零拷贝路径：LockDirectBuffer / UnlockDirectBuffer

原流程中一帧数据要经过两次拷贝：`sws_scale` 把 YUV 转成 RGBA 写进中间缓冲 → `RenderVideoFrame` 再逐行 memcpy 到窗口缓冲区。
当前版本在**不录像**时，视频刷新线程（`VideoOutput::Present`）直接锁住窗口缓冲区，让 `sws_scale` 把结果写进去，省掉中间缓冲和一次整帧拷贝：

```cpp
bool NativeRender::LockDirectBuffer(uint8_t **ppBits, int *pLineSize)
{
    if(m_NativeWindow == nullptr) return false;
    if(ANativeWindow_lock(m_NativeWindow, &m_NativeWindowBuffer, nullptr) != 0) return false;
    *ppBits = static_cast<uint8_t *>(m_NativeWindowBuffer.bits);
    *pLineSize = m_NativeWindowBuffer.stride * 4;   // 把 stride 换算成字节交给 sws_scale
    return true;
}

// VideoOutput::Present 中（视频刷新线程）：
if(renderType == VIDEO_RENDER_ANWINDOW && !m_Recorder
   && m_Render->LockDirectBuffer(&windowBits, &windowLineSize)) {
    uint8_t *dstData[4] = {windowBits, nullptr, nullptr, nullptr};
    int dstLineSize[4] = {windowLineSize, 0, 0, 0};
    sws_scale(m_SwsCtx.get(), frame->data, frame->linesize, 0, frame->height, dstData, dstLineSize);
    m_Render->UnlockDirectBuffer();
}
```

- `sws_scale` 的目标行宽直接传 `stride * 4`，它会自己按行步进，stride 填充问题自然解决。
- 录像时仍走旧路径，因为录像需要一份独立的 RGBA 图像交给编码器。
- `LockDirectBuffer` 是 `VideoRender` 的虚函数，默认返回 false；OpenGL 渲染器不支持，会自动走普通路径。

---

### 3.5 析构函数

```cpp
NativeRender::~NativeRender()
{
    if(m_NativeWindow)
        ANativeWindow_release(m_NativeWindow);
}
```

- `ANativeWindow_release` 使引用计数 -1，归零后 ANativeWindow 被系统回收。
- 先判断是否为 `nullptr`，避免重复释放导致崩溃。

---

## 四、整体数据流总结

```
Java Surface
    │
    │  ANativeWindow_fromSurface()
    ▼
ANativeWindow*（m_NativeWindow）
    │
    │  ANativeWindow_setBuffersGeometry()  ← 设置宽高和像素格式
    │
    │  ANativeWindow_lock()               ← 获取可写缓冲区地址（bits）
    ▼
像素缓冲区（dstBuffer）
    ▲                    ▲
    │ 逐行 memcpy         │ 不录像时：sws_scale 直接写入（零拷贝路径）
    │（处理 stride 对齐）   │
NativeImage.ppPlane[0]   AVFrame（YUV）
（RGBA，录像时）
    │
    │  ANativeWindow_unlockAndPost()      ← 提交给 SurfaceFlinger 显示
    ▼
屏幕
```

---

## 五、常见问题

**Q：为什么渲染用 RGBA，而不是 YUV？**

ANativeWindow 本身支持多种像素格式，但直接写入 YUV 需要硬件支持特定格式，兼容性较差。
通常在渲染前通过 FFmpeg 的 `sws_scale` 将 YUV 转换为 RGBA，再写入缓冲区，兼容性最好。

**Q：stride 一定大于等于 width 吗？**

是的。stride 是硬件对齐后的行宽，至少等于 width，通常大于 width。
实际值由系统/驱动决定，代码不能假设 stride == width。

**Q：ANativeWindow 和 OpenGL 渲染有什么区别？**

| 对比项 | ANativeWindow 直接渲染 | OpenGL ES 渲染 |
|--------|----------------------|----------------|
| 复杂度 | 低，直接写内存 | 高，需要配置 EGL/GLSL |
| 灵活性 | 低，仅支持基本像素写入 | 高，支持滤镜、变换等 |
| 性能 | CPU 做颜色转换（sws_scale），适合低分辨率 | GPU 做颜色转换，适合高分辨率 |
| 使用场景 | 简单播放、原型验证 | 生产级播放器、特效处理 |
