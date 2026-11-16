# 第三方源码

以源码形式编进 `learn-ffmpeg`（静态链接），与工程使用同一个 NDK 和 `c++_static`，不引入额外的 `.so`。
升级时整体替换对应目录，不要在原文件上做本地修改。

| 目录 | 来源 | 版本 | 许可证 | 用途 |
|---|---|---|---|---|
| `oboe/` | https://github.com/google/oboe | 1.9.3（tag `1.9.3`），只保留 `include/`、`src/`、`CMakeLists.txt`、`LICENSE` | Apache 2.0（见 `oboe/LICENSE`） | 音频输出：Android 8.1+ 走 AAudio，更低版本由 Oboe 内部回退到 OpenSL ES |
| `sonic/` | https://github.com/waywardgeek/sonic | commit `b93885dcb70aae50c6f76b0fe4e0868f029a077e`，只取 `sonic.c`、`sonic.h` | Apache 2.0（见文件头） | 倍速播放：变速不变调（WSOLA） |

为什么不用 Gradle 的 `com.google.oboe:oboe` 预编译包：它是用新版 NDK、`c++_shared` 编出来的 `.so`，
本工程固定 NDK 21 + `c++_static`，混用会在 prefab 阶段报 STL 不兼容，或者运行时找不到新版 libc++ 的符号。
