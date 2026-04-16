#include <cameraunlock/discovery/camera_discovery.h>
#include <cameraunlock/hooks/hook_manager.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace cameraunlock::discovery {

// Static storage for probe detours
uintptr_t(__fastcall* CameraDiscovery::s_originals[kMaxProbeSlots])(void*, void*, void*, void*) = {};
std::atomic<int> CameraDiscovery::s_callCounts[kMaxProbeSlots] = {};
std::atomic<uintptr_t> CameraDiscovery::s_lastThis[kMaxProbeSlots] = {};
CameraDiscovery* CameraDiscovery::s_instance = nullptr;
std::atomic<bool> CameraDiscovery::s_calibActive{false};
float CameraDiscovery::s_calibDeltas[3] = {};
size_t CameraDiscovery::s_calibAngleOffsets[3] = {};
bool CameraDiscovery::s_calibOffsetsSet = false;
std::atomic<int> CameraDiscovery::s_calibInjectedThisFrame{0};

// Probe detour instantiations — need unique function addresses for MinHook
// We generate 32 slots (4 candidates × 8 vfuncs)
using ProbeFn = uintptr_t(__fastcall*)(void*, void*, void*, void*);

template<int N> struct ProbeTable {
    static void Fill(ProbeFn* table) {
        table[N-1] = &ProbeDetour<N-1>;
        ProbeTable<N-1>::Fill(table);
    }
};
template<> struct ProbeTable<0> {
    static void Fill(ProbeFn*) {}
};

static ProbeFn s_probeDetours[kMaxProbeSlots];
static bool s_probeTableInit = false;

static void EnsureProbeTable() {
    if (!s_probeTableInit) {
        ProbeTable<kMaxProbeSlots>::Fill(s_probeDetours);
        s_probeTableInit = true;
    }
}

CameraDiscovery::CameraDiscovery() {
    s_instance = this;
    EnsureProbeTable();
}

CameraDiscovery::~CameraDiscovery() {
    Cleanup();
    if (s_instance == this) s_instance = nullptr;
}

void CameraDiscovery::Log(const char* fmt, ...) {
    if (!m_log) return;
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    m_log(buf);
}

void CameraDiscovery::Start(const DiscoveryConfig& config) {
    m_config = config;
    m_phase = Phase::FindingVtables;
    m_probeFrameCount = 0;
    m_activeSlot = -1;
    m_activeTarget = nullptr;
    m_instance.store(0);
    m_offsets = {};
    m_layout = {};
    m_calibAxis = CalibAxis::Yaw;
    m_calibFrame = 0;
    m_calibPulsing = false;
    m_candidates.clear();

    for (int i = 0; i < kMaxProbeSlots; i++) {
        s_callCounts[i].store(0);
        s_lastThis[i].store(0);
        s_originals[i] = nullptr;
    }

    Log("DISC: Starting discovery with %d candidate names", (int)config.candidate_names.size());
}

Phase CameraDiscovery::Advance() {
    // Reset per-frame calibration guard
    s_calibInjectedThisFrame.store(0, std::memory_order_relaxed);

    switch (m_phase) {
        case Phase::FindingVtables: m_phase = RunFindVtables(); break;
        case Phase::Probing:        m_phase = RunProbing(); break;
        case Phase::AnalyzingLayout: m_phase = RunAnalyzeLayout(); break;
        // Calibration skipped — heuristic axis assignment in AnalyzeLayout
        default: break;
    }
    return m_phase;
}

void CameraDiscovery::ReportVfuncCall(int slot, void* this_ptr) {
    if (slot < 0 || slot >= kMaxProbeSlots) return;
    s_callCounts[slot].fetch_add(1, std::memory_order_relaxed);
    s_lastThis[slot].store(reinterpret_cast<uintptr_t>(this_ptr), std::memory_order_relaxed);
}

CalibrationPulse CameraDiscovery::GetCalibrationPulse() const {
    CalibrationPulse p{};
    if (m_phase != Phase::Calibrating || !m_calibPulsing) return p;
    p.active = true;
    switch (m_calibAxis) {
        case CalibAxis::Yaw:   p.yaw = m_config.calibration_deg; break;
        case CalibAxis::Pitch: p.pitch = m_config.calibration_deg; break;
        case CalibAxis::Roll:  p.roll = m_config.calibration_deg; break;
        default: p.active = false; break;
    }
    return p;
}

void CameraDiscovery::SetInstancePointer(void* ptr) {
    m_instance.store(reinterpret_cast<uintptr_t>(ptr));
}

