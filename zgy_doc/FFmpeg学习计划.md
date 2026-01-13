# FFmpeg 系统学习计划

## 目录
1. [学习目标](#学习目标)
2. [学习路线](#学习路线)
3. [阶段详细规划](#阶段详细规划)
4. [实战项目](#实战项目)
5. [学习资源](#学习资源)
6. [评估标准](#评估标准)

---

## 学习目标

### 短期目标（1-2个月）
- 掌握 FFmpeg 基础 API 和核心概念
- 能独立实现简单的音视频播放器
- 理解音视频同步原理并实现
- 掌握基本的音视频编解码流程

### 中期目标（3-4个月）
- 掌握 OpenGL ES 渲染和视频滤镜
- 实现音视频录制和编码
- 理解硬件加速解码原理
- 能处理常见的性能优化问题

### 长期目标（5-6个月）
- 深入理解 FFmpeg 源码架构
- 能实现复杂的音视频特效
- 掌握流媒体协议和直播技术
- 构建完整的商业级播放器

---

## 学习路线

```
基础准备 → 解码播放 → 编码录制 → 高级特性 → 性能优化 → 项目实战
   ↓          ↓          ↓          ↓          ↓          ↓
 (1周)     (3-4周)    (2-3周)    (3-4周)    (2-3周)    (持续)
```

---

## 阶段详细规划

### 第一阶段：基础准备（第1周）

#### 学习目标
- 理解音视频基础概念
- 熟悉 FFmpeg 编译和集成
- 掌握 JNI 开发基础

#### 学习内容

**Day 1-2: 音视频基础理论**
- [ ] 视频编码基础
  - 帧的概念：I帧、P帧、B帧
  - 常见编码格式：H.264、H.265、VP9
  - 码率、帧率、分辨率的关系
  - YUV 颜色空间：YUV420P、NV12、NV21

- [ ] 音频编码基础
  - 采样率、位深、声道
  - 常见编码格式：AAC、MP3、Opus
  - PCM 数据格式

- [ ] 封装格式
  - 容器概念：MP4、FLV、TS、MKV
  - 流的概念：音频流、视频流、字幕流

**Day 3-4: FFmpeg 编译和集成**
- [ ] 学习内容
  - FFmpeg 架构概览
  - Android NDK 配置
  - FFmpeg 编译脚本编写
  - 集成到 Android Studio

- [ ] 实践任务
  - 编译 FFmpeg 库（包含 x264、fdk-aac）
  - 创建测试项目验证集成
  - 编写简单的 JNI 调用测试 FFmpeg 版本信息

**Day 5-7: JNI 和 C++ 基础**
- [ ] JNI 核心概念
  - JNI 函数注册（静态/动态）
  - Java 和 Native 类型转换
  - 异常处理
  - 引用管理：LocalRef、GlobalRef

- [ ] C++11 多线程
  - std::thread 线程管理
  - std::mutex 互斥锁
  - std::condition_variable 条件变量
  - std::unique_lock 和 std::lock_guard

- [ ] 实践任务
  - 实现一个简单的生产者-消费者模型
  - 编写线程安全的队列类

#### 学习资源
- [FFmpeg 官方文档](https://ffmpeg.org/documentation.html)
- [雷霄骅博客](https://blog.csdn.net/leixiaohua1020)
- 项目参考：`app/src/main/cpp/CMakeLists.txt`

#### 评估标准
- [ ] 能解释 YUV420P 的数据结构
- [ ] 能独立编译 FFmpeg 并集成到项目
- [ ] 能编写基础的 JNI 函数调用

---

### 第二阶段：视频解码与播放（第2-5周）

#### 第2周：ANativeWindow 渲染

**学习目标**
- 掌握 FFmpeg 解码流程
- 理解 ANativeWindow 渲染原理

**Day 1-3: FFmpeg 解码核心 API**
- [ ] 学习内容
  ```cpp
  // 核心数据结构
  AVFormatContext  // 封装格式上下文
  AVCodecContext   // 编解码器上下文
  AVCodec          // 编解码器
  AVPacket         // 编码数据包
  AVFrame          // 解码后的帧
  AVStream         // 流信息

  // 核心函数
  avformat_open_input()      // 打开文件
  avformat_find_stream_info()// 查找流信息
  avcodec_find_decoder()     // 查找解码器
  avcodec_open2()            // 打开解码器
  av_read_frame()            // 读取数据包
  avcodec_send_packet()      // 发送编码包
  avcodec_receive_frame()    // 接收解码帧
  ```

- [ ] 实践任务
  - 实现视频文件解析，打印流信息
  - 实现单帧视频解码，保存为 YUV 文件
  - 使用 FFplay 验证解码结果

**Day 4-7: ANativeWindow 视频渲染**
- [ ] 学习内容
  - ANativeWindow API 使用
  - YUV 到 RGB 转换（使用 swscale）
  - Surface 和 SurfaceView 的关系

- [ ] 实践任务
  - 实现 `NativeRender` 类
  - 实现完整的视频播放循环
  - 添加播放控制：播放、暂停、停止、seek

- [ ] 参考代码
  - `app/src/main/cpp/player/render/video/NativeRender.cpp`
  - `app/src/main/cpp/player/decoder/DecoderBase.cpp`

**作业**
- [ ] 实现一个简单的视频播放器，能播放本地 MP4 文件
- [ ] 添加进度显示和拖动功能
- [ ] 处理视频旋转元数据

#### 第3周：音频解码与播放

**学习目标**
- 掌握 OpenSL ES 音频播放
- 理解音频重采样

**Day 1-3: OpenSL ES 基础**
- [ ] 学习内容
  ```cpp
  // OpenSL ES 核心对象
  SLObjectItf engineObject;      // 引擎对象
  SLEngineItf engineEngine;      // 引擎接口
  SLObjectItf outputMixObject;   // 混音器对象
  SLObjectItf bqPlayerObject;    // 播放器对象
  SLPlayItf bqPlayerPlay;        // 播放接口
  SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue; // 缓冲队列
  ```

- [ ] 实践任务
  - 创建 OpenSL ES 引擎和播放器
  - 实现回调函数处理音频数据
  - 播放本地 PCM 文件

**Day 4-7: 音频解码和重采样**
- [ ] 学习内容
  - FFmpeg 音频解码流程
  - swresample 库使用
  - 音频格式转换：AV_SAMPLE_FMT_FLTP → AV_SAMPLE_FMT_S16

- [ ] 实践任务
  - 实现 `AudioDecoder` 类
  - 实现 `OpenSLRender` 类
  - 集成音频播放到播放器

- [ ] 参考代码
  - `app/src/main/cpp/player/decoder/AudioDecoder.cpp`
  - `app/src/main/cpp/player/render/audio/OpenSLRender.cpp`

**作业**
- [ ] 实现音频播放器，播放 MP3/AAC 文件
- [ ] 支持暂停、恢复、音量调节
- [ ] 处理不同采样率和声道的音频

#### 第4周：音视频同步

**学习目标**
- 深入理解 PTS/DTS 概念
- 掌握三种音视频同步策略

**Day 1-3: PTS 和同步原理**
- [ ] 学习内容
  - PTS (Presentation Time Stamp) 显示时间戳
  - DTS (Decoding Time Stamp) 解码时间戳
  - time_base 时间基
  - 三种同步方式：
    1. 视频同步到音频（推荐）
    2. 音频同步到视频
    3. 音视频同步到外部时钟

- [ ] 核心算法
  ```cpp
  // 计算延时
  long AVSync() {
      // 1. 获取音频时钟
      long audioClock = GetAudioClock();

      // 2. 获取视频时钟（当前帧 PTS）
      long videoClock = m_CurTimeStamp;

      // 3. 计算差值
      long diff = videoClock - audioClock;

      // 4. 返回延时（毫秒）
      return diff;
  }
  ```

**Day 4-7: 实现音视频同步**
- [ ] 实践任务
  - 实现视频同步到音频
  - 优化同步算法，处理抖动
  - 添加延时补偿机制

- [ ] 参考代码
  - `app/src/main/cpp/player/decoder/DecoderBase.cpp` (AVSync方法)

**作业**
- [ ] 完善播放器，实现音视频同步
- [ ] 测试不同格式文件的同步效果
- [ ] 处理 seek 后的同步问题

#### 第5周：综合练习与优化

**实践任务**
- [ ] 实现完整的 `FFMediaPlayer`
- [ ] 添加状态管理和错误处理
- [ ] 实现播放器生命周期管理
- [ ] 内存泄漏检测和修复

**作业**
- [ ] 实现播放器控制界面
- [ ] 支持在线视频播放（HTTP/RTMP）
- [ ] 添加播放统计信息显示

---

### 第三阶段：OpenGL ES 渲染与滤镜（第6-9周）

#### 第6周：OpenGL ES 基础

**学习目标**
- 掌握 OpenGL ES 3.0 基础
- 理解渲染管线

**Day 1-3: OpenGL ES 核心概念**
- [ ] 学习内容
  - OpenGL ES 渲染管线
  - 着色器语言（GLSL）
  - 顶点缓冲对象（VBO）
  - 纹理映射
  - EGL 环境配置

- [ ] 实践任务
  - 创建 OpenGL ES 上下文
  - 绘制简单的三角形
  - 纹理贴图基础

**Day 4-7: YUV 到 RGB 转换**
- [ ] 学习内容
  - YUV 纹理上传
  - 片元着色器实现颜色转换
  - YUV420P 格式处理

- [ ] 核心代码
  ```glsl
  // Fragment Shader
  precision mediump float;
  varying vec2 v_texCoord;
  uniform sampler2D y_texture;
  uniform sampler2D u_texture;
  uniform sampler2D v_texture;

  void main() {
      float y = texture2D(y_texture, v_texCoord).r;
      float u = texture2D(u_texture, v_texCoord).r - 0.5;
      float v = texture2D(v_texture, v_texCoord).r - 0.5;

      // BT.601 转换矩阵
      float r = y + 1.403 * v;
      float g = y - 0.344 * u - 0.714 * v;
      float b = y + 1.770 * u;

      gl_FragColor = vec4(r, g, b, 1.0);
  }
  ```

- [ ] 参考代码
  - `app/src/main/cpp/player/render/video/VideoGLRender.cpp`

**作业**
- [ ] 实现 OpenGL 视频渲染器
- [ ] 支持不同视频尺寸和比例
- [ ] 处理纹理坐标变换

#### 第7周：视频滤镜实现

**学习目标**
- 掌握 FFmpeg AVFilter
- 实现常见视频特效

**Day 1-4: FFmpeg AVFilter 框架**
- [ ] 学习内容
  ```cpp
  // AVFilter 核心 API
  AVFilterGraph    // 滤镜图
  AVFilterContext  // 滤镜上下文
  AVFilter         // 滤镜

  // 初始化流程
  avfilter_graph_alloc()           // 创建滤镜图
  avfilter_graph_create_filter()   // 创建滤镜
  avfilter_graph_parse_ptr()       // 解析滤镜描述
  avfilter_graph_config()          // 配置滤镜图
  av_buffersrc_add_frame()         // 添加输入帧
  av_buffersink_get_frame()        // 获取输出帧
  ```

- [ ] 常用滤镜
  ```
  vflip           # 垂直翻转
  hflip           # 水平翻转
  transpose       # 旋转
  crop            # 裁剪
  scale           # 缩放
  negate          # 反色
  edgedetect      # 边缘检测
  boxblur         # 模糊
  colorbalance    # 色彩平衡
  ```

**Day 5-7: 实现滤镜切换**
- [ ] 实践任务
  - 集成 AVFilter 到播放器
  - 实现动态切换滤镜
  - 优化滤镜性能

- [ ] 参考代码
  - 查看项目中 VideoGLRender 的滤镜实现

**作业**
- [ ] 实现至少 5 种视频滤镜
- [ ] 支持实时切换滤镜
- [ ] 实现滤镜参数调节

#### 第8周：OpenGL 滤镜和特效

**学习目标**
- 使用 GLSL 实现自定义滤镜
- 掌握图像处理算法

**学习内容**
- [ ] 灰度滤镜
  ```glsl
  void main() {
      vec4 color = texture2D(s_texture, v_texCoord);
      float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
      gl_FragColor = vec4(vec3(gray), color.a);
  }
  ```

- [ ] 高斯模糊
- [ ] 锐化
- [ ] 浮雕效果
- [ ] 马赛克
- [ ] 抖音特效（抖动、毛刺、分屏）

**实践任务**
- [ ] 实现 10+ 种 GLSL 滤镜
- [ ] 优化多通道渲染
- [ ] 实现滤镜链

#### 第9周：VR 全景播放器

**学习目标**
- 理解球面映射原理
- 实现 VR 交互

**学习内容**
- [ ] 球面坐标系
- [ ] 等距柱状投影到球面
- [ ] 陀螺仪数据处理
- [ ] 相机矩阵变换

**实践任务**
- [ ] 实现 `VRGLRender` 类
- [ ] 集成传感器控制
- [ ] 优化渲染性能

**参考代码**
- `app/src/main/cpp/player/render/video/VRGLRender.cpp`

---

### 第四阶段：音视频录制与编码（第10-13周）

#### 第10周：视频录制基础

**学习目标**
- 掌握 Camera2 API
- 理解视频编码流程

**Day 1-3: Camera2 集成**
- [ ] 学习内容
  - Camera2 架构
  - 预览会话配置
  - 图像数据回调（ImageReader）
  - 格式转换：NV21 → YUV420P

- [ ] 实践任务
  - 实现相机预览
  - 获取 YUV 帧数据
  - 使用 OpenGL 渲染预览

**Day 4-7: 视频编码**
- [ ] 学习内容
  ```cpp
  // 编码核心 API
  avformat_alloc_output_context2()  // 创建输出上下文
  avformat_new_stream()             // 添加流
  avcodec_find_encoder()            // 查找编码器
  avcodec_open2()                   // 打开编码器
  avcodec_send_frame()              // 发送原始帧
  avcodec_receive_packet()          // 接收编码包
  av_interleaved_write_frame()      // 写入文件
  av_write_trailer()                // 写入文件尾
  ```

- [ ] 编码器参数配置
  ```cpp
  // H.264 编码器配置
  codec_ctx->codec_id = AV_CODEC_ID_H264;
  codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
  codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  codec_ctx->width = 1280;
  codec_ctx->height = 720;
  codec_ctx->time_base = {1, 25};  // 25fps
  codec_ctx->bit_rate = 2000000;   // 2Mbps
  codec_ctx->gop_size = 12;        // I帧间隔
  codec_ctx->max_b_frames = 2;

  // x264 参数优化
  av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
  av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
  ```

**参考代码**
- `app/src/main/cpp/recorder/MediaRecorder.cpp`

**作业**
- [ ] 实现视频录制功能
- [ ] 支持不同分辨率和码率
- [ ] 添加实时预览

#### 第11周：音频录制

**学习目标**
- 掌握 AudioRecord 使用
- 理解 AAC 编码

**Day 1-4: 音频采集**
- [ ] 学习内容
  - AudioRecord API
  - 音频参数配置
  - 缓冲区管理

- [ ] 实践任务
  - 实现 PCM 音频录制
  - 保存为 WAV 文件

**Day 5-7: AAC 编码**
- [ ] 学习内容
  ```cpp
  // AAC 编码器配置
  codec_ctx->codec_id = AV_CODEC_ID_AAC;
  codec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
  codec_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
  codec_ctx->sample_rate = 44100;
  codec_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
  codec_ctx->channels = 2;
  codec_ctx->bit_rate = 128000;  // 128kbps
  ```

- [ ] 实践任务
  - 实现 `SingleAudioRecorder`
  - 集成到录制模块

**参考代码**
- `app/src/main/cpp/recorder/SingleAudioRecorder.cpp`

#### 第12周：音视频合成

**学习目标**
- 实现音视频同步录制
- 掌握 MP4 封装

**学习内容**
- [ ] 多线程编码架构
  ```
  主线程
    ├── 视频编码线程
    │   └── 从帧队列取数据 → 编码 → 写入文件
    └── 音频编码线程
        └── 从 PCM 队列取数据 → 编码 → 写入文件
  ```

- [ ] 时间戳同步
  ```cpp
  // 视频 PTS 计算
  frame->pts = video_frame_count *
      (stream->time_base.den / stream->time_base.num / fps);

  // 音频 PTS 计算
  frame->pts = audio_sample_count;
  ```

**实践任务**
- [ ] 实现 `MediaRecorderContext` 统一调度
- [ ] 处理音视频起始时间对齐
- [ ] 实现边录边存功能

**参考代码**
- `app/src/main/cpp/recorder/MediaRecorderContext.cpp`

#### 第13周：录制功能完善

**实践任务**
- [ ] 添加录制滤镜（美颜、贴纸）
- [ ] 实现暂停续录
- [ ] 添加视频片段剪辑
- [ ] 性能优化和内存管理

**作业**
- [ ] 实现微信小视频录制功能
- [ ] 支持最大录制时长限制
- [ ] 添加录制进度回调

---

### 第五阶段：高级特性（第14-17周）

#### 第14周：硬件加速解码

**学习目标**
- 掌握 MediaCodec 硬解码
- 理解软硬解切换策略

**Day 1-4: MediaCodec 解码**
- [ ] 学习内容
  ```cpp
  // MediaCodec API
  AMediaCodec_createDecoderByType()  // 创建解码器
  AMediaCodec_configure()            // 配置
  AMediaCodec_start()                // 启动
  AMediaCodec_dequeueInputBuffer()   // 获取输入缓冲
  AMediaCodec_queueInputBuffer()     // 提交输入
  AMediaCodec_dequeueOutputBuffer()  // 获取输出
  AMediaCodec_releaseOutputBuffer()  // 释放输出
  ```

- [ ] 直接渲染到 Surface
  ```cpp
  // 配置时传入 Surface
  AMediaCodec_configure(codec, format, surface, NULL, 0);

  // 渲染
  AMediaCodec_releaseOutputBuffer(codec, idx, true);
  ```

**Day 5-7: 实现硬解播放器**
- [ ] 实践任务
  - 实现 `HWCodecPlayer`
  - 处理不支持硬解的情况
  - 性能对比测试

**参考代码**
- `app/src/main/cpp/player/HWCodecPlayer.cpp`

**作业**
- [ ] 实现软硬解自动切换
- [ ] 对比软硬解性能和功耗
- [ ] 处理硬解兼容性问题

#### 第15周：音频可视化

**学习目标**
- 掌握音频频谱分析
- 实现可视化效果

**学习内容**
- [ ] FFT（快速傅里叶变换）
- [ ] 频谱数据提取
- [ ] OpenGL 绘制波形和柱状图

**实践任务**
- [ ] 实现 `AudioGLRender`
- [ ] 实现波形图、柱状图、圆形频谱
- [ ] 优化渲染性能

**参考代码**
- `app/src/main/cpp/player/render/audio/AudioGLRender.cpp`

#### 第16周：流媒体播放

**学习目标**
- 掌握 RTMP/HLS 协议
- 实现直播播放器

**学习内容**
- [ ] RTMP 协议基础
- [ ] HLS (m3u8) 播放
- [ ] 低延迟优化策略

**实践任务**
- [ ] 支持 RTMP 流播放
- [ ] 支持 HLS 播放
- [ ] 实现缓冲管理
- [ ] 处理网络抖动

**作业**
- [ ] 实现边播边录功能
- [ ] 添加网络状态监听
- [ ] 优化首屏加载时间

#### 第17周：高级编辑功能

**学习内容**
- [ ] 视频剪辑：裁剪、拼接
- [ ] 转场效果
- [ ] 画中画
- [ ] 水印添加
- [ ] 字幕渲染

**实践任务**
- [ ] 实现视频裁剪和拼接
- [ ] 添加转场特效
- [ ] 实现视频合成

---

### 第六阶段：性能优化与源码分析（第18-20周）

#### 第18周：内存优化

**学习内容**
- [ ] 对象池模式
  ```cpp
  class AVFramePool {
      queue<AVFrame*> m_FrameQueue;
      mutex m_Mutex;
  public:
      AVFrame* Acquire();      // 获取帧
      void Release(AVFrame*);  // 释放帧
  };
  ```

- [ ] 智能指针管理
- [ ] 内存泄漏检测（AddressSanitizer）
- [ ] 内存使用分析

**实践任务**
- [ ] 使用 Android Profiler 分析内存
- [ ] 优化内存分配策略
- [ ] 修复内存泄漏

#### 第19周：多线程优化

**学习内容**
- [ ] 线程池实现
- [ ] 无锁队列（Lock-Free Queue）
- [ ] 减少锁竞争
- [ ] CPU 亲和性设置

**实践任务**
- [ ] 实现高效的帧缓冲队列
- [ ] 优化线程同步机制
- [ ] 性能测试和对比

#### 第20周：FFmpeg 源码分析

**学习目标**
- 深入理解 FFmpeg 架构
- 能阅读和调试 FFmpeg 源码

**学习内容**
- [ ] FFmpeg 模块架构
  ```
  libavformat  - 封装格式处理
  libavcodec   - 编解码器实现
  libavutil    - 公共工具函数
  libswscale   - 图像缩放和格式转换
  libswresample- 音频重采样
  libavfilter  - 滤镜框架
  ```

- [ ] 关键流程分析
  - `av_read_frame()` 读取数据包流程
  - `avcodec_send_packet()` / `avcodec_receive_frame()` 解码流程
  - AVFilter 图执行流程

**实践任务**
- [ ] 编译调试版 FFmpeg
- [ ] 单步调试解码流程
- [ ] 分析 H.264 解码器实现

---

## 实战项目

### 项目1：基础播放器（第5周完成）
**功能要求**
- [x] 支持本地视频播放（MP4、FLV、MKV）
- [x] 音视频同步
- [x] 播放控制：播放、暂停、停止、seek
- [x] 进度显示和拖动
- [x] 音量调节

**技术要点**
- FFmpeg 解码
- ANativeWindow / OpenSL ES 渲染
- 音视频同步算法

### 项目2：OpenGL 滤镜播放器（第9周完成）
**功能要求**
- [x] OpenGL ES 渲染
- [x] 10+ 种视频滤镜
- [x] 实时切换滤镜
- [x] VR 全景播放

**技术要点**
- OpenGL ES 3.0
- GLSL 着色器
- FFmpeg AVFilter
- 陀螺仪传感器

### 项目3：音视频录制器（第13周完成）
**功能要求**
- [x] 视频录制（支持美颜滤镜）
- [x] 音频录制
- [x] 暂停续录
- [x] 实时预览
- [x] 输出 MP4 文件

**技术要点**
- Camera2 API
- H.264/AAC 编码
- 多线程编码
- 音视频同步

### 项目4：直播播放器（第16周完成）
**功能要求**
- [x] RTMP 直播流播放
- [x] HLS 播放
- [x] 低延迟优化
- [x] 边播边录
- [x] 弹幕功能

**技术要点**
- 流媒体协议
- 缓冲管理
- 网络优化

### 项目5：短视频编辑器（第17周完成）
**功能要求**
- [x] 视频剪辑（裁剪、拼接）
- [x] 滤镜和特效
- [x] 配乐和配音
- [x] 字幕添加
- [x] 导出视频

**技术要点**
- AVFilter 滤镜链
- 多轨道合成
- 时间轴管理

---

## 学习资源

### 官方文档
- [FFmpeg 官方文档](https://ffmpeg.org/documentation.html)
- [FFmpeg Wiki](https://trac.ffmpeg.org/wiki)
- [Android NDK 文档](https://developer.android.com/ndk)
- [OpenGL ES 文档](https://www.khronos.org/opengles/)

### 推荐书籍
- 《FFmpeg 从入门到精通》- 刘歧、赵文杰
- 《Android 音视频开发》- 何俊林
- 《OpenGL ES 3.0 编程指南》

### 优质博客
- [雷霄骅的专栏](https://blog.csdn.net/leixiaohua1020)
- [罗上文的博客](https://blog.csdn.net/luoshengyang)
- [项目 README 的教程链接](../README.md)

### 视频教程
- 扫描 README 中的二维码获取配套视频教程

### 开源项目参考
- [本项目 LearnFFmpeg](https://github.com/githubhaohao/LearnFFmpeg)
- [ijkplayer](https://github.com/bilibili/ijkplayer) - bilibili 播放器
- [ExoPlayer](https://github.com/google/ExoPlayer) - Google 官方播放器

---

## 评估标准

### 第一阶段评估（第5周）
- [ ] 能独立实现视频播放器，支持常见格式
- [ ] 理解解码流程和 FFmpeg API
- [ ] 实现音视频同步，误差 < 50ms
- [ ] 代码规范，无内存泄漏

### 第二阶段评估（第9周）
- [ ] 掌握 OpenGL ES 渲染管线
- [ ] 实现 10+ 种视频滤镜
- [ ] 完成 VR 播放器
- [ ] 渲染性能达到 60fps

### 第三阶段评估（第13周）
- [ ] 完成音视频录制功能
- [ ] 理解编码流程和参数配置
- [ ] 输出文件质量符合要求
- [ ] 编码性能优化

### 第四阶段评估（第17周）
- [ ] 实现硬件加速解码
- [ ] 完成流媒体播放器
- [ ] 掌握高级编辑功能
- [ ] 能处理复杂的音视频场景

### 第五阶段评估（第20周）
- [ ] 能分析 FFmpeg 源码
- [ ] 完成性能优化
- [ ] 构建完整的商业级播放器
- [ ] 能独立解决复杂技术问题

---

## 学习建议

### 1. 实践为主
- 每个知识点都要动手实现
- 不要只看不写
- 遇到问题先自己调试

### 2. 循序渐进
- 按照阶段顺序学习
- 打好基础再学高级内容
- 不要跳跃式学习

### 3. 深入理解
- 理解原理，不要死记硬背
- 多问为什么
- 查看源码实现

### 4. 总结归纳
- 每周写学习总结
- 记录遇到的问题和解决方案
- 整理知识体系

### 5. 交流讨论
- 加入技术社群
- 参与开源项目
- 分享学习心得

### 6. 持续优化
- 定期回顾代码
- 重构和优化
- 学习业界最佳实践

---

## 附录：关键 API 速查表

### FFmpeg 解码 API
```cpp
// 1. 打开文件
avformat_open_input(&formatCtx, url, NULL, NULL);
avformat_find_stream_info(formatCtx, NULL);

// 2. 查找流
for(int i = 0; i < formatCtx->nb_streams; i++) {
    if(formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        videoStreamIndex = i;
    }
}

// 3. 创建解码器
AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
avcodec_parameters_to_context(codecCtx, codecpar);
avcodec_open2(codecCtx, codec, NULL);

// 4. 解码循环
AVPacket *packet = av_packet_alloc();
AVFrame *frame = av_frame_alloc();

while(av_read_frame(formatCtx, packet) >= 0) {
    if(packet->stream_index == videoStreamIndex) {
        avcodec_send_packet(codecCtx, packet);
        while(avcodec_receive_frame(codecCtx, frame) == 0) {
            // 处理帧数据
        }
    }
    av_packet_unref(packet);
}

// 5. 清理
av_frame_free(&frame);
av_packet_free(&packet);
avcodec_free_context(&codecCtx);
avformat_close_input(&formatCtx);
```

### FFmpeg 编码 API
```cpp
// 1. 创建输出上下文
avformat_alloc_output_context2(&formatCtx, NULL, NULL, outFile);

// 2. 添加视频流
AVStream *stream = avformat_new_stream(formatCtx, NULL);
AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
AVCodecContext *codecCtx = avcodec_alloc_context3(codec);

// 配置编码器
codecCtx->width = 1280;
codecCtx->height = 720;
codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
codecCtx->time_base = {1, 25};
codecCtx->bit_rate = 2000000;

avcodec_open2(codecCtx, codec, NULL);
avcodec_parameters_from_context(stream->codecpar, codecCtx);

// 3. 打开输出文件
avio_open(&formatCtx->pb, outFile, AVIO_FLAG_WRITE);
avformat_write_header(formatCtx, NULL);

// 4. 编码循环
AVFrame *frame = av_frame_alloc();
AVPacket *packet = av_packet_alloc();

while(有数据) {
    // 填充 frame 数据
    frame->pts = frameCount++;

    avcodec_send_frame(codecCtx, frame);
    while(avcodec_receive_packet(codecCtx, packet) == 0) {
        av_interleaved_write_frame(formatCtx, packet);
        av_packet_unref(packet);
    }
}

// 5. 写入文件尾
av_write_trailer(formatCtx);

// 6. 清理
av_frame_free(&frame);
av_packet_free(&packet);
avcodec_free_context(&codecCtx);
avio_close(formatCtx->pb);
avformat_free_context(formatCtx);
```

---

**祝学习顺利！有问题可以参考项目代码或联系作者交流。**
