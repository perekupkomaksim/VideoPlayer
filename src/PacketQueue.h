#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
}

static constexpr int MAX_QUEUE_SIZE = 512;

class PacketQueue {
public:
    PacketQueue()  = default;
    ~PacketQueue() { flush(); }

    void push(AVPacket* pkt) {
        std::unique_lock<std::mutex> lk(m_mutex);

        m_cond.wait(lk, [this] {
            return static_cast<int>(m_queue.size()) < MAX_QUEUE_SIZE || m_abort;
        });

        if (m_abort) return;

        AVPacket* copy = av_packet_alloc();

        if (av_packet_ref(copy, pkt) < 0)
        {
            av_packet_free(&copy);
            return;
        }

        m_queue.push(copy);
        m_cond.notify_all();
    }

    void pushFlush() {
        std::lock_guard<std::mutex> lk(m_mutex);
        AVPacket* pkt = av_packet_alloc();
        m_queue.push(pkt);
        m_cond.notify_all();
    }

    bool pop(AVPacket*& pkt, bool block = true) {
        std::unique_lock<std::mutex> lk(m_mutex);
        if (block)
            m_cond.wait(lk, [this]
                {
                    return !m_queue.empty() || m_abort;
                });

        if (m_queue.empty()) return false;

        pkt = m_queue.front();
        m_queue.pop();
        m_cond.notify_all();
        return true;
    }

    void flush() {
        std::lock_guard<std::mutex> lk(m_mutex);
        while (!m_queue.empty()) {
            AVPacket* p = m_queue.front(); m_queue.pop();
            av_packet_free(&p);
        }
        m_cond.notify_all();
    }

    void abort() { m_abort = true; m_cond.notify_all(); }
    bool isAborted() const { return m_abort; }

    int size() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return static_cast<int>(m_queue.size());
    }

private:
    std::queue<AVPacket*> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    std::atomic<bool> m_abort{false};
};