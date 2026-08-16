// Tests for the discovery probe window's vfunc-slot choice: the call-count
// heuristic, the forced-index override that bypasses it, and the rate gate that
// decides when a forced slot is really running.

#include <cameraunlock/discovery/probe_selection.h>

#include <initializer_list>
#include <iostream>

namespace {

using namespace cameraunlock::discovery;

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

// The W3 mod's probe window. With it, the "looks like a per-frame update" band
// runs from probe_frames (600, ~1x per frame) to probe_frames*10 (6000).
constexpr int kProbeFrames = 600;
constexpr int kBandLow = kProbeFrames;
constexpr int kBandHigh = kProbeFrames * 10;

DiscoveryConfig MakeConfig(int forcedVfuncIndex) {
    DiscoveryConfig cfg{};
    cfg.probe_frames = kProbeFrames;
    cfg.forced_vfunc_index = forcedVfuncIndex;
    return cfg;
}

// Zero-initialised per-slot counters, addressed the way discovery addresses
// them: slot = candidate * kMaxVfuncsPerCandidate + vfunc.
struct Counts {
    int slots[kMaxProbeSlots] = {};

    Counts& Set(int candidate, int vfunc, int count) {
        slots[candidate * kMaxVfuncsPerCandidate + vfunc] = count;
        return *this;
    }
};

constexpr int Slot(int candidate, int vfunc) {
    return candidate * kMaxVfuncsPerCandidate + vfunc;
}

// One-shot selection with a fresh window, for tests that do not care about the
// rate gate's history.
ProbeSelection Select(const DiscoveryConfig& cfg, int frame,
                      std::initializer_list<int> vfuncCounts, const Counts& counts) {
    int arr[kMaxCandidates] = {};
    int n = 0;
    for (int v : vfuncCounts) arr[n++] = v;
    ForcedSlotWindow window{};
    return SelectProbeSlot(cfg, frame, arr, n, counts.slots, window);
}

// Convenience for the many heuristic tests, which all run at the moment the
// probe window closes.
ProbeSelection Heuristic(std::initializer_list<int> vfuncCounts, const Counts& counts) {
    return Select(MakeConfig(-1), kProbeFrames, vfuncCounts, counts);
}

// The situation the forced index exists for, drawn from The Witcher 3's
// CCustomCamera: vfunc[2] is the real per-frame camera update but has barely
// started firing by the end of the probe window, while vfunc[3], a getter the
// renderer hammers, sits squarely inside the per-frame band.
Counts LateCameraUpdate(int cameraUpdateCalls = 40) {
    Counts counts;
    counts.Set(0, 2, cameraUpdateCalls);  // the real update, only just started
    counts.Set(0, 3, 1200);               // high-frequency getter, ~2x per frame
    return counts;
}

// ---------------------------------------------------------------------------
// Default path: forced_vfunc_index left at -1, behaviour unchanged
// ---------------------------------------------------------------------------

void TestDefaultWaitsForTheWholeProbeWindow() {
    DiscoveryConfig cfg = MakeConfig(-1);
    Check(cfg.forced_vfunc_index == -1, "DiscoveryConfig defaults to auto-discovery");

    Counts counts = LateCameraUpdate();
    Check(Select(cfg, kProbeFrames - 1, {8}, counts).decision == ProbeDecision::KeepProbing,
          "auto-discovery keeps probing until probe_frames elapses");
    Check(!ProbeDecisionPossible(cfg, kProbeFrames - 1),
          "no decision is possible mid-window, so the counters need not be read");
    Check(ProbeDecisionPossible(cfg, kProbeFrames),
          "a decision becomes possible once the window closes");

    ProbeSelection done = Select(cfg, kProbeFrames, {8}, counts);
    Check(done.decision == ProbeDecision::Select,
          "auto-discovery selects once the window closes");
    Check(done.slot == Slot(0, 3), "auto-discovery picks the slot nearest the per-frame rate");
    Check(done.call_count == 1200, "the winner reports its own call count");
    Check(!done.forced, "a heuristic winner is not flagged as forced");
    Check(!done.forced_rejected, "nothing was rejected when nothing was forced");
}

void TestDefaultPrefersTheMostSpecificCandidate() {
    Counts counts;
    counts.Set(0, 1, 700);   // most-specific class, in the per-frame band
    counts.Set(1, 4, 650);   // base class, also in band and closer to 1x/frame

    Check(Heuristic({8, 8}, counts).slot == Slot(0, 1),
          "a qualifying first candidate wins over a later one");
}

void TestDefaultFallsThroughToALaterCandidate() {
    Counts counts;
    counts.Set(0, 1, 20);     // too few calls to qualify (< 50)
    counts.Set(1, 1, 1000);   // base class carries the real update

    Check(Heuristic({8, 8}, counts).slot == Slot(1, 1),
          "a first candidate with no qualifying slot yields to the next class");
}

void TestDefaultFailsWhenNothingWasCalled() {
    Counts counts;
    ProbeSelection sel = Heuristic({8}, counts);
    Check(sel.decision == ProbeDecision::Failed,
          "a silent probe window fails rather than picking an uncalled slot");
    Check(sel.slot == -1, "a failed selection reports no slot");
}

void TestNoCandidatesFails() {
    Counts counts;
    ForcedSlotWindow window{};
    int none[1] = {0};
    Check(SelectProbeSlot(MakeConfig(-1), kProbeFrames, none, 0, counts.slots, window).decision
              == ProbeDecision::Failed,
          "no candidate classes means no selection");
}

// ---------------------------------------------------------------------------
// The per-frame-rate band: the substance of the ranking
// ---------------------------------------------------------------------------

// Both slots sit inside the band, so distance ties and the higher count wins.
// Pins the tie-break direction: prefer the busier slot, not the first seen.
void TestBandTieBreaksOnTheHigherCount() {
    Counts counts;
    counts.Set(0, 1, 1000);
    counts.Set(0, 2, 2000);
    Check(Heuristic({8}, counts).slot == Slot(0, 2),
          "inside the band, the busier slot wins the tie");
}

// Same tie, opposite order. A later slot with an equal distance and a LOWER
// count must not displace the incumbent.
void TestBandTieDoesNotFavourTheLaterSlot() {
    Counts counts;
    counts.Set(0, 1, 2000);
    counts.Set(0, 2, 1000);
    Check(Heuristic({8}, counts).slot == Slot(0, 1),
          "an equal-distance slot with fewer calls does not displace the incumbent");
}

// The band's upper edge. 5000 is inside it and beats an in-band 700 on count;
// were the ceiling much lower, 5000 would read as an over-busy utility function
// and 700 would win instead.
void TestBandCeilingIsTenTimesPerFrame() {
    Counts counts;
    counts.Set(0, 1, kBandHigh - 1000);  // 5000, comfortably inside
    counts.Set(0, 2, 700);               // also inside, but quieter
    ProbeSelection sel = Heuristic({8}, counts);
    Check(sel.slot == Slot(0, 1) && sel.call_count == kBandHigh - 1000,
          "a slot well inside the ceiling outranks a quieter in-band slot");
}

// The band's lower edge, and the penalty for sitting under it. 500 is 100 short
// of the floor and 6100 is 100 over the ceiling, so they tie on distance and the
// count breaks it. Move the floor down and 500 becomes a perfect match instead.
void TestBandFloorIsOncePerFrame() {
    Counts counts;
    counts.Set(0, 1, kBandLow - 100);   // 500, just under the floor
    counts.Set(0, 2, kBandHigh + 100);  // 6100, just over the ceiling
    ProbeSelection sel = Heuristic({8}, counts);
    Check(sel.slot == Slot(0, 2) && sel.call_count == kBandHigh + 100,
          "a slot just under the floor is penalised exactly like one just over the ceiling");
}

// Distance below the floor has to scale with how far below. Two candidates, both
// under the floor and both too quiet to qualify outright, so the cross-candidate
// fallback ranks them on distance alone, the only place a below-floor distance
// decides anything.
void TestDistanceBelowTheFloorScales() {
    Counts counts;
    counts.Set(0, 1, 40);   // 560 short of the floor
    counts.Set(1, 1, 45);   // 555 short, closer, so it wins
    ProbeSelection sel = Heuristic({8, 8}, counts);
    Check(sel.decision == ProbeDecision::Select && sel.slot == Slot(1, 1),
          "among below-floor slots the closest to the floor wins");
}

// Same fallback, exact distance tie. The most-specific class keeps it.
void TestCrossCandidateTieKeepsTheMostSpecificClass() {
    Counts counts;
    counts.Set(0, 1, 40);
    counts.Set(1, 1, 40);
    Check(Heuristic({8, 8}, counts).slot == Slot(0, 1),
          "an exact cross-candidate distance tie keeps the most-specific class");
}

// Uncalled slots are not candidates at all. If they were, an untouched slot
// would score as "probe_frames short of the floor" and beat a genuinely busy
// one, then fail the zero-count guard and sink the whole run.
void TestUncalledSlotsAreNotCandidates() {
    Counts counts;
    counts.Set(0, 1, 100000);  // far above the ceiling, but the only slot called
    ProbeSelection sel = Heuristic({8}, counts);
    Check(sel.decision == ProbeDecision::Select && sel.slot == Slot(0, 1),
          "a wildly over-busy slot still beats seven uncalled ones");
    Check(sel.call_count == 100000, "the over-busy winner reports its real count");
}

// ---------------------------------------------------------------------------
// Forced path: forced_vfunc_index bypasses the heuristic
// ---------------------------------------------------------------------------

// The whole point of the feature: identical counts, opposite answers.
void TestForcedIndexOverridesTheHeuristicsMisPick() {
    Counts counts = LateCameraUpdate();

    ProbeSelection automatic = Select(MakeConfig(-1), kProbeFrames, {8}, counts);
    ProbeSelection forced = Select(MakeConfig(2), kProbeFrames, {8}, counts);

    Check(automatic.slot == Slot(0, 3), "the heuristic latches onto the high-frequency getter");
    Check(forced.decision == ProbeDecision::Select && forced.slot == Slot(0, 2),
          "the forced index selects the real camera update instead");
    Check(forced.forced, "a forced winner is flagged as forced");
    Check(forced.call_count == 40, "the forced winner reports its own call count");
}

void TestForcedIndexDoesNotWaitForTheProbeWindow() {
    Counts counts = LateCameraUpdate();
    Check(Select(MakeConfig(2), 1, {8}, counts).slot == Slot(0, 2),
          "a forced slot that is already firing is selected on the first frame");
}

// A forced index names one reversed class. It must not drift onto whatever
// else RTTI matched.
void TestForcedIndexOnlyConsidersTheFirstCandidate() {
    Counts counts;
    counts.Set(0, 2, 50);      // first candidate's forced slot, firing
    counts.Set(1, 2, 100000);  // base class's slot 2, far busier

    Check(Select(MakeConfig(2), kProbeFrames, {8, 8}, counts).slot == Slot(0, 2),
          "the forced index binds to the most-specific candidate only");
}

// ---------------------------------------------------------------------------
// Forced path: the rate gate
// ---------------------------------------------------------------------------

// Drives the forced path frame by frame at a fixed call rate, sharing one
// window across frames exactly as CameraDiscovery does. Returns the frame the
// slot was committed on, or -1 if it was never committed.
int ForcedSelectFrame(int callsPerHundredFrames, int maxFrames) {
    DiscoveryConfig cfg = MakeConfig(2);
    int vfuncs[1] = {8};
    Counts counts;
    ForcedSlotWindow window{};
    for (int frame = 1; frame <= maxFrames; ++frame) {
        counts.Set(0, 2, frame * callsPerHundredFrames / 100);
        ProbeSelection sel = SelectProbeSlot(cfg, frame, vfuncs, 1, counts.slots, window);
        if (sel.decision == ProbeDecision::Failed) return -2;
        if (sel.decision == ProbeDecision::Select) return frame;
    }
    return -1;
}

void TestForcedGateCommitsARunningCameraUpdate() {
    // 2x per frame: the normal shape of a per-frame camera update.
    const int fast = ForcedSelectFrame(200, 10000);
    Check(fast > 0 && fast <= kForcedSlotWindowFrames,
          "a slot running twice per frame is committed inside one window");

    // 1x per frame.
    const int once = ForcedSelectFrame(100, 10000);
    Check(once > 0 && once <= kForcedSlotWindowFrames,
          "a slot running once per frame is committed inside one window");

    // Exactly at the gate: 0.5x per frame.
    Check(ForcedSelectFrame(50, 10000) == kForcedSlotWindowFrames,
          "a slot running at exactly the threshold rate is committed at the window edge");
}

// The reason the gate is a rate and not a running total. At 0.1 calls per frame
// the cumulative count passes 30 at frame 300 and a total-based gate would
// commit an incidental vfunc as the per-frame camera update. The window keeps
// resetting instead, so it never does.
void TestForcedGateRejectsAStrayCallRate() {
    const int perWindow = kForcedSlotWindowFrames / 10;  // 6 calls per window
    Check(perWindow < kForcedSlotMinCallsPerWindow, "the stray rate is below the gate by design");

    const int frames = 20000;
    Check(frames / 10 > kForcedSlotMinCallsPerWindow,
          "the run is long enough that a cumulative gate would have committed");
    Check(ForcedSelectFrame(10, frames) == -1,
          "a trickle of stray calls never accumulates into a commit");
}

// Probing still has to run: the slot has to be firing before it is committed
// to, so discovery captures a real instance pointer rather than a null one.
void TestForcedGateWaitsOnASilentSlot() {
    Check(ForcedSelectFrame(0, 20000) == -1, "a forced slot that never fires is never committed");
}

// The wait is deliberately not bounded by probe_frames (a load-in longer than
// the window must not hand the choice back to the heuristic) and it must never
// surface as Failed, which consumers read as "retry in ten seconds".
void TestForcedWaitOutlastsTheProbeWindowWithoutFailing() {
    Counts counts = LateCameraUpdate(0);
    ProbeSelection sel = Select(MakeConfig(2), kProbeFrames * 10, {8}, counts);
    Check(sel.decision == ProbeDecision::KeepProbing,
          "waiting on a forced slot never times out into the heuristic");
    Check(sel.decision != ProbeDecision::Failed,
          "waiting on a forced slot never reports discovery failure");
}

// ---------------------------------------------------------------------------
// Forced path: an index that does not exist on the class actually found
// ---------------------------------------------------------------------------

void TestForcedIndexRangeCheck() {
    Check(ForcedIndexIsSelectable(2, 8), "an index inside the vtable is selectable");
    Check(ForcedIndexIsSelectable(0, 1), "index 0 is selectable on a one-vfunc class");
    Check(!ForcedIndexIsSelectable(4, 4), "one past the last vfunc is not selectable");
    Check(!ForcedIndexIsSelectable(6, 4), "an index past the vtable is not selectable");
    Check(!ForcedIndexIsSelectable(-1, 8), "no forced index means nothing to select");
}

// RTTI truncates vfunc_count at the first entry outside the module, so a class
// with 8 vfuncs can report 2. Answering Failed here would be a lie about a
// permanent condition: consumers treat probing failure as transient and rescan
// the whole module every ten seconds forever. Degrade to the heuristic instead.
void TestOutOfRangeForcedIndexUsesTheHeuristic() {
    Counts counts;
    counts.Set(0, 3, 1200);  // a slot the heuristic is happy to pick

    ProbeSelection sel = Select(MakeConfig(6), kProbeFrames, {4}, counts);
    Check(sel.decision != ProbeDecision::Failed,
          "an out-of-range forced index is not a discovery failure");
    Check(sel.decision == ProbeDecision::Select && sel.slot == Slot(0, 3),
          "an out-of-range forced index falls through to the heuristic");
    Check(sel.forced_rejected, "the ignored forced index is reported to the caller");
    Check(!sel.forced, "a heuristic winner is not flagged as forced");
}

void TestOutOfRangeForcedIndexStillHonoursTheProbeWindow() {
    Counts counts;
    counts.Set(0, 3, 1200);
    Check(Select(MakeConfig(6), 1, {4}, counts).decision == ProbeDecision::KeepProbing,
          "an out-of-range forced index waits out the probe window like auto-discovery");
}

}  // namespace

