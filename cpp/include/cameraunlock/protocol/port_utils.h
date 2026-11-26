#pragma once

#include <cstdint>

namespace cameraunlock {

// Validate a raw INI-parsed UDP port. OpenTrack uses the 1024-65535 range;
// values outside it (including the negative and >65535 values an atoi-style
// parse yields, which silently truncate to a wrong 16-bit port when cast
// straight to uint16_t - e.g. 70000 -> 4464) fall back to the supplied
// default. `valid` reports whether the raw value was in range.
inline uint16_t NormalizeUdpPort(int raw, uint16_t fallback, bool& valid) {
    valid = (raw >= 1024 && raw <= 65535);
    return valid ? static_cast<uint16_t>(raw) : fallback;
}

} // namespace cameraunlock