void CameraDiscovery::Cleanup() {
    RemoveProbeHooks();
    m_candidates.clear();
}

// ============================================================================
// Phase 1: Find vtables via RTTI
// ============================================================================

Phase CameraDiscovery::RunFindVtables() {
    for (auto& name : m_config.candidate_names) {
        if (m_candidates.size() >= kMaxCandidates) break;

        memory::VtableInfo vt{};
        if (memory::FindVtableFromRTTI(m_config.module, name, vt, kMaxVfuncsPerCandidate)) {
            Log("DISC: Found %s vtable at 0x%llX (%d vfuncs)", name.c_str(), vt.vtable_address, vt.vfunc_count);
            m_candidates.push_back({name, vt});
        } else {
            Log("DISC: %s not found via RTTI", name.c_str());
        }
    }

    if (m_candidates.empty()) {
        Log("DISC: No camera classes found — failed");
        return Phase::Failed;
    }

    InstallProbeHooks();
    return Phase::Probing;
}

// ============================================================================
// Phase 2: Probe vfuncs to find gameplay-active one
// ============================================================================

Phase CameraDiscovery::RunProbing() {
    m_probeFrameCount++;

    if (m_probeFrameCount < m_config.probe_frames) return Phase::Probing;

    // Log all results
    for (int c = 0; c < (int)m_candidates.size(); c++) {
        for (int v = 0; v < m_candidates[c].vtable.vfunc_count; v++) {
            int slot = c * kMaxVfuncsPerCandidate + v;
            int count = s_callCounts[slot].load();
            if (count > 0) {
                Log("DISC: %s::vfunc[%d] called %d times (this=%p)",
                    m_candidates[c].name.c_str(), v, count,
                    reinterpret_cast<void*>(s_lastThis[slot].load()));
            }
        }
    }

    // Selection: prefer a vfunc called at roughly per-frame rate (~60Hz).
    // A camera update runs 1-2x per frame. Utility functions run 100s of times.
    // Target: ~1-10 calls per frame = probe_frames to probe_frames*10 total calls.
    // Prefer the first candidate (most specific class) with a match.
    int targetCallsLow = m_config.probe_frames;       // ~1x per frame
    int targetCallsHigh = m_config.probe_frames * 10;  // ~10x per frame

    int bestSlot = -1;
    int bestCount = 0;
    int bestDistance = INT_MAX;  // distance from ideal range

    for (int c = 0; c < (int)m_candidates.size(); c++) {
        int candidateBestSlot = -1;
        int candidateBestDist = INT_MAX;
        int candidateBestCount = 0;

        for (int v = 0; v < m_candidates[c].vtable.vfunc_count; v++) {
            int slot = c * kMaxVfuncsPerCandidate + v;
            int count = s_callCounts[slot].load();
            if (count == 0) continue;

            // Distance from ideal per-frame range
            int dist = 0;
            if (count < targetCallsLow) dist = targetCallsLow - count;
            else if (count > targetCallsHigh) dist = count - targetCallsHigh;
            // else in range, dist = 0

            if (dist < candidateBestDist || (dist == candidateBestDist && count > candidateBestCount)) {
                candidateBestDist = dist;
                candidateBestSlot = slot;
                candidateBestCount = count;
            }
        }

        // If this candidate has any calls in/near the per-frame range, use it
        if (candidateBestSlot >= 0 && candidateBestCount >= 50) {
            bestSlot = candidateBestSlot;
            bestCount = candidateBestCount;
            bestDistance = candidateBestDist;
            break;  // prefer first candidate (most specific class)
        }
        // Fallback: track best overall
        if (candidateBestSlot >= 0 && candidateBestDist < bestDistance) {
            bestDistance = candidateBestDist;
            bestSlot = candidateBestSlot;
            bestCount = candidateBestCount;
        }
    }

    if (bestSlot < 0 || bestCount == 0) {
        Log("DISC: No vfuncs called during probe period — failed");
        RemoveProbeHooks();
        return Phase::Failed;
    }

    int ci = bestSlot / kMaxVfuncsPerCandidate;
    int vi = bestSlot % kMaxVfuncsPerCandidate;
    Log("DISC: Winner: %s::vfunc[%d] (%d calls)",
        m_candidates[ci].name.c_str(), vi, bestCount);

    m_activeSlot = bestSlot;
    m_activeTarget = reinterpret_cast<void*>(m_candidates[ci].vtable.vfuncs[vi]);
    m_instance.store(s_lastThis[bestSlot].load());

    // Remove all probe hooks except the winner
    // (we keep the winner hooked so we continue getting this-pointers)
    auto& hm = hooks::HookManager::Instance();
    for (int c = 0; c < (int)m_candidates.size(); c++) {
        for (int v = 0; v < m_candidates[c].vtable.vfunc_count; v++) {
            int slot = c * kMaxVfuncsPerCandidate + v;
            if (slot == bestSlot) continue;
            void* target = reinterpret_cast<void*>(m_candidates[c].vtable.vfuncs[v]);
            hm.DisableHook(target);
            hm.RemoveHook(target);
        }
    }

    return Phase::AnalyzingLayout;
}

