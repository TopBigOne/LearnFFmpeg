package com.monday.av.debug;

/**
 * AddressSanitizer 自检：故意触发一种内存错误，确认 ASan 已生效。
 *
 * 只在 asan 构建类型下执行（./gradlew :app:installAsan），其他构建调用 runCase 只会打日志。
 * 用法见 lib_av/src/main/cpp/util/ASAN.md。
 */
public final class ASanTester {
    public static final int HEAP_BUFFER_OVERFLOW = 1;
    public static final int STACK_BUFFER_OVERFLOW = 2;
    public static final int USE_AFTER_FREE = 3;
    public static final int USE_AFTER_SCOPE = 4;
    public static final int DOUBLE_FREE = 5;

    static {
        System.loadLibrary("learn-ffmpeg");
    }

    private ASanTester() {
    }

    /** 当前 native 库是否以 ASan 编译 */
    public static boolean isAsanBuild() {
        return nativeIsAsanBuild();
    }

    /**
     * 执行一个用例。ASan 生效时进程会立即终止，报告输出到 logcat（tag: wrap.sh / DEBUG / AddressSanitizer）。
     */
    public static void runCase(int caseId) {
        nativeRunCase(caseId);
    }

    private static native void nativeRunCase(int caseId);

    private static native boolean nativeIsAsanBuild();
}
