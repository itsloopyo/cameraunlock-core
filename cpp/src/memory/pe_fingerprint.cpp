#include <cameraunlock/memory/pe_fingerprint.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace cameraunlock::memory {

FingerprintMismatch ClassifyMismatch(const PeFingerprint& running,
                                     const PeFingerprint& reference) {
    if (running.TimeDateStamp > reference.TimeDateStamp) {
        return FingerprintMismatch::Newer;
    }
    if (running.TimeDateStamp < reference.TimeDateStamp) {
        return FingerprintMismatch::Older;
    }
    return FingerprintMismatch::Differs;
}

bool ReadPeFingerprint(void* moduleBase, PeFingerprint& out) {
    if (!moduleBase) return false;
    const auto base = reinterpret_cast<const std::uint8_t*>(moduleBase);
    __try {
        if (*reinterpret_cast<const std::uint16_t*>(base) != 0x5A4Du /* "MZ" */) {
            return false;
        }
        const auto e_lfanew = *reinterpret_cast<const std::uint32_t*>(base + 0x3c);
        const std::uint8_t* nt = base + e_lfanew;
        if (*reinterpret_cast<const std::uint32_t*>(nt) != 0x00004550u /* "PE\0\0" */) {
            return false;
        }
        // COFF FileHeader at nt+4 (TimeDateStamp at +4 within).
        // PE32+ optional header at nt+0x18 (SizeOfImage @+0x38, CheckSum @+0x40).
        out.TimeDateStamp = *reinterpret_cast<const std::uint32_t*>(nt + 4 + 4);
        const std::uint8_t* opt = nt + 4 + 20;
        out.SizeOfImage   = *reinterpret_cast<const std::uint32_t*>(opt + 0x38);
        out.CheckSum      = *reinterpret_cast<const std::uint32_t*>(opt + 0x40);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace cameraunlock::memory
