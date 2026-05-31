#pragma once
#include <atomic>
#include "PacketQueue.h"
#include "Clock.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/rational.h>
}

#include <SDL2/SDL.h>

class AudioOutput {
public:
    AudioOutput(AVCodecContext* ctx,
        PacketQueue* pktQueue,
        Clock* audioClock,
        AVRational timeBase);
    ~AudioOutput();

    bool open();
    void play();
    void pause();
    bool isPaused() const
    {
        return m_paused;
    }

    void setVolume(int vol);
    int  getVolume() const
    {
        return m_volume;
    }

    void flush();

private:
    static void sdlCallback(void* userdata, Uint8* stream, int len);
    void        audioCallback(Uint8* stream, int len);

    int  decodeAndResample();

    AVCodecContext* m_ctx;
    PacketQueue* m_pktQueue;
    Clock* m_clock;
    AVRational m_timeBase;

    SwrContext* m_swr{nullptr};
    SDL_AudioDeviceID m_dev{0};
    SDL_AudioSpec m_spec{};

    static constexpr int BUF_SIZE = 192000;
    uint8_t m_buf[BUF_SIZE]{};
    int m_bufSize{0};
    int m_bufIndex{0};

    AVPacket* m_pkt{nullptr};
    AVFrame* m_frame{nullptr};

    double m_audioPts{0.0};

    std::atomic<int> m_volume{SDL_MIX_MAXVOLUME};
    std::atomic<bool> m_paused{true};
    std::atomic<bool> m_doFlush{false};
};