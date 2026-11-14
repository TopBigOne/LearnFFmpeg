/**
 *
 * Created by 公众号：字节流动 on 2021/3/16.
 * https://github.com/githubhaohao/LearnFFmpeg
 * 最新文章首发于公众号：字节流动，有疑问或者技术交流可以添加微信 Byte-Flow ,领取视频教程, 拉你进技术交流群
 *
 * */

#include <LogUtil.h>
#include "AVPacketQueue.h"

AVPacketQueue::AVPacketQueue() {
}

AVPacketQueue::~AVPacketQueue() {
    Abort();
    Flush();
}

int AVPacketQueue::Put(AVPacket *pkt, PacketType type) {
    // 调用方已持有 m_Mutex
    if (abort_request) {
        return -1;
    }

    AVPacket *node = av_packet_alloc();
    if (node == nullptr) {
        return -1;
    }
    if (pkt != nullptr) {
        // 转移引用而不是拷贝数据：pkt 变为空包
        av_packet_move_ref(node, pkt);
    }

    m_Queue.push(Node{node, type});
    size += node->size;
    duration += node->duration;
    return 0;
}

int AVPacketQueue::PushPacket(AVPacket *pkt) {
    unique_lock<mutex> lock(m_Mutex);
    int ret = Put(pkt, PACKET_TYPE_DATA);
    m_CondVar.notify_all();
    lock.unlock();

    if (ret < 0) {
        av_packet_unref(pkt);
    }

    return ret;
}

int AVPacketQueue::PushFlushPacket() {
    unique_lock<mutex> lock(m_Mutex);
    int ret = Put(nullptr, PACKET_TYPE_FLUSH);
    m_CondVar.notify_all();
    return ret;
}

int AVPacketQueue::PushEOSPacket() {
    unique_lock<mutex> lock(m_Mutex);
    int ret = Put(nullptr, PACKET_TYPE_EOS);
    m_CondVar.notify_all();
    return ret;
}

void AVPacketQueue::ClearLocked() {
    while (!m_Queue.empty()) {
        av_packet_free(&m_Queue.front().pkt);
        m_Queue.pop();
    }
    size = 0;
    duration = 0;
}

void AVPacketQueue::Flush() {
    unique_lock<mutex> lock(m_Mutex);
    ClearLocked();
    m_CondVar.notify_all();
}

void AVPacketQueue::Abort() {
    unique_lock<mutex> lock(m_Mutex);
    abort_request = true;
    m_CondVar.notify_all();
}

void AVPacketQueue::Start() {
    unique_lock<mutex> lock(m_Mutex);
    abort_request = false;
    m_CondVar.notify_all();
}

int AVPacketQueue::GetPacket(AVPacket *pkt, PacketType *type, bool block) {
    unique_lock<mutex> lock(m_Mutex);
    for (;;) {
        if (abort_request) {
            return -1;
        }

        if (!m_Queue.empty()) {
            Node node = m_Queue.front();
            m_Queue.pop();
            size -= node.pkt->size;
            duration -= node.pkt->duration;
            av_packet_move_ref(pkt, node.pkt);
            av_packet_free(&node.pkt);
            if (type) *type = node.type;
            return 1;
        }

        if (!block) {
            return 0;
        }
        m_CondVar.wait(lock);
    }
}

int AVPacketQueue::GetPacketSize() {
    unique_lock<mutex> lock(m_Mutex);
    return static_cast<int>(m_Queue.size());
}

int AVPacketQueue::GetSize() {
    unique_lock<mutex> lock(m_Mutex);
    return size;
}

int64_t AVPacketQueue::GetDuration() {
    unique_lock<mutex> lock(m_Mutex);
    return duration;
}

int AVPacketQueue::IsAbort() {
    unique_lock<mutex> lock(m_Mutex);
    return abort_request;
}
