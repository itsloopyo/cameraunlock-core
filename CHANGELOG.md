# Changelog

All notable changes to cameraunlock-core are recorded here.

This library is consumed by ~92 head-tracking mod repos and by lopari. Anything under
**BREAKING** requires a matching edit in consuming repos; each entry names what to change.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added - a carried light that follows the head

`cameraunlock/effects/head_follow_light.h`, `CameraUnlock.Core.Effects` and
`CameraUnlock.Core.Unity.Effects` carry the shared half of the head-follow
light: the multiplier and its bound, the two scaling shapes, and, for Unity, the
apply-and-restore around the render pass.

Five mods had grown their own copy of this (prey, still-wakes-the-deep, repo,
resident-evil-requiem, outer-wilds) with five distinct config spellings over two
concepts - `CompensateFlashlight`, `[Torch] Enabled`, `FlashlightFollowsHead`,
`FlashlightScale`, `Multiplier` - one hardcoded multiplier that could not be
tuned, and one mod that never scaled at all. Finding the light stays per game and
always will; what moved is the number, the reasoning, the bound and the
arithmetic.

- `effects::kDefaultLightMultiplier` = 1.5, `kMaxLightMultiplier` = 5.0.
- `effects::ScaleHeadEuler` for a mod that has the pose as angles,
  `effects::ScaleHeadAngle` / `HeadFollowLightSettings.ScaleRotation` for one
  that only has the delta between the clean and the drawn basis. These are two
  different operations, not two spellings of one: they coincide only when a
  single axis is non-zero. Use whichever matches how the mod already composes
  the pose onto the camera.
- `ScaleRotation` folds the negative quaternion hemisphere before taking the
  half-angle, and derives the axis length from the vector part rather than from
  `w`. Without the fold, `q` and `-q` - the same rotation - scale to results
  180 degrees apart, and the output is unit-length and finite either way, so
  nothing downstream can catch it.
- `HeadFollowLight` and `HeadFollowLightRenderHook` for Unity.
- `reframework::kMaxFlashlightMultiplier` is now an alias of
  `effects::kMaxLightMultiplier` and stays for existing callers.

### Added - the ADS module is complete in both languages

`CameraUnlock.Core.Ads` ports `AdsMode`/`AdsModes`, `AdsFade` and
`AdsEntryPose` to C#, and `cameraunlock/ads/ads_blend.h` plus
`AdsPoseBlend` add the blend that was living in titanfall-2-headtracking. The
blend is the one that keeps ROLL out of the fade in every mode.
`ads::AdsSuspendsTracking` names the rule that only `paused` closes a mod tracking
gate.

### Added - two aim projections lifted out of titanfall-2-headtracking

`rendering/aim_ndc_projection.h` (`ProjectAimToNdc`, basis to basis) and
`rendering/world_reprojection.h` (`FrameCameras`, `ReprojectWorldPoint`). Core
now carries three projection families and the header comments say which to reach
for; `ProjectAimToNdc` is the default because it makes the fewest assumptions
about the engine.

### Added - three config concepts: `AdsMode`, `LightFollowsHead`, `LightMultiplier`

`AdsMode` was already recorded in `data/config-schema.json` under
`deliberately_unaliased` as a real cross-repo setting with no field to bind to;
the two light keys had no entry at all. All three now have fields on
`HeadTrackingConfigData` and `HeadTrackingConfig`.

Aliases are limited to the spellings actually shipped, because an alias is a
one-way door: removing one breaks every config file already on a user's disk.
`LightFollowsHead` takes `FlashlightFollowsHead` (repo) and
`CompensateFlashlight` (prey); `LightMultiplier` takes `FlashlightMultiplier`
(repo) and `FlashlightScale` (prey); `AdsMode` takes none, since titanfall-2 is
the only mod shipping the key and it spells it `AdsMode`.

The bare `Enabled` and `Multiplier` that still-wakes-the-deep and
resident-evil-requiem ship under their own `[Torch]` / `[Flashlight]` section
stay unaliased and are now recorded as such in `deliberately_unaliased`:
section-less, `Enabled` resolves to the master head-tracking switch and
`Multiplier` is as generic as `Scale`. Those two mods read their own keys.

`AdsMode` and `LightMultiplier` now log a value they could not use, matching
every neighbouring key. An out-of-range multiplier is REJECTED and the previous
value stands, rather than being clamped: running at 5 when the file says 8 is a
setting that does not do what it says.

`data/pipeline-conformance.json` gains `ads_fade_lower_ms`, `ads_fade_raise_ms`,
`light_multiplier_default` and `light_multiplier_max` in its `constants` block,
so a language port that has to restate them can pin them.

### Fixed - the ADS transition stepped the head pose on any reversal

`AdsFade` started each leg of the transition at that leg's own endpoint rather
than at the scale the interrupted leg had reached, so reversing direction moved
the pose by however far it had already travelled - up to the whole of it. The
worst case was the most common input there is: a tap of the aim button, released
a frame after it was pressed, removed a fully-applied head pose in one frame.
That is the jolt the class exists to remove.

A leg now starts from where the transition is and its duration is scaled by the
distance left, so an interrupted transition is continuous and travels at the same
rate as a whole one. The case that named this in both test suites could not fail
for it: it allowed a difference of 0.55 at a point where no implementation
returning a value in [0,1] can exceed 0.5.

`Elapsed` is also clamped at zero. The subtraction is unsigned, so a clock that
stepped backwards wrapped to an enormous elapsed and settled the transition on
the spot.

Consumers: titanfall-2-headtracking gets this on its next core pin bump. No API
change; `kLowerMs` and `kRaiseMs` are unchanged. `AdsFade.LowerMs` / `RaiseMs`
moved from `const` to `static readonly` on the C# side so a future retune cannot
leave an already-built mod on a value baked in at its compile time.

### BREAKING - the bare `Enabled` / `Enable` config keys no longer resolve

`data/config-schema.json` no longer aliases the bare spellings `Enabled` and
`Enable` onto `EnableOnStartup`, in either language.

Key matching is section-less by design, so the `[Position] Enabled` line that 28
mod repos ship in their `HeadTracking.ini` resolved to the master head-tracking
switch. `[Position] Enabled=false` - the documented way to turn 6DOF off - turned
the whole mod off instead, left position tracking enabled underneath, changed
outcome with line order when a file carried both keys, and logged "Config loaded
successfully" either way. `[Flashlight] Enabled`, `[Reticle] Enabled` and
`[Discovery] Enabled` collided the same way. Exactly one repo ships
`[General] Enabled`, and it now falls back to the shipped default (on).

To change in consuming repos: a mod whose INI names the master switch as a bare
`Enabled` must spell it `EnableOnStartup`, `AutoEnable`, `StartEnabled`,
`EnableAtStartup`, `EnabledOnStartup` or `EnableHeadTracking` - all still
accepted. Position tracking is `PositionEnabled` and its aliases. A bare
`Enabled` is now ignored, whatever section it sits in.

### Fixed - the RE Engine boundary negates z, so the forward lean leans forward

`reframework::ApplyViewSpacePositionOffset` passed the pipeline's z straight into
the camera's forward axis. Negative z is the forward lean everywhere inside the
pipeline and RE Engine's camera-local +z is forward, so a forward lean moved the
camera BACKWARDS - and onto the wrong budget, taking the generous 0.40m forward
allowance for backward travel and leaving the forward lean 0.10m.

Only the RE Engine plugins are affected; no other engine boundary goes through
this function.

### Fixed - the C++ config and memory guards

- `PluginConfig::Load` reads every number through `config::ReadFloatChecked`
  instead of `IniReader::ReadFloat`. `LocalSmoothing=0,15` read as 0.0 and passed
  every range check silently; it is now refused with a diagnostic naming the key.
  Hotkeys go through `config::IsBindableVirtualKey`, so `ToggleKey=0x230` no
  longer registers a binding `GetAsyncKeyState` can never report.
- Rotation and position sensitivity now floor at 0 rather than 0.1, so any single
  axis can be pinned. Roll could already be zeroed; yaw and pitch could not.
- `memory::SafeRead` stages through a local, so a read that faults part way
  across a page boundary leaves the destination untouched instead of half
  overwritten. `SafeWrite` cannot offer the same guarantee and now says so.
- `config::ParseFloatStrict` rejects hexadecimal and leading whitespace, which
  `TryParseConfigFloat` in the same library already did.

### Fixed - RE Engine gameplay probing

- The input-block probe bound both polarities of the same question to one check,
  so a title exposing only `get_isPlayerControllable` suppressed tracking exactly
  while the player had control. Split into separate blocked- and enabled-polarity
  checks.
- `GameFlowManager.Status` suppressed tracking on an invoke that never happened -
  an absent singleton and a faulted call both returned 0, and 0 reads as a boot
  flow state. `TryInvokeBool` / `TryInvokeInt` distinguish the two.
- `GetRenderFrame()` counts every render callback, not only gameplay ones. It
  froze the moment the gameplay gate closed, so `GetMarkerFocalLengths` served a
  menu the last gameplay frame's focal lengths for the rest of the session.
  `FrameProjection`'s validity flags now clear when no frame was projected.

### Changed - the C++ optional modules are compiled by `pixi run check`

`build-cpp` turns on `CAMERAUNLOCK_BUILD_HOOKS`, `_DISCOVERY`, `_UNREAL` and
`_REFRAMEWORK`. None of them were configured, so none were compiled, and a
compile error in the RE Engine driver reached a mod repo instead of this one.
praydog/REFramework plugin API 1.15.0 is vendored under `vendor/reframework/`
with its MIT notice so the driver builds from a clean checkout with no game and
nothing off the network; a consumer that vendors its own SDK still overrides with
`CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR`.


