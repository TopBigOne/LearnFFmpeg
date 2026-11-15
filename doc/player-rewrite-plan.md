# 播放器内核重写计划

目标：把 `app/src/main/cpp/player` 从"能跑的演示"改造成接近上线水准的播放器内核。
基线：原作者提交 `e8f24c9`。参照实现：ffplay.c（主）、ijkplayer `ff_ffplay.c`、Media3 ExoPlayer。

每个阶段都要能独立编译、能播放，再进入下一阶段。勾选框用来记录进度。

> **当前状态（2026-11-15）**：阶段 1 的代码修改已完成（提交 `7bb6cb2`、`3c777b8`、`4c14f16`），两个 ABI 编译通过，**尚未真机验收**。阶段 3、4、6 中有几项已顺带完成或部分完成，见各条备注。软解播放器目前仍是音频、视频解码器各自打开一次输入，阶段 2 的单 Demuxer 架构尚未开始。

---

## 阶段 0：准备

- [ ] 精读 ffplay.c，画出线程/数据流图，重点：`PacketQueue`、`FrameQueue`、`serial`、`compute_target_delay`、`video_refresh`、`audio_callback`、`stream_seek`
- [ ] 准备测试样本库（放在设备 `/sdcard/test_media/`，不进 git）：
  - [ ] H.264 + AAC 普通 MP4
  - [ ] 含 B 帧的 H.264、H.265
  - [ ] MP3 音频（每帧 1152 采样）、无音轨视频、纯音频文件
  - [ ] 可变帧率（手机录像）、非 0 起始时间戳的 TS
  - [ ] 损坏/截断文件、超长文件（> 2 小时）
  - [ ] 一个 HLS 直播流 / 点播流、一个 RTMP 或 RTSP 流
- [ ] CMake 增加 ASan 构建变体（`-fsanitize=address -fno-omit-frame-pointer`），后续每阶段用它跑一遍

## 阶段 1：修掉现有的硬伤（不改架构）

先在旧架构上修明确的 bug，熟悉代码，也作为后面对比的基准。

- [x] `OpenSLRender::HandleAudioFrameQueue`：Enqueue 后立即 `delete audioFrame` 导致释放后使用。改为保留到下一次回调再释放（或用固定的 2～3 个轮转缓冲区）
- [x] `OpenSLRender` 回调里不再等待队列攒满，队列空时送一段静音并立即返回
- [x] `ClearAudioCache` / `UnInit` 中 `for (i < size()) pop()` 只清一半，改为 `while (!empty())`
- [x] `AudioDecoder::OnFrameAvailable`：`swr_convert` 输出容量用采样数，按实际返回的采样数计算字节数再送渲染；去掉固定 1024 采样的假设（按 `frame->nb_samples` 和 `swr_get_out_samples` 分配）
- [x] `VideoDecoder`：`av_malloc` 配 `av_free`；去掉写死的 `/sdcard/learnffmpeg_output.mp4` 录制
  - 录制改为可选：Java 通过 `MEDIA_PARAM_RECORD_PATH` 传入应用私有目录路径，不传则不录
- [x] `DecoderBase::UpdateTimeStamp`：改用 `frame->best_effort_timestamp`
- [x] `m_Duration` 计算改为 `av_rescale(duration, 1000, AV_TIME_BASE)`
- [x] `rtsp_transport` / `stimeout` / `buffer_size` 等参数移到 `avformat_open_input` 的 options
- [x] 读到 EOF 后向解码器送空包，`receive_frame` 直到 `AVERROR_EOF`，避免丢尾帧
- [x] `HWCodecPlayer`：pts 用 `av_rescale_q` 转微秒再传 MediaCodec；`av_bitstream_filter_filter` 换成 `av_bsf_send_packet` / `av_bsf_receive_packet`；`memcpy` 前检查 `bufSize`
  - 同时重写了 HWCodecPlayer：Stop 能唤醒阻塞线程；seek 由解码线程自己 flush；AMediaFormat 由实际文件构建（不再读写死的 vr.mp4）；EOS 排空；无音轨可播- [x] `m_DecoderState`、`m_CurTimeStamp` 等跨线程变量改成 `std::atomic` 或统一加锁

**验收**（未完成）：ASan 版本播放样本库全部文件，无报错；MP3 播放无杂音；视频最后一秒能正常显示。

## 阶段 2：重建架构

### 目标结构

```
player/
  core/
    Clock.h/.cpp            # 时钟：pts + 漂移 + serial + 倍速
    PacketQueue.h/.cpp      # 按时长/字节限流，带 serial、abort
    FrameQueue.h/.cpp       # 固定容量环形队列，带 serial
    FFPtr.h                 # FFmpeg 对象的 unique_ptr + 自定义 deleter
  demux/
    Demuxer.h/.cpp          # 唯一的 avformat 上下文、读包线程、seek、中断回调
  decoder/
    Decoder.h/.cpp          # send/receive 状态机、flush、serial 检查
    AudioDecoder / VideoDecoder
  output/
    AudioOutput.h           # 接口
    OboeAudioOutput.cpp     # 阶段 3 实现；先保留 OpenSL 版本
    VideoRenderer.h         # 接口，复用现有 GL / ANativeWindow 渲染
  Player.h/.cpp             # 状态机 + 控制线程（消息队列），对外唯一入口
```

### 任务

> 已有基础：`common/AVPacketQueue` 已支持 FLUSH/EOS 标记包和 abort（目前只有 HWCodecPlayer 在用），`common/SyncClock` 已线程安全并支持暂停冻结，可作为 `PacketQueue` / `Clock` 的起点；serial 机制还没有。

