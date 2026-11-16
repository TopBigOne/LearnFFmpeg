/**
 * 运行时判断设备的 Android 版本（API level），用于调用高于 minSdk 的 NDK 函数
 */

#ifndef LEARNFFMPEG_APILEVEL_H
#define LEARNFFMPEG_APILEVEL_H

/**
 * 设备系统的 API level >= level 时为真，相当于 Java/Kotlin 里的 Build.VERSION.SDK_INT >= level。
 *
 * 用法：直接写在 if 条件里，把高版本 NDK 函数的调用包起来
 *
 *     if (DEVICE_API_AT_LEAST(28)) {
 *         AMediaCodec_getName(codec, &name);   // NDK 头文件里标注了 __INTRODUCED_IN(28)
 *     }
 *
 * 背景：工程 minSdk 是 23，CMakeLists.txt 打开了 __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__，
 * 高于 minSdk 的 NDK 函数按弱引用链接（老系统上 so 能正常加载，但这些函数地址为空，调用即崩溃），
 * 并开启 -Werror=unguarded-availability：调用这类函数时，如果外面没有版本判断，直接编译报错。
 * 报错信息会写明需要的版本，例如 "'AMediaCodec_getName' is only available on Android 28 or newer"。
 *
 * 为什么是宏而不是函数：
 *   编译器只认 if 条件里直接出现的 __builtin_available(...)，以此确认块内的调用是安全的。
 *   宏在编译前原样展开，编译器看到的仍是 __builtin_available；
 *   而包成函数（例如 bool IsApiAtLeast(int)，或用 __system_property_get / android_get_device_api_level
 *   自己判断）运行时结果一样，编译器却认不出来，照样报错。
 *
 * 展开后的写法 __builtin_available(android 28, *) 是 clang 内置语法（来自苹果的 @available），不是函数调用：
 *   - "android 28"：一个整体，表示 Android 平台、版本 >= 28，平台名和版本号之间用空格隔开；
 *   - "*"：其余平台，语法要求必须写。
 * 运行时它调用 compiler-rt 的 __isOSVersionAtLeast，第一次读取系统属性 ro.build.version.sdk 并缓存，
 * 之后每次只是比较整数，开销可以忽略。
 *
 * 注意：
 *   - 只在 if 条件里直接使用才算数。先赋值给变量（bool ok = DEVICE_API_AT_LEAST(28)）再 if (ok)，编译器不认。
 *   - 判断只对当前这个 if 块生效。前面判断过、后面另一处再调用时，那一处也要再包一层。
 *   - level 必须是整数常量（如 26、28），不能是变量。
 *
 * 参考：https://developer.android.com/ndk/guides/using-newer-apis
 */
#define DEVICE_API_AT_LEAST(level) __builtin_available(android level, *)

#endif //LEARNFFMPEG_APILEVEL_H
