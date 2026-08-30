# Porting the tracking pipeline to another language

For mods that cannot link `cameraunlock-core` and reimplement its pipeline
instead: CET Lua, UE4SS Lua, Rust ASI, Python add-ins. Those ports do not receive
core's fixes, so every invariant below has to be re-implemented and re-tested
locally.

This document exists because of a measured pattern. An audit of four independent
ports found that all four implemented the HCAM trailer correctly and **three of
four** got the interpolator's angle handling wrong, in the same way. The
difference is that HCAM has `hcam-inband-protocol.md` with a conformance vector
and the pipeline had nothing. Each section below therefore ends with a check that
fails on the wrong implementation, with the numbers a real port actually produced.

## Before porting: if you can link the core, link it

A port is only warranted where the host forbids native code. **If the mod already
loads a native DLL of its own, link `cameraunlock` and do not port** - not the
packet layer, not the interpolators, not the processors. A mod that has an ASI, a
RED4ext plugin, a UE4SS C++ module or any other DLL of its own has already paid
the cost that a port exists to avoid.

The evidence is in the audit. Every port that hand-wrote the packet layer has a
defect in it; the one port with a zero-defect packet layer is the one that did not
write it. `cyberpunk-2077-headtracking` is Lua on top of a native DLL, and that
DLL links the core (`native/CMakeLists.txt` links `cameraunlock`,
`native/src/UdpReceiver.cpp` instantiates `cameraunlock::UdpReceiver`), so the
finiteness gate, the cm-to-metres conversion and the trailer parse are the ones
this library ships and fixes. Its remaining defects are all in the Lua half - the
part it genuinely had to port.

The same reasoning applies inside a port. Draw the native/scripted boundary as
low as you can: everything below it is shared and maintained, everything above it
is yours to re-test forever.

## Conformance vectors

`data/pipeline-conformance.json` is the executable form of the checks below -
input sequence, expected output, tolerance - together with the constants block, so
there is one source rather than a number restated in six repos. Run it instead of
re-deriving the numbers:

    node scripts/pipeline-vectors/run-vectors.mjs --harness "<your harness>"

The runner owns every assertion; a port supplies only a small executor that reads
a line-oriented command stream and prints one line of numbers per step (see the
protocol comment at the top of `run-vectors.mjs`, and the C++ and C# harnesses in
`scripts/pipeline-vectors/harness/` for two worked examples). `pixi run vectors`
runs them against both of core's own implementations, so a change here fails in
core before it reaches a mod. Every vector carries the `section` number of the
invariant below that it comes from, so a failure names the paragraph that explains
it.

Where a local test and a vector disagree, the vector is right. Several ports'
hand-written tests encode the defect rather than catching it: one asserts the
held-forever extrapolation value exactly, one builds its expected pivot input with
the same wrong sign as the implementation so it passes either way, and one asserts
only that the output is finite.

## Ports found

| Repo | Language | Notes |
|---|---|---|
| `cyberpunk-2077-headtracking` | Lua over a native DLL | Links the core for the packet layer; only the Lua half is a port |
| `the-pathless-headtracking` | Lua (UE4SS) | 3DOF only - no position processor and no interpolator |
| `bioshock-infinite-headtracking` | Rust | |
| `bioshock-remastered-headtracking` | Rust | |
| `minecraft-java-edition-headtracking` | Java | 15 hand-written classes under `core/src/main/java/com/cameraunlock/core/`; the submodule is present but the Gradle build compiles nothing from it |
| `fusion-360-headtracking` | Python | |

## 1. Yaw and roll traverse the shortest arc

Reference: `cpp/include/cameraunlock/processing/pose_interpolator.h`,
`math::ShortestAngleDelta` and `math::NormalizeAngle`.

Angles arrive in `(-180, 180]`. A plain lerp between two samples that straddle
the seam travels the long way round. This applies to **yaw and roll only** -
pitch comes from `asin`, is bounded to +/-90, and cannot wrap, so giving it the
same treatment is wrong.

It has to be applied at **both** the output and the from-capture on a new sample.
Fixing only the output leaves an unwrapped value stored as `from`, and the next
segment goes the long way instead.

**Check.** Interpolate yaw 175 -> -175. Every intermediate value must satisfy
`|out| >= 175` and total travel must be 10 degrees, not 350. Test both
directions: a sign error in the delta passes one and fails the other.

Measured on real ports before fixing: quarter-way across the seam gave 87.5
instead of 177.5 (Lua); a 2-degree turn with the origin near the seam drove the
camera to -90 degrees and **held it** there until the head crossed back, because
the ~360-degree difference was then clamped to the rotation limit.

## 2. Extrapolation expires on a wall clock

Reference: `PoseInterpolator::SegmentPosition`.

