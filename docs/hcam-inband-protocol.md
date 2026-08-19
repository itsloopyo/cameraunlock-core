# HCAM in-band tracker signalling, version 1

Status: draft, not implemented. Revised after adversarial review.

Defines how a Headcam tracker app tells a cameraunlock receiver "this stream is
mine, and here is my recenter state" without changing the size of an OpenTrack
datagram.

Supersedes the 54-byte HCAM trailer. The trailer stays supported until the mod
fleet has re-synced (see Migration).


> **Superseded in part.** This document was written when the mod captured a
> centre on tracker connection. That capture is now opt-in and off by default
> (`AutoRecenterOnConnect`), so the races argued about below do not arise in a
> default build, and every source line number cited is pre-change. The reasoning
> is kept because it still applies to a mod that opts back in, and because
> clearing-on-identification remains the cheaper rule than enumerating every
> site.

## Problem

The trailer only works when the receiver is already running at the moment the
player presses CENTER. A mod that starts afterwards never sees a trailer,
because steady-state packets have to stay 48 bytes, so it falls back to centring
on whatever pose happens to be in flight: the player mid-launch, halfway out of
their chair, looking at their phone.

That is the whole motivation. Two claims that used to appear here have been
demoted because they do not survive checking:

- The repo has long held that oversized datagrams are discarded by plain
  opentrack with WSAEMSGSIZE, costing ~500 ms per CENTER press. Qt's Windows
  socket engine explicitly swallows WSAEMSGSIZE and returns a truncated read, so
  opentrack most likely reads the first 48 bytes and carries on. **This needs an
  empirical test against a real opentrack before anything in this repo relies on
  it in either direction**, including the comments in `PacketEncoder.kt:12` and
  `OpenTrackPacket.cs:120-123` that assert it today. Staying at 48 bytes is good
  hygiene regardless, but it is not the justification for this design.
- "The mod cannot tell a Headcam stream from opentrack" is true and is a real
  benefit, but see Identification: it is a heuristic, not an authentication.

## Design constraint

Every datagram is exactly 48 bytes and is a valid OpenTrack pose. No bursts, no
size changes.

The marker is *inert*, not *invisible*. It is plainly visible in a hex dump
(`H`, `C`, `M` at offsets 0, 8, 16 of every packet) and the zero-substitution
rule below is observable by a consumer that tests a pose axis for exact zero.
What it does not do is change any value by an amount any consumer can act on.

## Wire layout

The packet is unchanged: six little-endian IEEE 754 doubles, in order
`x, y, z, yaw, pitch, roll`, 48 bytes.

The marker is six bytes carried in the low eight mantissa bits of each double.
The wire encoding is little-endian, so the low eight mantissa bits of double `i`
are wire byte `8 * i`. Stated in wire-byte terms this is endian-neutral; a
big-endian host must byte-swap as it already must for the pose itself.

| Wire offset | Carried in | Marker field |
| --- | --- | --- |
| 0  | x     | magic 0 = `0x48` (`H`) |
| 8  | y     | magic 1 = `0x43` (`C`) |
| 16 | z     | magic 2 = `0x4D` (`M`) |
| 24 | yaw   | version (high nibble) and flags (low nibble) |
| 32 | pitch | recenter counter (u8, wraps) |
| 40 | roll  | checksum |

The three magic bytes spell `HCM`, deliberately **not** `HCAM`. The trailer's
4-byte magic is `HCAM` (`OpenTrackPacket.cs:42-45`) and the two must never be
confused by a port author reading prose instead of the table. Call this one the
in-band magic and never write it as `HCAM`.

Byte 3 is `(version << 4) | flags`. Version is 1. Flags:

| Bit | Meaning |
| --- | --- |
| 0 | NEUTRAL_PENDING: the app is mid-recenter and its neutral is about to change |
| 1-3 | reserved, sender writes 0, receiver ignores |

