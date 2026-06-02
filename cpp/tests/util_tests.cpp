// Tests for the timing and protocol validation helpers.
//
// These cover real defects found during mod security/reliability audits:
//   1. QpcTicksToMicros - `ticks * 1000000 / freq` overflows a signed 64-bit
//      multiply once the QueryPerformanceCounter value grows large (~10 days
//      of uptime at 10 MHz), wrapping the derived timestamp negative and
//      corrupting any cache window keyed on it.
//   2. NormalizeUdpPort - a raw INI port cast straight to uint16_t silently
//      truncates out-of-range values (70000 -> 4464, a different live port)
//      instead of falling back to the default.
//   3. SanitizeFinite - std::clamp passes NaN through unchanged, so a
//      "nan"/"inf"/overflowing literal in a user-edited INI poisons every
//      downstream sin/cos/view-matrix computation.

#include <cameraunlock/time/qpc_clock.h>
#include <cameraunlock/time/frame_clock.h>
#include <cameraunlock/protocol/port_utils.h>
#include <cameraunlock/math/finite_utils.h>
#include <cameraunlock/memory/pe_fingerprint.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

void TestQpcConversion() {
    using cameraunlock::time::QpcTicksToMicros;
    std::cout << "QpcTicksToMicros:\n";

    Check(QpcTicksToMicros(10'000'000, 10'000'000) == 1'000'000,
          "one second at 10MHz == 1e6 us");

    Check(QpcTicksToMicros(5'000'000, 10'000'000) == 500'000,
          "half second at 10MHz == 5e5 us");

    Check(QpcTicksToMicros(0, 10'000'000) == 0, "zero ticks == 0 us");

    Check(QpcTicksToMicros(123, 0) == 0, "freq 0 returns 0 (no UB)");

    // The regression case: a counter large enough that ticks*1000000 overflows
    // int64. 14 days at 10 MHz = 1.2096e13 ticks; *1e6 = 1.2096e19 > INT64_MAX.
    const int64_t freq = 10'000'000;
    const int64_t fourteenDaysTicks = freq * 60ll * 60ll * 24ll * 14ll;
    const uint64_t expectedUs = 14ull * 24 * 60 * 60 * 1'000'000ull;
    Check(QpcTicksToMicros(fourteenDaysTicks, freq) == expectedUs,
          "14-day counter does not overflow (matches exact us)");

    // Monotonic across the old overflow boundary: a later tick must yield a
    // larger microsecond value (the naive version went non-monotonic here).
    const uint64_t a = QpcTicksToMicros(fourteenDaysTicks, freq);
    const uint64_t b = QpcTicksToMicros(fourteenDaysTicks + freq, freq);
    Check(b > a && (b - a) == 1'000'000, "monotonic +1s past overflow boundary");
}

void TestNormalizeUdpPort() {
    using cameraunlock::NormalizeUdpPort;
    std::cout << "NormalizeUdpPort:\n";

    bool valid = false;

    Check(NormalizeUdpPort(4242, 4242, valid) == 4242 && valid,
          "in-range default port accepted");

    Check(NormalizeUdpPort(1024, 4242, valid) == 1024 && valid,
          "lower bound 1024 accepted");
    Check(NormalizeUdpPort(65535, 4242, valid) == 65535 && valid,
          "upper bound 65535 accepted");

    // The truncation regression: 70000 must NOT become (uint16_t)70000 == 4464.
    Check(NormalizeUdpPort(70000, 4242, valid) == 4242 && !valid,
          "70000 falls back to default (not truncated to 4464)");

    Check(NormalizeUdpPort(1023, 4242, valid) == 4242 && !valid,
          "privileged/reserved 1023 falls back");
    Check(NormalizeUdpPort(0, 4242, valid) == 4242 && !valid,
          "port 0 falls back");
    Check(NormalizeUdpPort(-1, 4242, valid) == 4242 && !valid,
          "negative port falls back (not wrapped to 65535)");
}

