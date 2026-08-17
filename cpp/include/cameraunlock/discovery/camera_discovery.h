#pragma once

#include <cameraunlock/memory/rtti_vtable.h>
#include <cameraunlock/discovery/float_classifier.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace cameraunlock::discovery {

enum class Phase {
    Idle,
    FindingVtables,
    Probing,
    AnalyzingLayout,
    Complete,
    Failed,
};

// Discovered camera rotation offsets
struct CameraOffsets {
    size_t yaw_offset;       // byte offset from instance base
    size_t pitch_offset;
    size_t roll_offset;
    float yaw_sign;          // +1 or -1
    float pitch_sign;
    float roll_sign;
    bool valid;
};

struct DiscoveryConfig {
    void* module;                              // game module handle
    std::vector<std::string> candidate_names;  // RTTI class names to try
    int probe_frames       = 180;              // frames to probe vfuncs (~3s at 60fps)
    int instance_size      = 512;              // bytes to analyze

    // Pin the per-frame camera update to a known vfunc index on the first
    // (most-specific) candidate instead of letting the call-count heuristic
    // choose. Leave at -1, the default, for auto-discovery.
    //
    // The heuristic ranks slots by how close their call count sits to a
    // per-frame rate, which mis-picks when the real camera update has not
    // begun firing inside the probe window (a probe that lands on a load-in,
    // say). It then latches silently onto a high-frequency getter and head
    // tracking "never starts" on that launch. When the correct index is known
    // from reversing, forcing it removes that non-determinism.
    //
    // Probing still runs, so the slot's instance pointer is captured the same
    // way, and selection still waits until the slot is being called at a
    // per-frame RATE (see kForcedSlotMinCallsPerWindow in probe_selection.h)
    // before committing. That wait is not bounded by probe_frames: a forced
    // slot that never fires keeps the phase in Probing rather than picking
    // something else, and logs a periodic line saying so.
    //
    // An index that does not exist on the class actually found is reported as
    // an error once and then ignored, leaving auto-discovery to run. It is not
    // a discovery failure; see probe_selection.h for why that distinction
    // matters to consumers.
    int forced_vfunc_index = -1;               // -1 = auto-discover
};

// Rolling window backing the forced-slot rate gate. Lives here rather than in
// probe_selection.h so CameraDiscovery can hold one by value; the thresholds
// that give it meaning are in probe_selection.h alongside the algorithm.
struct ForcedSlotWindow {
    int start_frame = 0;   // probe frame the current window opened on
    int start_count = 0;   // the forced slot's call count when it opened
};

using LogFn = void(*)(const char* msg);

// Maximum probe slots: 4 candidate classes × 8 vfuncs = 32
constexpr int kMaxCandidates = 4;
constexpr int kMaxVfuncsPerCandidate = 8;
constexpr int kMaxProbeSlots = kMaxCandidates * kMaxVfuncsPerCandidate;

class CameraDiscovery {
public:
    CameraDiscovery();
    ~CameraDiscovery();

    void Start(const DiscoveryConfig& config);
    Phase Advance();
    Phase GetPhase() const { return m_phase; }

    // Vfunc probe callback — each probe detour calls this
    void ReportVfuncCall(int slot, void* this_ptr);

    void SetInstancePointer(void* ptr);

    // Results (valid when phase == Complete)
    const CameraOffsets& GetOffsets() const { return m_offsets; }
    const LayoutReport& GetLayout() const { return m_layout; }
    void* GetActiveVfuncTarget() const { return m_activeTarget; }
    void* GetInstancePointer() const { return reinterpret_cast<void*>(m_instance.load()); }

    void SetLogCallback(LogFn fn) { m_log = fn; }
    void Cleanup();

    // Probe detour originals — public so template detours can access them
    static uintptr_t(__fastcall* s_originals[kMaxProbeSlots])(void*, void*, void*, void*);
    static std::atomic<int> s_callCounts[kMaxProbeSlots];
    static std::atomic<uintptr_t> s_lastThis[kMaxProbeSlots];
    static CameraDiscovery* s_instance;

private:
    void Log(const char* fmt, ...);
    Phase RunFindVtables();
    Phase RunProbing();
    Phase RunAnalyzeLayout();

    void InstallProbeHooks();
    void RemoveProbeHooks();

    DiscoveryConfig m_config;
    Phase m_phase = Phase::Idle;
    LogFn m_log = nullptr;

    // Vtable discovery results
    struct CandidateInfo {
        std::string name;
        memory::VtableInfo vtable;
    };
    std::vector<CandidateInfo> m_candidates;

    // Probing state
    int m_probeFrameCount = 0;
    int m_activeSlot = -1;
    void* m_activeTarget = nullptr;
    ForcedSlotWindow m_forcedWindow{};

    // Instance
    std::atomic<uintptr_t> m_instance{0};

    // Layout analysis
    LayoutReport m_layout{};
    std::vector<size_t> m_candidateAngleOffsets;

    CameraOffsets m_offsets{};
};

// Template probe detours — each slot gets a unique function address.
template<int Slot>
static uintptr_t __fastcall ProbeDetour(void* thisPtr, void* a2, void* a3, void* a4) {
    CameraDiscovery::s_callCounts[Slot].fetch_add(1, std::memory_order_relaxed);
    CameraDiscovery::s_lastThis[Slot].store(reinterpret_cast<uintptr_t>(thisPtr), std::memory_order_relaxed);
    return CameraDiscovery::s_originals[Slot](thisPtr, a2, a3, a4);
}

} // namespace cameraunlock::discovery
