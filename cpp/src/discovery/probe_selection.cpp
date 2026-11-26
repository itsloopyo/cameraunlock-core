#include <cameraunlock/discovery/probe_selection.h>

#include <climits>

namespace cameraunlock::discovery {

bool ForcedIndexIsSelectable(int forced_vfunc_index, int first_candidate_vfunc_count) {
    return forced_vfunc_index >= 0 && forced_vfunc_index < first_candidate_vfunc_count;
}

namespace {

// The call-count heuristic, unchanged: prefer a vfunc called at roughly
// per-frame rate (~60Hz). A camera update runs 1-2x per frame. Utility
// functions run 100s of times. Target: ~1-10 calls per frame = probe_frames to
// probe_frames*10 total calls. Prefer the first candidate (most specific class)
// with a match.
ProbeSelection SelectByCallRate(const DiscoveryConfig& config,
                                const int* candidate_vfunc_counts,
                                int candidate_count,
                                const int* call_counts) {
    ProbeSelection sel{};

    const int targetCallsLow = config.probe_frames;        // ~1x per frame
    const int targetCallsHigh = config.probe_frames * 10;  // ~10x per frame

    int bestSlot = -1;
    int bestCount = 0;
    int bestDistance = INT_MAX;  // distance from ideal range

    for (int c = 0; c < candidate_count; c++) {
        int candidateBestSlot = -1;
        int candidateBestDist = INT_MAX;
        int candidateBestCount = 0;

        for (int v = 0; v < candidate_vfunc_counts[c]; v++) {
            int slot = c * kMaxVfuncsPerCandidate + v;
            int count = call_counts[slot];
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
        // Otherwise track the best across all candidates
        if (candidateBestSlot >= 0 && candidateBestDist < bestDistance) {
            bestDistance = candidateBestDist;
            bestSlot = candidateBestSlot;
            bestCount = candidateBestCount;
        }
    }

    if (bestSlot < 0 || bestCount == 0) {
        sel.decision = ProbeDecision::Failed;
        return sel;
    }

    sel.decision   = ProbeDecision::Select;
    sel.slot       = bestSlot;
    sel.call_count = bestCount;
    return sel;
}

}  // namespace

ProbeSelection SelectProbeSlot(const DiscoveryConfig& config,
                               int probe_frame_count,
                               const int* candidate_vfunc_counts,
                               int candidate_count,
                               const int* call_counts,
                               ForcedSlotWindow& forced_window) {
    ProbeSelection sel{};

    if (candidate_count <= 0 || candidate_vfunc_counts == nullptr || call_counts == nullptr) {
        sel.decision = ProbeDecision::Failed;
        return sel;
    }

    const bool forcedRequested = config.forced_vfunc_index >= 0;
    const bool forcedSelectable =
        ForcedIndexIsSelectable(config.forced_vfunc_index, candidate_vfunc_counts[0]);

    if (forcedRequested && forcedSelectable) {
        // Only the first (most-specific) candidate is eligible: a forced index
        // is a fact about one reversed class, not about whatever else RTTI
        // happened to match. Candidate 0, so slot == vfunc index.
        const int slot = config.forced_vfunc_index;
        const int count = call_counts[slot];
        const int elapsed = probe_frame_count - forced_window.start_frame;
        const int delta = count - forced_window.start_count;

        if (delta >= kForcedSlotMinCallsPerWindow) {
            sel.decision   = ProbeDecision::Select;
            sel.slot       = slot;
            sel.call_count = count;
            sel.forced     = true;
            return sel;
        }

        // Too slow for a per-frame update. Drop the window and start counting
        // again, so occasional calls can never add up to a commit.
        if (elapsed >= kForcedSlotWindowFrames) {
            forced_window.start_frame = probe_frame_count;
            forced_window.start_count = count;
        }

        // Deliberately NOT bounded by probe_frames: a load-in longer than the
        // probe window must not hand the choice back to the heuristic, which is
        // the exact mis-pick the forced index exists to remove.
        sel.decision = ProbeDecision::KeepProbing;
        return sel;
    }

    if (probe_frame_count < config.probe_frames) {
        sel.decision = ProbeDecision::KeepProbing;
        return sel;
    }

    sel = SelectByCallRate(config, candidate_vfunc_counts, candidate_count, call_counts);
    sel.forced_rejected = forcedRequested;  // set only when a forced index was ignored
    return sel;
}

} // namespace cameraunlock::discovery