`checksum = (0xA5 + magic0 + magic1 + magic2 + versionflags + counter) & 0xFF`

An additive checksum is deliberate. Against uniformly random low mantissa bits
any surjective 8-bit function collides at 1/256, so a CRC of the same width
would buy nothing here, and this is trivially portable. It is *not* as strong as
a CRC in general: it is blind to compensating errors across fields. That is
acceptable because the magic is validated separately and there is no adversary
to defeat (see Identification).

Offset 32 is the recenter counter in every version of this protocol. A future
version may repurpose the flag nibble but must not move the counter, because a
version-1 receiver reads offset 32 unconditionally once the magic and checksum
validate.

### Precision cost

Overwriting the low eight mantissa bits moves a value by up to 255 ULP **in
either direction**: this is an overwrite, not an OR, so a field whose low byte
was already high can decrease. The sign bit is in the high byte and is never
touched, so the sign never changes, but the magnitude can go either way.

The relative bound is `255 x 2^-52` = `5.66e-14`, attained only when the
significand is 1.0. Concretely:

- yaw at 180 degrees is in binade [128, 256), so at most `255 x 2^-45` =
  `7.25e-12` degrees.
- a position of 50 cm is in binade [32, 64), so at most `255 x 2^-47` =
  `1.81e-12` cm.

Both are far below any tracker's noise floor, and below float32 resolution,
which matters because every consumer in this repo narrows to `float` on parse
(`OpenTrackPacket.cs:83`, `:114`). That narrowing is what keeps the marker from
disturbing the C++ session's exact-equality duplicate-sample filter
(`head_tracking_session.h:187-188`).

This bound covers fields that are marked in place. Substituted fields (below)
are outside it.

### Non-normal fields

Marking a double whose exponent field is zero produces a subnormal. Marking one
whose exponent is all-ones produces something worse: `+Inf` (`7FF0...00`) with
magic `0x48` written in becomes `7FF0000000000048`, a **signalling** NaN, and
the reverse also happens (a NaN whose payload is only in the low byte becomes
exactly `+Inf` when a zero counter byte is written). Verified.

Rules, in order:

1. If any of the six fields is NaN or infinite, **send the packet unmarked**. Do
   not substitute. A tracker emitting Inf has a bug and the diagnostic must
   survive to the receiver, which already rejects NaN and Inf
   (`OpenTrackPacket.cs:76-81`). Losing the marker for those packets costs
   nothing; the stream re-identifies as soon as sane values resume.
2. Otherwise, for each field that is zero or subnormal, replace it with
   `copysign(1e-9, value)` before writing the marker byte. Use `copysign`, not
   `value < 0`, or `-0.0` maps to `+1e-9` and the Kotlin and Swift encoders
   disagree bit-for-bit on a case that occurs on every session with position
   disabled.
3. Write the six marker bytes.

`1e-9` is chosen over leaving a subnormal on the wire because a subnormal
operand can hit a microcode assist in a consumer's per-frame math. It is not
immune to going subnormal downstream: a pure decay chain `x *= 0.15` reaches
subnormal in about 3 seconds at 120 Hz. But starting from a realistic pose value
of 0.5 instead only buys ~11 more frames, so the substitution does not
meaningfully change anyone's subnormal exposure, and real smoothing filters
converge on their input rather than decaying to zero.

Exact zero is not hypothetical: a user who disables position or locks roll makes
those fields hard zeros for the whole session, which is why substitution exists
rather than skip-marking. The cost is that those users ship `1e-9` instead of
`0.0`, which any consumer testing `== 0.0` can observe. No consumer in this repo
or in opentrack does, but the claim is "no consumer we know of", not "nobody
can".

## Sender rules

1. Encode the pose as now.
2. Apply the non-normal rules above.
3. Write the six marker bytes at wire offsets 0, 8, 16, 24, 32, 40.
4. Every packet is marked. There is no burst and no unmarked steady state.
5. Set NEUTRAL_PENDING from the moment a recenter is requested until the
   capture completes.
