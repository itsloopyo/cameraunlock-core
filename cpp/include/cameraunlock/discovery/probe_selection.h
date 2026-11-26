#pragma once

#include <cameraunlock/discovery/camera_discovery.h>

namespace cameraunlock::discovery {

// What the probing phase should do with the counts it has so far.
enum class ProbeDecision {
    KeepProbing,  // window still open, or a forced slot has not fired enough yet
    Select,       // a winner was found, see ProbeSelection::slot
    Failed,       // no vfunc was called at all; discovery gives up
};

struct ProbeSelection {
    ProbeDecision decision = ProbeDecision::KeepProbing;
    int  slot       = -1;      // candidate * kMaxVfuncsPerCandidate + vfunc
    int  call_count = 0;       // calls the winning slot received
    bool forced     = false;   // the forced index chose this slot
    bool forced_rejected = false;  // forced index was out of range; heuristic ran instead
};

// A forced slot is committed only once it takes kForcedSlotMinCallsPerWindow
// calls inside a single window of kForcedSlotWindowFrames probe frames: a
// RATE, not a running total. A cumulative total cannot tell "the camera update
// is now running" from "a state-transition vfunc has fired occasionally for the
// last hour", and the second of those must never be committed as the per-frame
// update. 30 calls per 60 frames is 0.5x per frame; a real camera update runs
// 1-2x per frame and clears it in well under a second, while an incidental
// vfunc never accumulates enough inside one window.
constexpr int kForcedSlotWindowFrames = 60;
constexpr int kForcedSlotMinCallsPerWindow = 30;

// How often to log that a forced slot is still being waited on. The wait is
// unbounded by design, so without this the log simply stops after the probe
// install lines and a mod that never starts looks identical to one that
// crashed silently.
constexpr int kForcedWaitLogIntervalFrames = 300;  // ~5s at 60fps

// True when a forced index is set AND names a vfunc that actually exists on the
// most-specific candidate. Both "nothing forced" and "forced but out of range"
// answer false; callers that must tell those apart test the index for >= 0.
//
// Out of range is reachable without author error: FindVtableFromRTTI derives
// vfunc_count by walking entries until one falls outside the module, so a class
// with 8 vfuncs can report 2 on a build where entry 2 points at a thunk.
bool ForcedIndexIsSelectable(int forced_vfunc_index, int first_candidate_vfunc_count);

// Cheap guard for the per-frame caller: false when SelectProbeSlot is certain
// to answer KeepProbing whatever the counters say, so the counters need not be
// snapshotted at all. Conservative: a true answer does not promise a decision.
//
// A forced index left out of range keeps answering true, which is why
// CameraDiscovery clears such an index once, up front, rather than rediscovering
// it every frame.
inline bool ProbeDecisionPossible(const DiscoveryConfig& config, int probe_frame_count) {
    return config.forced_vfunc_index >= 0 || probe_frame_count >= config.probe_frames;
}

// Decide which probed vfunc slot drives the per-frame camera update.
//
// Split out of CameraDiscovery so the choice can be exercised without a live
// game process, MinHook, or a real vtable: everything it needs is the config,
// how far the probe window has advanced, how many vfuncs each candidate
// contributed, and the per-slot call counts.
//
// candidate_vfunc_counts: candidate_count entries, most-specific class first.
// call_counts: kMaxProbeSlots entries, indexed
//              slot = candidate * kMaxVfuncsPerCandidate + vfunc.
//
// An out-of-range forced index degrades to the heuristic with forced_rejected
// set, never to Failed. Failed from this phase means "no vfunc was called",
// which consumers read as transient and retry on; answering it for a permanent
// configuration problem turns those consumers into an endless rescan loop.
ProbeSelection SelectProbeSlot(const DiscoveryConfig& config,
                               int probe_frame_count,
                               const int* candidate_vfunc_counts,
                               int candidate_count,
                               const int* call_counts,
                               ForcedSlotWindow& forced_window);

} // namespace cameraunlock::discovery
