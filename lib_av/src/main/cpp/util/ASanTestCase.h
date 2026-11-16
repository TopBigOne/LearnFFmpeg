/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#ifndef LEARNFFMPEG_ASANTESTCASE_H
#define LEARNFFMPEG_ASANTESTCASE_H
#include "LogUtil.h"

/**
 * @brief AddressSanitizer (ASan) 测试用例：每个用例故意制造一种内存错误，用来确认 ASan 已生效。
 *
 * 用法（详见 lib_av/src/main/cpp/util/ASAN.md）：
 *   ./gradlew :app:installAsan
 *   adb shell am start -n com.byteflow.learnffmpeg/.MainActivity --ei asan_case 1
 *   adb logcat | grep -A30 AddressSanitizer
 *
 * ASan 发现第一个错误就会打印报告并终止进程，所以一次只能跑一个用例（Run 传编号）。
 * 只有 asan 构建类型才会调用到这里，普通构建里这些错误不会被检测，只会造成未定义行为。
 */
class ASanTestCase {
public:
    enum Case {
        kHeapBufferOverflow = 1,   // 堆缓冲区越界：heap-buffer-overflow
        kStackBufferOverflow = 2,  // 栈缓冲区越界：stack-buffer-overflow
        kUseAfterFree = 3,         // 释放后使用：heap-use-after-free
        kUseAfterScope = 4,        // 作用域外使用栈变量：stack-use-after-scope
        kDoubleFree = 5,           // 重复释放：attempting double-free
    };

    /** 执行一个用例；ASan 生效时进程会在这里终止并在 logcat 输出报告 */
    static void Run(int caseId) {
        LOGCATE("ASanTestCase::Run case=%d", caseId);
        switch (caseId) {
            case kHeapBufferOverflow:  HeapBufferOverflow(); break;
            case kStackBufferOverflow: StackBufferOverflow(); break;
            case kUseAfterFree:        UseAfterFree(); break;
            case kUseAfterScope:       UseAfterScope(); break;
            case kDoubleFree:          DoubleFree(); break;
            default: LOGCATE("ASanTestCase::Run unknown case %d (valid: 1-5)", caseId); return;
        }
        // 能走到这里说明 ASan 没有生效（不是 asan 构建，或运行时没有加载）
        LOGCATE("ASanTestCase::Run case=%d finished without ASan report: ASan is NOT active", caseId);
    }

private:
    // 越界下标通过 volatile 读出：编译器在编译期看不出越界，不会直接优化掉或报 -Warray-bounds
    static int OutOfBoundsIndex() {
        static volatile int index = 1024;
        return index;
    }

    __attribute__((noinline)) static void HeapBufferOverflow() {
        int *arr = new int[1024];
        arr[0] = 11;
        arr[OutOfBoundsIndex()] = 12;  // 越界写：有效索引为 0-1023
        LOGCATE("HeapBufferOverflow arr[0]=%d", arr[0]);
        delete[] arr;
    }

    __attribute__((noinline)) static void StackBufferOverflow() {
        int arr[1024];
        arr[0] = 11;
        arr[OutOfBoundsIndex()] = 12;  // 越界写：有效索引为 0-1023
        LOGCATE("StackBufferOverflow arr[0]=%d", arr[0]);
    }

    __attribute__((noinline)) static void UseAfterFree() {
        int *arr = new int[1024];
        arr[0] = 11;
        delete[] arr;
        LOGCATE("UseAfterFree arr[0]=%d", arr[0]);  // 读已释放的内存
    }

    __attribute__((noinline)) static void UseAfterScope() {
        volatile int *p = nullptr;
        {
            int a = 0;
            p = &a;  // 保存局部变量地址
        }            // a 的作用域结束
        *p = 1111;   // 写已离开作用域的栈变量
        LOGCATE("UseAfterScope *p=%d", *p);
    }

    __attribute__((noinline)) static void DoubleFree() {
        int *volatile arr = new int[1024];
        arr[0] = 11;
        delete[] arr;
        delete[] arr;  // 同一块内存释放两次
    }
};

#endif //LEARNFFMPEG_ASANTESTCASE_H
