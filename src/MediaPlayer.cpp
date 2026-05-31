#include "MediaPlayer.h"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <windows.h>

extern "C" {
#include <libavutil/time.h>
}

MediaPlayer::MediaPlayer(const std::string& filePath)
    : m_filePath(filePath) {}

MediaPlayer::~MediaPlayer() { cleanup(); }

bool MediaPlayer::run() {
    if (!openFile()) return false;
    if (!openVideoCodec()) return false;
    if (!openAudioCodec()) return false;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        std::cerr << "[SDL] Помилка ініціалізації: " << SDL_GetError() << "\n";
        return false;
    }
    if (TTF_Init() < 0) {
        std::cerr << "[SDL] Помилка під час ініціалізації TTF: " << TTF_GetError() << "\n";
    }

    std::string title = "VideoPlayer - "
        + std::filesystem::path(m_filePath).filename().string();

    double duration = m_fmt->duration > 0
        ? static_cast<double>(m_fmt->duration) / AV_TIME_BASE
        : 0.0;

    std::string codecName;
    if (m_vidCtx) {
        const AVCodec* c = avcodec_find_decoder(m_vidCtx->codec_id);
        codecName = c ? c->name : "невідомо";
    }

    PlayerCallbacks cb;
    cb.onPlayPause = [this]()
        {
            togglePlayPause();
        };
    cb.onSeek = [this](double d)
        {
            seek(d);
        };
    cb.onVolumeChange = [this](int dv)
        {
            changeVolume(dv);
        };
    cb.onStop = [this]()
        {
            stop();
        };

    if (m_audCtx && m_audIdx >= 0) {
        m_audioOutput = std::make_unique<AudioOutput>(
            m_audCtx, &m_audioQueue, &m_audioClock,
            m_fmt->streams[m_audIdx]->time_base);

        if (!m_audioOutput->open()) {
            std::cerr << "[MediaPlayer] Не вдалося відкрити аудіовихід\n";
            m_audioOutput.reset();
        } else {
            m_audioOutput->setVolume(m_volume);
            std::cout << "[MediaPlayer] Відкрито аудіо, частота дискретизації="
                << m_audCtx->sample_rate << "\n";
        }
    }

    if (m_vidCtx && m_vidIdx >= 0) {
        m_videoDecoder = std::make_unique<VideoDecoder>(
            m_vidCtx, &m_videoQueue,
            m_fmt->streams[m_vidIdx]->time_base);
    }

    int vw = m_vidCtx ? m_vidCtx->width : 1280;
    int vh = m_vidCtx ? m_vidCtx->height : 720;

    m_renderer = std::make_unique<VideoRenderer>(
        vw, vh, title, duration, codecName,
        m_videoDecoder.get(), &m_audioClock, cb);

    if (!m_renderer->init()) return false;

    m_stopRead = false;
    m_paused   = false;

    if (m_videoDecoder) m_videoDecoder->start();
    if (m_audioOutput) m_audioOutput->play();

    m_readThread = std::thread(&MediaPlayer::readLoop, this);

    m_renderer->setPaused(false);
    m_renderer->run();

    stop();
    return true;
}

void MediaPlayer::togglePlayPause() {
    m_paused = !m_paused;
    if (m_audioOutput) {
        if (m_paused) m_audioOutput->pause();
        else m_audioOutput->play();
    }
    m_renderer->setPaused(m_paused);
}

void MediaPlayer::seek(double deltaSec) {
    if (!m_fmt) return;

    double current = m_audioClock.hasStarted() ? m_audioClock.get() : m_renderer->currentTime();
    double target = std::clamp(current + deltaSec, 0.0,
        static_cast<double>(m_fmt->duration) / AV_TIME_BASE);

    std::cout << "[Перемотка] Від " << current << " до " << target << "\n";

    int64_t ts = static_cast<int64_t>(target * AV_TIME_BASE);
    m_seeking   = true;

    m_videoQueue.flush();
    m_audioQueue.flush();

    m_videoQueue.pushFlush();
    m_audioQueue.pushFlush();

    avformat_seek_file(m_fmt, -1, INT64_MIN, ts, INT64_MAX, AVSEEK_FLAG_BACKWARD);

    if (m_videoDecoder) m_videoDecoder->flushFrames();
    if (m_audioOutput)  m_audioOutput->flush();

    m_audioClock.set(target);
    m_renderer->setCurrentTime(target);
    m_seeking = false;

    std::cout << "[Перемотка] Готово\n";
}

