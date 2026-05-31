#include "AudioOutput.h"
#include <cstring>
#include <algorithm>
#include <iostream>

AudioOutput::AudioOutput(AVCodecContext* ctx,
    PacketQueue* pktQueue,
    Clock* audioClock,
    AVRational timeBase)
    : m_ctx(ctx), m_pktQueue(pktQueue), m_clock(audioClock),
      m_timeBase(timeBase)
{
    m_frame = av_frame_alloc();
}

AudioOutput::~AudioOutput() {
    if (m_dev) {
        SDL_PauseAudioDevice(m_dev, 1);
        SDL_CloseAudioDevice(m_dev);
    }
    swr_free(&m_swr);
    av_frame_free(&m_frame);
    if (m_pkt) av_packet_free(&m_pkt);
}

bool AudioOutput::open() {
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_Init(SDL_INIT_AUDIO) < 0) {
            std::cerr << "[Аудіо] Помилка під час виклику SDL_Init(SDL_INIT_AUDIO): "
                << SDL_GetError() << "\n";
            return false;
        }
    }

    if (m_ctx->ch_layout.nb_channels == 0) {
        std::cerr << "[Аудіо] Схема каналу відсутня, передбачається стерео\n";
        av_channel_layout_default(&m_ctx->ch_layout, 2);
    }

    SDL_AudioSpec wanted{};
    wanted.freq = m_ctx->sample_rate;
    wanted.format = AUDIO_S16SYS;
    wanted.channels = std::min(m_ctx->ch_layout.nb_channels, 2);
    wanted.silence = 0;
    wanted.samples = 1024;
    wanted.callback = AudioOutput::sdlCallback;
    wanted.userdata = this;

    m_dev = SDL_OpenAudioDevice(nullptr, 0, &wanted, &m_spec,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
        SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if (!m_dev) {
        std::cerr << "[Аудіо] Помилка SDL_OpenAudioDevice: " << SDL_GetError() << "\n";
        return false;
    }

    std::cout << "[Аудіо] Пристрій відкрито: частота=" << m_spec.freq
              << " канали=" << static_cast<int>(m_spec.channels)
              << " формат=" << m_spec.format << "\n";

    AVChannelLayout out_ch;
    av_channel_layout_default(&out_ch, m_spec.channels);

    int ret = swr_alloc_set_opts2(&m_swr,
        &out_ch, AV_SAMPLE_FMT_S16, m_spec.freq,
        &m_ctx->ch_layout, m_ctx->sample_fmt,  m_ctx->sample_rate,
        0, nullptr);

    if (!m_swr || swr_init(m_swr) < 0) {
        std::cerr << "[Аудіо] Помилка під час запуску swr_init\n";
        av_channel_layout_uninit(&out_ch);
        return false;
    }
    av_channel_layout_uninit(&out_ch);
    return true;
}

void AudioOutput::play()
{
    m_paused = false; SDL_PauseAudioDevice(m_dev, 0);
}
void AudioOutput::pause() { m_paused = true;  SDL_PauseAudioDevice(m_dev, 1); }

void AudioOutput::setVolume(int vol) {
    m_volume = std::clamp(vol, 0, SDL_MIX_MAXVOLUME);
}

void AudioOutput::flush() {
    m_doFlush = true;
    m_audioPts = 0.0;
}

void AudioOutput::sdlCallback(void* userdata, Uint8* stream, int len) {
    static_cast<AudioOutput*>(userdata)->audioCallback(stream, len);
}

void AudioOutput::audioCallback(Uint8* stream, int len) {
    SDL_memset(stream, 0, len);

    if (m_doFlush) {
        m_doFlush = false;
        m_bufSize = 0;
        m_bufIndex = 0;
        avcodec_flush_buffers(m_ctx);
        if (m_pkt)
        {
            av_packet_free(&m_pkt);
            m_pkt = nullptr;
        }

        if (m_swr)
        {
            swr_close(m_swr);
            if (swr_init(m_swr) < 0)
            {
                std::cerr << "[Аудіо] Помилка swr_init після очищення\n";
            }
        }
        return;
    }

    while (len > 0) {
        if (m_bufIndex >= m_bufSize) {
            int got = decodeAndResample();
            if (got <= 0) break;
        }

        int avail = m_bufSize - m_bufIndex;
        int copy  = std::min(avail, len);

        SDL_MixAudioFormat(stream, m_buf + m_bufIndex, AUDIO_S16SYS, copy, m_volume.load());

        m_bufIndex += copy;
        stream += copy;
        len -= copy;
    }
}

int AudioOutput::decodeAndResample() {
    while (true) {
        int ret = avcodec_receive_frame(m_ctx, m_frame);

        if (ret == 0) {
            if (m_frame->pts != AV_NOPTS_VALUE) {
                m_audioPts = m_frame->pts * av_q2d(m_timeBase);
            }
            
            else {
                m_audioPts += static_cast<double>(m_frame->nb_samples) / m_ctx->sample_rate;
            }
            m_clock->set(m_audioPts);

            int maxSamples = static_cast<int>(
                av_rescale_rnd(
                    swr_get_delay(m_swr, m_ctx->sample_rate)
                    + m_frame->nb_samples,
                    m_spec.freq, m_ctx->sample_rate, AV_ROUND_UP));

            uint8_t* out = m_buf;

            const uint8_t* in_data[AV_NUM_DATA_POINTERS];
            for (int i = 0; i < AV_NUM_DATA_POINTERS; ++i)
                in_data[i] = m_frame->data[i];

            int out_samples = swr_convert(m_swr, &out, maxSamples,
                in_data, m_frame->nb_samples);

            av_frame_unref(m_frame);

            if (out_samples < 0) {
                std::cerr << "[Аудіо] Помилка swr_convert\n";
                return 0;
            }
            if (out_samples == 0) return 0;

            m_bufSize  = out_samples * m_spec.channels * 2;
            m_bufIndex = 0;
            return m_bufSize;
        }

        if (ret != AVERROR(EAGAIN)) return 0;

        if (!m_pktQueue->pop(m_pkt, false)) return 0;

        if (m_pkt->data == nullptr) {
            avcodec_flush_buffers(m_ctx);
            av_packet_free(&m_pkt);
            m_pkt = nullptr;
            continue;
        }

        avcodec_send_packet(m_ctx, m_pkt);
        av_packet_free(&m_pkt);
        m_pkt = nullptr;
    }
}