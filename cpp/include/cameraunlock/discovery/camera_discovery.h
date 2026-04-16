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
    Calibrating,
    Complete,
    Failed,
};

// Rotation pulse the mod should inject during calibration
struct CalibrationPulse {
    float yaw;
    float pitch;
    float roll;
    bool active;
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
    float calibration_deg  = 5.0f;             // rotation to inject per axis
    int pulse_frames       = 15;               // frames to hold each pulse
    int settle_frames      = 10;               // frames to wait between pulses
    int instance_size      = 512;              // bytes to analyze/snapshot
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

    // Calibration interface
    CalibrationPulse GetCalibrationPulse() const;
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

    // Calibration injection — probe detours apply this after calling original
    static std::atomic<bool> s_calibActive;
    static float s_calibDeltas[3];          // yaw, pitch, roll to inject
    static size_t s_calibAngleOffsets[3];   // byte offsets for the 3 floats in the angle group
    static bool s_calibOffsetsSet;
    static std::atomic<int> s_calibInjectedThisFrame;  // guard: only inject once per frame

private:
    void Log(const char* fmt, ...);
    Phase RunFindVtables();
    Phase RunProbing();
    Phase RunAnalyzeLayout();
    Phase RunCalibrating();

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

    // Instance
    std::atomic<uintptr_t> m_instance{0};

    // Layout analysis
    LayoutReport m_layout{};

    // Calibration state
    enum class CalibAxis { Yaw, Pitch, Roll, Done };
    CalibAxis m_calibAxis = CalibAxis::Yaw;
    int m_calibFrame = 0;
    bool m_calibPulsing = false;
    std::vector<float> m_preSnapshot;
    std::vector<float> m_postSnapshot;
    std::vector<size_t> m_candidateAngleOffsets;

    CameraOffsets m_offsets{};
};

// Template probe detours — each slot gets a unique function address.
// During calibration, the winning slot also injects rotation pulses.
template<int Slot>
static uintptr_t __fastcall ProbeDetour(void* thisPtr, void* a2, void* a3, void* a4) {
    CameraDiscovery::s_callCounts[Slot].fetch_add(1, std::memory_order_relaxed);
    CameraDiscovery::s_lastThis[Slot].store(reinterpret_cast<uintptr_t>(thisPtr), std::memory_order_relaxed);
    uintptr_t ret = CameraDiscovery::s_originals[Slot](thisPtr, a2, a3, a4);

    // Calibration injection — apply rotation pulse after game processes (once per frame)
    if (CameraDiscovery::s_calibActive.load(std::memory_order_relaxed) &&
        CameraDiscovery::s_calibOffsetsSet) {
        int expected = 0;
        if (CameraDiscovery::s_calibInjectedThisFrame.compare_exchange_strong(
                expected, 1, std::memory_order_relaxed)) {
            uintptr_t inst = reinterpret_cast<uintptr_t>(thisPtr);
            for (int i = 0; i < 3; i++) {
                *reinterpret_cast<float*>(inst + CameraDiscovery::s_calibAngleOffsets[i]) +=
                    CameraDiscovery::s_calibDeltas[i];
            }
        }
    }

    return ret;
}

} // namespace cameraunlock::discovery