6. **Increment the counter on every change to the wire zero, without exception.**

Rule 6 is the invariant the receiver depends on, and the current app violates it
in two places. `calibrate(recenterReason = null)`, the blind first-frame grab
(`HeadTrackingViewModel.kt:1083`), redefines the wire zero without firing
`onRecenter`, so `signalRecenter` never runs. `handleSessionReset()`
(`CalibrationCoordinator.kt:520-540`) zeroes the calibration offset silently.
Either path moves the centre with no announcement, which the receiver reads as
the player's head having moved. Both must bump the counter.

Rule 5 exists because an app-side recenter is not instantaneous. It is pending
for `DETECTION_GRACE_S` + `STILL_HOLD_S` at minimum and up to
`PENDING_MAX_WAIT_S` = 10 s (`CalibrationCoordinator.kt:86-92`), and throughout
that window the app transmits against the *previous* neutral. A mod that
identifies the stream during that window and discards its own centring would sit
on a stale neutral for up to 10 seconds and then snap. NEUTRAL_PENDING lets the
receiver hold instead.

## Receiver rules

### Identification

A packet is marked if the three magic bytes match, the version nibble is >= 1,
and the checksum is correct. A source is identified when marked packets have
been observed continuously across a window in which the pose values themselves
changed: specifically 30 consecutive marked packets during which the upper seven
bytes of at least one field differ from their value at the start of the window.

The motion requirement is not decoration. The per-packet false-positive rate
against a uniform-random source is `2^-32`, about once per 1.1 years of
continuous 120 Hz streaming, but consecutive packets from a real tracker are not
independent draws. The sources that can collide are ones whose low mantissa
bytes are *frozen*, and for those, `P(next packet also matches) = 1`. Requiring
N consecutive matches buys nothing against exactly the population that can fail.
Requiring the marker to stay valid while the values move does, because a low
byte that is an artefact of the value cannot hold still while the value changes.

Trackers that compute in float32 and widen to double cannot collide at all:
widening zeroes the low 29 mantissa bits, so byte 0 is always `0x00` and the
magic gate rejects immediately. That covers a large share of the third-party
population.

The marker is an identification hint, not authentication. There is no secret in
it and this document is public. Anything on the network that wants to be
identified as a Headcam stream will be. See Threat model.

**Identification is revocable.** It is dropped after 30 consecutive packets from
the current source that fail the marker check, and cleared immediately on a
source endpoint change. It is not sticky for the lifetime of the source. A
sticky misidentification would be unrecoverable without restarting the game.

**Identification survives silence.** A source that goes quiet for minutes and
returns keeps its identification and its counter baseline. Re-identifying from
scratch would re-arm the mod's connection auto-recenter, which would then fire
during the app's hold-still prompt and capture exactly the wrong centre, which
is the failure this whole design exists to prevent. After a loss of 10 s the app
runs its own guided recenter and bumps the counter
(`CalibrationCoordinator.kt:353-355`), so anything the receiver needs to learn
it will be told.

**Source state is a single slot**, not a table: the endpoint we are currently
consuming, plus its identification state and counter baseline. No per-source map,
because an unbounded map keyed on a remote-supplied endpoint is a memory
exhaustion vector inside a game process. Two concurrent senders on one port is
already broken today (neither receiver demuxes by source; `OpenTrackReceiver.cs:446`
uses the endpoint only for an `IsLoopback` test) and stays out of scope. To
avoid an interleaving sender making identification unreachable, a foreign packet
does not immediately clear the slot; the 30-packet revocation window covers it.

### Centre semantics

For an identified stream the receiver's centre is identity, for rotation and for
position. It is set on identification and again on every counter change.

