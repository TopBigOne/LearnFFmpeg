/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#ifndef LEARNFFMPEG_THREADSAFEQUEUE_H
#define LEARNFFMPEG_THREADSAFEQUEUE_H

#include <queue>
#include <memory>
#include <mutex>
#include <chrono>
#include <condition_variable>

/**
 * 线程安全队列（元素类型 T 为指针）
 *
 *  - 所有访问都在锁内进行（原来的 Empty() 不加锁，与 Push 并发是数据竞争）；
 *  - WaitPop：队列为空时在条件变量上阻塞等待，取代消费端 usleep 轮询；
 *  - 可选容量上限：满了丢弃最旧的元素并返回给调用方释放，防止消费跟不上时内存无限增长；
 *  - WakeUpAll：通知所有等待者"不会再有新数据了"，用于停止时让消费线程尽快退出。
 */
template<typename T>
class ThreadSafeQueue {

public:
    /**
     * @param maxSize 容量上限，0 表示不限制
     */
    explicit ThreadSafeQueue(size_t maxSize = 0) : m_maxSize(maxSize) {}

    ThreadSafeQueue(ThreadSafeQueue const &other) = delete;
    ThreadSafeQueue &operator=(ThreadSafeQueue const &other) = delete;

    void SetMaxSize(size_t maxSize) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_maxSize = maxSize;
    }

    /**
     * 入队
     * @return 队列已满时被挤出的最旧元素（调用方负责释放），否则返回 nullptr
     */
    T Push(T new_value)
    {
        T dropped = nullptr;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            if(m_maxSize > 0 && m_dataQueue.size() >= m_maxSize) {
                dropped = m_dataQueue.front();
                m_dataQueue.pop();
            }
            m_dataQueue.push(new_value);
        }
        m_condVar.notify_one();
        return dropped;
    }

    /** 非阻塞出队，队列为空返回 nullptr */
    T Pop(){
        std::lock_guard<std::mutex> lk(m_mutex);
        return PopLocked();
    }

    /**
     * 阻塞出队：队列为空时最多等待 timeoutMs 毫秒（期间有数据或 WakeUpAll 会立即返回）
     * @return 取到的元素；超时或已唤醒且队列为空时返回 nullptr
     */
    T WaitPop(int timeoutMs) {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_condVar.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                           [this] { return !m_dataQueue.empty() || m_wakeUp; });
        return PopLocked();
    }

    /** 唤醒所有阻塞在 WaitPop 上的线程，之后 WaitPop 不再等待 */
    void WakeUpAll() {
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_wakeUp = true;
        }
        m_condVar.notify_all();
    }

    bool Empty() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_dataQueue.empty();
    }

    int Size() {
        std::lock_guard<std::mutex> lk(m_mutex);
        return static_cast<int>(m_dataQueue.size());
    }

private:
    T PopLocked() {
        if(m_dataQueue.empty()) return nullptr;
        T res = m_dataQueue.front();
        m_dataQueue.pop();
        return res;
    }

    mutable std::mutex m_mutex;
    std::queue<T> m_dataQueue;
    std::condition_variable m_condVar;
    size_t m_maxSize = 0;
    bool m_wakeUp = false;
};
#endif //LEARNFFMPEG_THREADSAFEQUEUE_H