Progress past 1.0 is a short prediction that keeps velocity continuous between
samples. Clamping it to `1.0 + max_extrapolation_fraction` and then **holding**
parks the output at up to 1.5x the last reported pose indefinitely whenever
samples stop arriving - a tracker app streaming its last value while the face is
lost, or a head so still that consecutive samples are bit-identical and the
duplicate filter suppresses them. A 25-degree head turn renders as 37.5 and
stays.

So the prediction expires: hold for `kExtrapolationHoldSeconds` (0.25), then ease
to 1.0 over `kExtrapolationDecaySeconds` (0.35) with smoothstep.

The hold matters as much as the decay. A dropped packet or two is still a live
feed and must behave as before - retreating on a dropped packet pulls the camera
*backwards* against a head that is still turning, which reads worse than the flat
spot it replaces.

**Time it on the wall clock, not on progress.** Progress is measured in units of
an estimated sample interval, and that estimate only updates when a new sample
arrives - so it is stale by construction in exactly the stall case this exists to
handle.

**Check.** Feed samples, then stop. Output must settle on the last *reported*
pose (25, not 37.5) and stay. A separate check must confirm a sub-0.25s dropout
still reaches and holds the 1.5x cap, so the anti-judder behaviour was not traded
away.

## 3. The default sample interval is 1/30, not 1/60

Reference: `kDefaultSampleInterval`.

Before the first two samples establish a real interval, the estimate is a guess.
Guessing 1/60 for a 30 Hz tracker makes the first second of a session jolt.
Native mods had that jolt and Unity mods did not, because only one of them had
the wrong default.

**Check.** Drive a 30 Hz feed from cold and assert the first second contains no
step larger than an ordinary inter-sample delta.

## 4. Position clamps before AND after smoothing

Reference: `cpp/include/cameraunlock/processing/position_processor.h`, `ClampToLimits`
(called at both the pre-smoothing and post-smoothing sites).

Clamping only the output lets the smoothing state itself wind up outside the
limits, and the output then pins at a limit for hundreds of milliseconds after
the head has already come back.

Two further things ports get wrong here:

- **The vertical clamp is asymmetric**: `[-limit_y_down, +limit_y]`. A mod whose
  config exposes one vertical limit must mirror it into both (see
  `PositionSettings::Symmetric`), otherwise the downward budget silently sits on
  the core default, decoupled from the value the user set.
- **Negative z is the forward lean.** Engines whose camera-local +z is forward
  negate at the boundary, not with an `invert_z` flag.

## 5. Gimbal lock - and it is not only in quaternion conversion

Reference: `Quat4::ToEulerYXZ`, `QuaternionUtils.ToEulerYXZ`.

Two separate bugs, and both are easy to reproduce in a port:

- The lock branch must not reuse the general yaw formula. At exactly 90 degrees
  of pitch both of its `atan2` arguments are zero, so it returns `atan2(0, 0)` =
  0 and the rotation is discarded. Use
  `atan2(2*(x*y - w*z), 1 - 2*(y*y + z*z))`.
- The threshold must be `0.9999995`, not `1.0`. Testing `sinPitch >= 1.0`
  essentially never fires: float error leaves it a shade under 1 even for a
  quaternion built at exactly 90 degrees (measured 0.99999982), so the degenerate
  branch runs anyway.

**This singularity is not confined to quaternion->Euler.** A port with no
quaternion type at all still has it wherever it decomposes an orientation -
a matrix->Euler `basis_to_rotator` has exactly the same degenerate case. One
Rust port concluded "not applicable, no quaternion conversion" and had a
worst-case error of 1.5708 radians, a full right angle of rotation silently
dropped.