The app has already subtracted its own neutral, so the stored centre expressed in
wire terms *is* zero. There is no pose to transmit and nothing to subtract.
Centring on the latest pose, which is what the trailer path does today
(`HeadTrackingSession.cs:137`), is only correct when the packet arrives in the
same instant the app zeroed. That is precisely the assumption that breaks when
the mod starts late. Identity is correct in both cases and removes the
double-subtract failure class (fixed once in `613a0d0`) by removing the
subtraction.

**Both centre levels must be zeroed, not just the processor.** The C# stack has
two: the receiver-level offset (`OpenTrackReceiver._offsetYaw` etc., set by the
public `Recenter()`, subtracted inside `GetLatestPose()`, cleared only by
`ResetOffset()`) and the processor-level centre (`TrackingProcessor.RecenterTo`,
`PositionProcessor.SetCenter`). A mod that has ever called `receiver.Recenter()`
carries a receiver-level bias that no processor-level identity can see or clear.
The C++ stack is worse: it centres at the receiver level *on purpose*, because
its `TrackingProcessor` centre manager overwrites rather than composes
(`head_tracking_session.h:65-72`), and `UdpReceiver` has no `ResetOffset()` at
all while its sibling `PollingUdpReceiver` does. See Implementation notes.

**Identification clears existing offsets, it does not merely suppress future
ones.** This mattered because several auto-recenter sites fired before
identification could complete. `StaticHeadTrackingCore.cs:149-154` centres on the
first frame `IsReceiving` is true, and `IsReceiving` goes true after one packet
(`OpenTrackReceiver.cs:447`), which is before a 30-packet identification window
can close. `ViewMatrixTrackingController` centres on session start
(`:310-323`), again after transition-in (`:343-363`), and again on every
`OnTrackingEnabled` (`:257-264`). `HeadTrackingSession.Update` runs
`HandleConnectionRecenter()` at line 122 before consuming the request at line
124, and `StabilizationFrames` is publicly settable (`:86`). Suppression alone
loses every one of those races. Clearing on identification wins them all,
because whatever they set is discarded when identification lands.

The mod's automatic connection recenter is additionally suppressed while a
source is identified, so it cannot re-fire later.

### The mod's own recenter hotkey

A mod-side recenter must **compose** with the app's neutral and **survive**
counter changes. It must not occupy the same slot as the stream centre.

This is a requirement, not a nicety. The app's neutral is defined against the
phone and cannot encode where the monitor is, so a player whose phone is mounted
15 degrees off-axis needs a persistent mod-side correction. If a counter change
wipes it, that correction is destroyed by every sagging stand
(`DEVICE_MOVED` fires on a 5 degree tilt held 1.5 s), every 10-second face loss,
and every display rotation, none of which the player initiated.

### Counter handling

The counter is honoured only on a packet that passes the full magic, version and
checksum check. Never read offset 32 from an unvalidated packet: on an
unidentified stream that byte is a live mantissa bit that changes nearly every
packet, which would fire a recenter per frame.

A press is a counter delta of 1 to 127 treating the byte as wrapping. Do **not**
test `!=`. UDP reorders and duplicates, and a sequence like `N-1, N, N-1, N`
around a press produces three change events under `!=`, each resetting both
interpolators and the processor smoothing and firing `OnRemoteRecenter`, which
mods act on. Under the trailer this exposure was bounded by a 500 ms burst.
Putting the counter on every packet makes it continuous, so the delta rule is
load-bearing rather than a refinement.

On a press: reset the pose and position interpolators and the processor
smoothing so the view snaps rather than blending from the pre-press drift, set
both centre levels to identity, then notify (`OnRemoteRecenter`).

While NEUTRAL_PENDING is set: do not fire the connection auto-recenter where a
mod has opted back into it (one is coming), and hold the current centre. Adopt
identity when the flag clears with the accompanying counter change.

The counter is advisory *for centring*: the centre is identity whether or not any
particular press was observed, so a missed press costs an interpolator reset
rather than a wrong centre. It is not advisory for the mod's own hotkey offset,
which is why that offset must survive counter changes rather than being cleared
by them.