// ============================================================================
// Phase 3: Analyze instance memory layout
// ============================================================================

Phase CameraDiscovery::RunAnalyzeLayout() {
    uintptr_t inst = m_instance.load();
    if (inst == 0) {
        Log("DISC: No instance pointer — failed");
        return Phase::Failed;
    }

    Log("DISC: Analyzing instance at %p (%d bytes)...",
        reinterpret_cast<void*>(inst), m_config.instance_size);

    // Read instance memory (skip vtable pointer at +0x00)
    int skipBytes = 8;  // skip vtable ptr
    int analyzeSize = m_config.instance_size - skipBytes;
    if (analyzeSize <= 0) analyzeSize = 256;

    m_layout = ClassifyMemoryRegion(
        reinterpret_cast<const void*>(inst + skipBytes), analyzeSize);

    // Adjust offsets to be relative to instance base (not the skip-adjusted pointer)
    for (int i = 0; i < m_layout.group_count; i++) {
        m_layout.groups[i].offset += skipBytes;
    }

    // Log findings
    for (int i = 0; i < m_layout.group_count; i++) {
        auto& g = m_layout.groups[i];
        const char* typeName = "?";
        switch (g.type) {
            case FloatClass::Position:   typeName = "Position"; break;
            case FloatClass::Angle:      typeName = "Angle"; break;
            case FloatClass::FOV:        typeName = "FOV"; break;
            case FloatClass::Quaternion: typeName = "Quaternion"; break;
            default: break;
        }
        if (g.count == 1) {
            Log("DISC: +0x%03X: %s = %.2f", (int)g.offset, typeName, g.values[0]);
        } else {
            Log("DISC: +0x%03X: %s = (%.2f, %.2f, %.2f%s)", (int)g.offset, typeName,
                g.values[0], g.values[1], g.values[2],
                g.count == 4 ? ", ..." : "");
        }
    }

    // Identify yaw/pitch/roll by value heuristics instead of calibration.
    // Yaw = compass heading (largest absolute value, typically >45°)
    // Pitch = look up/down (moderate value, typically 1-45°)
    // Roll = tilt (near zero in normal gameplay)
    //
    // Collect ALL angle-like floats from the layout.
    m_candidateAngleOffsets.clear();
    for (int i = 0; i < m_layout.group_count; i++) {
        if (m_layout.groups[i].type == FloatClass::Angle) {
            size_t base = m_layout.groups[i].offset;
            for (int j = 0; j < m_layout.groups[i].count; j++) {
                m_candidateAngleOffsets.push_back(base + j * sizeof(float));
            }
        }
    }

    if (m_candidateAngleOffsets.empty()) {
        Log("DISC: No angle candidates found — failed");
        return Phase::Failed;
    }

    // Read all candidate float values
    struct AngleCandidate { size_t offset; float value; float absValue; };
    std::vector<AngleCandidate> candidates;
    for (size_t off : m_candidateAngleOffsets) {
        float val = *reinterpret_cast<float*>(inst + off);
        candidates.push_back({off, val, std::fabsf(val)});
    }

    // Find yaw: the angle-like float with the largest absolute value (compass heading).
    // Then assume roll/pitch/yaw are consecutive floats (standard layout in game engines).
    std::sort(candidates.begin(), candidates.end(),
              [](const AngleCandidate& a, const AngleCandidate& b) { return a.absValue > b.absValue; });

    size_t yawOff = 0;
    bool foundYaw = false;
    for (auto& c : candidates) {
        if (c.absValue > 45.0f && c.absValue < 360.0f) {
            yawOff = c.offset;
            foundYaw = true;
            break;
        }
    }

    if (!foundYaw) {
        // Fallback: just take the largest
        if (!candidates.empty()) {
            yawOff = candidates[0].offset;
            foundYaw = true;
        }
    }

    if (!foundYaw) {
        Log("DISC: Could not identify yaw — failed");
        return Phase::Failed;
    }

    // Camera Euler angles are stored consecutively: roll, pitch, yaw
    // (or some permutation). Yaw is at yawOff. Pitch is at yawOff-4, roll at yawOff-8.
    size_t pitchOff = yawOff - sizeof(float);
    size_t rollOff  = yawOff - 2 * sizeof(float);

    // Read the values for logging
    float yawVal = *reinterpret_cast<float*>(inst + yawOff);
    float pitchVal = *reinterpret_cast<float*>(inst + pitchOff);
    float rollVal = *reinterpret_cast<float*>(inst + rollOff);

    Log("DISC: Found consecutive angles: roll=+0x%X(%.1f) pitch=+0x%X(%.1f) yaw=+0x%X(%.1f)",
        (int)rollOff, rollVal, (int)pitchOff, pitchVal, (int)yawOff, yawVal);

    m_offsets.yaw_offset = yawOff;
    m_offsets.pitch_offset = pitchOff;
    m_offsets.roll_offset = rollOff;
    // Signs from manual testing: yaw inverted, pitch normal, roll normal
    m_offsets.yaw_sign = -1.0f;
    m_offsets.pitch_sign = 1.0f;
    m_offsets.roll_sign = -1.0f;
    m_offsets.valid = true;

    return Phase::Complete;
}

