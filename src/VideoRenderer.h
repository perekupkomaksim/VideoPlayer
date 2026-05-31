#pragma once
#include <string>
#include <functional>
#include "Clock.h"
#include "VideoDecoder.h"

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

struct PlayerCallbacks {
    std::function<void()> onPlayPause;
    std::function<void(double)> onSeek;
    std::function<void(int)> onVolumeChange;
    std::function<void()> onStop;
};

class VideoRenderer {
public:
    VideoRenderer(int videoW, int videoH,
        const std::string& title,
        double duration,
        const std::string& codecName,
        VideoDecoder* decoder,
        Clock*        audioClock,
        PlayerCallbacks callbacks);
    ~VideoRenderer();

    bool init();
    void run();

    void setVolume(int vol) {
        m_volume = vol;
    }
    void setPaused(bool p);
    void setCurrentTime(double t)
    {
        m_currentTime = t;
    }
    double currentTime() const
    {
        return m_currentTime;
    }

private:
    void handleEvent(const SDL_Event& ev);
    void renderFrame(AVFrame* frame);
    void drawUI();
    void drawText(const std::string& text, int x, int y,
                  SDL_Color color = {255,255,255,255});

    int m_videoW, m_videoH;
    std::string m_title;
    double m_duration;
    std::string m_codecName;
    VideoDecoder* m_decoder;
    Clock* m_audioClock;
    PlayerCallbacks m_cb;

    SDL_Window* m_window {nullptr};
    SDL_Renderer* m_renderer {nullptr};
    SDL_Texture* m_texture {nullptr};
    TTF_Font* m_font {nullptr};

    SwsContext* m_sws {nullptr};

    bool m_paused {false};
    int m_volume {100};
    double m_currentTime{0.0};
    bool m_running {true};
    bool m_showUI {true};
    Uint32 m_lastMouse {0};

    Uint32 m_wallStart {0};
    double m_wallPts {0.0};

    SDL_Rect m_progressRect{};
};