void MediaPlayer::changeVolume(int delta) {
    if (delta == -999) {
        if (!m_muted) {
            m_volumeBeforeMute = m_volume;
            m_volume = 0;
            m_muted  = true;
        }
        
        else {
            m_volume = m_volumeBeforeMute;
            m_muted  = false;
        }
    }
    
    else {
        m_muted  = false;
        m_volume = std::clamp(m_volume + delta, 0, SDL_MIX_MAXVOLUME);
    }

    if (m_audioOutput) m_audioOutput->setVolume(m_volume);
    m_renderer->setVolume(m_volume);
}

void MediaPlayer::stop() {
    m_stopRead = true;
    m_videoQueue.abort();
    m_audioQueue.abort();
    if (m_videoDecoder) m_videoDecoder->stop();
    if (m_audioOutput)  m_audioOutput->pause();
    if (m_readThread.joinable()) m_readThread.join();
}

void MediaPlayer::readLoop() {
    AVPacket* pkt = av_packet_alloc();

    while (!m_stopRead) {
        if (m_seeking) { av_usleep(10000); continue; }

        if (m_videoQueue.size() >= MAX_QUEUE_SIZE && m_audioQueue.size() >= MAX_QUEUE_SIZE) {
            av_usleep(5000);
            continue;
        }

        int ret = av_read_frame(m_fmt, pkt);
        if (ret == AVERROR_EOF) {
            m_videoQueue.pushFlush();
            m_audioQueue.pushFlush();
            break;
        }
        
        if (ret < 0) { av_usleep(10000); continue; }

        if (pkt->stream_index == m_vidIdx) {
            m_videoQueue.push(pkt);
        }
        
        else if (pkt->stream_index == m_audIdx) {
            m_audioQueue.push(pkt);
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
}

bool MediaPlayer::openFile() {
    if (avformat_open_input(&m_fmt, m_filePath.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[VideoPlayer] Не вдається відкрити: " << m_filePath << "\n";
        return false;
    }
    if (avformat_find_stream_info(m_fmt, nullptr) < 0) {
        std::cerr << "[VideoPlayer] Немає інформації\n";
        return false;
    }
    av_dump_format(m_fmt, 0, m_filePath.c_str(), 0);

    m_vidIdx = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    m_audIdx = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (m_vidIdx < 0 && m_audIdx < 0) {
        std::cerr << "[VideoPlayer] Не знайдено відтворюваних потоків\n";
        return false;
    }
    return true;
}

bool MediaPlayer::openVideoCodec() {
    if (m_vidIdx < 0) return true;

    AVStream* st     = m_fmt->streams[m_vidIdx];
    const AVCodec* c = avcodec_find_decoder(st->codecpar->codec_id);
    if (!c) { std::cerr << "[VideoPlayer] Немає відеодекодера\n"; return false; }

    m_vidCtx = avcodec_alloc_context3(c);
    avcodec_parameters_to_context(m_vidCtx, st->codecpar);
    m_vidCtx->thread_count = 0;

    if (avcodec_open2(m_vidCtx, c, nullptr) < 0) {
        std::cerr << "[VideoPlayer] Не вдається відкрити відеокодек\n";
        return false;
    }
    return true;
}

bool MediaPlayer::openAudioCodec() {
    if (m_audIdx < 0) return true;

    AVStream* st     = m_fmt->streams[m_audIdx];
    const AVCodec* c = avcodec_find_decoder(st->codecpar->codec_id);
    if (!c) { std::cerr << "[VideoPlayer] Немає аудіодекодера\n"; return false; }

    m_audCtx = avcodec_alloc_context3(c);
    avcodec_parameters_to_context(m_audCtx, st->codecpar);

    if (m_audCtx->ch_layout.nb_channels == 0) {
        std::cerr << "[VideoPlayer] Схема аудіоканалів відсутня, передбачається стерео\n";
        av_channel_layout_default(&m_audCtx->ch_layout, 2);
    }

    if (avcodec_open2(m_audCtx, c, nullptr) < 0) {
        std::cerr << "[VideoPlayer] Не вдається відкрити аудіокодек\n";
        return false;
    }
    return true;
}

void MediaPlayer::cleanup() {
    m_videoDecoder.reset();
    m_audioOutput.reset();
    m_renderer.reset();

    if (m_vidCtx) { avcodec_free_context(&m_vidCtx); }
    if (m_audCtx) { avcodec_free_context(&m_audCtx); }
    if (m_fmt) { avformat_close_input(&m_fmt); }

    TTF_Quit();
    SDL_Quit();
}