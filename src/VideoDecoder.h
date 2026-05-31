#pragma once
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include "PacketQueue.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

class VideoDecoder {
public:
    VideoDecoder(AVCodecContext* ctx,
        PacketQueue*   pktQueue,
        AVRational     timeBase);
    ~VideoDecoder();

    void start();
    void stop();

    AVFrame* popFrame();
    double peekPts() const;
    bool empty() const;
    int  count() const;
    void flushFrames();

private:
    void decodeLoop();

    AVCodecContext* m_ctx;
    PacketQueue* m_pktQueue;
    AVRational m_timeBase;

    std::queue<AVFrame*> m_frameQueue;
    mutable std::mutex m_frameMutex;
    std::condition_variable m_frameCond;

    std::thread m_thread;
    std::atomic<bool> m_stop{false};

    static constexpr int MAX_FRAMES = 3;
};