## Threat model

State it plainly rather than implying a guarantee the design cannot give.

The marker is unauthenticated and this spec is public. Anyone who can send UDP
to the tracking port can present a conforming stream. Doing so lets them force
the centre to identity, suppress the mod's auto-recenter, and, because neither
receiver filters poses by source endpoint today, drive the player's view. That
last part is true right now, before any of this ships: two forged 48-byte
packets are not required, one is enough, because the receiver accepts whatever
arrives.

So this protocol does not widen the existing exposure in any way that matters,
and the honest mitigation is the one that was always needed: filter incoming
poses to a single source endpoint. That is out of scope here but should be
tracked separately.

Revocable identification (above) exists so an *accidental* misidentification
recovers on its own. It is not a defence against a deliberate one.

## Removed from this version: the presence reply

An earlier draft included an 8-byte "receiver alive" reply so the app could show
a real connection indicator. It is cut, for three reasons that only surfaced
under review:

- It is not load-bearing. Once the counter is in-band, the mod needs nothing
  from the app and the app needs nothing from the mod. The reply bought a badge.
- It converts a silent port into a self-identifying, version-disclosing
  responder. For anyone port-forwarding the tracking port for phone-over-WAN
  use, that is an internet-wide enumerable oracle for "this host runs a
  cameraunlock mod, this version, and the game is running right now". No
  amplification argument speaks to that.
- It regresses a hazard this repo has already fixed once. The C++ socket
  disables `SIO_UDP_CONNRESET` with a comment explaining that Windows surfaces
  ICMP port-unreachable as a recv error and kills throughput
  (`udp_socket.cpp:61-71`). The C# receiver has no such ioctl
  (`OpenTrackReceiver.cs:135`) and handles only `TimedOut` and `Interrupted` in
  its receive loop (`:451-470`). The moment it sends to a phone that has closed
  its socket, `ConnectionReset` starts arriving and is swallowed without even
  maintaining the disconnect counter.

Two of its rules were also unimplementable as written: "never reply to a
broadcast source" needs the datagram's *destination* address, which requires
`IP_PKTINFO` and `ReceiveMessageFrom`, and on a multi-homed host the reply's
source address is chosen by the routing table rather than by the interface the
request arrived on, so a VPN or a Hyper-V adapter silently breaks it.

If a connection indicator is wanted later it should be specified on its own,
with those three problems solved first.

## Implementation notes

New public API required:

- C++ `UdpReceiver::ResetOffset()`, to match `PollingUdpReceiver::ResetOffset()`
  (`polling_udp_receiver.cpp:184`). Without it the threaded C++ receiver cannot
  be driven to identity at all.
- A way to express "centre is identity" that also snaps. In C#,
  `TrackingProcessor.RecenterTo(TrackingPose.Zero)` sets the centre but leaves
  `_hasSmoothedValue` true, so the next `Process()` lerps
  (`TrackingProcessor.cs:98-103`); `ResetSmoothing()` is also needed, and the
  order matters. Position identity is
  `PositionProcessor.SetCenter(default(PositionData))`.
- Marker parse helpers must ship a `byte[]` overload. The `ReadOnlySpan<byte>`
  paths in `OpenTrackPacket.cs:154` are gated behind
  `NETSTANDARD2_1_OR_GREATER || NET5_0_OR_GREATER`, so net35/net40/net472/
  netstandard2.0 get nothing from them. `double.IsSubnormal` is .NET Core 3.0+,
  so the sender-side check needs `BitConverter.DoubleToInt64Bits` with manual
  exponent masking on the old targets.

Consumers to change, none of which the first draft listed:

- C#: `OpenTrackPacket.cs`, `OpenTrackReceiver.cs`, `HeadTrackingSession.cs`,
  `RemoteRecenter.cs`, `StaticHeadTrackingCore.cs:149-162` and `:247-252`,
  `ViewMatrixTrackingController.cs:180-184`, `:257-264`, `:310-323`, `:325-329`,
  `:343-363`.
