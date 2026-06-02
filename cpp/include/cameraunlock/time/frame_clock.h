#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace cameraunlock::time {

// Per-pipeline frame-delta clock. Tick() returns seconds since the previous
// Tick(), clamped to [0, maxDt] so an alt-tab stall or breakpoint doesn't
// feed a huge dt into smoothing/interpolation and snap the view. Each
// processing pipeline (rotation, position) owns an instance so their dt
// streams stay independent; the QPC frequency is shared (it is constant for
// the lifetime of the process).
class FrameClock {
public:
    explicit FrameClock(float maxDt = 0.1f) : max_dt_(maxDt) {}

    float Tick() {
        if (!initialized_) {
            QueryPerformanceCounter(&last_);
            initialized_ = true;
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = static_cast<float>(
            static_cast<double>(now.QuadPart - last_.QuadPart) /
            static_cast<double>(Freq().QuadPart));
        last_ = now;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > max_dt_) dt = max_dt_;
        return dt;
    }

private:
    static const LARGE_INTEGER& Freq() {
        static LARGE_INTEGER f = [] {
            LARGE_INTEGER q;
            QueryPerformanceFrequency(&q);
            return q;
        }();
        return f;
    }

    LARGE_INTEGER last_{};
    bool initialized_ = false;
    float max_dt_;
};

}  // namespace cameraunlock::time