int RunProbeSelectionTests() {
    std::cout << "Probe selection tests:\n";
    TestDefaultWaitsForTheWholeProbeWindow();
    TestDefaultPrefersTheMostSpecificCandidate();
    TestDefaultFallsThroughToALaterCandidate();
    TestDefaultFailsWhenNothingWasCalled();
    TestNoCandidatesFails();
    TestBandTieBreaksOnTheHigherCount();
    TestBandTieDoesNotFavourTheLaterSlot();
    TestBandCeilingIsTenTimesPerFrame();
    TestBandFloorIsOncePerFrame();
    TestDistanceBelowTheFloorScales();
    TestCrossCandidateTieKeepsTheMostSpecificClass();
    TestUncalledSlotsAreNotCandidates();
    TestForcedIndexOverridesTheHeuristicsMisPick();
    TestForcedIndexDoesNotWaitForTheProbeWindow();
    TestForcedIndexOnlyConsidersTheFirstCandidate();
    TestForcedGateCommitsARunningCameraUpdate();
    TestForcedGateRejectsAStrayCallRate();
    TestForcedGateWaitsOnASilentSlot();
    TestForcedWaitOutlastsTheProbeWindowWithoutFailing();
    TestForcedIndexRangeCheck();
    TestOutOfRangeForcedIndexUsesTheHeuristic();
    TestOutOfRangeForcedIndexStillHonoursTheProbeWindow();
    return g_failures;
}