- C++: `opentrack_packet.h`, `head_tracking_session.h`, plus the `.cpp` files
  that hold the counter state: `udp_receiver.cpp:292`, `:316-321`,
  `udp_receiver.h:141-143`, `polling_udp_receiver.cpp:62`, `:217-222`,
  `polling_udp_receiver.h:76`, `:126-127`.
- Rust: `bioshock-infinite-headtracking/src/opentrack.rs` and
  `bioshock-remastered-headtracking/src/opentrack.rs`. These use a wall-clock
  gap re-arm rather than a timeout counter, so the edit differs from the C# one.
- Lua: none. The Cyberpunk mod's `modules/udp.lua` reads a flag off a text frame
  produced by the vendored C++ core over TCP and never sees a packet byte. The
  additive checksum needs no Lua justification; that rationale is dropped.

Tests that will fail on day one and must be updated in the same change:

- `RecenterTrailerTests.cs:137-157` sends a non-zeroed pose `(45, 30, 15)` with a
  counter change and asserts the output is 0. That holds only under
  `RecenterTo(latest)`; under identity it becomes 45/30/15.
- `cpp/tests/session_tests.cpp:312-331` asserts `GetLastRaw().yaw == 0` and
  depends on `Recenter()` changing the receiver's offset.
  `head_tracking_session.h:72` names this test as the guard for exactly this
  refactor.

Open items found while reviewing, not caused by this protocol, worth separate
tracking: `OpenTrackReceiver.cs:13` claims the coordinate transformer is applied
at receive time, but `ReceiveLoop` never touches it and no session calls the
transformed accessor, so a configured transformer is dead code. And the C++
auto-recenter is already a hold-still settle detector keyed on packets
(`head_tracking_session.h:138-167`), not the frame-counted one the C# side still
runs, so the two implementations disagree today.

## Migration

Apps ship through the App Store and Play Store and auto-update within days. Mods
update when a user re-downloads an installer ZIP, which can be months, and every
mod pins the core as a submodule tracking `main` rather than a version. There is
no release-cycle mechanism to hang a deprecation on: the csproj is at
`1.0.0` and the repo has no tags. So the ordering is driven by the slow side.

1. Receivers accept in-band markers **and** the existing trailer. The trailer
   path switches to identity centre semantics at the same time, which is safe
   for shipped apps: they zero their output before signalling, so identity was
   always right at press time. Mark the trailer constants and
   `TryParseRecenterCounter` `[Obsolete]` in this step, since C# `public const`
   is inlined at the consumer's compile site and silent removal later breaks
   recompiles rather than loads.
2. Apps emit in-band markers on every packet and **keep** emitting the trailer.
   Android and iOS move together.
3. The trailer comes out of the apps only once the mod fleet has actually
   re-synced, measured by mod releases rather than by elapsed time. An app-side
   removal that lands before a mod re-syncs silently costs that mod its remote
   recenter with no diagnostic.
4. Receivers drop trailer parsing at a major bump, which first requires the repo
   to have versioning at all.

`PacketEncoder.kt` and `PacketEncoder.swift` are described elsewhere as mirrors
that must not diverge. They already have: Swift splits `encode` and
`encodeForBroadcast` and carries a test-reset hook, Kotlin has one `encode` that
appends the trailer for every consumer, and the recenter-reason enums differ.
Do not assume a shared implementation can be ported once.

**The USB path needs an explicit decision.** iOS's TCP transport keeps fixed
48-byte frames because `headcam-usb` framing depends on it
(`PacketEncoder.swift:13-14`, `TCPTrackingServer.swift:183`), and
`headcam-usb/src/forwarder.rs:14` forwards to UDP 4242, the same port every mod
binds. In-band marking fits that path with no framing change, so USB users would
gain remote recenter for the first time. That is a feature nobody has asked for;
decide it rather than letting it happen.

