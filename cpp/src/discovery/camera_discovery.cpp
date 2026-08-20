#include <cameraunlock/discovery/camera_discovery.h>
#include <cameraunlock/discovery/probe_selection.h>
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

namespace {

// m_instance is a this-pointer a probe detour captured, possibly many frames
// before the analysis runs. A level transition or a cutscene camera swap in
// between frees the object, so every read of it carries its own SEH frame
// (which is also why these are free functions - MSVC rejects __try in a
// function that needs C++ object unwinding).
bool SafeClassify(uintptr_t addr, size_t size, LayoutReport& out) {
    __try {
        out = ClassifyMemoryRegion(reinterpret_cast<const void*>(addr), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeReadFloat(uintptr_t addr, float& out) {
    __try {
        out = *reinterpret_cast<const float*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

// Probe detour instantiations - need unique function addresses for MinHook
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
    // Must precede the state reset below. A run that reached Probing leaves the winning
    // slot deliberately hooked, and RemoveProbeHooks recovers its targets from
    // m_candidates - so clearing that list first strands the hooks permanently. The next
    // camera update then enters ProbeDetour with s_originals[Slot] already nulled and
    // calls through a null trampoline. Consumers rescan ~10s after a Failed, which is
    // exactly the path that reaches this.
    Cleanup();

    m_config = config;
    m_phase = Phase::FindingVtables;
    m_probeFrameCount = 0;
    m_activeSlot = -1;
    m_activeTarget = nullptr;
    m_forcedWindow = {};
    m_instance.store(0);
    m_offsets = {};
    m_layout = {};
    m_candidates.clear();

    for (int i = 0; i < kMaxProbeSlots; i++) {
        s_callCounts[i].store(0);
        s_lastThis[i].store(0);
        s_originals[i] = nullptr;
    }

    Log("DISC: Starting discovery with %d candidate names", (int)config.candidate_names.size());
}

Phase CameraDiscovery::Advance() {
    switch (m_phase) {
        case Phase::FindingVtables: m_phase = RunFindVtables(); break;
        case Phase::Probing:        m_phase = RunProbing(); break;
        case Phase::AnalyzingLayout: m_phase = RunAnalyzeLayout(); break;
        default: break;
    }
    return m_phase;
}

void CameraDiscovery::ReportVfuncCall(int slot, void* this_ptr) {
    if (slot < 0 || slot >= kMaxProbeSlots) return;
    s_callCounts[slot].fetch_add(1, std::memory_order_relaxed);
    s_lastThis[slot].store(reinterpret_cast<uintptr_t>(this_ptr), std::memory_order_relaxed);
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
        Log("DISC: No camera classes found - failed");
        return Phase::Failed;
    }

    // Validate a forced index here, the first moment the real vfunc count is
    // known, so the problem is reported once rather than rediscovered every
    // probe frame. An index that does not exist is downgraded to auto-discovery
    // rather than failing: Failed from probing means "no camera activity, try
    // again later" to consumers, several of which respond by tearing discovery
    // down and rescanning ~10s later. A permanently bad index answered with
    // Failed would put them in a rescan loop for the process lifetime, blaming
    // the wrong cause in the log.
    if (m_config.forced_vfunc_index >= 0 &&
        !ForcedIndexIsSelectable(m_config.forced_vfunc_index, m_candidates[0].vtable.vfunc_count)) {
        Log("DISC: ERROR forced vfunc[%d] does not exist on %s, only %d vfunc(s) resolved; "
            "falling back to auto-discovery for this run",
            m_config.forced_vfunc_index, m_candidates[0].name.c_str(),
            m_candidates[0].vtable.vfunc_count);
        m_config.forced_vfunc_index = -1;
    }

    InstallProbeHooks();
    return Phase::Probing;
}

// ============================================================================
// Phase 2: Probe vfuncs to find gameplay-active one
// ============================================================================

Phase CameraDiscovery::RunProbing() {
    m_probeFrameCount++;

    // This runs from the swapchain Present hook every frame until a winner is
    // picked, so the steady-state path must stay allocation-free. (The one-shot
    // layout analysis that follows does allocate; it runs once.) On the auto
    // path the answer is fixed until the window closes, so skip the counter
    // reads entirely rather than snapshotting 32 atomics per frame.
    if (!ProbeDecisionPossible(m_config, m_probeFrameCount)) return Phase::Probing;

    // Snapshot the shared counters once so the decision below sees a single
    // consistent view of the probe window rather than a moving one.
    const int candidateCount = (int)m_candidates.size();
    int vfuncCounts[kMaxCandidates];
    for (int c = 0; c < candidateCount; c++) vfuncCounts[c] = m_candidates[c].vtable.vfunc_count;

    int callCounts[kMaxProbeSlots];
    for (int i = 0; i < kMaxProbeSlots; i++)
        callCounts[i] = s_callCounts[i].load(std::memory_order_relaxed);

    ProbeSelection sel = SelectProbeSlot(m_config, m_probeFrameCount, vfuncCounts,
                                         candidateCount, callCounts, m_forcedWindow);

    if (sel.decision == ProbeDecision::KeepProbing) {
        // Waiting on a forced slot never times out, so say so periodically,
        // otherwise the log just stops and "forced slot never fired" looks
        // exactly like the silent non-start this feature was added to fix.
        if (m_config.forced_vfunc_index >= 0 &&
            m_probeFrameCount % kForcedWaitLogIntervalFrames == 0) {
            Log("DISC: forced vfunc[%d] still waiting, %d calls after %d frames",
                m_config.forced_vfunc_index,
                callCounts[m_config.forced_vfunc_index], m_probeFrameCount);
        }
        return Phase::Probing;
    }

    // Log all results
    for (int c = 0; c < (int)m_candidates.size(); c++) {
        for (int v = 0; v < m_candidates[c].vtable.vfunc_count; v++) {
            int slot = c * kMaxVfuncsPerCandidate + v;
            if (callCounts[slot] > 0) {
                Log("DISC: %s::vfunc[%d] called %d times (this=%p)",
                    m_candidates[c].name.c_str(), v, callCounts[slot],
                    reinterpret_cast<void*>(s_lastThis[slot].load()));
            }
        }
    }

    if (sel.decision == ProbeDecision::Failed) {
        Log("DISC: No vfuncs called during probe period - failed");
        RemoveProbeHooks();
        return Phase::Failed;
    }

    int ci = sel.slot / kMaxVfuncsPerCandidate;
    int vi = sel.slot % kMaxVfuncsPerCandidate;
    Log(sel.forced ? "DISC: Winner (forced): %s::vfunc[%d] (%d calls)"
                   : "DISC: Winner: %s::vfunc[%d] (%d calls)",
        m_candidates[ci].name.c_str(), vi, sel.call_count);

    m_activeSlot = sel.slot;
    m_activeTarget = reinterpret_cast<void*>(m_candidates[ci].vtable.vfuncs[vi]);
    m_instance.store(s_lastThis[sel.slot].load());

    // Remove all probe hooks except the winner
    // (we keep the winner hooked so we continue getting this-pointers)
    //
    // Pruned by target ADDRESS, not by slot: DisableHook/RemoveHook are keyed on
    // the address, and aliased vtable entries are routine (inherited stubs, and
    // MSVC release builds fold identical functions under /OPT:ICF). Skipping only
    // the winning slot therefore unhooked the winner through one of its aliases,
    // after which discovery reports success while m_instance goes permanently
    // stale at the first scene change.
    auto& hm = hooks::HookManager::Instance();
    void* keepTarget = m_activeTarget;
    for (int c = 0; c < (int)m_candidates.size(); c++) {
        for (int v = 0; v < m_candidates[c].vtable.vfunc_count; v++) {
            void* target = reinterpret_cast<void*>(m_candidates[c].vtable.vfuncs[v]);
            if (target == keepTarget) continue;
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
        Log("DISC: No instance pointer - failed");
        return Phase::Failed;
    }

    Log("DISC: Analyzing instance at %p (%d bytes)...",
        reinterpret_cast<void*>(inst), m_config.instance_size);

    // Read instance memory (skip vtable pointer at +0x00)
    int skipBytes = 8;  // skip vtable ptr
    int analyzeSize = m_config.instance_size - skipBytes;
    if (analyzeSize <= 0) analyzeSize = 256;

    if (!SafeClassify(inst + skipBytes, static_cast<size_t>(analyzeSize), m_layout)) {
        Log("DISC: Instance at %p faulted during layout analysis - failed",
            reinterpret_cast<void*>(inst));
        return Phase::Failed;
    }

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
        Log("DISC: No angle candidates found - failed");
        return Phase::Failed;
    }

    // Read all candidate float values
    struct AngleCandidate { size_t offset; float value; float absValue; };
    std::vector<AngleCandidate> candidates;
    for (size_t off : m_candidateAngleOffsets) {
        float val = 0.0f;
        if (!SafeReadFloat(inst + off, val)) {
            Log("DISC: Instance at %p faulted reading +0x%X - failed",
                reinterpret_cast<void*>(inst), (int)off);
            return Phase::Failed;
        }
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
        Log("DISC: Could not identify yaw - failed");
        return Phase::Failed;
    }

    // Camera Euler angles are stored consecutively: roll, pitch, yaw
    // (or some permutation). Yaw is at yawOff. Pitch is at yawOff-4, roll at yawOff-8.
    // yawOff is unsigned and the derived offsets walk backwards from it. Group offsets
    // carry an 8-byte skip, so anything below that would put roll at or before offset 0 -
    // the vtable pointer - and the mod would write head-tracking floats over it, taking
    // out the next virtual call on the camera object. Below 8 it underflows outright and
    // the write lands at inst-4.
    if (yawOff < 2 * sizeof(float) + 8) {
        Log("DISC: Yaw offset +0x%X too low to carry a preceding pitch/roll pair - failed",
            (int)yawOff);
        return Phase::Failed;
    }

    size_t pitchOff = yawOff - sizeof(float);
    size_t rollOff  = yawOff - 2 * sizeof(float);

    // Read the values for logging
    float yawVal = 0.0f, pitchVal = 0.0f, rollVal = 0.0f;
    if (!SafeReadFloat(inst + yawOff, yawVal) ||
        !SafeReadFloat(inst + pitchOff, pitchVal) ||
        !SafeReadFloat(inst + rollOff, rollVal)) {
        Log("DISC: Instance at %p faulted reading the angle triple - failed",
            reinterpret_cast<void*>(inst));
        return Phase::Failed;
    }

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
