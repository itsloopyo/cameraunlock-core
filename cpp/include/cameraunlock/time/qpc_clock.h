#pragma once

#include <cstdint>

#include "cameraunlock/os/win32.h"

namespace cameraunlock::time {

// Convert QueryPerformanceCounter ticks to microseconds without the signed
// int64 overflow that `ticks * 1000000 / freq` suffers once the counter grows
// large. A 10 MHz QPC overflows a 64-bit multiply after ~10 days of uptime
// (and QPC keeps counting across sleep/hibernate), at which point the product
// wraps negative and derived timestamps become garbage. Splitting into a
// whole-second part plus a sub-second remainder keeps every intermediate
// within int64 range for any realistic QPC frequency.
inline uint64_t QpcTicksToMicros(int64_t ticks, int64_t freq) {
    if (freq <= 0) return 0;
    const int64_t secs = ticks / freq;
    const int64_t rem = ticks % freq;
    return static_cast<uint64_t>(secs) * 1000000ULL +
           static_cast<uint64_t>((rem * 1000000) / freq);
}

#if CAMERAUNLOCK_WIN32
// Current QueryPerformanceCounter value in microseconds, using the
// overflow-safe split conversion above.
inline uint64_t QpcNowMicros() {
    static LARGE_INTEGER freq = {};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return QpcTicksToMicros(now.QuadPart, freq.QuadPart);
}
#endif

} // namespace cameraunlock::time