## Consequences for other trackers

- opentrack, Tobii, SmoothTrack and anything else sending plain OpenTrack are
  unaffected on the wire. They receive nothing from us, and the marker changes
  nothing they can read.
- A Headcam stream relayed through opentrack arrives with the marker destroyed,
  because opentrack resamples every axis through a 16384-bucket integer-indexed
  spline LUT and clamps the result, so the output is a function of LUT samples
  rather than of the input's low bits. There is no bypass configuration. The mod
  treats it as unidentified and, where a mod has opted back into it, falls back
  to its own auto-recenter. That was the behaviour for that chain before the
  auto-recenter became opt-in.
- A **pure byte relay** (socat, a fan-out forwarder, a router hairpin) preserves
  the marker exactly, so identification and remote recenter do survive one. The
  mod identifies the relay's endpoint. Several phones behind one relay collapse
  to a single endpoint and their counters will interleave; that configuration is
  not supported.
- opentrack's UDP tracker defaults to the same port 4242 and binds with
  `ShareAddress | ReuseAddressHint`. Co-residency on one PC is a pre-existing
  hazard this repo already documents (`udp_socket.cpp:40-48`) and this protocol
  neither improves nor worsens it.

## Conformance vector

Pose `x=1.0, y=2.0, z=3.0, yaw=10.0, pitch=-5.0, roll=0.0`, version 1, flags 0,
counter 7.

- roll is zero, so it is substituted with `+1e-9` before marking.
- byte 3 = `(1 << 4) | 0` = `0x10`.
- checksum = `(0xA5 + 0x48 + 0x43 + 0x4D + 0x10 + 0x07) & 0xFF` = `0x94`.

The 48 bytes on the wire, which is the only thing worth diffing against:

```
off 0  : 48 00 00 00 00 00 F0 3F
off 8  : 43 00 00 00 00 00 00 40
off 16 : 4D 00 00 00 00 00 08 40
off 24 : 10 00 00 00 00 00 24 40
off 32 : 07 00 00 00 00 00 14 C0
off 40 : 94 D6 26 E8 0B 2E 11 3E
```

Resulting values:

| Field | Sent | Delta from input |
| --- | --- | --- |
| x | `1.0 + 72 x 2^-52` | `+1.599e-14` |
| y | `2.0 + 67 x 2^-51` | `+2.975e-14` |
| z | `3.0 + 77 x 2^-51` | `+3.419e-14` |
| yaw | `10.0 + 16 x 2^-49` | `+2.842e-14` |
| pitch | `-(5.0 + 7 x 2^-50)` | `-6.217e-15` |
| roll | `1e-9` with low byte `0x94` | `-2.068e-25` |

Note the last two rows. pitch is negative and grows in magnitude here only
because its low byte was `0x00`; roll **decreases**, because `1e-9` has low byte
`0x95` and the marker overwrites it with a smaller value. Any test vector whose
inputs all have zero low bytes will hide that the operation is an overwrite, so
implementations should also be tested against inputs with high low bytes.

## Rejected alternatives

- **Periodic hello from the app.** Any announce packet is oversized. Even if the
  WSAEMSGSIZE cost turns out to be zero, a recurring size change in a stream
  that is otherwise fixed-width is a compatibility risk with no upside once the
  counter fits in the payload.
- **Trailer plus a receiver reply to trigger a re-burst.** Needs bidirectional
  traffic to fix what six spare bytes fix outright, and leaves the oversized
  burst in the protocol permanently. See also the reply channel's own problems
  above.
- **ICMP port-unreachable sniffing in the app.** Zero wire footprint, but the
  evidence is one-sided: errors prove nobody is listening, silence proves
  nothing, because a firewall dropping inbound traffic is indistinguishable from
  a live receiver.
