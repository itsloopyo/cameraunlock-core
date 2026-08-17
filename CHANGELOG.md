# Changelog

All notable changes to cameraunlock-core are recorded here.

This library is consumed by ~92 head-tracking mod repos and by lopari. Anything under
**BREAKING** requires a matching edit in consuming repos; each entry names what to change.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Security

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

### Fixed

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

### Added

- `SmoothingUtils.SmoothAngle(current, target, smoothing, deltaTime)` - wrap-aware angle
  smoothing. Purely additive.
- `CameraUnlock.Core.Tests/Regressions/ReviewRegressionTests.cs` - 31 tests, each
  verified to fail against the pre-fix code.

### Known - tracker pivot compensation is inverted (NOT fixed here)

`PositionProcessor` builds its pivot as `new Vec3(0, 0, +TrackerPivotForward)`, but this
library's convention is that **negative z is forward**, so the vector points at the back
of the head. Since rotation is linear, `R(-v) - (-v) = -(R(v) - v)`: the computed
artifact is the exact negation of the real one, and `pos - artifact` therefore **doubles**
the phantom translation it is meant to remove. The Headcam trackers pin the same
convention with a test ("wire +Z out the back of the head"), so the receiver and the
tracker disagree.

Not fixed in this release because the correct minimal fix depends on the wire's lateral
polarity, which is contested in the tracker code and pinned by no test on either side. If
`+x` is the user's right, only the depth term is wrong; if it is the user's left,
everything doubles. The deciding test is physical: run a 6DOF mod, lean right, and see
which way the camera goes.

Related and unfixed: the C# default `TrackerPivotForward = 0.01f` and the C++
`m_trackerPivotForward = 0.15f` are a 15x unintended divergence (a commit that lowered
the C# value to mask this very bug was never ported), and
`ViewMatrixTrackingController.cs` passes `-interpolated.Pitch` where every other call
site passes `+`. No test in either suite exercises a non-identity rotation with a
non-zero pivot.


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
