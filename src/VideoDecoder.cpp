#include "VideoDecoder.h"
#include <iostream>

VideoDecoder::VideoDecoder(AVCodecContext* ctx,
    PacketQueue* pktQueue,
    AVRational timeBase)
    : m_ctx(ctx), m_pktQueue(pktQueue), m_timeBase(timeBase) {
}

VideoDecoder::~VideoDecoder() {
    stop();
    flushFrames();
}

void VideoDecoder::start() {
    m_stop = false;
    m_thread = std::thread(&VideoDecoder::decodeLoop, this);
}

void VideoDecoder::stop() {
    m_stop = true;
    m_pktQueue->abort();
    m_frameCond.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void VideoDecoder::flushFrames() {
    std::lock_guard<std::mutex> lk(m_frameMutex);
    while (!m_frameQueue.empty()) {
        AVFrame* f = m_frameQueue.front(); m_frameQueue.pop();
        av_frame_free(&f);
    }
    m_frameCond.notify_all();
}

AVFrame* VideoDecoder::popFrame() {
    std::lock_guard<std::mutex> lk(m_frameMutex);
    if (m_frameQueue.empty()) return nullptr;
    AVFrame* f = m_frameQueue.front(); m_frameQueue.pop();
    m_frameCond.notify_one();
    return f;
}

double VideoDecoder::peekPts() const {
    std::lock_guard<std::mutex> lk(m_frameMutex);
    if (m_frameQueue.empty()) return -1.0;
    int64_t pts = m_frameQueue.front()->best_effort_timestamp;
    if (pts == AV_NOPTS_VALUE) pts = m_frameQueue.front()->pts;
    return pts * av_q2d(m_timeBase);
}

bool VideoDecoder::empty() const {
    std::lock_guard<std::mutex> lk(m_frameMutex);
    return m_frameQueue.empty();
}

int VideoDecoder::count() const {
    std::lock_guard<std::mutex> lk(m_frameMutex);
    return static_cast<int>(m_frameQueue.size());
}

void VideoDecoder::decodeLoop() {
    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = nullptr;

    while (!m_stop) {
        {
            std::unique_lock<std::mutex> lk(m_frameMutex);
            m_frameCond.wait(lk, [this] {
                return static_cast<int>(m_frameQueue.size()) < MAX_FRAMES || m_stop;
                });
        }
        if (m_stop) break;

        if (!m_pktQueue->pop(pkt)) break;

        if (pkt->data == nullptr) {
            avcodec_flush_buffers(m_ctx);
            av_packet_free(&pkt);
            continue;
        }

        while (!m_stop) {
            int ret = avcodec_send_packet(m_ctx, pkt);
            if (ret == 0) break;

            if (ret == AVERROR(EAGAIN)) {
                int r = avcodec_receive_frame(m_ctx, frame);
                if (r == 0) {
                    AVFrame* copy = av_frame_alloc();
                    av_frame_ref(copy, frame);
                    av_frame_unref(frame);
                    {
                        std::lock_guard<std::mutex> lk(m_frameMutex);
                        m_frameQueue.push(copy);
                    }
                    m_frameCond.notify_one();
                    continue;
                }
                if (r == AVERROR_EOF) break;
                if (r < 0) break;
            }
            
            break;
        }

        av_packet_free(&pkt);
        pkt = nullptr;

        while (!m_stop) {
            int ret = avcodec_receive_frame(m_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;

            AVFrame* copy = av_frame_alloc();
            av_frame_ref(copy, frame);
            av_frame_unref(frame);

            {
                std::lock_guard<std::mutex> lk(m_frameMutex);
                m_frameQueue.push(copy);
            }
            m_frameCond.notify_one();
        }
    }

    av_frame_free(&frame);
}