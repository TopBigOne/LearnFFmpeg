# recorder：录制模块

相机预览 + 滤镜渲染，以及三种录制：音视频（MP4）、单视频（MP4）、单音频（AAC）。

## 目录结构

| 目录 | 职责 | 文件 |
|---|---|---|
| `context/` | JNI 层入口：创建、调度各录制器，连接相机渲染与录制器（绑定/解绑硬件编码器的输入 Surface、开关 GPU 回读） | `MediaRecorderContext` |
| `pipeline/` | 录制流水线：接收音视频数据 → 编码 → 封装写文件 | `MediaRecorder`（音视频）、`SingleVideoRecorder`（单视频、边播边录）、`SingleAudioRecorder`（单音频） |
| `camera/` | 相机画面的 GL 渲染：滤镜、上屏、回读给软件编码、画到硬件编码器 Surface | `GLCameraRender` |
| `codec/` | 编码与格式转换：MediaCodec 硬件编码、libyuv 图像转换、渲染端向录制器取 pts 的接口 | `HwVideoEncoder`、`YuvConvert`、`VideoEncoderSink` |
| `common/` | 公共基础：统一时钟、MP4 封装辅助、音频数据块 | `RecorderClock.h`、`RecorderMuxer.h`、`AudioChunk.h` |

## 依赖方向

```
context ──> pipeline ──> codec ──> common
   │                       ^
   └──────> camera ────────┘
```

- 只允许按箭头方向依赖：`codec`、`common` 不依赖上层；`camera` 与 `pipeline` 互不依赖，
  二者通过 `codec/VideoEncoderSink.h` 这个接口交互（渲染端向录制器要每帧的 pts），由 `context` 把它们连起来。
- 头文件统一按相对 `recorder/` 的路径引用，例如 `#include "codec/HwVideoEncoder.h"`，从路径就能看出所属模块。