// ============================================================================
// Phase 4: Calibrate — inject known rotation, detect which floats change
// ============================================================================

static void SnapshotAngles(uintptr_t inst, const std::vector<size_t>& offsets, std::vector<float>& out) {
    out.resize(offsets.size());
    __try {
        for (size_t i = 0; i < offsets.size(); i++) {
            out[i] = *reinterpret_cast<float*>(inst + offsets[i]);
        }
    }
    __except (1) {}
}

Phase CameraDiscovery::RunCalibrating() {
    uintptr_t inst = m_instance.load();
    if (inst == 0) return Phase::Failed;

    if (m_calibAxis == CalibAxis::Done) {
        s_calibActive.store(false);
        m_offsets.valid = (m_offsets.yaw_offset != 0 || m_offsets.pitch_offset != 0);
        if (m_offsets.valid) {
            Log("DISC: Calibration complete — yaw=+0x%X(%+.0f) pitch=+0x%X(%+.0f) roll=+0x%X(%+.0f)",
                (int)m_offsets.yaw_offset, m_offsets.yaw_sign,
                (int)m_offsets.pitch_offset, m_offsets.pitch_sign,
                (int)m_offsets.roll_offset, m_offsets.roll_sign);
            return Phase::Complete;
        }
        Log("DISC: Calibration found no matching offsets — failed");
        return Phase::Failed;
    }

    // Set up calibration angle offsets for the probe detour (once)
    if (!s_calibOffsetsSet && !m_candidateAngleOffsets.empty()) {
        // Find the first angle group and use its 3 offsets
        for (int i = 0; i < m_layout.group_count; i++) {
            if (m_layout.groups[i].type == FloatClass::Angle && m_layout.groups[i].count >= 3) {
                size_t base = m_layout.groups[i].offset;
                s_calibAngleOffsets[0] = base;
                s_calibAngleOffsets[1] = base + 4;
                s_calibAngleOffsets[2] = base + 8;
                s_calibOffsetsSet = true;
                Log("DISC: Calibration target: angle group at +0x%X", (int)base);
                break;
            }
        }
        if (!s_calibOffsetsSet) {
            Log("DISC: No angle group with 3+ floats found");
            return Phase::Failed;
        }
    }

    m_calibFrame++;

    int settleEnd = m_config.settle_frames;
    int pulseEnd = settleEnd + m_config.pulse_frames;

    if (m_calibFrame == settleEnd) {
        // Take pre-snapshot, then start pulse via probe detour
        SnapshotAngles(inst, m_candidateAngleOffsets, m_preSnapshot);

        // Set up the pulse for the probe detour to apply on ONE axis
        s_calibDeltas[0] = s_calibDeltas[1] = s_calibDeltas[2] = 0;
        switch (m_calibAxis) {
            case CalibAxis::Yaw:   s_calibDeltas[2] = m_config.calibration_deg; break;
            case CalibAxis::Pitch: s_calibDeltas[1] = m_config.calibration_deg; break;
            case CalibAxis::Roll:  s_calibDeltas[0] = m_config.calibration_deg; break;
            default: break;
        }
        s_calibActive.store(true);
    }
    else if (m_calibFrame == pulseEnd) {
        // Stop pulse, take post-snapshot
        s_calibActive.store(false);
        SnapshotAngles(inst, m_candidateAngleOffsets, m_postSnapshot);

        // Find the offset with the LARGEST delta — that's the axis we pulsed.
        // Only consider offsets within the calibration target angle group
        // (3 consecutive floats). The pulsed axis should have a much larger
        // delta than the other two.
        float minDelta = 1.0f;  // ignore tiny changes (noise, mouse movement)
        size_t bestOffset = 0;
        float bestSign = 1.0f;
        float bestAbsDelta = 0;
        bool found = false;

        for (size_t i = 0; i < 3; i++) {
            size_t off = s_calibAngleOffsets[i];
            // Find this offset in the candidate list to get its index
            for (size_t j = 0; j < m_candidateAngleOffsets.size(); j++) {
                if (m_candidateAngleOffsets[j] != off) continue;
                float delta = m_postSnapshot[j] - m_preSnapshot[j];
                float absDelta = std::fabsf(delta);
                if (absDelta > minDelta && absDelta > bestAbsDelta) {
                    bestAbsDelta = absDelta;
                    bestOffset = off;
                    bestSign = (delta > 0) ? 1.0f : -1.0f;
                    found = true;
                }
                break;
            }
        }

        const char* axisName = m_calibAxis == CalibAxis::Yaw ? "Yaw" :
                               m_calibAxis == CalibAxis::Pitch ? "Pitch" : "Roll";
        if (found) {
            Log("DISC: %s axis: offset=+0x%X, delta=%.2f, sign=%+.0f", axisName,
                (int)bestOffset, bestAbsDelta * bestSign, bestSign);
        } else {
            Log("DISC: %s axis: no significant delta found", axisName);
        }

        switch (m_calibAxis) {
            case CalibAxis::Yaw:
                if (found) { m_offsets.yaw_offset = bestOffset; m_offsets.yaw_sign = bestSign; }
                m_calibAxis = CalibAxis::Pitch;
                break;
            case CalibAxis::Pitch:
                if (found) { m_offsets.pitch_offset = bestOffset; m_offsets.pitch_sign = bestSign; }
                m_calibAxis = CalibAxis::Roll;
                break;
            case CalibAxis::Roll:
                if (found) { m_offsets.roll_offset = bestOffset; m_offsets.roll_sign = bestSign; }
                m_calibAxis = CalibAxis::Done;
                break;
            default: break;
        }

        m_calibFrame = 0;
    }

    return Phase::Calibrating;
}