- [ ] `FFPtr.h`：为 `AVFormatContext`、`AVCodecContext`、`AVFrame`、`AVPacket`、`SwrContext`、`SwsContext` 写 RAII 封装，新代码不再手动 free
- [ ] `PacketQueue`：put/get/flush/abort；记录缓冲时长和字节数；put 时附带当前 serial；seek 时插入 flush 标记包
- [ ] `FrameQueue`：固定容量（视频 3、音频 9），`peek`/`next`/`last`，阻塞读写可被 abort 唤醒
- [ ] `Demuxer`：单线程读包，按流分发到音频/视频 PacketQueue；缓冲满时等待；设置 `interrupt_callback` 支持退出和超时
- [ ] `Decoder`：严格处理 `EAGAIN`/`EOF`；遇到 flush 标记执行 `avcodec_flush_buffers` 并更新 serial；丢弃 serial 不一致的包
- [ ] Seek：控制线程置位 seek 请求 → Demuxer 调 `avformat_seek_file` → 清空队列、serial+1 → 解码器 flush → 时钟重置
- [ ] `Player` 状态机：Idle → Preparing → Prepared → Playing ⇄ Paused → Completed / Error → Released；所有对外接口投递到控制线程串行执行
- [ ] JNI 层改为只和 `Player` 交互，回调统一走一个消息分发线程（避免在解码线程里直接调 Java）

**验收**：
- 音视频只打开一次输入（日志确认）
- 快速连续 seek 50 次，不花屏、不串音、不卡死
- 任意状态下调用 release，1 秒内返回，无泄漏（ASan LeakSanitizer）

## 阶段 3：同步与音频输出

- [ ] 音频输出换成 Oboe（AAudio 优先，低版本回退 OpenSL）；回调中不加锁、不分配内存，从无锁环形缓冲读数据，不够时补静音
- [ ] 音频时钟 = 已写入数据的末尾 pts − 设备中待播时长；用 `getTimestamp` 校正延迟
  - 部分完成：OpenSL 版本以"正在播放的那一帧的 pts + 已播放时长"作为音频时钟，但没有扣除设备输出延迟
- [ ] 视频渲染线程按 ffplay `compute_target_delay` 逻辑追音频时钟：落后丢帧、超前等待，不在解码线程 sleep
  - 部分完成：已以音频时钟为主时钟，落后超过 100ms 丢帧、超前等待；但等待仍发生在视频解码线程里，没有独立的渲染线程和 FrameQueue- [x] 无音轨时主时钟退回外部时钟（系统时钟）
- [x] 暂停/恢复时时钟正确冻结和继续
- [ ] 倍速播放：音频接入 sonic 或 soundtouch（变速不变调），时钟按倍速推进

**验收**：
- 用音画同步测试视频（闪光+哔声）录屏检查，偏差稳定在 ±40ms 内
- 0.5x / 1.0x / 2.0x 下同步正常，暂停 10 分钟恢复后不跳帧、不追帧

## 阶段 4：解码与渲染完善

- [ ] 硬解优先，初始化或运行中失败时自动降级软解
- [ ] MediaCodec 处理 `INFO_OUTPUT_FORMAT_CHANGED`、stride/crop；支持直接输出到 Surface
  - 部分完成：HWCodecPlayer 已直接输出到 Surface（不涉及 stride/crop）；`INFO_OUTPUT_FORMAT_CHANGED` 目前只打日志
- [x] 软解开启多线程：`thread_count = 0`、`thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE`
  - 已设置 `thread_count = 0`；`thread_type` 的默认值就是 FRAME | SLICE，未显式设置
- [ ] 渲染层处理 Surface 销毁与重建（切后台、旋转），EGL 上下文丢失后能恢复
- [ ] 色彩空间：区分 BT.601 / BT.709、full / limited range，选择对应 shader 矩阵
- [ ] 处理分辨率中途变化（直播流常见）

## 阶段 5：网络与直播

- [ ] 网络超时、断线自动重连（指数退避，最大次数可配置）
- [ ] 缓冲策略：起播阈值、卡顿恢复阈值、最大缓冲时长分开配置（参考 ExoPlayer `DefaultLoadControl`）
- [ ] 回调 buffering 开始/结束、缓冲进度
- [ ] 首帧优化：调小 `probesize` / `analyzeduration`；记录各阶段耗时（连接、探测、首包、首帧解码、首帧渲染）
- [ ] 直播追帧：缓冲超过阈值时轻微倍速或丢帧追赶
- [ ] 时间戳跳变、回绕的处理

## 阶段 6：工程化

- [x] 日志分级，Release 版关闭逐帧日志（`LOGCATV` / `LOGCATD` 在 `NDEBUG` 下编译为空）
- [ ] 错误码体系：区分 IO、解复用、解码、渲染、音频设备错误，统一上报到 Java 层
- [ ] 播放质量统计：首帧耗时、卡顿次数与总时长、丢帧率、音画偏差、解码失败率
- [ ] 自动化回归脚本：用 adb 逐个播放样本库，每个文件执行播放 → seek → 暂停 → 恢复 → 释放，汇总结果
- [ ] 压测：连续 1000 次 prepare/release 无崩溃，内存稳定
- [ ] TSan 构建跑一遍，消除所有数据竞争
- [ ] 至少在 3 台不同厂商、不同 Android 版本的真机上回归

---

## 进度记录

| 阶段 | 开始日期 | 完成日期 | 备注 |
|---|---|---|---|
| 0 准备 | | | |
| 1 修 bug | 2026-11-15 | | 代码已改完，待真机验收 |
| 2 架构 | | | |
| 3 同步 | | | |
| 4 解码渲染 | | | |
| 5 网络 | | | |
| 6 工程化 | | | |
