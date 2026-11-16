#include "JniEventDispatcher.h"
#include <chrono>
#include <LogUtil.h>
#include "PlayerMessage.h"

// Java 层回调方法：void playerEventCallback(int msgType, float msgValue)
static const char *JAVA_EVENT_METHOD = "playerEventCallback";
static const char *JAVA_EVENT_SIGNATURE = "(IF)V";

std::shared_ptr<JniEventDispatcher> JniEventDispatcher::Create(JNIEnv *env, jobject javaPlayer) {
    JavaVM *vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK) return nullptr;

    jclass clazz = env->GetObjectClass(javaPlayer);
    jmethodID method = env->GetMethodID(clazz, JAVA_EVENT_METHOD, JAVA_EVENT_SIGNATURE);
    env->DeleteLocalRef(clazz);
    if (method == nullptr) {
        env->ExceptionClear();
        LOGCATE("JniEventDispatcher::Create method %s not found", JAVA_EVENT_METHOD);
        return nullptr;
    }

    jobject globalRef = env->NewGlobalRef(javaPlayer);
    std::shared_ptr<JniEventDispatcher> dispatcher(new JniEventDispatcher(vm, globalRef, method));
    try {
        // 线程持有一份 shared_ptr：即使 Shutdown 超时 detach，对象也会活到线程退出
        dispatcher->m_Thread = std::thread(&JniEventDispatcher::ThreadMain, dispatcher);
    } catch (const std::system_error &e) {
        LOGCATE("JniEventDispatcher::Create start thread fail: %s", e.what());
        env->DeleteGlobalRef(globalRef);
        dispatcher->m_RefReleased = true;
        return nullptr;
    }
    return dispatcher;
}

JniEventDispatcher::JniEventDispatcher(JavaVM *vm, jobject globalRef, jmethodID method)
        : m_JavaVM(vm), m_JavaObj(globalRef), m_Method(method) {
}

JniEventDispatcher::~JniEventDispatcher() {
    // 正常情况下线程已 join 或 detach；这里只防御 "从未启动线程" 的情况
    if (m_Thread.joinable()) m_Thread.detach();
}

void JniEventDispatcher::Post(int msg, float value) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Quit) return;
    // 进度和渲染请求只关心最新值：Java 侧处理慢时合并，避免队列无限增长
    if (msg == MSG_DECODING_TIME || msg == MSG_REQUEST_RENDER) {
        for (Event &event : m_Events) {
            if (event.msg == msg) {
                event.value = value;
                return;
            }
        }
    }
    m_Events.push_back(Event{msg, value});
    m_Cond.notify_one();
}

void JniEventDispatcher::Shutdown(int timeoutMs) {
    std::unique_lock<std::mutex> lock(m_Mutex);
    m_Quit = true;
    m_Events.clear();
    m_Cond.notify_all();

    if (!m_Thread.joinable()) return;
    if (m_Thread.get_id() == std::this_thread::get_id()) {
        // Java 在回调里直接 release：线程回到 Loop 后看到 m_Quit 自行退出
        m_Thread.detach();
        return;
    }

    bool exited = m_Cond.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return m_Exited; });
    lock.unlock();
    if (exited) {
        m_Thread.join();
        if (!m_RefReleased) {
            // 分发线程没能 Attach，全局引用由调用方（Java 线程）删除
            JNIEnv *env = nullptr;
            if (m_JavaVM->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) == JNI_OK) {
                env->DeleteGlobalRef(m_JavaObj);
                m_RefReleased = true;
            }
        }
    } else {
        LOGCATE("JniEventDispatcher::Shutdown java callback did not return in %dms, detach", timeoutMs);
        m_Thread.detach();
    }
}

void JniEventDispatcher::ThreadMain(std::shared_ptr<JniEventDispatcher> self) {
    JNIEnv *env = nullptr;
    JavaVMAttachArgs args{JNI_VERSION_1_6, "FFPlayerEvent", nullptr};
    if (self->m_JavaVM->AttachCurrentThread(&env, &args) != JNI_OK) {
        LOGCATE("JniEventDispatcher attach thread fail");
        std::lock_guard<std::mutex> lock(self->m_Mutex);
        self->m_Exited = true;
        self->m_Cond.notify_all();
        return;
    }

    self->Loop(env);

    env->DeleteGlobalRef(self->m_JavaObj);
    self->m_JavaVM->DetachCurrentThread();
    {
        std::lock_guard<std::mutex> lock(self->m_Mutex);
        self->m_RefReleased = true;
        self->m_Exited = true;
        self->m_Cond.notify_all();
    }
}

void JniEventDispatcher::Loop(JNIEnv *env) {
    std::unique_lock<std::mutex> lock(m_Mutex);
    for (;;) {
        m_Cond.wait(lock, [this] { return m_Quit || !m_Events.empty(); });
        if (m_Quit) break;
        Event event = m_Events.front();
        m_Events.pop_front();

        lock.unlock();
        env->CallVoidMethod(m_JavaObj, m_Method, event.msg, event.value);
        // 线程长期附加在 JVM 上，Java 抛出的异常必须清掉，否则后续 JNI 调用都会出错
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        lock.lock();
    }
}
