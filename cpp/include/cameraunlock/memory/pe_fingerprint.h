#pragma once

#include <cstdint>

namespace cameraunlock::memory {

// PE-header fingerprint of a loaded module. TimeDateStamp + SizeOfImage +
// CheckSum together are unique per built EXE: a relink that produced an
// identical triple never happens in practice, and three independent fields
// means a tampered/repacked EXE fails the match instead of silently
// mis-routing. This is the routing key for per-build offset profiles
// (append-only build registries): RVAs are pinned to a fingerprint, the mod
// fingerprints the running EXE at load time, and no match leaves the mod
// dormant rather than hooking against stale RVAs.
struct PeFingerprint {
    std::uint32_t TimeDateStamp;
    std::uint32_t SizeOfImage;
    std::uint32_t CheckSum;

    bool Matches(const PeFingerprint& other) const {
        return TimeDateStamp == other.TimeDateStamp
            && SizeOfImage == other.SizeOfImage
            && CheckSum == other.CheckSum;
    }
};

// How a running EXE's fingerprint differs from a reference profile, used to
// word the "mod is staying dormant" log line. Note that some engines emit a
// deterministic-build hash into TimeDateStamp rather than a real timestamp,
// in which case Newer/Older cannot be told apart and should get the same
// user guidance.
enum class FingerprintMismatch {
    Newer,    // Running TimeDateStamp > reference.
    Older,    // Running TimeDateStamp < reference.
    Differs,  // Same TimeDateStamp, different size or checksum (tampered/repacked).
};

FingerprintMismatch ClassifyMismatch(const PeFingerprint& running,
                                     const PeFingerprint& reference);

// Read the fingerprint from a loaded module's PE headers. SEH-guarded:
// returns false on a malformed/unmapped header instead of faulting.
// moduleBase is the HMODULE / module base address.
bool ReadPeFingerprint(void* moduleBase, PeFingerprint& out);

}  // namespace cameraunlock::memory