// ============================================================================
// Probe hook management
// ============================================================================

void CameraDiscovery::InstallProbeHooks() {
    auto& hm = hooks::HookManager::Instance();

    for (int c = 0; c < (int)m_candidates.size(); c++) {
        for (int v = 0; v < m_candidates[c].vtable.vfunc_count; v++) {
            int slot = c * kMaxVfuncsPerCandidate + v;
            void* target = reinterpret_cast<void*>(m_candidates[c].vtable.vfuncs[v]);

            auto st = hm.CreateHook(target, reinterpret_cast<void*>(s_probeDetours[slot]),
                                     reinterpret_cast<void**>(&s_originals[slot]));
            if (st != hooks::HookStatus::Ok) {
                Log("DISC: Failed to hook %s::vfunc[%d]: %s",
                    m_candidates[c].name.c_str(), v, hooks::HookStatusToString(st));
                continue;
            }

            st = hm.EnableHook(target);
            if (st != hooks::HookStatus::Ok) {
                Log("DISC: Failed to enable %s::vfunc[%d]: %s",
                    m_candidates[c].name.c_str(), v, hooks::HookStatusToString(st));
                continue;
            }

            Log("DISC: Probing %s::vfunc[%d] at +0x%llX (slot %d)",
                m_candidates[c].name.c_str(), v,
                m_candidates[c].vtable.vfuncs[v] - reinterpret_cast<uintptr_t>(m_config.module), slot);
        }
    }
}

void CameraDiscovery::RemoveProbeHooks() {
    auto& hm = hooks::HookManager::Instance();

    for (int c = 0; c < (int)m_candidates.size(); c++) {
        for (int v = 0; v < m_candidates[c].vtable.vfunc_count; v++) {
            void* target = reinterpret_cast<void*>(m_candidates[c].vtable.vfuncs[v]);
            hm.DisableHook(target);
            hm.RemoveHook(target);
        }
    }
}

} // namespace cameraunlock::discovery
