# 用 AddressSanitizer（ASan）检查 native 内存错误

ASan 在编译时给每次内存访问插入检查，运行时能当场抓到越界读写、释放后使用、重复释放等错误，
并打印出错位置和这块内存的分配/释放位置。代价是变慢约 2 倍、内存占用增加约 2~3 倍，所以单独做成一个构建类型。

## 1. 构建并安装

```bash
./gradlew :app:installAsan
```

`asan` 构建类型（`app`、`lib_av` 各有一个，自动匹配）会：

- 用 `-fsanitize=address` 编译 `liblearn-ffmpeg.so`，并定义宏 `LEARNFFMPEG_ASAN`（`lib_av/src/main/cpp/CMakeLists.txt`）；
- 从 NDK 取出 ASan 运行时 `libclang_rt.asan-*-android.so` 和启动脚本 `wrap.sh` 打进 APK
  （`app/build.gradle.kts` 的 `PrepareAsanRuntimeTask`，不提交二进制）。
  `wrap.sh` 让系统启动进程时先加载 ASan 运行时，它要求应用可调试、native 库解压到磁盘，这两项 asan 构建都已配置。

普通 debug / release 构建完全不受影响。

## 2. 确认 ASan 生效：触发测试用例

`ASanTestCase.h` 里每个用例故意制造一种内存错误。ASan 发现第一个错误就会终止进程，所以一次跑一个：

```bash
adb shell am start -n com.byteflow.learnffmpeg/.MainActivity --ei asan_case 1
```

| 编号 | 错误类型 | ASan 报告 |
|---|---|---|
| 1 | 堆缓冲区越界 | heap-buffer-overflow |
| 2 | 栈缓冲区越界 | stack-buffer-overflow |
| 3 | 释放后使用 | heap-use-after-free |
| 4 | 作用域外使用栈变量 | stack-use-after-scope |
| 5 | 重复释放 | attempting double-free |

非 asan 构建传了 `asan_case` 也不会执行（只打一条日志），不会真的破坏内存。

## 3. 真正的用法：用 asan 构建跑功能

装好 asan 构建后正常使用 App（录制、播放……），然后看有没有报告：

```bash
adb logcat -d | grep "ERROR: AddressSanitizer"
```

## 4. 看懂报告：符号化

ASan 报告里的调用栈只有 `liblearn-ffmpeg.so+0x4fc444` 这样的地址，用脚本换成函数名和源码行号
（必须是和手机上同一次构建的产物）：

```bash
adb logcat -d -s wrap.sh | python3 scripts/asan_symbolize.py
# 32 位设备：... | python3 scripts/asan_symbolize.py --abi armeabi-v7a
```

符号化后的报告示例：

```
==19465==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x005564440100 ...
WRITE of size 4 at 0x005564440100 thread T0
    #0 0x6cc472e444  ASanTestCase::HeapBufferOverflow() lib_av/src/main/cpp/util/ASanTestCase.h:59:33
    #1 0x6cc472e198  ASanTestCase::Run(int) lib_av/src/main/cpp/util/ASanTestCase.h:38:40
0x005564440100 is located 0 bytes after 4096-byte region [...)
allocated by thread T0 here:
    #1 0x6cc472e38c  ASanTestCase::HeapBufferOverflow() lib_av/src/main/cpp/util/ASanTestCase.h:57:20
```

读法：第一行是错误类型；`WRITE of size 4` 是越界写了 4 字节；第一组调用栈是出错位置；
`is located 0 bytes after 4096-byte region` 说明紧挨着一块 4096 字节内存的末尾越界；
`allocated by` 是这块内存在哪里分配的（释放后使用时还会有 `freed by`）。

## 注意事项

- **ASan 构建默认不用硬件编码**：实测三星设备的系统编码器（`libsfplugin_ccodec.so` 的
  `CCodec::customConfigVideoEncoder`）在 ASan 运行时下 configure 时栈保护报错崩溃，与本工程代码无关，
  所以 asan 构建里录制走 x264。需要测硬件编码时：`adb shell setprop debug.learnffmpeg.asan_hw_encode 1`。
- **只检查本工程的源码**：预编译的 FFmpeg / x264 / fdk-aac 没有插桩，其内部的越界检测不到；
  但它们的 `malloc` / `free` 仍经过 ASan，释放后使用、重复释放这类堆错误依然能发现。
- ASan 支持 Android 8.1 及以上。arm64 设备在 Android 14+ 上还可以用开销更低的 HWASan，本工程暂未配置。
