#include "VideoRenderer.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

extern "C" {
#include <libavutil/imgutils.h>
}

static std::string formatTime(double seconds) {
    if (seconds < 0) seconds = 0;
    int total = static_cast<int>(seconds);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    std::ostringstream oss;
    if (h > 0) { oss << h << ":"; }
    oss << std::setw(2) << std::setfill('0') << m << ":"
        << std::setw(2) << std::setfill('0') << s;
    return oss.str();
}

VideoRenderer::VideoRenderer(int videoW, int videoH,
    const std::string& title,
    double duration,
    const std::string& codecName,
    VideoDecoder* decoder,
    Clock* audioClock,
    PlayerCallbacks callbacks)
    : m_videoW(videoW), m_videoH(videoH),
    m_title(title), m_duration(duration),
    m_codecName(codecName),
    m_decoder(decoder), m_audioClock(audioClock),
    m_cb(std::move(callbacks))
{}

VideoRenderer::~VideoRenderer() {
    if (m_sws) sws_freeContext(m_sws);
    if (m_texture) SDL_DestroyTexture(m_texture);
    if (m_renderer) SDL_DestroyRenderer(m_renderer);
    if (m_window) SDL_DestroyWindow(m_window);
    if (m_font) TTF_CloseFont(m_font);
}

void VideoRenderer::setPaused(bool p) {
    m_paused = p;
    if (p) {
        m_wallStart = 0;
    }
}