It is also not an edge case in practice: engines using a quantised integer
rotator (Unreal's `FRotator`) quantise both the game's view pitch and the head
pitch, so their sum lands on exactly 90 degrees regularly.

**Check.** Decompose an orientation built at exactly 90 degrees of pitch and
assert the heading survives. Sweep a few hundred thousand random orientations and
assert worst-case round-trip error stays at the representation's quantisation
floor rather than approaching a right angle.

## 6. Pivot compensation is built on negative z, and defaults to off

Reference: `PositionProcessor`, `TrackerPivotForward`.

Head rotation moves the tracked point even when the body has not, and subtracting
that artifact keeps a pure head turn from also panning the view. The pivot vector
must be `(0, 0, -TrackerPivotForward)`.

Rotation is linear, so `R(-v) - (-v) == -(R(v) - v)`: building it as `+z`
computes the exact negation of the real artifact, and subtracting that **adds**
the phantom translation instead of removing it, doubling it.

The default is **0** - compensation off. The correct arm length is a property of
the tracker app, not of this library: the Headcam Android app already applies its
own eye-anchor offset while iOS applies none. A port that ships a non-zero
default is guessing on the user's behalf.

## 7. Deadzone must be continuous at its edge

Reference: `math::deadzone_utils`, `DeadzoneSettings`.

Subtract the deadzone in the input's direction rather than gating on it. Gating
means an input just past the threshold jumps: with a deadzone of 5, an input of
4.99 gives 0 and 5.01 gives 5.01, so a 0.02-degree movement pops the camera 5
degrees. Note this fires on the DEFAULT settings, where `DeadzoneMax <=
DeadzoneMin`.

## 8. Reset the interpolator on recenter

Reference: `PoseInterpolator::Reset`, documented "call on recenter".

A recenter captures a neutral pose. If the interpolator is not reset first, what
gets captured is whatever it is currently *showing* - a blend between two
samples, or on a frame with no fresh packet, the entire pre-press pose.

The residual is permanent and, because yaw and roll are inverted downstream, it
is **mirrored** - the same visible symptom as the double-subtract bug that
`hcam-inband-protocol.md` describes, and reachable without any double
subtraction.

It hides at 60 fps on a 60 Hz tracker, where `dt >= sampleInterval` and the
blend completes within the frame. One Lua port measured, with 8 degrees of
pre-press drift:

| render fps | parked residual, mirrored |
|---|---|
| 60 | 0.000 |
| 75 | 8.000 |
| 144 | 4.847 |
| 240 | 8.000 |

Key the reset off the "capture armed" state rather than the keypress, so it
covers every path that recenters - the hotkey and a remote HCAM press. Wiring it
to the hotkey misses the other.

**Check.** Arm a recenter at several render rates above the tracker rate and
assert the captured neutral equals the raw pose the tracker reported, exactly.
Cover a press while the head is *moving*, which is the local-hotkey form of the
same defect.

## 9. The HCAM trailer does not recenter

A port must **parse** a 54-byte datagram - bytes 0-47 are the pose and it has to
reach the pipeline like any other packet - and must **not** recenter on it.

Headcam owns centring: it zeroes its own output when the player presses CENTER.
With the mod-side centre at identity (invariant 10) that zeroed stream is already
correct, so there is nothing left for the mod to do. Acting on the trailer as
well is the double-centre this doctrine exists to prevent, reached through
another door.

Older Headcam builds still send the trailer, so ignoring it is a positive
requirement, not an absence. Keep any `TryConsumeRecenterRequest`-shaped accessor
you already expose and have it report nothing, rather than deleting it and
breaking callers.

**Check.** Send a trailered packet and assert two things: the pose it carried
reaches the output, and no recenter happened. Then set a centre with the hotkey,
send a trailered packet, and assert the hotkey centre survives.

## 10. The mod-side center starts at identity

The pipeline consumes the tracker's pose as absolute, the way TrackIR does: the
driver owns the center and the game does not keep one of its own until the player
asks. Headcam zeroes at tracking start and on every CENTER press, opentrack
centers on its own Center bind, so the stream arriving at a port is already
centered.

A port that captures the incoming pose as a center when the connection comes up
puts a SECOND center in series with that one, and the two drift apart because
each side recenters at moments the other cannot see. At startup both are near
zero and it looks fine. The symptom shows up later: the player presses Center on
a tracker that sends no HCAM trailer, the tracker's output drops to zero, the
port keeps subtracting the pose it captured at session start, and the view parks
at the negated drift until the player also presses the mod's hotkey. Two presses
for one recenter.

So the center is identity until the recenter hotkey or an HCAM trailer press sets
one. This is the mistake most likely to be carried across from an older port: it
was `cameraunlock-core`'s own behaviour until the CHANGELOG entry "the session no
longer captures a center on connect", and every full local port copied it.

**Check.** Feed an uncentered stream (yaw 40) for a few hundred frames, then drop
the stream to zero with no trailer, and assert the output lands at zero rather
than -40. Run it for position as well as rotation: the position center is captured
separately and a rotation-only test passes with the position half broken.

## 11. The engine boundary owns the z flip, never `InvertZ`

Reference: `PositionApplicator`, `ViewMatrixModifier`, `PositionSettings.InvertZ`.

Negative z is the forward lean everywhere inside the pipeline, and the asymmetric
clamp is built on that: `[-LimitZ, +LimitZBack]` puts the generous 0.40m budget on
the negative side. Most engines call +z forward, so exactly one flip is needed,
and it belongs in the code that hands the offset to the engine.

`InvertZ` cannot do that job. It is applied in step 2 of the processor, *before*
the clamp, so a mod that flips there clamps an already-flipped axis: the forward
lean gets the 0.10m backward allowance and the backward lean gets the 0.40m
forward one. The direction looks right, which is why this survives testing - only
the travel is wrong, and it has been found in production more than once. `InvertZ`
is for a tracker whose z genuinely runs the other way, nothing else. The same
applies to `InvertY` against a y-down engine.

Beware a port that offers two apply paths. Ours does - a view-space matrix
composition and a transform-space decomposition - and Unity's view space looks
down -z while its transforms call +z forward, so one path needed the flip and the
other did not. They disagreed, one mode leaned forward and the other backward, and
the runtime yaw-mode toggle switched between them mid-session.

**Check.** Feed the same forward-lean offset through every apply path with a
rotated game camera and assert they place the camera at the same world point.
