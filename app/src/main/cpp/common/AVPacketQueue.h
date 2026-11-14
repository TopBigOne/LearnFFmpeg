/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#ifndef LEARNFFMPEG_AVPACKETQUEUE_H
#define LEARNFFMPEG_AVPACKETQUEUE_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

extern "C" {
#include <libavcodec/avcodec.h>
};

using namespace std;

/**
 * 队列中元素的类型
 *  - DATA ：普通数据包
 *  - FLUSH：seek 之后插入的标记，消费线程收到后自己去清空解码器（避免 seek 线程跨线程操作解码器）
 *  - EOS  ：文件读完的标记，消费线程收到后通知解码器"输入结束"，把缓存的帧全部取出
 */
enum PacketType {
    PACKET_TYPE_DATA,
    PACKET_TYPE_FLUSH,
    PACKET_TYPE_EOS
};

/**
 * 线程安全的 AVPacket 队列（参考 ffplay 的 PacketQueue）
 *
 * 与旧实现相比：
 *  - 不再按值拷贝 AVPacket 结构体（FFmpeg 新版本已不保证 sizeof(AVPacket) 稳定），
 *    而是用 av_packet_move_ref 转移引用；
 *  - 支持 FLUSH/EOS 两种标记；
 *  - Abort() 会唤醒所有阻塞在 GetPacket 上的线程，退出时不会卡死。
 */
class AVPacketQueue {
public:
    AVPacketQueue();

    virtual ~AVPacketQueue();

    // 入队数据包：pkt 的数据引用被"移走"，调用后 pkt 变为空包，可直接复用
    int PushPacket(AVPacket *pkt);

    // 入队 FLUSH 标记
    int PushFlushPacket();

    // 入队 EOS 标记
    int PushEOSPacket();

    // 清空队列中的所有元素
    void Flush();

    // 终止：唤醒所有等待者，之后 Push/Get 都返回 -1
    void Abort();

    // 开始（取消终止状态）
    void Start();

    /**
     * 获取数据包
     * @param pkt   输出：数据包（调用方负责 av_packet_unref）
     * @param type  输出：元素类型，可为 nullptr
     * @param block 队列为空时是否阻塞等待
     * @return 1 取到数据；0 队列为空（非阻塞）；-1 已终止
     */
    int GetPacket(AVPacket *pkt, PacketType *type = nullptr, bool block = true);

    int GetPacketSize();

    int GetSize();

    // 队列中数据包的总时长（单位：流的 time_base）
    int64_t GetDuration();

    int IsAbort();

private:
    struct Node {
        AVPacket  *pkt;
        PacketType type;
    };

    int Put(AVPacket *pkt, PacketType type);
    void ClearLocked();

private:
    mutex m_Mutex;
    condition_variable m_CondVar;
    std::queue<Node> m_Queue;
    int size = 0;
    int64_t duration = 0;
    bool abort_request = false;
};


#endif //LEARNFFMPEG_AVPACKETQUEUE_H