### BREAKING - dev pre-releases now publish the Nexus ZIP as well

`Publish-NightlyBuild` attaches both `<ModName>-dev-installer.zip` and
`<ModName>-dev-nexus.zip` to the rolling `dev` pre-release, hashes both, and
lists both under `SHA-256:` in the release notes. `pixi run package` in nearly
every mod already writes `release/<ModName>-v<Version>-nexus.zip`; only the
installer ever went up, so anyone installing by hand or through a mod manager
was stuck on the last tagged release.

The Nexus ZIP is now required, on the same terms as the installer: missing or
older than the build start is fatal rather than a silent one-asset publish.

To change in consuming repos: nothing, if `pixi run package` writes the Nexus
ZIP at the default path. Otherwise pass the new `-NexusZipPath` to point at it,
or `-NoNexusZip` for a mod that has no extract-to-game-folder layout.

### Added - the C# receiver reports its first accepted packet

`OpenTrackReceiver` now emits one latched `First tracker packet accepted from
<endpoint> (local|remote source)` through its `Log` callback, re-armed on
Start/Stop.

Its callback previously only fired for port contention and socket errors, so a
managed mod's log showed the port bound and then nothing. Every consumer had
invented its own answer to "did packets arrive" - polling `IsReceiving`, latching
inside a property getter, or in two cases nothing at all.

### Changed - receiver diagnostics default to the shared log instead of nowhere

`UdpReceiver` and `PollingUdpReceiver` now default their log sink to
`logging::Line` rather than to an empty `std::function`. Mods with their own
logger still override it via `SetLog`; mods that never open a core log get a
no-op.

Forgetting `SetLog` silently discarded the bind result and the latched
first-packet line - the two lines a "no head tracking" report turns on. Two of
five repos in one sample had missed it, with nothing to indicate the diagnostics
were going into a void.

Its first-packet line is emitted only for a packet that PARSES, and says
"First tracker packet accepted", matching the C# receiver. The latch is one-shot,
so reporting before validation let a stray keepalive or a LAN broadcast consume
it and the real tracker packet then never reported - the exact false split the
line exists to prevent.

`PollingUdpReceiver`'s bind-failure line is latched, cleared when a bind
succeeds. `Initialize()` is caller-driven and a mod that retries a busy port
calls it on a timer, so without the latch the new default sink turned a silent
retry loop into one repeated line every few seconds for the whole session.

### Fixed - em-dashes removed from source and log strings

`camera_discovery.cpp` wrote em-dashes into user-facing log lines ("... - failed"),
and 17 other files carried them in comments. Forty in total, now plain hyphens.
The house rule is fleet-wide, and the discovery ones were reaching end-user logs.

### Added - PollingUdpReceiver can report whether packets arrived

`PollingUdpReceiver` now has `SetLog()`, matching `UdpReceiver`. It logs the
bind result on `Initialize()` and one latched `First UDP packet received` line.

The threaded receiver has had both for a long time; the polling one had neither,
so a mod built on it could not answer "did a tracker packet ever reach the game"
from its log, and every such mod had to hand-roll the line. Set the sink before
`Initialize()` so the bind result is captured.

### Fixed - the log's previous session survives a relaunch

`logging::Open()` truncates (`CREATE_ALWAYS`), so a mod's log has always started
fresh per run. It now renames the outgoing file to `<name>.prev.log` first.

This matters because `EmergencyLine` and `crash_handler` write the unhandled
exception report - faulting module, address, stack frames - into that same file.
A player who crashes and relaunches to reproduce it destroyed the report before
they ever thought to send it. Consuming mods get this automatically; the only
visible change is one extra `.prev.log` beside the existing log, worth adding to
a mod's documented uninstall file list.

A rename that fails is reported in the freshly opened log rather than dropped:
`CREATE_ALWAYS` has already truncated the file by then, so a silent failure
leaves `.prev.log` holding an arbitrarily old session while the docs tell the
reader it is the previous launch. A first-ever launch has nothing to rotate and
stays quiet.

The rotation happens once per process, not once per `Open()`. A mod that honours
a "log to file" setting closes and can reopen the log inside the same run, and
rotating there would file the run in progress away as the previous generation.

Several mods added the same `MoveFileExW` immediately before `Open()` before this
landed. Those are harmless (the second rename finds no source file and the target
name is identical), so they can come out whenever the mod next bumps its
submodule rather than urgently.

### BREAKING - the HCAM trailer no longer recenters

A CENTER press signalled through the 54-byte packet trailer used to recenter the
pipeline. It no longer does anything. The trailer is still parsed, its counter is
still tracked, and `TryConsumeRecenterRequest()` / `RemoteRecenter.TryConsume()`
are still on the API - they just always report nothing.

This follows from the centre being identity by default. Headcam zeroes its own
output when the player presses CENTER, so the stream arriving at the mod is
already centred and there is nothing left for the mod to do. Acting on the
trailer as well was a second centre in series with the app's, which is the same
defect the connect-time capture had.

Ignoring the trailer is a positive requirement rather than an absence: older
Headcam builds still send it, and the receivers must not act on those either.

- **Migration**: nothing to change to keep building. Every trailer-related member
  stays: `OpenTrackPacket.TryParseRecenterCounter`,
  `OpenTrackReceiver.TryConsumeRecenterRequest`, `RemoteRecenter.TryConsume`,
  `ViewMatrixTrackingController.OnRemoteRecenter`, and C++
  `UdpReceiver` / `PollingUdpReceiver::TryConsumeRecenterRequest`,
  `HeadTrackingSession::GetRemoteRecenterCount` and `kHasRemoteRecenter`.
- **What a user loses.** A centre set with the mod's recenter hotkey is no longer
  cleared when the app re-zeroes, so after using the hotkey the player recentres
  with the hotkey rather than on the phone. A trailered press also no longer
  resets the interpolator, so the step is smoothed rather than snapped; at
  `RemoteSmoothing = 0.15` that is a few hundred ms on a phone over wifi.
- The consumption sites in `HeadTrackingSession`, `ViewMatrixTrackingController`,
  `StaticHeadTrackingCore` and the C++ session are left in place. They are the
  mechanism, and a mod supplying its own `ITrackingDataSource` can still raise a
  request; the shipped receivers simply never do.
- `UdpReceiver` still uses a trailer sighting to seed its jump-confirm gate. The
  trailer no longer recentres, but it still marks the jump to the app's new
  neutral as real, which is what that gate needs to know.
- Regression coverage: `Receiver_Trailer_NeverRaisesARecenterRequest`,
  `Receiver_Trailer_StillDeliversThePose`, `Session_Trailer_DoesNotMoveTheView`,
  `Session_Trailer_LeavesAHotkeyCentreAlone`,
  `RemoteRecenter_Trailer_NeverConsumesARequest`, and the C++ receiver tests.
  Re-arming the raise in `OpenTrackReceiver` fails four of them.


### BREAKING - the session no longer captures a center on connect

Every entry point that ran head tracking captured the incoming pose as a center
offset shortly after packets started arriving: `HeadTrackingSession` after
`StabilizationFrames` fresh frames, `ViewMatrixTrackingController` on
`BeginTrackingSession` (and again when the transition-in completed),
`StaticHeadTrackingCore` on the first connection, and the C++
`HeadTrackingSession` once the pose had been held still.

That put a SECOND center in series with the tracker's own, and the two drift
apart because each side recenters at moments the other cannot see. The cost lands
on the user:

- **opentrack, AITrack, any sender with its own Center bind and no HCAM trailer.**
  The user presses Center, the sender's output drops to zero, and the mod is
  still subtracting the pose it captured at session start - so the view parks at
  the negated drift and the user has to hit the mod's recenter hotkey as well.
  Two presses for one recenter, and no signal exists that would let the mod
  collapse its center on its own.
- **Headcam.** The trailer already re-syncs the two centers, so this was
  invisible there. It was insurance against a tracker whose origin is arbitrary
  at startup, and Headcam zeroes at tracking start, so the insurance was being
  paid for by every other sender.

Native head tracking does not work this way: with TrackIR the driver owns the
center and the game consumes the pose as absolute. The pipeline now matches that.
The center is identity until something asks for one - the user's hotkey, or a
tracker-app CENTER press arriving through the HCAM trailer.

The trailer is unaffected and still does the work no pose value can express:
resetting the interpolators so the view snaps rather than slewing across the
step, clearing smoothing history, and driving the notification.

- **Migration**: nothing to change to keep building. A mod that genuinely needs
  the old behaviour - a tracker with an arbitrary startup origin that cannot zero
  itself - opts back in:
  - `HeadTrackingSession.AutoRecenterOnConnect = true`
  - `ViewMatrixTrackingController.AutoRecenterOnConnect = true`
  - `StaticHeadTrackingCore.AutoRecenterOnConnect = true`
  - C++ `HeadTrackingSession::SetAutoRecenterOnConnect(true)`
  - `MultiPlayerTrackingManager.ApplyAutoRecenterOnConnect(true)` for every player

  Opting in reintroduces the double-center for that mod. The four sites are NOT
  interchangeable: `StaticHeadTrackingCore` has no settle window and captures on
  the first frame `IsReceiving` is true, so opting in there bakes in whatever pose
  the player holds during the intro screens.
- `HeadTrackingSession.StabilizationFrames`,
  `MultiPlayerTrackingManager.ApplyStabilizationFrames` and C++
  `SetStabilizationFrames` are unchanged but are only consulted while the opt-in
  is on. `TrackingLossHandler.StabilizationFrames` is a different property on the
  tracking-loss path and is unaffected.
