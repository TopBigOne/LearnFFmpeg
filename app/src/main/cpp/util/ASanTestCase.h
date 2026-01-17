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
 * @brief AddressSanitizer (ASan) 测试用例类
 *
 * 用于测试和演示各种内存错误场景，帮助验证ASan工具的检测能力
 * 警告：此类包含故意制造的内存错误，仅用于测试目的
 */
class ASanTestCase {
    /**
     * @brief 堆缓冲区溢出测试
     * 故意访问堆分配数组的越界位置，触发heap-buffer-overflow错误
     */
    static void HeapBufferOverflow() {
        int *arr = new int[1024];
        arr[0] = 11;
        arr[1024] = 12;  // 越界访问：有效索引为 0-1023
        LOGCATE("HeapBufferOverflow arr[0]=%d, arr[1024]",arr[0], arr[1024]);
    }

    /**
     * @brief 栈缓冲区溢出测试
     * 故意访问栈分配数组的越界位置，触发stack-buffer-overflow错误
     */
    static void StackBufferOverflow() {
        int arr[1024];
        arr[0] = 11;
        arr[1024] = 12;  // 越界访问：有效索引为 0-1023
        LOGCATE("StackBufferOverflow arr[0]=%d, arr[1024]",arr[0], arr[1024]);
    }

    /**
     * @brief 释放后使用测试
     * 故意在内存释放后继续访问，触发heap-use-after-free错误
     */
    static void UseAfterFree() {
        int *arr = new int[1024];
        arr[0] = 11;
        delete [] arr;  // 释放内存
        LOGCATE("UseAfterFree arr[0]=%d, arr[1024]",arr[0], arr[1024]);  // 使用已释放的内存
    }

    /**
     * @brief 双重释放测试
     * 故意对同一块内存执行两次delete操作，触发double-free错误
     */
    static void DoubleFree() {
        int *arr = new int[1024];
        arr[0] = 11;
        delete [] arr;  // 第一次释放
        delete [] arr;  // 第二次释放同一块内存
        LOGCATE("UseAfterFree arr[0]=%d",arr[0]);
    }

    /**
     * @brief 作用域外使用栈变量测试
     * 故意在变量作用域结束后继续使用其地址，触发stack-use-after-scope错误
     */
    static int *p;  // 静态指针，用于保存局部变量地址
    static void UseAfterScope()
    {
        {
            int a = 0;
            p = &a;  // 保存局部变量地址
        }  // a 的作用域结束
        *p = 1111;  // 使用已销毁的栈变量
        LOGCATE("UseAfterScope *p=%d",*p);
    }

public:
    /**
     * @brief 主测试函数
     * 依次执行所有ASan测试用例
     * 警告：运行此函数会触发多种内存错误
     */
    static void MainTest() {
        HeapBufferOverflow();
        StackBufferOverflow();
        UseAfterFree();
        UseAfterScope();
        DoubleFree();
    }
};
int *ASanTestCase::p = nullptr;  // 静态成员变量定义
#endif //LEARNFFMPEG_ASANTESTCASE_H
