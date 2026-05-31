#pragma once
#include <cstdint>
#include <SDL2/SDL.h>

class Clock {
public:
    Clock() = default;

    void set(double pts) {
        m_pts = pts;
        m_lastUpdated = SDL_GetTicks();
    }

    double get() const {
        if (m_lastUpdated == 0) return 0.0;
        return m_pts + static_cast<double>(SDL_GetTicks() - m_lastUpdated) / 1000.0;
    }

    bool hasStarted() const { return m_lastUpdated != 0; }

    void reset() { m_pts = 0.0; m_lastUpdated = 0; }

private:
    double m_pts{0.0};
    uint32_t m_lastUpdated{0};
};