void TestSanitizeFinite() {
    using cameraunlock::math::SanitizeFinite;
    std::cout << "SanitizeFinite:\n";

    const float kNaN = std::nanf("");
    const float kInf = std::numeric_limits<float>::infinity();

    Check(SanitizeFinite(1.5f, 1.0f, 0.1f, 5.0f) == 1.5f,
          "in-range finite value passes through");
    Check(SanitizeFinite(99.0f, 1.0f, 0.1f, 5.0f) == 5.0f,
          "above-range value clamps to max");
    Check(SanitizeFinite(-1.0f, 1.0f, 0.1f, 5.0f) == 0.1f,
          "below-range value clamps to min");

    // The regression case: clamp(NaN, lo, hi) returns NaN, so without the
    // isfinite guard a NaN escapes range validation entirely.
    Check(SanitizeFinite(kNaN, 1.0f, 0.1f, 5.0f) == 1.0f,
          "NaN falls back to default (not passed through clamp)");
    Check(SanitizeFinite(kInf, 1.0f, 0.1f, 5.0f) == 1.0f,
          "+Inf falls back to default");
    Check(SanitizeFinite(-kInf, 1.0f, 0.1f, 5.0f) == 1.0f,
          "-Inf falls back to default");

    // The fallback itself is still clamped into range, so a caller passing an
    // out-of-range default can't smuggle it past validation.
    Check(SanitizeFinite(kNaN, 99.0f, 0.1f, 5.0f) == 5.0f,
          "out-of-range fallback is clamped into range");
}

void TestPeFingerprint() {
    using cameraunlock::memory::ClassifyMismatch;
    using cameraunlock::memory::FingerprintMismatch;
    using cameraunlock::memory::PeFingerprint;
    using cameraunlock::memory::ReadPeFingerprint;
    std::cout << "PeFingerprint:\n";

    const PeFingerprint a{0x1000, 0x2000, 0x3000};
    Check(a.Matches(PeFingerprint{0x1000, 0x2000, 0x3000}),
          "identical triple matches");
    Check(!a.Matches(PeFingerprint{0x1001, 0x2000, 0x3000}),
          "different TimeDateStamp rejects");
    Check(!a.Matches(PeFingerprint{0x1000, 0x2001, 0x3000}),
          "different SizeOfImage rejects");
    Check(!a.Matches(PeFingerprint{0x1000, 0x2000, 0x3001}),
          "different CheckSum rejects");

    Check(ClassifyMismatch({0x2000, 0, 0}, {0x1000, 0, 0}) == FingerprintMismatch::Newer,
          "larger running timestamp classifies Newer");
    Check(ClassifyMismatch({0x0500, 0, 0}, {0x1000, 0, 0}) == FingerprintMismatch::Older,
          "smaller running timestamp classifies Older");
    Check(ClassifyMismatch({0x1000, 1, 2}, {0x1000, 3, 4}) == FingerprintMismatch::Differs,
          "same timestamp, different size/checksum classifies Differs");

    // The test executable itself is a mapped PE image with valid headers.
#ifdef _WIN32
    PeFingerprint self{};
    Check(ReadPeFingerprint(GetModuleHandleW(nullptr), self),
          "reads own module's PE header");
    Check(self.SizeOfImage > 0, "own SizeOfImage is non-zero");

    PeFingerprint dummy{};
    Check(!ReadPeFingerprint(nullptr, dummy), "null module returns false");

    // Not a PE header: SEH guard / magic check must reject, not fault.
    static const std::uint8_t notPe[64] = {};
    Check(!ReadPeFingerprint(const_cast<std::uint8_t*>(notPe), dummy),
          "non-PE memory rejected without faulting");
#endif
}

void TestFrameClock() {
    using cameraunlock::time::FrameClock;
    std::cout << "FrameClock:\n";

    FrameClock clock;
    const float first = clock.Tick();
    Check(first >= 0.0f && first <= 0.1f, "first tick is near-zero and clamped");

    // Successive ticks are non-negative and clamped to the max delta even if
    // the thread stalls between them.
    FrameClock clamped(0.05f);
    clamped.Tick();
    Sleep(120);
    const float dt = clamped.Tick();
    Check(dt == 0.05f, "stall longer than maxDt clamps to maxDt");
}

}  // namespace

int RunUtilTests() {
    std::cout << "Timing / protocol validation tests\n";

    TestQpcConversion();
    TestNormalizeUdpPort();
    TestSanitizeFinite();
    TestPeFingerprint();
    TestFrameClock();

    if (g_failures == 0) {
        std::cout << "Util tests: all passed\n";
    } else {
        std::cout << "Util tests: " << g_failures << " failure(s)\n";
    }
    return g_failures;
}
