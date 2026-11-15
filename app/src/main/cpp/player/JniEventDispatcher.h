/**
 * JNI 事件分发线程：播放器内部线程只把事件放进队列，由这一个线程统一回调 Java
 * （FFMediaPlayer.playerEventCallback），解码 / 渲染 / 控制线程都不直接调用 Java。
 *
 *  - 线程启动时 Attach 到 JVM 一次，退出时 Detach，并负责删除 Java 对象的全局引用；
 *  - 高频事件（进度、请求渲染）在队列里合并：同类事件还没被取走时只更新数值，不会越积越多；
 *  - Shutdown 等待线程退出，但如果线程正卡在 Java 回调里（例如 Java 层在回调中等待 UI 线程，
 *    而 UI 线程正在调用 release），超时后放弃等待并 detach：线程持有自身的 shared_ptr，
 *    回调返回后自行清理，release 不会因此死锁。
 */

#ifndef LEARNFFMPEG_JNIEVENTDISPATCHER_H
#define LEARNFFMPEG_JNIEVENTDISPATCHER_H

#include <jni.h>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

class JniEventDispatcher {
public:
    /**
     * @param env        调用线程（Java 线程）的 JNIEnv
     * @param javaPlayer Java 层 FFMediaPlayer 对象，内部会创建全局引用
     * @return 失败（找不到回调方法、线程创建失败）返回 nullptr
     */
    static std::shared_ptr<JniEventDispatcher> Create(JNIEnv *env, jobject javaPlayer);

    ~JniEventDispatcher();

    JniEventDispatcher(const JniEventDispatcher &) = delete;
    JniEventDispatcher &operator=(const JniEventDispatcher &) = delete;

    /** 任意线程调用，不阻塞 */
    void Post(int msg, float value);

    /**
     * 停止分发线程，丢弃尚未分发的事件；最多等待 timeoutMs
     * 在分发线程自身（即 Java 回调里）调用时不会等待
     */
    void Shutdown(int timeoutMs);

private:
    struct Event {
        int msg;
        float value;
    };

    JniEventDispatcher(JavaVM *vm, jobject globalRef, jmethodID method);

    static void ThreadMain(std::shared_ptr<JniEventDispatcher> self);

    void Loop(JNIEnv *env);

    JavaVM *m_JavaVM;
    jobject m_JavaObj;          // 全局引用，由分发线程退出时删除
    jmethodID m_Method;

    std::mutex m_Mutex;
    std::condition_variable m_Cond;
    std::deque<Event> m_Events;
    bool m_Quit = false;
    bool m_Exited = false;
    bool m_RefReleased = false;
    std::thread m_Thread;
};

#endif //LEARNFFMPEG_JNIEVENTDISPATCHER_H