- **C++ `HeadTrackingSession::HasCentered()` changes meaning by default.** It used
  to become true a second or so into every session; it now stays false until the
  player or the tracker asks for a center. A mod gating "tracking is ready" on it
  waits forever, with no compile error. Gate on `GetRotation()` succeeding instead.
- **Known limitation on the C++ `UdpReceiver`.** Its jump-confirm gate
  (`cpp/src/protocol/udp_receiver.cpp:275`) exists because a recenter and a tracker
  losing the head look identical from outside: both are a large jump followed by a
  pose that stops moving. The gate is bypassed for HCAM-trailered packets and only
  those, so an untrailered tracker-side center is held back one packet, and if the
  tracker then repeats bit-identical values it stays held until the pose changes
  again. Headcam is unaffected because it sends the trailer. This is pre-existing
  and is not addressed here.
- **This fix does not reach a mod that implements the capture itself.** Fourteen
  repos carry their own auto-recenter on top of core: `cyberpunk-2077`,
  `witcher-3`, `dorfromantik`, `subnautica`, `green-hell`, `peak`, `firewatch`,
  `gone-home`, `obra-dinn`, `eternal-afternoon`, `the-painscreek-killings`,
  `prey`, `fallout-new-vegas` and `minecraft-bedrock-edition-headtracking`.
  (This list previously counted `minecraft-head-tracking` separately; it is the
  pre-rename name of the same repository, not a second one.) Three more carry a full local port
  with the capture built in: `headlook` (C#), `fusion-360-headtracking` (Python)
  and `minecraft-java-edition-headtracking` (Java). Each needs the capture
  deleted in its own tree - there is nothing to opt into, because the correct
  behaviour is no capture at all. `fallout-new-vegas` is the worst of them: it
  captures on the very first valid packet with no settle window.
- **A drained CENTER press no longer strands an earlier hotkey centre.**
  `ViewMatrixTrackingController` discards a trailer press that lands while it is
  not applying tracking, because a latched one would anchor the next session to an
  arbitrary first packet. With a hotkey centre installed that discard left the
  centre subtracting from an already-zeroed stream and parked the view at the
  negated drift on the next session. Consuming the press now clears the centre to
  identity, which is what matches the stream the app is sending.
- New API: `HeadTrackingSession.AutoRecenterOnConnect`,
  `ViewMatrixTrackingController.AutoRecenterOnConnect`,
  `ViewMatrixTrackingController.LastTrackingPosition`,
  `StaticHeadTrackingCore.AutoRecenterOnConnect`,
  `MultiPlayerTrackingManager.ApplyAutoRecenterOnConnect(bool)`, and C++
  `HeadTrackingSession::SetAutoRecenterOnConnect` / `IsAutoRecenterOnConnect`.
- Regression coverage: the opentrack sequence (uncentred stream, then the stream
  drops to zero with no trailer) runs end to end in `HeadTrackingSessionTests`
  for rotation and position, in `ViewMatrixTrackingControllerTests`, and as
  `TestTrackerSideCenterLandsAtZero` in `session_tests.cpp`. Default-off and
  opt-in tests cover all four sites, including `StaticHeadTrackingCore`, which
  had no behavioural coverage of its own before. The hotkey and trailer paths,
  now the only ways a centre is ever created, gained assertions that they move
  the view and that the captured centre is where it should be, rather than only
  that a flag was drained.


### BREAKING - every Unity position boundary now takes the offset in the pipeline's own convention

`PositionProcessor` has always treated NEGATIVE z as the forward lean, and its
asymmetric clamp is built on that: `[-LimitZ, +LimitZBack]` puts the generous
0.40m on the forward side. The Unity helpers that hand that offset to a camera
did not agree with it, or with each other:

- `ViewMatrixModifier.ApplyHeadRotation(..., positionOffset)` composes in view
  space, where Unity looks down -z, so it already matched the pipeline.
- `ViewMatrixModifier.ApplyHeadRotationDecomposed(..., positionOffset)` places
  the camera through `transform.rotation`, where +z is forward, so the same
  offset moved the camera the OTHER way.
- `PositionApplicator.ToCameraLocalWorld` / `ToHorizonLockedWorld` project into
  world space and had the same +z-forward reading.
- `SplitInjectionCameraTracker.Apply` (shipped to IL2CPP mods as source) writes
  `transform.position` through the camera's own basis, so it read +z as forward
  too.

So a mod that toggled `WorldSpaceYaw` at runtime flipped its lean direction
mid-session, and mods compensated by setting `PositionSettings.InvertZ = true`.
That inverts in step 2 of the processor, ahead of the clamp, which transposes the
asymmetric budget: the forward lean gets the 0.10m backward allowance and the
backward lean gets the 0.40m forward one. Direction still looks right, so it
survives testing - the symptom is only that leaning in barely moves while pulling
back moves a lot.

The flip now happens once, inside the decomposed path and inside
`PositionApplicator`, where the pipeline meets the engine. Every offset-taking
Unity API takes the offset exactly as `PositionProcessor.Process` returns it.

- **Migration**: a mod feeding these APIs must stop pre-flipping z.
  - Passing `invertZ: true` to `PositionSettings` purely to correct the
    direction: change it to `false`. That also restores the intended
    forward/backward travel, which was mirrored before.
  - Negating z by hand after `Process` (Green Hell does this at the
    `ToHorizonLockedWorld` call): drop the negation. An `-X` negation beside it
    is unrelated and stays.
  - Mods that never set `invertZ` and compose the view matrix themselves in view
    space (peak, subnautica) were already correct and need no change.
  - IL2CPP mods driving `SplitInjectionCameraTracker` (sons-of-the-forest) take
    the source change on their next build, so the same `invertZ` flip applies.
  - A mod whose `InvertPositionZ` is a persisted user setting defaulting to
    `true` (firewatch, gone-home, eternal-afternoon) needs the key re-defaulted
    AND renamed, or every existing config file keeps the old value and inverts.
- `InvertZ` keeps its real meaning: a tracker whose z runs the other way.
- Regression coverage lands with it in `PositionOffsetConventionTests`: the two
  apply paths must place the camera at the same world point for the same offset.

### BREAKING - `Copy-SharedBundle` no longer moves your submodule pointer

`Copy-SharedBundle` fast-forwarded `cameraunlock-core` to `origin/main` on every
call, so `pixi run package` silently moved the submodule working tree out from
under the developer. The artifact was then built against a core commit the mod's
history does not record, and `git status` grew an unexplained
` M cameraunlock-core` that a later scripted commit could sweep up. Three repos
hit exactly that during a fleet sweep.

- Refreshing is **opt-in** now: pass `-RefreshCore`, or call
  `Update-CameraUnlockCoreToRemoteTip` directly and commit the pointer.
- `-NoRefresh` is kept, accepted and ignored, because most of the fleet's
  `package-release.ps1` passes it and removing a parameter would break those
  callers for nothing. Passing both it and `-RefreshCore` throws rather than
  silently picking one.
- **What this costs**: the old default was a real guarantee - a fix to an install
  body, `find-game.ps1` or `games.json` reached a mod's users on that mod's next
  release with no pointer bump. That is gone. A mod ships whatever core commit it
  pins, so a stale pin ships a stale bundle. `Copy-SharedBundle` therefore always
  reports the core commit it bundled and warns when that commit is behind the
  `origin/main` the checkout last saw. It never fetches and never fails the run.
- **Migration**: nothing to change to keep building. To keep the old behaviour on
  a specific mod, pass `-RefreshCore`. Note that a mod still pinned to a core
  commit *before* this change keeps the old auto-refresh until its pointer is
  bumped.

### Fixed (continued)

- **`quat4.h` shadowed `angle_utils.h` and hard-broke every `/W4 /WX` consumer.**
  `Quat4::FromYawPitchRoll` and `ToEulerYXZ` declared function-local
  `constexpr float kDegToRad` / `kRadToDeg`, which live in `cameraunlock::math`
  alongside `angle_utils.h`'s namespace-scope `constexpr double` constants of the
  same names. Neither header is wrong alone; together they are MSVC C4459, and for
  the mods compiling core headers with warnings-as-errors that escalated to
  `error C2220` and no binary at all. Seven repos were confirmed broken. Renamed
  to `kDegToRadF` / `kRadToDegF`; the emitted maths is bit-identical.
  `cameraunlock_headers_strict` now pulls all 45 self-contained public headers
  into one TU at `/W4 /WX` so this class of defect cannot recur - every build in
  this repo had included those two headers separately, which is why nothing here
  saw it while it was fatal downstream.
- **A recenter press that arrived while tracking was off corrupted the next
  session.** The receive thread raises the request whenever a trailer press lands,
  but `ViewMatrixTrackingController` only consumed it inside its
  `enabled && IsReceiving` branch and nothing else cleared the latch. A press made
  with tracking toggled off survived indefinitely and fired on the first frame of
  the next session - where it cancelled the stabilise-then-recenter
  `BeginTrackingSession` had just armed (`Recenter()` clears
  `_recenterOnStabilize`) and anchored the whole session to whichever raw pose
  arrived first. Drained in the not-applying path now.
  `OnRemoteRecenter`'s doc also read as forbidding any mod-side consume, which is
  stricter than the truth: a consume strictly ordered *after* `ProcessFrame` is
  safe, and several mods legitimately do it.

### Added (continued)

- `docs/porting-the-pipeline.md` - the invariants a non-C#/C++ port of the
  pipeline has to reproduce, each with a check that fails on the wrong
  implementation. Written because an audit of four independent ports (two Lua, one
  Rust, one Python) found all four had the HCAM trailer right and three of four
  had the interpolator's angle handling wrong in the same way. The difference was
  that HCAM has a spec with a conformance vector and the pipeline had nothing.
- `cameraunlock_headers_strict` - compile-only CMake target, all self-contained
  public headers in one TU at the strictest warning level the fleet uses. `/WX` is
  deliberately NOT applied to the `cameraunlock` library target, because mods build
  core from source via `add_subdirectory` and that would turn any future compiler's
  new warning into a fleet-wide outage.
- `Write-CoreBundleProvenance` - reports which core commit a release bundle came
  from, and warns when it is behind.
- `data/games.json`: `persona-5-royal`, recovered from a mod's vendored submodule
  where it had been added locally and never made it upstream.

### BREAKING - tracker pivot compensation was inverted, and is now opt-in

`PositionProcessor` built its pivot vector as `+z`, but negative z is forward
throughout this library. Rotation is linear, so `R(-v) - (-v) == -(R(v) - v)`: the
computed artifact was the exact negation of the real one, and `pos - artifact`
therefore **added** it, doubling the phantom translation it was written to remove.
Confirmed from both ends of the wire - the Headcam trackers pin the same convention
with a unit test ("wire +Z out the back of the head").

- `TrackerPivotForward` now defaults to **0** (compensation off) in BOTH ports,
  replacing `0.01f` in C# and `0.15f` in C++. That 15x split existed because the C#
  value had been lowered to mask this very bug and was never ported back. The correct
  arm length is not a property of this library: the Headcam Android app already
  applies its own eye-anchor offset (3.5cm up, 2.5cm forward) while iOS applies none.
  It must be measured per tracker app.
- **Migration**: a mod that set `TrackerPivotForward` explicitly still compiles, but the
  value now *subtracts* the arc rather than adding it and needs re-tuning. A mod on the
  default now gets no compensation at all - 6DOF will feel cleaner, because the phantom
  translation that accompanied head rotation is gone, but it will feel different.
- `PositionProcessor.Process`'s second parameter is renamed `physicalRotationQ` /
  `physical_rotation_q` and **must** now be the centered rotation from BEFORE per-axis
  sensitivity and inversion. Both `HeadTrackingSession` ports and
  `ViewMatrixTrackingController` do this internally. A mod calling `Process` directly
  should pass `TrackingProcessor.GetSmoothedRotation(...)`.
- Both ports now clamp before smoothing as well as after, so the smoothing state can no
  longer wind up outside the limits and pin the output at a limit for hundreds of ms
  after the head has returned.

### BREAKING - the C++ pipeline now matches the C# one

The two ports disagreed on both the centring and the smoothing, so the same tracker
produced different camera motion in a native mod and a Unity mod - and each port's
comment asserted the opposite rationale to the other's.

- Centring moved from component-wise Euler subtraction to quaternion composition.
  `CenterOffsetManager` gains `ApplyOffsetQuat` and `ComposeAdditionalOffset` (the C#
  names). The Euler `ApplyOffset` remains for callers that genuinely want per-axis trim
  and now documents that it is **not** equivalent for a compound centre.
- Smoothing moved from quaternion SLERP to per-axis Euler. Slerp follows the great
  circle, and that arc's Euler decomposition carries a roll term for compound movement -
  diagonal head motion rolled the horizon in native mods and produced exactly zero roll
  in Unity mods.
- Added `math::SmoothAngle` and a `float` overload of `ShortestAngleDelta`, matching C#.
- **Added** `TrackingProcessor::ResetSmoothing()`, `UdpReceiver::ResetOffset()`.
- **Migration**: `HeadTrackingSession::Recenter()` now calls `ResetSmoothing()` instead
  of `Reset()`, so a mod-configured centre offset survives a recenter. A mod that called
  `Reset()` purely to clear smoothing should switch to `ResetSmoothing()`.

### BREAKING - `PositionSettings` (C++) gained `limit_y_down`

C# has always had an asymmetric vertical limit; C++ could not express one, so a config
ported across silently widened the downward budget into player-body clipping. The
constructor is now the full asymmetric form with a `Symmetric()` factory, mirroring what
C# did and for the same reason: two adjacent arities let a stale positional call rebind a
slot with no compiler signal.

```cpp
// was: 9 required floats
PositionSettings(sx, sy, sz, limX, limY, limZ, limZBack, local, remote, [inverts]);
// now: 10 required floats, limitYDown inserted after limitY
PositionSettings(sx, sy, sz, limX, limY, limYDown, limZ, limZBack, local, remote, [inverts]);
// or, exactly the old argument list:
PositionSettings::Symmetric(sx, sy, sz, limX, limY, limZ, limZBack, local, remote, [inverts]);
```

- **Migration**: change `PositionSettings foo(...)` to
  `PositionSettings foo = PositionSettings::Symmetric(...)`. Affects
  abzu, prey, assetto-corsa-rally, assetto-corsa-evo, assassins-creed-unity, mixtape and
  witcher-3.

### BREAKING - camera-discovery calibration removed

The whole path was dead: `Advance()` had no `Phase::Calibrating` case, so
`RunCalibrating` was unreachable, `m_calibPulsing` was never set, and
`GetCalibrationPulse()` always returned inactive. Removed `CalibrationPulse`,
`GetCalibrationPulse()`, `Phase::Calibrating` (remaining enumerators renumber), the
`s_calib*` statics, and `DiscoveryConfig::calibration_deg` / `pulse_frames` /
`settle_frames`.

- **Migration**: witcher-3 is the only consumer; delete those three config assignments.

### BREAKING - `AxisConfig.Target` and `TargetAxis` removed

Nothing read `Target` - `MappingConfig` routes by `Source` - it was not serialised, and a
fleet grep found no mod referencing it outside vendored copies of this repo.

### BREAKING - install/uninstall templates

Every mod must re-sync `install.cmd` and `uninstall.cmd`: the `:parse_args` block
changed in all 15 scripts, and it is contractually byte-identical across them.

- A game path containing `!` was mangled and rejected with **exit 2**, which a launcher
  reads as an unrecoverable malformed argument. The cause is delayed expansion stripping
  `!` from the expanded text of the whole line - `%~1` included, so testing `%~1` instead
  is not a fix. `:parse_args` now runs with delayed expansion off.
- UE4SS mods were **permanently uninstallable** (no handler existed at all). They now
  work, but `uninstall.cmd`'s CONFIG BLOCK needs `UE4_BINARIES_RELDIR` set to the same
  value `install.cmd` uses.
- A blank required CONFIG BLOCK name is now a hard exit 1 at install time, because a
  blank one could resolve `rmdir /s /q` to the whole UE4SS `Mods` tree and
  `Remove-Item -Recurse` to the game root.
- BepInEx wrong-arch replacement now prompts unless `/y`. Automation must pass `/y`.
- `package-bepinex-mod.ps1` returns one object with `GithubZip` / `NexusZip` instead of
  bare stdout lines. Any mod-side `release.ps1` capturing its stdout must be updated.

### BREAKING - PowerShell

- `Remove-OldDoorstopFiles` no longer deletes anything unless the state file records
  `framework.installed_by_us`. It was removing `winhttp.dll` and `version.dll` - BepInEx
  5's own proxy and Ultimate ASI Loader's - so installing a Cecil mod alongside a BepInEx
  mod stopped BepInEx loading entirely. `-Force` restores the old behaviour.
- `Invoke-DevDeployCecil` gained `-CleanDoorstop` (default off); doorstop cleanup no
  longer runs by default.
- `Invoke-HeadTrackingPatch` now throws on a patcher compile failure instead of returning
  `Success = $false`.
- `New-ScreenCenterPatcher` emits `ScreenCenterPatcher_<sanitised-marker>_<hash8>`; a mod
  hardcoding `[ScreenCenterPatcher]::...` after calling it will break. The hash suffix is
  load-bearing, not cosmetic: sanitising alone is not injective, so `cul.center` and
  `cul-center` collided on one generated type and the second marker silently got the
  first's patcher.
- `Get-ScreenCenterPatcherCode`'s `-TypeName` is now mandatory. Its old default was the
  bare `ScreenCenterPatcher`, which handed a direct caller exactly the colliding name.
- `Get-BepInExPluginsPath` / `Get-MelonLoaderModsPath` are re-homed to
  `GamePathDetection.psm1` and re-exported from `ModLoaderSetup.psm1`, so no import
  changes - but the separator is now consistently `\`.

### BREAKING - Unity (source-compatible, binary-breaking: recompile)

`CameraCallbackLifecycle.RegisterPreCull` / `RegisterPreRender` /
`RegisterWillRenderCanvases` gained an optional `UnityEngine.Object owner = null`. Mods
should start passing their plugin MonoBehaviour: without an owner, a destroyed
subscriber's throw aborts Unity's multicast invocation and silently stops **every later
subscriber** on `Camera.onPreCull`, including other mods' hooks.

Newly `[Obsolete]`, each with a correct replacement:
`CanvasCompensation.RepositionChildren` / `RepositionElement(7-arg)`,
`CrosshairUtility.OffsetByScreenPixels(2-arg)`,
`DecoupledMovementHelper.ApplyDecoupled` / `ApplyDecoupledFadeOut` /
`ResetCameraYawOffset` (Euler forms),
`OpenTrackReceiver.GetLatestPoseTransformed`.

### Fixed

- **The coordinate transformer was never applied.** `OpenTrackReceiver`'s class doc says
  it transforms at receive time, but the only consumer was `GetLatestPoseTransformed()`,
  which is not on `ITrackingDataSource` and was called from nowhere in the library. A mod
  that passed one got `HasTransformer == true`, a camera yawing the wrong way, and no
  error. `GetLatestPose()` now applies it.
- **Gimbal lock discarded the rotation.** `ToEulerYXZ`'s lock branches reused the general
  yaw formula, whose two `atan2` arguments are identically zero at exactly 90 degrees of
  pitch - so it returned `atan2(0, 0) = 0`. Testing `sinPitch >= 1` also never fired,
  because float error leaves it a shade under 1 even for a quaternion built at exactly 90
  (measured 0.9998), so the degenerate branch ran anyway. Both the formula and the
  threshold are fixed, in both ports.
- **Pose, position and timestamp are published atomically.** Each field was individually
  volatile but the group was not, so a reader could pair packet N's timestamp with packet
  N-1's position - which the interpolator reads as a new sample, latches one behind, then
  skips the real value because it arrives on an unchanged timestamp.
- **`AxisConfig.MaxInputRange` defaulted to 180** while head-tracking input lives within
  roughly +/-30 degrees, pinning every non-linear curve to its near-zero end: the shipped
  `Competitive` preset was 0.60x overall, *slower* than the `Default` it claims to beat.
  Now 45, and values at or below zero are rejected (0 made the normalisation `0/0`, and
  NaN survives both clamps, so one at-rest frame poisoned the axis permanently).
- **`ApplyDeadzone` had a hard step** whenever `DeadzoneMax <= DeadzoneMin`, which is the
  default: with `DeadzoneMin = 5`, an input of 4.99 gave 0 and 5.01 gave 5.01, so a 0.02
  degree movement popped the camera 5 degrees.
- **Harmony transpilers silently disabled every patch in the mod.** Replacing a matched
  instruction with a new `CodeInstruction` dropped its labels, and an unresolvable label
  aborts the whole `PatchAll` - so the camera hook never applied either. Operands were
  also compared by reference, which Mono does not guarantee across modules.
- **The HUD marched off screen.** `CanvasCompensation.RepositionChildren` was a
  read-modify-write with no stored original, so a per-frame call accumulated without
  bound; with any roll it compounded into a full revolution every 72 frames.
- **A skipped `OnPostRender` compounded forever.** `LookAimDecoupledHook` mutated the
  camera transform in `OnPreCull` and restored only in `OnPostRender`, which does not run
  when a camera culls without rendering - one skip made the tracked rotation the next
  frame's clean base.
- **Pattern scanning crashed on packed games.** Every scan walked the full `SizeOfImage`
  with raw dereferences and no `VirtualQuery` or SEH; Denuvo/VMProtect titles map sections
  `PAGE_NOACCESS` until first execution, so the game closed to desktop during the loading
  screen with no log line.
- **The crash handler could deadlock the game.** It called loader-lock APIs once per stack
  frame, so a crash that already held that lock froze the process with no dump - strictly
  worse than the crash it replaced. It also could not report `STACK_OVERFLOW`, the one
  case it explicitly enumerated, because it put ~2.5KB on an exhausted stack.
- **A crash report could be written into the player's save file.** `file_log` read the
  handle outside its mutex, and Win32 recycles handle values.
- **The DX12 overlay had four independent faults**, including resetting a command
  allocator every `Present` with no fence anywhere (explicit UB, presenting as
  intermittent driver TDRs) and leaking a device reference plus two heaps per presented
  frame on any initialisation failure.
- **Silent install failures.** Every deploy `copy` discarded its exit status and printed
  "Deployed" regardless, with `>nul` hiding "Access is denied" - so an unelevated install
  into Program Files reported success and exited 0.
- **`ConvertFrom-Json -AsHashtable` does not exist on PowerShell 5.1**, which is what
  every install-time entry point runs. `Get-ModLoaderState` was unusable for any installed
  game, and the binding error was re-reported as "State file is corrupt: delete it
  manually" - pointing users at a valid file.
- Plus: the shim installer enshrining its own DLL as the user's backup; the ASI uninstall
  deleting ReShade's proxy DLLs; `Update-VendoredLoader` silently downgrading on a
  back-ported release; `ea_search_paths` being dead data (100% detection failure for
  Dragon Age Inquisition); `-LiteralPath` throughout (a game path containing `[` reported
  as not installed); a BOM breaking UE4SS's `mods.txt` parser; tag-name script injection
  in the release workflow; and three CI gates that could pass while failing.
- **The DX12 overlay's fence state was shared across three threads with no lock.** One
  non-atomic `UINT64` counter and one auto-reset event served `Present` (render thread),
  `ResizeBuffers` (whichever thread resizes) and `Remove` (unload thread). Two threads in
  the counter's read-modify-write hand out the same fence value, and D3D12 requires
  monotonically increasing signals per queue, so the wait returns while the GPU is still
  reading the resources the caller is about to free. With one event, whichever waiter the
  OS wakes consumes the other's signal and the loser blocks on an `INFINITE` wait - a
  permanent game hang. Everything is now serialised under one mutex, and both waits are
  bounded above Windows' 2s TDR delay so a removed device can no longer hang the render
  thread.
- **The overlay headers were never compiled.** All three bodies live behind
  `CAMERAUNLOCK_DX*_OVERLAY_IMPLEMENTATION` and their dependencies are vendored per mod,
  so no build here had ever expanded them - the blind spot that shipped both the fence
  race and `MH_DisableHook(nullptr)` (which disables *every* MinHook hook in the process,
  other mods' included). `cameraunlock_overlay_compile` typechecks all three on every
  build against minimal stubs.
- **`Find-UE4BinariesPath` returned the engine's tool folder.** It checked
  `Engine\Binaries\Win64` before scanning for the project folder, and every UE install
  ships that directory - so any game whose project folder is not named after its install
  folder (Palworld ships `Pal`, Hogwarts Legacy ships `Phoenix`) resolved to
  CrashReportClient's folder instead of the one holding the game exe, which is the only
  place UE4SS's `dwmapi.dll` can sit.
- **`Get-ReleaseVersionKey` ranked releases by the first digit run in the tag.**
  `BepInEx_x64_5.4.22.0` scored `64.0` and `UE4SS_v3.0.1` scored `4.0`, both outranking
  every real version. Bounded in practice only because `-VersionPrefix` usually made each
  candidate set homogeneous - and it defaults to empty.
- **Two patch markers could share one generated patcher.** `New-ScreenCenterPatcher`
  flattened the marker through `[^A-Za-z0-9_]`, which is not injective: `cul.center` and
  `cul-center` both became `ScreenCenterPatcher_cul_center`, and the by-name lookup handed
  the second marker a patcher hard-coded with the first's string. The marker is the only
  thing preventing a double patch.
- **Release notes shipped with a BOM.** `Out-File -Encoding utf8` means UTF-8 *with* BOM
  on PowerShell 5.1, and the file goes straight to `gh release create --notes-file`, so
  every published release body opened with a literal mojibake prefix. The first-release
  branch used `Set-Content` with no encoding at all, mangling non-ASCII commit subjects
  through the system ANSI codepage.
- **`uninstall.ps1` aimed `Remove-Item -Recurse -Force` at the wrong tree.** Its
  containment check used `[System.IO.Path]::GetFullPath`, which resolves relative paths
  against `[Environment]::CurrentDirectory` - and that does not follow `Set-Location`. So
  running it from the game folder with `-GamePath .` validated the real game folder
  (`Test-Path` goes through the provider) while the containment check resolved `.` to the
  process start directory. Root and target resolved consistently wrong, so the check
  passed.
- **The release workflow's Lopari predicate was not Lopari's.** It keyed on
  `install_strategy -eq 'External'`; lopari.app's `update-metadata.mjs`, which actually
  stamps pins, filters on `public === true` plus a release carrying an `-installer.zip`
  and never reads `install_strategy`. A dev-only catalog entry therefore demanded the
  sync token and then polled for a pin that is never stamped, running the 10 minute
  deadline into a hard failure.
- Plus: `HotkeyPoller`'s `noexcept` move operations restarting a thread (a failed spawn is
  `std::terminate`, killing the game with no diagnostic); the DX11/DX9 overlays copying a
  `std::function` per frame on the render thread inside a hooked `Present`, and a second
  `Install()` silently stealing the process-wide hooks from the first;
  `CameraCallbackLifecycle`'s preCull/preRender wrappers invoking the static callback
  unguarded, so `ForceCleanupAll` mid-dispatch would abort Unity's whole invocation list
  and take every other mod's camera hook with it; `:remove_UE4SS` deleting two of the five
  files it laid down while reporting success; `mods.txt` deregistration matching a name
  prefix (so removing `HeadTracking` deregistered `HeadTracking Extras`); and
  `sync-discord-announce` exiting 0 from a dry run that found a dozen unreconciled repos.
- **`ForceCleanupAll` left the previous owner believing it still owned the slot.**
  It is static and cannot reach instances, so after `A.Register()` ->
  `ForceCleanupAll()` -> `B.Register()`, `A.Dispose()` unregistered **B's** callback
  while `B.HasPreCull` went on reporting true. A mod shutting down silently killed a
  live overlay's camera hook - the exact failure `CameraCallbackLifecycle` exists to
  prevent. Ownership is now only real while the static slot still names that instance.
- **`SplitInjectionCameraTracker` dropped real cameras.** `IsTokenStart` treated any
  lowercase predecessor as a word boundary, so `ui` matched mid-word and `yuicamera` /
  `EquiviewCamera` lost head tracking - Yui, Rui, Sui and Gui are ordinary romanised
  names. The tail-word rule was also a bare prefix test, so `cam` swallowed camp, camo,
  campaign and camshaft. The file ships to IL2CPP mods as source and belonged to no
  `.csproj`, so nothing here compiled it; it is now linked into the Unity test project
  with 38 tests over the filter.
- **`Invoke-DevDeployCecil` reported success after a failed patch** for two of the three
  result shapes in use: `-is [hashtable]` is false for both `[pscustomobject]` and
  `[ordered]` (an `OrderedDictionary`), and `PSObject.Properties` does not see a
  Hashtable's keys at all. Matched on shape now.

### Added

- `SmoothingUtils.SmoothAngle` (C#) and `math::SmoothAngle` (C++) - wrap-aware angle
  smoothing around the +/-180 seam.
- `UI/CanvasChildrenCompensator` - the stateful replacement for
  `CanvasCompensation.RepositionChildren`.
- `CameraLifecycleManager.ShouldLogNow()` and `CameraRecheckInterval`.
- `AimDecouplingState.GetAimDirectionForViewMatrix` / `GetScreenOffsetForViewMatrix`, and
  `CameraRotationComposer.GetTrackingOnlyRotationMatchingAdditive`, so view-matrix mods
  and the additive composer each have a correctly-named counterpart.
- `PositionInterpolator::Update(raw, is_new_sample, delta_time)` (C++), mirroring
  `PoseInterpolator` - the position path was using the raw receive timestamp and so
  estimated half the true sample interval for a phone resending at 60Hz off a 30Hz sensor.
- Test coverage for the pivot path (which had none in either port, in any test, which is
  why the sign error survived) and for gimbal lock, plus fake-null modelling in the Unity
  stubs so the destroyed-object defect class is reachable from a test at all.
- `cameraunlock_overlay_compile` - a compile-only CMake target that expands all three
  overlay implementation blocks against minimal ImGui/kiero/MinHook stubs. It builds
  under the existing `pixi run build-cpp`, so the headers can no longer ship untypechecked.

### Earlier in this cycle

The first pass of the same review, already on this branch. Kept separate because the
entries above supersede several of its notes - in particular the pivot defect it
recorded as *Known* is now fixed.

#### Security

- **The reusable release workflow no longer interpolates the git tag into a shell.**
  `${{ github.ref_name }}` was substituted textually into three `run:` blocks before
  pwsh parsed them, so a tag name containing a double quote closed the assignment and
  ran the remainder as code - in a job holding `contents: write`, the Discord webhook
  and the Lopari token, reachable by anyone who can push a tag. The tag-format check
  cannot help: the injected code runs on the assignment line, before the regex. The tag
  now arrives through `env:` as `$env:REF_NAME`. The same change landed in
  `scripts/templates/discord-announce-step.yml` and `catalog-pin-dispatch-step.yml`,
  which are copied into self-hosted release workflows - **mod repos that do not call the
  reusable workflow need re-syncing to pick this up.**
- **`actions/checkout` is pinned to a commit SHA** rather than the mutable `@v6` tag,
  per the repo's own action-pinning rule. It was the only third-party action here.
- **The GitHub token is no longer attached to file downloads** in `ModLoaderSetup.psm1`.
  DirectUrl mode is documented for non-GitHub sources, so a dev with `GH_TOKEN` exported
  sent their PAT to Thunderstore; and a `browser_download_url` redirects to a presigned
  S3 URL that answers 400 when a second auth mechanism rides along.

#### Fixed

No public signature or default constant changes. Behaviour changes are called out
individually.

- **Aim projection no longer mirrors behind the camera.** `ScreenOffsetCalculator`
  guarded its perspective divide with `|az| < epsilon`, but `az = cos(pitch)cos(yaw)`
  goes *negative* past 90 degrees, sailed through, and flipped the sign: a 100 degree
  yaw reported a reticle just right of centre while the aim was behind the player.
  Reachable with a sensitivity multiplier on an ordinary head turn. Both projection
  paths now return the centred offset the degenerate case already documented.
  **Behaviour change:** yaw beyond 90 degrees returns `(0,0)` where it previously
  returned a mirrored non-zero offset. The C++ `ProjectCrosshair` had the same defect in
  a different form - it clamped `bDepth`, pinning the marker to the opposite edge - and
  now reports `valid = false`, matching `ProjectAimQuatHorPlus`.
- **Rotation smoothing and interpolation take the shortest arc.** Yaw and roll come out
  of `ToEulerYXZ` in (-180, 180] and were lerped as plain scalars, so a 1 degree head
  movement across the seam travelled -359 degrees and swung the camera the long way
  round. Added `SmoothingUtils.SmoothAngle` (additive) and used it in `TrackingProcessor`
  and `PoseInterpolator`. Identical to the old arithmetic for every non-wrapping input.
- **`OpenTrackReceiver` hardened against races and hostile packets.** Six defects, most
  remotely reachable since the socket binds `INADDR_ANY`: the recenter trailer was
  honoured on packets whose pose failed validation (centring on the pre-press drift -
  the double-subtract failure by another route); `_udpClient` was null-checked and then
  re-read for `Receive`, giving an unhandled background-thread exception; `RetryLoop`
  and `Start` could publish a socket after `Stop` had run, wedging the receiver with
  `_isRunning` true while every status surface reported healthy; `_isConnected` was set
  by unvalidated datagrams; the timestamp was published before the position, so a reader
  could pair a new timestamp with the previous packet's position; and neither `Start` nor
  `Stop` cleared the timestamp, so a closed receiver reported fresh data.
  **Behaviour change:** `Start(port)` returns `false` when already running on a
  *different* port, instead of reporting success and staying on the old one.
- **Profile I/O.** A CR/LF in any field split into extra `key=value` lines on read,
  truncating a multi-line description and letting a setting value inject `IsReadOnly`,
  which bricks the profile - it can no longer be saved. Out-of-range enums parsed
  "successfully" through `Enum.Parse` and silently killed an axis. `MaxInputRange` was
  live but never serialised, so curve feel changed between sessions. Non-float settings
  were written with the current culture and read back invariant. Writes were not atomic.
  And the profile name is a path component that was never validated. The on-disk format
  is unchanged and old files still parse.
- **Config boundary.** `TryParseInt` is pinned to `InvariantCulture` like its neighbours,
  and `UdpPort` is range-checked (1-65535) instead of reaching `UdpClient`'s constructor
  and killing the plugin at `Awake()` with a socket stack trace naming no config key.
- **Unity: features that must survive a pause.** The view-matrix transition-*out* ran on
  `Time.deltaTime`, which is zero at `timeScale 0` - so with
  `SceneGameStateDetector.DisableWhenPaused` (the default) the fade could never complete
  and the pause menu rendered through a view matrix still rotated by whatever the head
  was doing. Hotkey cooldowns ran on `Time.time` and went dead in menus. Both now use
  unscaled time. Transition-*in* deliberately stays scaled: it runs alongside the
  pipeline, and its completion captures the recentre.
- **Unity: dead guards and a per-frame allocation.** `TemporaryRotationScope` compared
  `baseRotation == default`, which Unity implements as `Dot(a,b) > 0.999999f` - false
  even for `default` itself, so the guard never fired and an unset rotation reached the
  transform as the zero quaternion. `CameraLifecycleManager` gated `OnCameraLost()`
  behind its *logging* throttle, skipping subclass cleanup on a second loss inside five
  seconds. `CameraSpeedInfluenceModifier` subtracted raw angles, reading a turn past
  0/360 as a 359-degree-per-frame slew. `GameUIFinder` lowered each keyword inside a
  whole-scene scan loop.
- **Harmony transpilers.** Replacing a matched instruction with a new `CodeInstruction`
  dropped its labels and exception blocks; branch targets land on exactly the kind of
  instruction these patterns match, and an unresolvable label aborts the whole
  `PatchAll` - so every other patch in the mod, camera hook included, silently never
  applied. Operands were also compared with `ReferenceEquals`, which Mono does not
  guarantee across modules, giving a patch that matched nothing and reported success.
- **C++ memory safety.** Four guards in code that runs inside the player's game process:
  `camera_discovery` derived pitch/roll offsets by subtracting from the yaw offset with
  no lower bound (writing head-tracking floats over the vtable pointer), and cleared its
  hook bookkeeping while probe hooks were still installed (calling through a null
  trampoline on the next rescan); `rtti_vtable` read vfunc entries before bounds-checking
  them and ignored its own documented cap; `ue_runtime` divided by consumer-supplied
  layout fields without checking for zero.
- **C++ crash handler no longer suppresses the host's.** It discarded the filter it
  displaced, so installing the mod silently disabled the game's own crash reporting.
- **C++ hotkey callbacks no longer fire under the lock**, which hard-deadlocked the
  polling thread for any callback that rebinds a key. `IsValidHotkeyCode` also accepts
  letters and digits, which the chord-binding convention documented but it rejected.
- **C++/C# parity.** The position cm-to-m conversion validated the scaled value in C++
  and the raw value in C#, so a band of hostile inputs was accepted by native mods and
  rejected by Unity mods. The recenter re-arm window was 500 ms (threaded) / 1000 ms
  (polling) against a documented ~5 s and a C# implementation of 5000 ms, so a Wi-Fi
  stall inside a recenter burst fired a second, spurious recentre.
- **Install templates: the Cecil pristine-backup guard.** `install-cecil.cmd` and
  `uninstall.cmd` both *documented* the `PATCH_MARKER` guard in their CONFIG BLOCK and
  neither implemented it - only the shared bodies did. Without it, a missing `.original`
  leads to the patched assembly being captured as the backup, then restored over the
  game's and the backup deleted: `TypeLoadException` on launch with nothing to recover
  from short of a Steam file verify. Ported from the bodies. **`PATCH_MARKER` in
  `install-cecil.cmd` is now empty by default**, so an unedited copy fails loudly rather
  than capturing a corrupt backup. **Every Cecil mod repo needs re-syncing.**
- **Release tooling.** `ConvertFrom-Json -AsHashtable` is PowerShell 6+, but every
  install-time entry point runs Windows PowerShell 5.1 - so `Get-ModLoaderState` was
  unusable for any installed game and the binding error was re-reported as "State file is
  corrupt: delete it manually". `New-ChangelogFromCommits` spliced raw commit subjects
  into a `-replace` *replacement* string, where `$&` expands to the whole match and
  inlined the entire changelog into the release body. `Get-CsprojVersion` returned the
  capture untrimmed, failing the CI tag-match gate with two identical-looking versions.
- **Three gates that could pass while failing.** `validate-manifest.mjs` treated a
  manifest declaring zero sources as valid; `package-bepinex-mod.ps1` skipped a missing
  `install.cmd` and published a ZIP with no installer; `sync-discord-announce.mjs` always
  exited 0, including on `YAML_INVALID_REVERTED`.

#### Added

- `SmoothingUtils.SmoothAngle(current, target, smoothing, deltaTime)` - wrap-aware angle
  smoothing. Purely additive.
- `CameraUnlock.Core.Tests/Regressions/ReviewRegressionTests.cs` - 31 tests, each
  verified to fail against the pre-fix code.

#### Known - tracker pivot compensation is inverted

Recorded here when it was still unfixed. **Now fixed** - see the pivot entry at the
top of this release. Kept only so the reasoning trail is intact: the defect was found
by review, deliberately deferred once because the correct fix depended on a wire
convention that no test pinned on either side, then confirmed from the tracker repos
and fixed.

### BREAKING - smoothing is now two user parameters

The single smoothing factor and its hidden 0.15 baseline floor are gone. Smoothing is
now two user-configurable values selected per connection from the packet source address:

| Parameter | Default | Applies to |
|-----------|---------|------------|
| `LocalSmoothing` | `0.0` | Tracker running on the machine running the mod (loopback) |
| `RemoteSmoothing` | `0.15` | Tracker on a remote network device |

Selection goes through `SmoothingUtils.GetEffectiveSmoothing(local, remote, isRemote)`.
No call site picks the value itself. Local users now get the lightest available response by
default instead of the old floored 0.15: smoothing 0.0 drops the added smoothing lag to
nothing, leaving only the frame interpolation floor at speed 50, a flat 20 ms time constant.
It is the lightest setting, not zero latency. The old 0.15 floor mapped to speed 42.5, a
23.5 ms time constant, so the default saves about 3.5 ms of lag on a local tracker.

- **Removed** `SmoothingUtils.BaselineSmoothing` / `math::kBaselineSmoothing`.
- **Removed** the single-argument `GetEffectiveSmoothing(float)`.
- **Removed** `TrackingProcessor.SmoothingFactor` and `PositionSettings.Smoothing`.
- **Added** `LocalSmoothing`, `RemoteSmoothing` and `IsRemoteConnection` to
  `TrackingProcessor`, `PositionProcessor` / `PositionSettings`, and their C++ ports.

### BREAKING - `PositionSettings` has a single constructor

`PositionSettings` previously had two constructors: a 9-float symmetric one and a
10-float asymmetric one. When `Smoothing` became `LocalSmoothing` + `RemoteSmoothing`,
the asymmetric overload landed on 9 required floats too, which is exactly the arity the
pre-migration asymmetric constructor took. Every stale positional call still compiled and
silently rebound one slot to the left, turning a forward-lean limit into a smoothing
value with no compiler signal:

```
new PositionSettings(1,1,1, 0.30, 0.20, 0.08, 0.40, 0.10, 0.35)   // old asymmetric call
  -> limitYDown=0.20, limitZ=0.08, limitZBack=0.40, local=0.10, remote=0.35
```

There is now exactly ONE constructor, taking the full asymmetric form with 10 required
floats, plus a named factory for the symmetric case:

```csharp
new PositionSettings(sx, sy, sz, limitX, limitY, limitYDown, limitZ, limitZBack,
                     localSmoothing, remoteSmoothing, [invertX, invertY, invertZ]);

PositionSettings.Symmetric(sx, sy, sz, limitX, limitY, limitZ, limitZBack,
                           localSmoothing, remoteSmoothing, [invertX, invertY, invertZ]);
```

Both old shapes (8-float symmetric and 9-float asymmetric) now fail with CS7036.

- **Migration**: a 9-float call becomes `PositionSettings.Symmetric(...)`; a 10-float call
  compiles unchanged.
- **Added** `PositionSettings.WithSmoothing(local, remote)`, returning a copy with only
  the smoothing pair replaced.

### BREAKING - `SmoothedRotationState.Update` renamed

`Update(Quaternion, float)` is now `UpdateWithEffectiveSmoothing(Quaternion, float)`.

The signature never changed but the meaning of the float did: it used to be the raw user
smoothing value, floored internally and snapped below 0.001, and it is now the
already-selected effective value, used verbatim. Every call site compiled unchanged and
behaved differently, and a caller still passing its raw single smoothing value would get
it applied verbatim and never consult the connection flag, leaving `RemoteSmoothing` as
dead config. This was the only public surface in the migration that changed semantics
silently, so it was renamed to force every call site to be looked at.

- **Migration**: pass the result of
  `SmoothingUtils.GetEffectiveSmoothing(local, remote, isRemote)`, never a raw setting.

### BREAKING - position Z box clamp was transposed

`PositionProcessor` (C#) clamped z to `[-LimitZBack, +LimitZ]`. Negative z is the forward
lean, so the correct range is `[-LimitZ, +LimitZBack]`. With the shipped defaults
(`LimitZ = 0.40` forward, `LimitZBack = 0.10` back) forward lean was being given the tight
0.10 backward budget and backward lean the generous 0.40. The C++ side was already
correct, so the two languages disagreed.

- **Migration**: any mod that compensated by passing its limitZ / limitZBack arguments
  swapped must now un-swap them.

### BREAKING - `ITrackingDataSource` and `ITrackingProcessor` gained members

- `ITrackingDataSource` gained `IsDataFresh(int)`, `GetLatestPosition()` and
  `TryConsumeRecenterRequest()`, so the interface now covers everything the per-frame
  pipeline needs and the owners can be driven by a test double.
- `ITrackingProcessor` gained `LocalSmoothing`, `RemoteSmoothing` and
  `IsRemoteConnection`. A processor that cannot be told the locality of the current
  connection is stuck on `LocalSmoothing` forever; declaring them makes that invariant
  compiler-enforceable.
- `HeadTrackingSession`, `ViewMatrixTrackingController` and `RemoteRecenter.TryConsume`
  now take `ITrackingDataSource` instead of the concrete `OpenTrackReceiver`.
  `MultiPlayerTrackingManager` and `StaticHeadTrackingCore` still construct their own
  receiver because they own its lifecycle.
- **Migration**: any mod class implementing either interface must add the new members.
  Passing an `OpenTrackReceiver` to the widened constructors needs no change.

### Fixed

- **Settings assignment no longer clobbers smoothing.** The two smoothing values live
  inside `PositionSettings`, which is assigned wholesale, so `ApplySmoothing` followed by
  `ApplyPositionSettings` silently reset position smoothing to the struct's defaults while
  rotation smoothing kept the configured value. `HeadTrackingSession` and
  `MultiPlayerTrackingManager` (and the C++ `HeadTrackingSession`) now own the pair as
  their own state and recompose it onto every settings assignment, so call order no longer
  matters in either direction. `Update()` re-asserts it, so a write straight to a
  caller-held processor is corrected rather than persisting.
- **Session smoothing getters report the effective state.** `HeadTrackingSession`'s
  getters read the session's own values rather than the rotation processor's copy, so they
  no longer lie after a clobber. Added `HeadTrackingSession.PositionSettings`,
  `MultiPlayerTrackingManager.LocalSmoothing` / `.RemoteSmoothing`, and the C++
  `GetLocalSmoothing()` / `GetRemoteSmoothing()` / `SetPositionSettings()` /
  `GetPositionSettings()`.
- **C++ `IsRemoteConnection()` detection no longer requires `const`.** The
  `kHasRemoteConnection` trait probed through `std::declval<const T&>()` while its sibling
  `HasRecenterRequest` probed through `std::declval<T&>()`. An adapter whose
  `IsRemoteConnection()` merely lacked `const` failed detection with zero diagnostic: the
  propagation block compiled away, the session reported local forever and every remote
  user silently got `0.0` instead of `0.15`. The `static_assert(kHasRemoteConnection)`
  pattern did not catch it because the sibling trait was looser. Now uses the non-const
  form, which accepts both spellings.
- **NaN in config no longer poisons the camera permanently.** `ConfigParsingUtils.TryParseFloat`
  accepted `"NaN"`, `"Infinity"` and `"-Infinity"`, and the `[0, 1]` clamp on smoothing was
  NaN-transparent because every comparison against NaN is false. A `localsmoothing=NaN`
  reached `exp()` and the smoothed pose stayed NaN for the rest of the session.
  `TryParseFloat` now rejects non-finite values at the config boundary, which fixes the
  sensitivity and reticle-colour keys at the same time, and the smoothing keys fall back
  to their documented per-key defaults with a warning. A configured `0.0` still survives
  untouched: this is validation, never a floor.
- **Loopback classification now agrees across languages.** C++ `IsRemoteAddress` compared
  against `INADDR_LOOPBACK` exactly, so only `127.0.0.1` counted as local while C# used
  `IPAddress.IsLoopback`, which covers all of `127.0.0.0/8`. Pointing a tracker at
  `127.0.0.2` made C++ mods call a same-machine sender remote and C# mods call it local.
  C++ now matches the whole `127.0.0.0/8` block, asserted against the same address set in
  both suites.
- **The connection flag is only set by packets that pass validation.** `OpenTrackReceiver`
  classified any datagram of at least the minimum size, including one that failed pose
  parsing, so a malformed packet from a LAN host could flip a local user onto
  `RemoteSmoothing`. It is now set inside the successful-parse branch, and reset in
  `Start()` and `Stop()` so a previous session's locality cannot leak into a new one.
- **`RemoteRecenter.TryConsume` propagates the connection flag.** It is the helper aimed
  at mods that hand-wire the pipeline, which is exactly the population with no other
  component that owns the flag. It now pushes `IsRemoteConnection` onto both processors
  every call, not only when a recenter is pending.
- **`SmoothedEulerState` no longer snaps at low smoothing.** The `smoothing < 0.001` snap
  was unreachable while `GetEffectiveSmoothing` floored at `0.15`. With the floor gone and
  `LocalSmoothing` defaulting to `0.0` it would have become the default path for every
  local user, producing exactly the stepped output this migration removed from
  `SmoothedRotationState`, the C++ `CalculateSmoothingFactor` and the mods.
- **`StaticHeadTrackingCore` comment now matches behaviour.** `Initialize()` overwrites
  both smoothing values from config, so the claim that they could be set beforehand
  "without silently going nowhere" was false. Documented that config wins at startup and
  the setters are for use after `Initialize()`. `Shutdown()` now resets both to their
  defaults instead of leaving the previous session's config readable through the static
  getters.

### Added

- `DiscoveryConfig.forced_vfunc_index` (C++, default `-1`), pinning the per-frame camera
  update to a known vfunc index on the most-specific candidate instead of letting the
  call-count heuristic choose. The heuristic mis-picks when the real update has not begun
  firing inside the probe window, latching onto a high-frequency getter, and the mod then
  "never starts" on that launch. Default `-1` is auto-discovery, byte-for-byte the previous
  ranking; no consumer that leaves it alone can observe a difference. This was previously
  carried as an uncommitted hand-edit inside one mod's vendored copy of this repo, which
  would have been destroyed by the next submodule bump.
- `discovery/probe_selection.h`: `SelectProbeSlot`, `ProbeDecision`, `ProbeSelection`,
  `ForcedIndexIsSelectable`, `ProbeDecisionPossible`, `ForcedSlotWindow`,
  `kForcedSlotWindowFrames`, `kForcedSlotMinCallsPerWindow`,
  `kForcedWaitLogIntervalFrames`. The probe window's decision extracted from
  `CameraDiscovery::RunProbing` as a free function with no MinHook, live-process or vtable
  dependency, so both the heuristic and the forced path are unit tested for the first time.
  Built into the base `cameraunlock` library rather than the optional discovery module,
  which needs MinHook and cannot be linked into the test executable.
  - A forced slot is committed only once it reaches `kForcedSlotMinCallsPerWindow` calls
    inside one `kForcedSlotWindowFrames` window: a rate, not a running total. A total
    cannot tell a running camera update from a vfunc that has fired occasionally for an
    hour. The wait is deliberately unbounded by `probe_frames` and logs progress every
    `kForcedWaitLogIntervalFrames`.
  - A forced index that does not exist on the class actually found is logged as an error
    once, at the moment the vfunc count first becomes known, and then ignored in favour of
    auto-discovery. It is **not** reported as `Phase::Failed`. Failure from probing means
    "no camera activity, try again later", and several consumers respond by tearing
    discovery down and rescanning ~10s later; a permanent misconfiguration answered with
    `Failed` would put them in a rescan loop for the process lifetime. Out of range is
    reachable without author error, because `FindVtableFromRTTI` truncates `vfunc_count` at
    the first entry that falls outside the module.
- `CameraUnlock.Core.Unity.Tests`, a test assembly for the Unity half of the library. The
  shipped UnityEngine assemblies cannot be used in a test host because nearly every member
  of `Quaternion`, `Matrix4x4`, `Time` and `Camera` is an extern into the native player, so
  the classes under test are compiled from source against checked-in managed stubs. This
  gives `ViewMatrixTrackingController` and `SmoothedRotationState` executable coverage for
  the first time; the controller is where a silent connection-flag propagation bug already
  hid once.
- C++ coverage for the smoothing model, which previously had none: the selection function,
  both `CalculateSmoothingFactor` overloads, the default constants, `PositionSettings`
  smoothing fields, the asymmetric Z clamp and the loopback classifier, plus session-level
  tests for connection-flag propagation, a live local/remote/local switch, non-const
  `IsRemoteConnection()` detection, graceful degradation without it, and settings/smoothing
  order independence.
- Test doubles implementing `ITrackingDataSource` in both test assemblies, so a source
  reporting a REMOTE connection can be driven. Every previous connection assertion was
  `Assert.False`, because a receiver bound to a UDP port can only be fed from loopback.

### Notes

- `PoseInterpolator.MaxExtrapolationFraction` keeps its `0.5` default. Removing the
  smoothing floor does not let extrapolation overshoot through: extrapolation happens in
  the interpolator, upstream of the stage that applies smoothing, and measured overshoot is
  identical at `0.0` and `0.15`. The speed clamp at `FrameInterpolationSpeed = 50` is what
  damps the pipeline, and it applies at every smoothing value. See
  `ExtrapolationSmoothingIndependenceTests`.
- The retired `smoothing` / `smoothingfactor` config key is NOT migrated into the new keys.
  The old value carried a hidden `0.15` floor, so the number in an existing config does not
  mean what it used to and copying it across would be a guess. It now emits a one-time
  warning naming both replacements instead of vanishing silently.

### Added - `cameraunlock::os` module and EXE path resolution

`cameraunlock/os/module_paths.h` provides `SelfModuleDirectory(HMODULE = nullptr)`,
`HostExeDirectory()`, their `Narrow` (ANSI) counterparts, and the two testable
primitives underneath them, `DirectoryOf` and `NarrowToAnsi`.

Every C++ mod hand-rolled `GetModuleFileName` plus a last-separator split, at
eight different correctness levels. Three failures are handled here once:
the buffer grows until the name fits (a fixed `MAX_PATH` turns a deep install
path into a dormant mod), a separator-less path is refused rather than becoming
`\HeadTracking.ini` at the root of the current drive, and ANSI narrowing refuses
best-fit mapping rather than naming a different directory that exists.

To change in consuming repos: nothing. Replacing a local copy is optional.

### Added - `cameraunlock::config` value guards

`cameraunlock/config/value_guards.h` provides `SanitizeSmoothing`,
`SanitizeSensitivity`, `SanitizePositionLimit`, `IsBindableVirtualKey`,
`ParseFloatStrict`, `ReadRawValue`, `ReadFloatChecked` and
`WarnRetiredSmoothingKey`. Each takes the mod's own printf-style log sink, so
the diagnostic keeps the mod's prefix; a null sink still corrects the value.

`ReadRawValue` strips inline comments before parsing and `ParseFloatStrict`
requires the whole token, which is what catches `LocalSmoothing=0,15` - a
European decimal comma that `strtod` reads as a valid `0.0`. The retired
`Smoothing` warning was copy-pasted into 56 repos; it is one function now.

They build on `math::SanitizeFinite` rather than reimplementing it.

### Added - `cameraunlock::memory::SafeRead` / `SafeWrite`

`cameraunlock/memory/safe_memory.h` provides SEH-guarded `SafeRead<T>`,
`SafeWrite<T>`, `SafeReadU8`, `AccessViolationFilter`, and counting overloads
that take the call site's own `std::atomic<uint64_t>` fault counter.

Only `EXCEPTION_ACCESS_VIOLATION` is handled - a breakpoint, a stack overflow
or a C++ exception travelling through keeps unwinding to whoever owns it. That
is what separates these from a blanket swallow. The core's only previous
guarded reads were UE-shaped and lived in `unreal/ue_runtime.h`, so every
non-Unreal mod wrote its own, and none had an 8-bit read.

Windows-only: the header `#error`s elsewhere rather than degrading.

### Added - game-window discovery outside the REFramework target

`cameraunlock/os/game_window.h` provides `FindGameWindow()` and
`CenterGameWindowOnce(WindowLogFn)` in the always-on `cameraunlock` target.
The routine is the one that already lived in `src/reframework/game_window.cpp`
(pid filter, `IsWindowVisible`, `GW_OWNER`, a 200px floor,
`MONITOR_DEFAULTTONEAREST`, `rcWork` centring); it was unusable by the seven
non-REFramework mods that re-implemented it only because it sat behind
`CAMERAUNLOCK_BUILD_REFRAMEWORK` and logged through `reframework::Log`.

`reframework::CenterGameWindowOnce()` keeps its name and behaviour and is now a
forwarder. `cameraunlock_reframework` links `cameraunlock`.

To change in consuming repos: nothing.

### Added - MinHook is vendored

`vendor/minhook` is a verbatim mirror of MinHook `c3fcafdc10146beb5919319d0683e44e3c30d537`
(v1.3.4, 2025-03-28). `CAMERAUNLOCK_BUILD_HOOKS=ON` now provides the `minhook`
target itself when the consumer has not already defined one; a consumer that
defines its own still wins, unchanged.

The fleet provisioned MinHook five incompatible ways across three versions. Six
of eleven repos in one group hit the network at CMake configure time, including
on the release job; one pinned a mutable tag. `portal-2-headtracking` already
preferred a `cameraunlock-core/vendor/minhook` directory that did not exist, so
its branch never fired.

MinHook declares `cmake_minimum_required(VERSION 3.0...3.5)` and CMake 4 removed
compatibility below 3.5, so the `add_subdirectory` is wrapped in a scoped
`CMAKE_POLICY_VERSION_MINIMUM 3.5`.

### Added - `input::VK::PageUp` / `input::VK::PageDown`

`0x21` and `0x22` were missing from the `input::VK` table and from
`VirtualKeyToString`, while `IsValidHotkeyCode` already accepted them. Two of
the three fleet-standard nav-cluster bindings were therefore only expressible as
raw numbers, and a config dump printed `Unknown` for a key the user had pressed.