bool VideoRenderer::init() {
    int winW = 1280;
    int winH = 720;

    m_window = SDL_CreateWindow(m_title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winW,
        winH,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

    SDL_SetWindowMinimumSize(m_window, 640, 360);

    if (!m_window) {
        std::cerr << "[SDL] CreateWindow: " << SDL_GetError() << "\n";
        return false;
    }

    m_renderer = SDL_CreateRenderer(m_window, -1,
        SDL_RENDERER_ACCELERATED |
        SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer)
        m_renderer = SDL_CreateRenderer(m_window, -1, 0);

    if (!m_renderer) {
        std::cerr << "[SDL] CreateRenderer: " << SDL_GetError() << "\n";
        return false;
    }

    m_texture = SDL_CreateTexture(m_renderer,
        SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        m_videoW, m_videoH);
    if (!m_texture) {
        std::cerr << "[SDL] CreateTexture: " << SDL_GetError() << "\n";
        return false;
    }

    if (TTF_WasInit()) {
        const char* fonts[] = {
            "C:/Windows/Fonts/arial.ttf",
            "C:/Windows/Fonts/segoeui.ttf",
            nullptr
        };

        for (int i = 0; fonts[i]; ++i) {
            m_font = TTF_OpenFont(fonts[i], 14);
            if (m_font) break;
        }
    }

    m_lastMouse = SDL_GetTicks();
    return true;
}

void VideoRenderer::run() {
    SDL_Event ev;

    while (m_running) {
        while (SDL_PollEvent(&ev))
            handleEvent(ev);

        m_showUI = (SDL_GetTicks() - m_lastMouse < 3000) || m_paused;

        if (!m_paused && !m_decoder->empty()) {
            double videoPts = m_decoder->peekPts();

            double masterClock;
            if (m_audioClock->hasStarted()) {
                masterClock = m_audioClock->get();
            }
            
            else {
                if (m_wallStart == 0) {
                    m_wallStart = SDL_GetTicks();
                    m_wallPts   = videoPts;
                }
                masterClock = m_wallPts +
                    static_cast<double>(SDL_GetTicks() - m_wallStart) / 1000.0;
            }

            double delay = videoPts - masterClock;

            if (delay > 0.1) {
                Uint32 waitMs = static_cast<Uint32>(std::min(delay * 1000.0 - 5.0, 50.0));
                if (waitMs > 0) SDL_Delay(waitMs);
            }
            else if (delay < -0.5) {
                
                AVFrame* stale = m_decoder->popFrame();
                if (stale) {
                    m_currentTime = videoPts;
                    av_frame_free(&stale);
                }

                if (!m_audioClock->hasStarted()) {
                    m_wallStart = SDL_GetTicks();
                    m_wallPts = videoPts;
                }
            }
            else {
                AVFrame* frame = m_decoder->popFrame();
                if (frame) {
                    renderFrame(frame);
                    av_frame_free(&frame);
                    m_currentTime = masterClock;
                    
                    if (!m_audioClock->hasStarted()) {
                        m_wallStart = SDL_GetTicks();
                        m_wallPts   = videoPts;
                    }
                }
            }
        }

        drawUI();
        SDL_RenderPresent(m_renderer);

        if (m_paused || m_decoder->empty())
            SDL_Delay(16);
    }
}

void VideoRenderer::handleEvent(const SDL_Event& ev) {
    switch (ev.type) {
    case SDL_QUIT:
        m_running = false;
        m_cb.onStop();
        break;

    case SDL_MOUSEMOTION:
        m_lastMouse = SDL_GetTicks();
        m_showUI    = true;
        break;

    case SDL_MOUSEBUTTONDOWN:
        m_lastMouse = SDL_GetTicks();
        if (ev.button.button == SDL_BUTTON_LEFT) {
            int mx = ev.button.x, my = ev.button.y;
            if (m_duration > 0 &&
                mx >= m_progressRect.x &&
                mx <= m_progressRect.x + m_progressRect.w &&
                my >= m_progressRect.y &&
                my <= m_progressRect.y + m_progressRect.h)
            {
                double ratio  = static_cast<double>(mx - m_progressRect.x) / m_progressRect.w;
                double target = ratio * m_duration;
                m_cb.onSeek(target - m_currentTime);
                m_wallStart = 0;
            }
            if (ev.button.clicks == 2) {
                bool full = SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(m_window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
        }
        break;

    case SDL_KEYDOWN:
        switch (ev.key.keysym.sym) {
        case SDLK_SPACE: m_cb.onPlayPause(); break;
        case SDLK_RIGHT: m_cb.onSeek(+10.0); m_wallStart = 0; break;
        case SDLK_LEFT: m_cb.onSeek(-10.0); m_wallStart = 0; break;
        case SDLK_UP: m_cb.onVolumeChange(+10); break;
        case SDLK_DOWN: m_cb.onVolumeChange(-10); break;
        case SDLK_m: m_cb.onVolumeChange(-999); break;
        case SDLK_f: {
            bool full = SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
            SDL_SetWindowFullscreen(m_window, full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
        }
        case SDLK_ESCAPE:
            m_running = false;
            m_cb.onStop();
            break;
        }
        break;
    }
}

void VideoRenderer::renderFrame(AVFrame* frame) {
    SDL_UpdateYUVTexture(m_texture, nullptr,
        frame->data[0], frame->linesize[0],
        frame->data[1], frame->linesize[1],
        frame->data[2], frame->linesize[2]);

    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
    SDL_RenderClear(m_renderer);

    int winW, winH;
    SDL_GetWindowSize(m_window, &winW, &winH);
    int drawH = winH - 60;

    float scale = std::min(static_cast<float>(winW)  / m_videoW,
        static_cast<float>(drawH) / m_videoH);
    int dstW = static_cast<int>(m_videoW * scale);
    int dstH = static_cast<int>(m_videoH * scale);
    SDL_Rect dst{(winW - dstW) / 2, (drawH - dstH) / 2, dstW, dstH};
    SDL_RenderCopy(m_renderer, m_texture, nullptr, &dst);
}

void VideoRenderer::drawUI() {
    int winW, winH;
    SDL_GetWindowSize(m_window, &winW, &winH);

    if (!m_showUI) return;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, 10, 10, 10, 210);
    SDL_Rect bar{0, winH - 60, winW, 60};
    SDL_RenderFillRect(m_renderer, &bar);

    int pbX = 10, pbY = winH - 52, pbW = winW - 20, pbH = 10;
    m_progressRect = {pbX, pbY, pbW, pbH};

    SDL_SetRenderDrawColor(m_renderer, 80, 80, 80, 255);
    SDL_RenderFillRect(m_renderer, &m_progressRect);

    double progress = (m_duration > 0)
        ? std::clamp(m_currentTime / m_duration, 0.0, 1.0)
        : 0.0;
    int fillW = static_cast<int>(pbW * progress);

    SDL_SetRenderDrawColor(m_renderer, 30, 140, 255, 255);
    SDL_Rect fill{pbX, pbY, fillW, pbH};
    SDL_RenderFillRect(m_renderer, &fill);

    SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
    SDL_Rect knob{pbX + fillW - 5, pbY - 3, 10, pbH + 6};
    SDL_RenderFillRect(m_renderer, &knob);

    std::string timeStr = formatTime(m_currentTime) + " / " + formatTime(m_duration);
    std::string stateStr = m_paused ? "[ Пауза ]" : "[ Плей ]";
    std::string volStr = m_volume > 0 ? "Гучність: " + std::to_string(m_volume * 100 / SDL_MIX_MAXVOLUME) + "%" : "Гучність: [Вимк.]";
    std::string infoStr = m_codecName + " " + std::to_string(m_videoW) + "x" + std::to_string(m_videoH);

    drawText(timeStr, 10, winH - 38);
    drawText(stateStr, winW / 2 - 35, winH - 38);
    drawText(volStr, winW - 120, winH - 38);
    drawText(infoStr, 10, winH - 20, {160, 160, 160, 255});
}

void VideoRenderer::drawText(const std::string& text, int x, int y, SDL_Color color) {
    if (!m_font || text.empty()) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(m_font, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(m_renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) return;
    int w, h;
    SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
    SDL_Rect dst{x, y, w, h};
    SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}