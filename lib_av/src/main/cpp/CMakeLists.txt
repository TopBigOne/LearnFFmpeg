# Sets the minimum version of CMake required to build the native library.
cmake_minimum_required(VERSION 3.4.1)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++17")

set(jnilibs ${CMAKE_SOURCE_DIR}/prebuilt)   # FFmpeg / x264 / fdk-aac 预编译静态库，按 ABI 分目录
set(libname learn-ffmpeg)

include_directories(
        include
        glm
        common
        ${CMAKE_SOURCE_DIR}/thirdparty/sonic
        ${CMAKE_SOURCE_DIR}/thirdparty/libyuv/include
        ${CMAKE_SOURCE_DIR}/recorder
        ${CMAKE_SOURCE_DIR}/util
        ${CMAKE_SOURCE_DIR}/player
        ${CMAKE_SOURCE_DIR}/player/decoder
        ${CMAKE_SOURCE_DIR}/player/render
        ${CMAKE_SOURCE_DIR}/player/render/audio
        ${CMAKE_SOURCE_DIR}/player/render/video)

link_directories(
        ${jnilibs}/${ANDROID_ABI})

file(GLOB src-files
        ${CMAKE_SOURCE_DIR}/*.cpp
        ${CMAKE_SOURCE_DIR}/util/*.cpp
        ${CMAKE_SOURCE_DIR}/common/*.cpp
        ${CMAKE_SOURCE_DIR}/player/*.cpp
        ${CMAKE_SOURCE_DIR}/player/core/*.cpp
        ${CMAKE_SOURCE_DIR}/player/demux/*.cpp
        ${CMAKE_SOURCE_DIR}/player/output/*.cpp
        ${CMAKE_SOURCE_DIR}/player/decoder/*.cpp
        ${CMAKE_SOURCE_DIR}/player/render/video/*.cpp
        ${CMAKE_SOURCE_DIR}/player/render/audio/*.cpp
        ${CMAKE_SOURCE_DIR}/recorder/SingleVideoRecorder.cpp
        ${CMAKE_SOURCE_DIR}/recorder/SingleAudioRecorder.cpp
        ${CMAKE_SOURCE_DIR}/recorder/MediaRecorderContext.cpp
        ${CMAKE_SOURCE_DIR}/recorder/GLCameraRender.cpp
        ${CMAKE_SOURCE_DIR}/recorder/MediaRecorder.cpp
        ${CMAKE_SOURCE_DIR}/recorder/YuvConvert.cpp
        ${CMAKE_SOURCE_DIR}/recorder/HwVideoEncoder.cpp
        ${CMAKE_SOURCE_DIR}/thirdparty/sonic/sonic.c
        )

# Oboe 以源码编译成静态库（说明见 thirdparty/README.md），头文件通过 target 的 PUBLIC include 传递
add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/oboe ${CMAKE_CURRENT_BINARY_DIR}/oboe)

# libyuv 用官方 CMakeLists 编译（说明见 thirdparty/README.md）：它按 ABI 给 NEON / SVE / SME 代码加上
# 对应的编译参数，运行时再按 CPU 能力选择实现。EXCLUDE_FROM_ALL：只编译被链接的静态库 yuv，
# 不编译它自带的命令行工具。不需要 JPEG 解码
set(LIBYUV_DISABLE_JPEG ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/libyuv ${CMAKE_CURRENT_BINARY_DIR}/libyuv EXCLUDE_FROM_ALL)

add_library( # Sets the name of the library.
             ${libname}

             # Sets the library as a shared library.
             SHARED

             # Provides a relative path to your source file(s).
             ${src-files}
        )

#add_library(avutil
#        SHARED
#        IMPORTED)
#set_target_properties(avutil
#        PROPERTIES IMPORTED_LOCATION
#        ${jnilibs}/${ANDROID_ABI}/libavutil.so)
#
#add_library(swresample
#        SHARED
#        IMPORTED)
#set_target_properties(swresample
#        PROPERTIES IMPORTED_LOCATION
#        ${jnilibs}/${ANDROID_ABI}/libswresample.so)
#
#add_library(avcodec
#        SHARED
#        IMPORTED)
#set_target_properties(avcodec
#        PROPERTIES IMPORTED_LOCATION
#        ${jnilibs}/${ANDROID_ABI}/libavcodec.so)
#
#add_library(avfilter
#        SHARED
#        IMPORTED)
#set_target_properties(avfilter
#        PROPERTIES IMPORTED_LOCATION
#        ${jnilibs}/${ANDROID_ABI}/libavfilter.so)
#
#add_library(swscale
#        SHARED
#        IMPORTED)
#set_target_properties(swscale
#        PROPERTIES IMPORTED_LOCATION
#        ${jnilibs}/${ANDROID_ABI}/libswscale.so)
#
#add_library(avformat
#        SHARED
#        IMPORTED)
#set_target_properties(avformat
#        PROPERTIES IMPORTED_LOCATION
#        ${jnilibs}/${ANDROID_ABI}/libavformat.so)

#ASAN 需要 NDK20 编译，Android P 以上运行
#target_compile_options(${libname} PUBLIC -fsanitize=address -fno-omit-frame-pointer)
#set_target_properties(${libname} PROPERTIES LINK_FLAGS -fsanitize=address)

set(third-party-libs
        oboe
        yuv
        avformat
        avcodec
        avfilter
        swresample
        swscale
        avutil
        fdk-aac
        x264
        )

set(native-libs
        android
        mediandk
        EGL
        GLESv3
        OpenSLES
        log
        m
        z
        )

target_link_libraries( # Specifies the target library.
                       ${libname}

                       # Links the target library to the log library
                       # included in the NDK.
                       ${log-lib}
                       ${third-party-libs}
                       ${native-libs}
                       )
# 预编译 FFmpeg 的 aarch64 汇编以非 PIC 方式寻址全局符号（如 ff_vp9_subpel_filters），
# NDK r22+ 默认的 lld 会拒绝链接；-Bsymbolic 让这些符号在本库内直接绑定，所有代码都静态链接进这一个 .so，没有副作用
target_link_options(${libname} PRIVATE "-Wl,-Bsymbolic")
