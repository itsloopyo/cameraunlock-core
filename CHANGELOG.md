# Changelog

All notable changes to cameraunlock-core are recorded here.

This library is consumed by ~92 head-tracking mod repos and by lopari. Anything under
**BREAKING** requires a matching edit in consuming repos; each entry names what to change.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added - a lock on the game and a whole-session wrapper for background tests

`IsolatedGameTest.psm1` had launch, input, capture, proof and stop, and every mod wrote the rest
round it by hand: 37 mods carry a `.lab/isolated.ps1`, and nothing stopped two sessions taking one
game. In Project Zomboid three runs started in the same second on a hand-made lock folder, and
each put back a save another had played in. All of this is new functions and new named
parameters. No existing call changes.

- `Enter-GameRig` / `Exit-GameRig` / `Get-GameRig`: one lock per game, in
  `%LOCALAPPDATA%\CameraUnlock\rig\<process name>`, outside every repo. Taken and released inside
  a named mutex, with an owner record (name, process id, when, what for), a queue served oldest
  first, release only by the taker, and a take-over only when the taker's process has gone, the
  game is not running and the taker left nothing to restore. `-WholeGpu` holds the graphics card
  against every rig, for work with no game such as an image generation run. `-Token` takes the
  lock back in a later process of the same session.
- `Invoke-IsolatedGameSession`, and its halves `Start-IsolatedGameSession` /
  `Stop-IsolatedGameSession` for a session over several processes (`Get-IsolatedGameSession`,
  `Save-IsolatedGameSession`): lock, save, prepare, test port, pose sender, launch, the caller's
  block, stop, collect, restore, release, on every way out, the game not starting included.
- `Save-GameTestState -Folders`, restored as a robocopy mirror that never empties the folder
  first, and `Restore-GameTestState -RetrySeconds` for a file the stopped game still holds.
  `-Files` is no longer mandatory, so a save can be folders alone.
- `Set-ModTestPort -IniPath -Port`: the `UdpPort` rewrite, every other byte kept.
- `Wait-GameLogLine -Path -Match -TimeoutSeconds`.
- `Start-GameProcessSampler` / `Stop-GameProcessSampler`: private bytes, working set, dedicated
  video memory and processor time to a CSV (`powershell/Sample-GameProcess.ps1`).
- `Invoke-GameInput` writes a session back to its `SessionFile` after each script, for a session
  that has one. A session object built by hand has none and plays as before.
- `data/isolated-input.json`: `mod_hosts.jvm`, supported and proven in project-zomboid, and
  `input_paths.glfw`, unsupported. `Assert-IsolatedInputCovers` takes `-ModHost jvm`.
- `pixi run test-powershell-game-rig`, part of `test-powershell`.

`docs/isolated-input.md` has the lock, the wrapper and the script a mod's `.lab/isolated.ps1`
comes down to.

### Added - CenterWindowInWorkArea, a window centre that can be called after every placement

`cameraunlock/os/game_window.h` gains `constexpr int CenteredOrigin(int workStart, int workLength,
int windowLength)`, the origin that centres a window on one axis of a work area, and
`bool CenterWindowInWorkArea(HWND hwnd, WindowLogFn log)`, which centres the window it is given in
the work area of the monitor it is on. It keeps no once-per-process latch and does not raise or
activate the window, so a mod calls it from wherever the game places its window: at creation, on a
change of windowed resolution, on the way back from fullscreen. It leaves a minimised, maximised
or captionless window alone, and one wider or taller than the work area, and says nothing when the
window is already centred. C# has the same pair as `CameraUnlock.Core.Os.GameWindow`.

`CenterGameWindowOnce` is unchanged: it still runs once per process, raises the window, and leaves
a window that fills the work area on either axis where it is.

### Added - conformance checks for a pipeline port and a long [Unreleased]

`scripts/conformance.ps1` gains two checks, tested by `pixi run test-conformance-checks`.

- `pipeline-port` fails a repo that carries a port of the tracking pipeline in Java, Kotlin, Lua,
  Python or Rust (a file that names OpenTrack, opens a UDP socket and reads from it, or a file
  that names a sample interval and extrapolates) unless `pixi run test` runs
  `scripts/pipeline-vectors/run-vectors.mjs`. Where a `CMakeLists.txt` of the repo links
  `cameraunlock`, or a `build.rs` compiles from `cameraunlock-core/cpp/`, it fails whether the
  vectors run or not, quoting `docs/porting-the-pipeline.md`: link core and delete the port.
  On 2026-10-07 it fails six repos checked out beside this core: beamng-drive-headtracking and
  lopari (no vectors), euro-truck-simulator-2-headtracking (no pixi.toml), and
  bioshock-remastered-headtracking, cyberpunk-2077-headtracking and
  project-zomboid-headtracking (a port beside native code that builds core).
- `changelog-unreleased` warns when `CHANGELOG.md`'s `[Unreleased]` holds more than 50
  top-level bullets. A release keeps a non-empty `[Unreleased]` as written and reads no commit,
  so every bullet becomes one version's entry. Five repos draw it today, at 197, 66, 61, 60
  and 51 bullets.
### Added - CameraUnlockCore.dll, the C interface for a host that is not C++ or C#

`cpp/include/cameraunlock/c/cameraunlock.h` and the CMake option `CAMERAUNLOCK_BUILD_C` (off by
default, on in `pixi run build-cpp`): the receiver, the session, the lean clamp, the zoom
compensation, the aim modes and the lean handover behind one call a frame, the config owner over
rows a host describes at run time, the hotkey poller as bits a host takes, and the file log.
`docs/c-interface.md` is the contract. Nothing calls back into the host and nothing joins a thread
at process exit. project-zomboid-headtracking, a Java agent, is the first host it was written for.

For it, added and changing no caller: `HeadTrackingSession::ResetTransientState`,
`camera::ScaleWideAngleForZoom` (an angle past 90 degrees keeps its side),
`config::HeadTrackingConfigTableFrom` (the concepts as any container, over a given defaults
instance), `ConfigTable::Default` (a non-global concept's own default as text), an `EnumCodec`
constructor from a vector, and `LeanClamp::kMinimumLean` made public.

### Changed - the C# receiver holds a jump back as the C++ one does

`OpenTrackReceiver` now runs every pose through `PoseJumpGate`: a step of more than 8 degrees is
held one packet and dropped when the next packet repeats it bit for bit, which is what a tracker
that has lost the head sends. A turn that continues is delayed by one packet at its start and no
more. A CENTER press in the HCAM trailer passes at once. `FrozenPacketCount` counts what was
refused. The C++ gate moved out of `UdpReceiver` into `cameraunlock::PoseJumpGate` with no change
to what it publishes; `data/fixtures/pose-jump-gate/cases.tsv` holds the two to the same answers
and `confirm_jump_degrees` joins the conformance constants. A C# test that sends one large jump
and expects it published needs a second packet after it.

### Added - Application.runInBackground in the IL2CPP stubs

`csharp/stubs/il2cpp/UnityEngineCoreModuleStubs.cs` gains `UnityEngine.Application` with the static
`runInBackground`, read off Sons of the Forest's `BepInEx/interop/UnityEngine.CoreModule.dll`, so an
IL2CPP mod's Debug build can keep the game running behind other windows for a background test.

### Changed - Titanfall 2 aim mode key row (Insert, Shift+Alt+U)

`data/config-format.json` `per_game` lists `TrueFreeLookKey` for titanfall-2-headtracking. Left Ctrl
toggles crouch in the game and leaves the player crouched after release, so the aim mode chord is on
Shift+Alt. Insert and U are free.

### Added - the C# aim marker and the camera's share of the lean, for a Unity shooter

`IMGUIReticle` gains `ReticleStyle.Cross`, a plus-shaped crosshair with an outline, and `Opacity`,
applied at draw time so a fade does not rebuild the texture: together they are the aim marker of
free look with a marker for a C# mod, which has no `AimMarker` renderer. `ViewMatrixTrackingController`
gains `CameraLeanShare`, called in the render hook with the clamped lean and answering the share
the view carries, so a mod can hand the rest to its rig through `LeanHandover`. The Forest is the
first mod on both.

### Fixed - isolated input never hands the game the real cursor or real raw input

In The Forest (Unity 5.6, Rewired) the person's mouse, moved in another window, turned the
game's view during a background test. `GetCursorPos` answered the real cursor until a `cursor`
command had placed one; it now answers the middle of the game window until then. A real keyboard
or mouse event read through `GetRawInputData` or `GetRawInputBuffer`, which a window registered
with `RIDEV_INPUTSINK` is sent in the background, is emptied, and the log says so the first time.
Measured after the change: the view held still for 45 s with no scripted input while the machine
was in use, where the run before it had pitched 19 degrees unasked.

### Added - isolated input drives an SDL2 game; Red Eclipse's aim mode key

`data/isolated-input.json` records `sdl` as supported through raw input: Red Eclipse reads all
its input through `SDL2.dll`, SDL2 reads raw input and window messages, and the existing path
drove the game with nothing pushed into SDL's event queue (`docs/isolated-input.md`, Measured).
`Assert-IsolatedInputCovers` no longer warns on a game that loads `sdl2.dll`.

`data/config-format.json` `per_game` gains red-eclipse-headtracking's `TrueFreeLookKey`,
measured in game on 2026-10-04: the game binds Insert to addbot and left Ctrl is a dash that
moves the player, so the aim mode is on `Shift+Alt+U` alone.

### Fixed - isolated input: a held mouse button was not in the key state

- A `mouse` command now also holds `VK_LBUTTON`, `VK_RBUTTON` or `VK_MBUTTON` in the
  state `GetAsyncKeyState`, `GetKeyState` and `GetKeyboardState` answer from, as
  Windows does for a real button. A Plague Tale: Innocence raised its sling for a
  scripted right button only once it did.
- `Invoke-GameInput` retries the move of the command file when it lands on the
  mod's read of it, instead of failing the script.

### Fixed - isolated input: a game that goes by WM_NCACTIVATE took no scripted input

Deus Ex: Mankind Divided reads a `WM_INPUT` only while its application object reports itself
active, and that state follows `WM_NCACTIVATE`. Behind other windows it had been told it was
inactive, so every scripted key and click reached its window procedure and was dropped. The
game window's procedure no longer sees a `WM_NCACTIVATE` that deactivates it (the frame still
gets it, through `DefWindowProc`), and is posted one that activates it when the window is
found, beside the `WM_ACTIVATEAPP`, `WM_ACTIVATE` and `WM_SETFOCUS` it already got. Proven in
the game: `docs/isolated-input.md`, Measured.

### Added - Deus Ex: Mankind Divided's hotkey lists

`data/config-format.json` `per_game` gains the four key lists of
deus-ex-mankind-divided-headtracking, measured in game on 2026-10-04: left Ctrl toggles crouch,
so the chords use Shift+Alt, and the game acts on Y and H, so the toggle has J and yaw mode T.

### Added - REFramework: the head rotation the camera got, for a light that follows the head

`cameraunlock::reframework::GetAppliedHeadRotation(yaw, pitch, roll)` returns the rotation the
camera pipeline applied this frame, in degrees: the processed pose after the stock sights share
and the zoom factor. It returns false on a frame that applied no tracking. A carried light that
follows the head scales this pose, so in stock sights the beam eases onto the aim with the view
as the sights come up. `PluginMod::GetProcessedRotation` is the pose before both, and a light
that scaled it stayed turned with the head while the view had gone back to the aim (measured in
Resident Evil Requiem).

To change in consuming repos: an REFramework mod whose light follows the head reads
`GetAppliedHeadRotation` where it read `PluginMod::GetProcessedRotation`.

### Added - a game or mod can move the scripted cursor in isolated input

`cameraunlock::dev::LetGameMoveScriptedCursor(true)` makes `SetCursorPos` move the script's
cursor (what `GetCursorPos` answers, with its `WM_MOUSEMOVE`) once a `cursor` command has placed
it. The real cursor still does not move. Off by default, where `SetCursorPos` does nothing as
before. For a mod that places the cursor itself: The Ascent draws its crosshair as the mouse
cursor, and the mod rests the cursor on the point the shot lands.

### Added - scripted XInput controllers in isolated input

A test can now play a controller, and so a second local player. The command file takes
`pad <n> <button> <down|up|tap>`, `pad <n> stick <left|right> <x> <y>` and
`pad <n> trigger <left|right> <pull>`, and `cameraunlock/dev/isolated_input.h` answers
`XInputGetState` from them in whichever XInput DLL the game has loaded. A controller is plugged
in from its first command on; the real controllers are not read. The pure half is
`cameraunlock/dev/gamepad_state.h`. Proven in The Ascent, where it joined and steered player
two of a couch co-op game. `data/isolated-input.json` marks `xinput` supported.

To change in consuming repos: nothing. Rebuild the host DLL
(`pixi run build-isolated-input-host`) for a C# mod to pick it up.

### Fixed - a scripted Ctrl, Shift or Alt is held as its left-hand key too

`cameraunlock/dev/isolated_input.h` held only `VK_CONTROL`, `VK_SHIFT` or `VK_MENU` in the
synthetic key state for a script's modifier, so a game or mod that asks for the left-hand key
never saw it: in Untitled Goose Game a Unity plugin's `Ctrl+Shift` chords did not fire. The
state now holds `VK_LCONTROL`, `VK_LSHIFT` or `VK_LMENU` beside it, as Windows does for the
real key. Rebuild the host DLL (`pixi run build-isolated-input-host`) to pick it up.

### Added - isolated input for a C# mod, and for keys polled with `GetKeyState`

Built for the first Unity title to be tested in the background, and not yet run in a game:
`managed` and `key-state` stay `unsupported` in `data/isolated-input.json` until one is proven
(docs/isolated-input.md, A C# mod).

- `cameraunlock/dev/isolated_input.h` detours `GetKeyState` and `GetKeyboardState` and answers
  both from the synthetic key state: the high bit while the script holds the key, the low bit
  flipped by each press. The log names each the first time the game calls it. A native dev build
  that already calls `StartIsolatedInput` gets them on its next core bump, and a start now fails
  where either cannot be detoured.
- `cpp/tools/isolated_input_host`: the header as an x64 DLL, `CameraUnlockIsolatedInput.dll`,
  with MinHook and the C++ runtime linked in. One export,
  `int CameraUnlockStartIsolatedInput(const wchar_t* commandFile)`, and nothing done on load. It
  logs to `CameraUnlockIsolatedInput.log` beside the command file. `pixi run
  build-isolated-input-host` builds it to `cpp/tools/isolated_input_host/build/Release/`.
- `CameraUnlock.Core.Dev.IsolatedInput.StartIfAsked(string modFolder, Action<string> log)`, in
  every target of `CameraUnlock.Core`: with `CameraUnlockInput.txt` in the folder it loads the
  host DLL from the same folder and starts it, and without the file it does nothing. It throws
  when the file is there and the DLL is missing, does not load or does not start.
- `IsolatedGameTest.psm1`: `Start-IsolatedGame -ModHost managed [-HostDll <path>]` copies the host
  DLL beside the mod and `Stop-IsolatedGame` removes it. `Copy-IsolatedInputHost` is that copy
  alone. The session object gains `HostDll`. `-ModHost` defaults to `native`, so no caller changes.
- `pixi run test-isolated-input-host` runs the C# entry point and the DLL in a child process and
  reads the three key state functions back. It is not part of `check`.

### Added - stock sights, the fourth aim mode

Owner ruling of 2026-10-04. The aim mode key now steps sights locked, free look with a marker,
true free look, stock sights and round again. In stock sights, while the sights are up, the
head's yaw, pitch and whole lean (all three axes) ease out on a fade and only roll stays, so the
sight picture is the game's own. At the hip nothing changes. It eases, it never snaps, and it is
not a pause of tracking.

- `data/config-schema.json`: the concept `StockSights`, `[Position]`, bool, default false,
  global, canonical, no alias, after `FreeLookMarker`. `StockSights=true` is stock sights whatever
  `TrueFreeLook` and `FreeLookMarker` hold. A file without it is in the mode it was in.
- `ads/aim_mode.h` and `CameraUnlock.Core.Ads`: `AimMode::StockSights` (C# value 3) as the fourth
  enumerator, `AimModeLabel` / `AimModes.Label` giving `Aim mode: stock sights`, and two new
  functions. `IsFreeLook(mode)` is true in the two free look modes only, and is what a mod hands
  `LeanHandover` as `trueFreeLook`. `StockSightsEngaged(mode, aiming)` is `aiming` in stock
  sights and false otherwise.
- There is no new pose function. A mod runs a second `AdsFade` fed `StockSightsEngaged`, and
  multiplies yaw, pitch and the three lean axes by its output (1 at the hip, 0 with the sights
  up) before the zoom compensation, the lean clamp, the hand-over and the reticle read the pose.
  Roll is never scaled.
- Both `HeadTrackingConfigTable`s bind the concept (`HeadTrackingConfig::stock_sights`,
  `HeadTrackingConfigData.StockSights`). The deprecated flat readers do not read it.
- REFramework layer: `PluginConfig::stockSights`, a Writable `StockSights` row with
  `PluginConfigSchema::trueFreeLook`, listed in the legacy import's `follows_defaults_ini`.
  `PluginMod::CycleAimMode` saves the three values in one save and the mode at startup is decoded
  from the three. The camera pipeline eases the pose out itself wherever the descriptor has
  `isAiming`, and resets that fade on every frame it applies nothing. `isAiming` is now polled on
  every tracked frame, where it was polled only on frames with a lean.
  `PluginMod::IsTrueFreeLook` is `IsFreeLook` of the mode, so it is false in stock sights.
- The `TrueFreeLookKey` file comment names the fourth mode and the third setting, on two lines.
- Tests in `ads_tests.cpp` and `AdsTests.cs` (the eight decodes, each encode, the cycle, the
  labels, `IsFreeLook`, `StockSightsEngaged`, no marker in stock sights, and the pose through the
  fade: the hip, sights up, mid-transition, a reversal from the aim button and from the mode key,
  and the other three modes untouched), `plugin_config_canonical_tests.cpp` (the fresh row, the
  import, the four saves of the cycle) and the canonical fixtures that list every concept.

### Changed - **BREAKING** the aim mode is three config values, and the two-value forms are gone

Every shooter mod that binds the aim mode stops compiling at its next pin bump, which is the
prompt to add stock sights. Nothing is kept as an overload.

- C++ `DecodeAimMode(bool trueFreeLook, bool freeLookMarker)` is now
  `DecodeAimMode(bool trueFreeLook, bool freeLookMarker, bool stockSights)`.
- C++ `struct AimModePair { trueFreeLook, freeLookMarker }` is now
  `struct AimModeSettings { trueFreeLook, freeLookMarker, stockSights }`, and `EncodeAimMode`
  returns it. Stock sights encodes as false, false, true. `trueFreeLook` is now false for stock
  sights as well as for sights locked, where it used to be `mode != SightsLocked`.
- C# `AimModes.Decode(bool, bool)` is now `AimModes.Decode(bool, bool, bool)`, and
  `AimModes.Encode(mode, out trueFreeLook, out freeLookMarker)` is now
  `AimModes.Encode(mode, out trueFreeLook, out freeLookMarker, out stockSights)`.
- `NextAimMode(AimMode::TrueFreeLook)` and `AimModes.Next(AimMode.TrueFreeLook)` return stock
  sights, where they returned sights locked.
- The `TrueFreeLookKey` file comment changed and `StockSights` is a new global row, so the
  rendered `CameraUnlock.ini` of every mod that binds the aim mode changes.
- What each consuming shooter mod does:
  1. Bind `StockSights` in its config table, marked Writable, beside `TrueFreeLook` and
     `FreeLookMarker`, and run `pixi run render-config`.
  2. Pass the three values to `DecodeAimMode` / `AimModes.Decode` at startup.
  3. In the cycle handler, write the three values `EncodeAimMode` / `AimModes.Encode` gives and
     save them in one `Save`.
  4. Replace any `mode != SightsLocked` test that meant free look with `IsFreeLook(mode)`, the
     `trueFreeLook` argument of `LeanHandover::Update` and the weapon pass's eye included.
  5. Keep a second `AdsFade`, feed it `StockSightsEngaged(mode, aiming)` once per rendered frame
     from the polled aim state, multiply yaw, pitch and the three lean axes by its output before
     anything else reads the pose, and `Reset()` it wherever tracking is suppressed.
  6. Add the fourth mode to the README's aim section and the toast.
- REFramework mods with `PluginConfigSchema::trueFreeLook` need steps 1 (the render only) and 6:
  the layer does the rest. A mod of that kind that reads `PluginMod::GetAimMode()` itself checks
  each comparison against the fourth value.

### Changed - Deep Rock Galactic's hotkey rows follow the rule for a key the game takes

- `data/config-format.json` `per_game` for deep-rock-galactic-headtracking, from a measurement on
  2026-10-04: `CycleTrackingModeKey=PageUp, Ctrl+Shift+J` as before, and
  `YawModeKey=PageDown, Ctrl+Shift+T`, which had no chord. The game acts on G and H with Ctrl and
  Shift held and on none of J, T, U, K and B.
- docs/isolated-input.md, Measured: Deep Rock Galactic takes a letter key with Ctrl and Shift
  held from a script.
- Consumer change: deep-rock-galactic-headtracking only, which marks `YawModeKey` `PerGame` with
  the new list.

### Fixed - a lean in moved an RE Engine camera backwards

- `reframework/re_math.h` `ViewSpaceOffsetToWorld` no longer negates z. An RE Engine camera
  looks down the negative row 2 of its world matrix, so the pipeline's z, negative for a
  forward lean, is already the engine's. The negation, added on 2026-08-30 on the belief that
  row 2 was forward, drove a lean in backwards on the 0.40m budget and a lean back forwards on
  0.10m in every REFramework mod.
- Measured: in Resident Evil 2, with no mod loaded, the engine's own view and projection
  matrices put eye - row2 * 5 m in front of the camera and eye + row2 * 5 m behind it. In
  Requiem a lean in of 30 cm now brings the room closer, where it took the eye back.
- New `CameraForward(camera, out)` in `re_math.h`: the direction a camera looks, the negative
  row 2. The camera pipeline's `aimForward` is now that, so the forward stop
  (`forwardStopMetres`) holds a lean in and the `Lean:` log line reads positive along the aim
  for one.
- `re_math_tests.cpp` and `math_tests.cpp` hold the direction to the measured one. The first
  had locked the negation in on the same wrong assumption.
- Consuming repos: every REFramework mod leans the right way at its next pin bump, with no
  source change. A mod that casts or measures along the view from `m[2]` of the camera matrix
  (Requiem's aim trace did) is casting behind the player and changes to `CameraForward`.

### Changed - **BREAKING** the Java agent install starts the mod through a boot class, not `-javaagent`

A Project Zomboid install made by `install-body-javaagent.cmd` did not start: the game exited
within a second with `Could not find agent library instrument on the library path`. Its launcher
loads `jvm.dll` by path, so at start-up `instrument.dll` cannot find `jli.dll` and `java.dll`
unless `jre64\bin` is on PATH. Measured with no Java on PATH and with another JDK on it.

- `scripts/jvm-site-config.ps1` now writes the site config with the mod's boot class as
  `mainClass`, each agent jar appended to `classpath`, and `-Dcameraunlock.mainClass=<the stock
  mainClass>` and `-XX:+EnableDynamicAgentLoading` first in `vmArgs`. It takes a mandatory
  `-MainClass`. A site config is the mod's when it names one of the jars anywhere, so one written
  by the earlier `-javaagent` shape is still replaced on install and removed on uninstall.
- `install-body-javaagent.cmd` requires `JVM_MAIN_CLASS`, and the wrapper template sets it.
- `Invoke-DevDeployJavaAgent` and `Invoke-DevDeployJavaAgentToPath` take a mandatory `-MainClass`.
- `uninstall-body.cmd` matches the site config on the jar name.
- The mod's side of the contract: the jar holds the boot class, whose `main` loads the jar into
  the running JVM as a Java agent and then calls the `main` of the class the
  `cameraunlock.mainClass` system property names. `project-zomboid-headtracking`'s
  `com.cameraunlock.zomboid.Boot` is the reference.
- Consumer change (project-zomboid-headtracking, the only user): add
  `set "JVM_MAIN_CLASS=..."` to `scripts/install.cmd`, pass `-MainClass` in `scripts/deploy.ps1`,
  ship the boot class and an `Agent-Class` manifest entry.

### Changed - REFramework mods cycle three aim modes, and a lean in can stop at the eye relief

- With `PluginConfigSchema::trueFreeLook`, `PluginConfigTable` binds `[Position] FreeLookMarker`
  (Writable) beside `TrueFreeLook`, and the legacy import leaves it to Defaults.ini. New field
  `PluginConfig::freeLookMarker`, default false.
- `PluginMod::CycleAimMode` replaces `ToggleTrueFreeLook` on the `TrueFreeLookKey` list: sights
  locked, free look with a marker, true free look, saving `TrueFreeLook` and `FreeLookMarker` in
  one save and logging `AimModeLabel`. `PluginMod::GetAimMode` returns the mode;
  `IsTrueFreeLook` is true in both free look modes. `ToggleTrueFreeLook` stays as a deprecated
  forwarder to `CycleAimMode`.
- Core draws no marker for a REFramework mod. A mod that sets `trueFreeLook` draws it from
  `GetAimMode` and `AimMarkerOpacity`, or its second mode looks the same as its third.
- New `CameraPipelineDescriptor::forwardStopMetres` (default 0, no stop) and
  `RigLean::SetForwardStop`: with the sights up, a lean in stops that far forward of the game's
  eye (`LeanHandover::SetForwardStop`). The value is measured in the game.
- The pipeline logs a `Lean:` line, requested against applied, along the aim and across it, every
  two seconds while the head is off centre and at once when the sights change.
- Consuming repos: a REFramework mod with `trueFreeLook` gains a `FreeLookMarker` row in its
  committed `CameraUnlock.ini` on the next `render-config`, and its render test fails at its next
  pin bump until that is run.

### Added - isolated input posts a key as `WM_KEYDOWN` and `WM_KEYUP` too

- `cameraunlock/dev/isolated_input.h`: every key the script presses is posted to the game window
  as the window message Windows sends beside raw input, unless the game registered its keyboard
  with `RIDEV_NOLEGACY`. Unreal Engine 4 takes raw input for the mouse alone and reads its keys
  from the window procedure, so Deep Rock Galactic turned its view for `move` and answered to no
  key. Measured there: `down W` walks the player and `Escape` opens the pause menu
  (`data/isolated-input.json`, `docs/isolated-input.md`).
- Consuming repos: nothing to change. A dev build picks it up at its next core bump.

### Fixed - isolated input installs where `SetCursorPos` is already written over

- `cameraunlock/dev/isolated_input.h`: where `user32.dll`'s `SetCursorPos` cannot be detoured,
  the dev build detours `NtUserSetCursorPos` in `win32u.dll`, the system call stub it jumps to.
  In Resident Evil Requiem with REFramework loaded the function starts with a `ret` by the time a
  plugin loads, MinHook refused it, and isolated input took every detour back out. A detour that
  fails now logs MinHook's status and the function's first 16 bytes.
- Resident Evil Requiem is proven for the DirectInput and `GetAsyncKeyState` paths
  (`data/isolated-input.json`, `docs/isolated-input.md`).
- Consumer change: none. Dev builds only.

### Added - a dev deploy for a Java agent mod

- `Invoke-DevDeployJavaAgent` in `powershell/DevDeploy.psm1` deploys a Java agent mod to every
  installed copy of the game: the agent jars go next to the exe and `scripts/jvm-site-config.ps1`
  writes the site config, the same two writes `install-body-javaagent.cmd` makes. A site config
  that does not load the mod stops the deploy before anything is copied.
- Consumer change: none. project-zomboid-headtracking's `scripts/deploy.ps1` is the first caller.

### Changed - one rule for a hotkey a game takes, and Far Cry 6's rows follow it

- docs/canonical-config.md, "A key the game takes": the clash is measured in the game, a modifier
  that misbehaves moves every chord of the mod to `Shift+Alt` or `Ctrl+Alt`, and a letter the game
  acts on moves that action to the first free letter of `J, T, U, H, K, B, N`, actions taken in slot
  order, or to its nav-cluster key alone when none is left. A `per_game` hotkey entry that follows
  the rule needs no other approval. `T` is no longer held back.
- `data/config-format.json` `per_game` for far-cry-6-headtracking now lists all four hotkey rows,
  from a measurement on 2026-10-03: `ToggleKey=End, Ctrl+Shift+T`,
  `CycleTrackingModeKey=PageUp, Ctrl+Shift+K`, `YawModeKey=PageDown, Ctrl+Shift+N` and
  `TrueFreeLookKey=Insert`. The game acts on Y, U, G, H, J and B with Ctrl and Shift held.
- Consumer change: far-cry-6-headtracking only, which marks the four rows `PerGame` with those
  lists. Entries made before the rule (MudRunner, Starfield, Arx Fatalis, Deep Rock Galactic,
  Trepang2) stand until each mod is next worked on.

### Changed - isolated input is proven in Ready or Not, and the proof's blind spot is written down

- `data/isolated-input.json`: `async-key-state` and `cursor-position` are also proven in
  ready-or-not. Unreal takes mouse buttons from window messages, so its scripts place the cursor
  first.
- docs/isolated-input.md: what was measured there, and a limit of `Test-IsolatedInputProof`: it
  passed on a weapon swaying at idle with no input taken, so such a title is proven from the
  mod's log.
- No consumer change.

### Added - isolated input places the cursor for a menu and keeps the game in the background by itself

Far Cry 6's menus read where the cursor is, and its windows take the foreground more than once.
Proven in that game in the background (docs/isolated-input.md, Measured).

- A new command, `cursor <x> <y>`, puts the mouse cursor at a point of the client area: as a
  `WM_MOUSEMOVE`, as what `GetCursorPos` answers, and a `mouse` button then also arrives as the
  window message of a click there.
- The game's window procedure no longer sees the messages that say it lost the foreground
  (`WM_ACTIVATEAPP`, `WM_ACTIVATE`, `WM_KILLFOCUS`). A mod that dropped them in a window procedure
  of its own can take that out.
- The dev build gives the real foreground back from inside the game whenever one of its windows
  takes it. A game that took the foreground after `Start-IsolatedGame`'s settle time used to keep
  it, and Windows refused a hand-back from outside the game.
- The game window is looked for again once the one first found is gone or hidden (a splash
  screen).
- `data/isolated-input.json`: `directinput` is also proven in far-cry-6, and `cursor-position` is
  new and `supported`.
- The DX12 overlay's `Present1` hook is confirmed in a game: Far Cry 6 presents through `Present1`
  alone, and `AimMarkerDX12` drew there.
- No consumer change. A dev build that already calls `StartIsolatedInput` gets all of it on its
  next core bump.

### Added - isolated input answers DirectInput 8 keyboards and mice

A game that reads its keyboard and mouse through `dinput8.dll` got nothing from a test script:
isolated input only posted raw input and answered `GetAsyncKeyState`.

- `cameraunlock/dev/isolated_input.h` detours `GetDeviceState`, `GetDeviceData`, `Acquire`, `Poll`,
  `SetCooperativeLevel` and `Release` in the `IDirectInputDevice8` vtable (A and W, found from
  throwaway devices), once `dinput8.dll` is loaded in the process. Keyboard and mouse devices answer
  from the script alone, immediate and buffered, and the real devices are never acquired. Other
  DirectInput devices are left as they were, and `dinput8.dll` is never loaded into a game that has
  not loaded it.
- `cameraunlock/dev/directinput_state.h` is the pure half (virtual key to `DIK_` code, the US key
  for a character, keyboard and mouse state, buffered events), tested in
  `directinput_state_tests.cpp`.
- The command language is unchanged. `text` presses US-keyboard keys on a DirectInput keyboard.
- Proven in Fallout: New Vegas (Steam), which reads its keyboard through both `GetDeviceState` and
  `GetDeviceData` and its mouse through `GetDeviceState`: `directinput` is `supported` in
  `data/isolated-input.json`. What was measured is in `docs/isolated-input.md`.
- No consumer change: `StartIsolatedInput` is the only call, as before. The raw input path is
  unchanged.

### Added - `install-body-javaagent.cmd`, for a mod that is a Java agent in a game with its own JVM launcher

Project Zomboid is a Java program whose native launcher reads the JVM's arguments from
`ProjectZomboid64.json` next to the exe, and from `ProjectZomboid64.site.json` instead when that
is there. Its mod is a jar the JVM loads with `-javaagent`, which no existing body could install.

- `scripts/install-body-javaagent.cmd` and `scripts/templates/install-wrapper-javaagent.cmd`:
  copies `MOD_DLLS` from `plugins\` next to the exe and writes `<Exe>.site.json` as the stock
  `<Exe>.json` with `-javaagent:<jar>` first in `vmArgs`, one for each `.jar` in `MOD_DLLS`. The
  site config is generated on every install, so a reinstall picks up a game update's changes to
  the stock file. A site config that is already there and does not load the mod stops the install
  before anything is copied.
- `scripts/jvm-site-config.ps1` does the JSON work, and `Copy-SharedBundle` stages it in `shared/`.
- `uninstall-body.cmd`: `FRAMEWORK_TYPE=JavaAgent` removes the site config while it still loads
  one of the jars in `MOD_DLLS` or `LEGACY_DLLS`, leaves one that does not, then removes the jars.
  The uninstall wrapper template names the new value in its comment and gains no CONFIG BLOCK line.
- `scripts/test-javaagent-installer.ps1` covers both scripts against a fixture game folder.
- Not solved by the body, and stated in its header: Project Zomboid's launcher loads `jvm.dll` by
  path, so `instrument.dll` cannot find `jli.dll` and `java.dll` unless `jre64\bin` is on PATH, and
  the JVM stops at start-up. The mod has to arrange that load before the install is usable.
- No consumer change for any other mod.

### Added - `IsolatedGameTest.psm1` and a coverage registry, so every mod's in-game tests run in the background

In-game tests no longer use the real keyboard, mouse or foreground. A title isolated input cannot
drive yet is refused with what to build in core, so coverage grows as mods meet new kinds of game.

- `powershell/IsolatedGameTest.psm1`: `Assert-IsolatedInputCovers` and `Get-GameInputPaths` (a
  game binary's import table against the registry), `Start-IsolatedGame` (launches and hands the
  foreground back), `Invoke-GameInput` (plays a script and reports whether the game ever held the
  real foreground), `Save-GameCapture`, `Test-CaptureDiffers`, `Test-IsolatedInputProof`,
  `Stop-IsolatedGame`, `Save-GameTestState` / `Restore-GameTestState`, `Start-TestPoseSender` /
  `Set-TestPose`, `Get-ModTestPort`. It has no function that sends real input.
- `data/isolated-input.json`: each input path and mod kind as `supported`, `unsupported` or
  `not-needed`. Supported today: raw input and `GetAsyncKeyState`, in a native mod. Unsupported,
  each with what to build: buffered raw input, `GetKeyState` / `GetKeyboardState`, DirectInput,
  GameInput, SDL, and C# and script-only mods.
- `powershell/Send-TestPose.ps1`: the OpenTrack sender `Start-TestPoseSender` runs.
- docs/isolated-input.md is now the procedure: testing a title, and a game it does not cover yet.
- `pixi run test-powershell-isolated-game-test`, part of `test-powershell`.
- No consumer change. A mod opts in by wiring `StartIsolatedInput` into its dev build.

### Changed - Starfield's four hotkey rows are per_game, and isolated input is proven against a game

- `data/config-format.json` `per_game` lists `ToggleKey`, `CycleTrackingModeKey`, `YawModeKey` and
  `TrueFreeLookKey` for starfield-headtracking (owner approved 2026-10-03). Left Ctrl is the game's
  sneak toggle and the game acts on G, H and J with any modifiers held, so its committed file holds
  `End, Shift+Alt+Y`, `PageUp, Shift+Alt+T`, `PageDown` and `Insert, Shift+Alt+U`.
- docs/isolated-input.md records what was measured in Starfield with the real foreground watched
  through every script: keyboard, mouse, console text and the mod's hotkeys all work with the game
  in the background. It replaces the earlier note that the result was not proven.

### Fixed - the DX12 overlay hooks the DXGI swap chain under Streamline's wrapper

In a game that loads NVIDIA Streamline (`sl.interposer.dll`) every swap chain made in the process
comes back wrapped, the overlay's throwaway one included, so `DX12Overlay` was hooking the
wrapper's `Present`. Measured in Starfield (Steam, 1.16.244.0):

- With frame generation on, the game presents the wrapper 60 times a second and Streamline
  presents the DXGI swap chain underneath 120 times a second from its own thread, through its own
  queue. The overlay drew into the wrapper's buffer and the draw never reached the screen.
- With frame generation off, the wrapper's `Present` runs twice a frame, nested, for two wrapper
  objects, and only the inner one holds a queue that submits. The swap chain check added in the
  entry below built the overlay for the outer one, so it drew nothing at all. That regression is
  fixed here.

`Install` now asks the throwaway swap chain for the object it wraps (Streamline's
`StreamlineRetrieveBaseInterface` id, repeated while it answers) and takes the `Present` and
`ResizeBuffers` to hook from that one. Where nothing wraps the swap chain, nothing changes. Checked
in Starfield with a build that draws a marker every frame: on screen with frame generation on, with
it off, and after switching it either way in the settings mid-session, where the swap chain comes
back on a different queue each time and the overlay rebuilds for it. The log says "swap chains here
are wrapped by Streamline, so the hooks go on the DXGI swap chain under the wrapper".

With frame generation on the detour, and so the render callback, runs on Streamline's presenting
thread, once per frame shown, generated frames included. No consumer change.

### Added - isolated input: a lab build feeds its game keyboard and mouse from inside the process

A test session used to need the real foreground and the real mouse and keyboard, so nobody could
use the machine while one ran. `cameraunlock/dev/isolated_input.h` posts synthetic raw input to
the game's own window and answers `GetRawInputData` for it, and detours `GetForegroundWindow`,
`GetAsyncKeyState`, `ClipCursor`, `GetClipCursor`, `SetCursorPos` and `SetForegroundWindow` so the
game and the mod behave as if the game had the foreground while it sits behind other windows.

- `StartIsolatedInput(commandFile, log)` installs the detours and plays the command file each time
  the number on its first line changes, then writes `<file>.done`.
- `cameraunlock/dev/input_script.h` is the command language (`down`, `up`, `tap` with chords,
  `mouse`, `move`, `text`, `wait`), pure and tested in `input_script_tests.cpp`.
- For games that read raw input. Lab builds only: with it on the game does not answer to the real
  keyboard. docs/isolated-input.md has the rest. Not yet proven against a game: the one Starfield
  session was contaminated by real input, see that file.
- No consumer change.

### Fixed - the DX12 overlay reads the swap chain's queue again whenever it rebuilds

`DX12Overlay` resolved the swap chain's queue once and kept it for the session. A game that
rebuilds its swap chain can come back presenting through another queue: Starfield does when frame
generation is switched in its settings. The old queue stays alive, because the overlay holds a
reference to every DIRECT queue it has seen, so the next draw was submitted on it against the new
back buffers.

- The queue is forgotten whenever the device resources are rebuilt (first `Present`, and the
  `Present` after every `ResizeBuffers`) and resolved again from the swap chain being presented.
- The overlay remembers which swap chain it was built from and draws only on that one's `Present`.
  A `Present` for any other is dropped and logged once.
- The log lines name the swap chain: "device resources initialized for swap chain X, 2 buffers at
  WxH" and "swap chain X presents through DIRECT queue 1 of 2 seen".
- No consumer change.

### Fixed - the DX12 overlay draws on the swap chain's own queue, and says why a frame was dropped

`DX12Overlay` submitted its draw on the first DIRECT command queue it saw in its
`ExecuteCommandLists` detour. A game can run more than one (Starfield runs two with frame
generation on), and a mod that installs the overlay mid-game sees whichever submits next. A draw
on any queue but the swap chain's is not ordered against `Present`, lands after the flip and never
reaches the screen, with nothing in the log.

- `Install` measures where a swap chain keeps its queue, on the throwaway swap chain and queue it
  already creates (one pointer away on Windows 11: `+0x140` of the object at the swap chain's
  `+0x10`), and logs it. The game's swap chain is read at the same place, and the answer is used
  once that queue has been seen submitting. The log names it: "the swap chain presents through
  DIRECT queue 2 of 2 seen".
- Where the place cannot be measured, a lone DIRECT queue is still taken after 120 presents, as
  before. With several and no measurement nothing is drawn, and the log says so once.
- Every way `RenderFrame` can drop a frame it had something to draw in now logs once (no
  `IDXGISwapChain3`, back buffer index, fence wait, allocator or list reset, list close, hook
  gone), and the first frame drawn logs its vertex count, back buffer and size.
- New `cameraunlock/rendering/held_pointer.h`, `FindHeldPointer`: which one of a set of candidate
  pointers an object holds, `kHeldNone` or `kHeldAmbiguous` otherwise. Tested in
  `held_pointer_tests.cpp`.
- No consumer change. Not fixed here: in Starfield with frame generation on, the draw is submitted
  on the swap chain's queue before every `Present` and still does not reach the screen. With frame
  generation off it does.

### Added - `[Position] FreeLookMarker`, and `TrueFreeLookKey` cycles three aim modes

`FreeLookMarker` is a canonical concept: bool, default `false`, no aliases, global, directly after
`TrueFreeLook`. With `TrueFreeLook` it is the aim mode (see the entry below for the pair).

- Fields `HeadTrackingConfigData.FreeLookMarker` and `HeadTrackingConfig::free_look_marker` (the
  C++ member after every existing one), bound by both `HeadTrackingConfigTable`s.
- The deprecated flat readers do not read it, as they do not read `TrueFreeLook`.
- `TrueFreeLookKey` keeps its name. Its schema doc and `TrueFreeLook`'s describe the cycle, and its
  file comment is now "Cycles the aim mode: sights locked, free look with a marker, true free look
  (TrueFreeLook, FreeLookMarker)."
- Inserting the concept in the schema's order renumbers the later values of `schema::Concept`.
- 23 concepts are global, where 22 were.
- The `head-tracking/` and `global/Defaults.ini` fixtures and docs/canonical-config.md carry it.

Not done here: the `preferences/` fixtures and `preference_modes` in
data/pipeline-conformance.json do not carry the aim mode. The REFramework half is the entry
"REFramework mods cycle three aim modes" above.

Consuming repos: a committed canonical file that binds `TrueFreeLookKey` with core's comment
changes on the next `render-config`, and the mod's render test fails at its next pin bump until
it is run. A shooter on `HeadTrackingConfigTable` that takes the three-mode cycle binds
`FreeLookMarker`, marks it Writable beside `TrueFreeLook`, and saves both in one `Save`.

### Changed - zoom compensation scales only the lean across the view

A lean along the view moves nothing across the frame, it brings the scene closer, so the zoom
factor no longer applies to it (owner ruling of 2026-10-03): scaled, a lean in through a 4x scope
kept a quarter of its travel. Yaw, pitch and the part of the lean perpendicular to the view axis
scale as before, and roll is still left alone.

- `cameraunlock::camera::ScaleLeanForZoom(lean, view_axis, factor)` in
  `camera/zoom_compensation.h` and `ZoomCompensation.ScaleLeanForZoom(lean, viewAxis, factor)`
  return the lean with only its perpendicular part scaled. The view axis is unit length and in the
  frame the lean is in, and its sign makes no difference.
- The REFramework camera pipeline uses it, with the camera's own z as the view axis. A mod that
  sets `unzoomedFovDegrees` now applies a forward or backward lean in full at any zoom, where it
  was multiplied by the factor.

Consuming repos: a mod that multiplies its whole lean by the zoom factor calls `ScaleLeanForZoom`
instead. No vector in data/pipeline-conformance.json changed.

### Added - the aim mode pair: `AimMode`, its cycle, its labels and the marker's opacity

Aiming down sights has three modes, cycled on the `TrueFreeLookKey` list (owner ruling of
2026-10-03): sights locked, free look with a marker, and true free look. The mode is stored as two
bools, `TrueFreeLook` and `FreeLookMarker`. `cameraunlock/ads/aim_mode.h` and
`CameraUnlock.Core.Ads` (`AimMode`, `AimModes`) hold what every mod would otherwise spell again:

- `DecodeAimMode(trueFreeLook, freeLookMarker)` / `AimModes.Decode`: false/false is sights locked,
  true/true free look with a marker, true/false true free look. `FreeLookMarker` alone is sights
  locked, and `TrueFreeLook` alone is true free look, so a config from before the marker keeps
  its mode.
- `EncodeAimMode(mode)` returning `AimModePair` / `AimModes.Encode(mode, out trueFreeLook, out
  freeLookMarker)`: the pair a mode is saved as, never false with true.
- `NextAimMode(mode)` / `AimModes.Next`: the cycle, in that order.
- `AimModeLabel(mode)` / `AimModes.Label`: `Aim mode: sights locked`, `Aim mode: free look with
  marker`, `Aim mode: true free look`.
- `AimMarkerOpacity(mode, sightsUp)` / `AimModes.MarkerOpacity`: `sightsUp` (0 at the hip, 1 with
  the sights fully up) in free look with a marker, 0 in the other two modes.

The C# methods throw `ArgumentOutOfRangeException` for a value outside the enum.
`LeanHandover` is unchanged: its `trueFreeLook` argument is true in both free look modes.

Consuming repos: nothing to change. A shooter that takes the three-mode cycle calls these instead
of writing its own.

### Added - install and uninstall across a move from the Cecil patcher to BepInEx

A mod that moves off the Mono.Cecil patch onto BepInEx left upgraders with the patched
`Assembly-CSharp.dll` and the old DLLs in `Managed`, so the old mod loaded beside the new plugin.
Now:

- `install-body-bepinex.cmd` runs the release's own `uninstall.cmd /y`, in a child `cmd`, when the
  state file names another framework, before anything is deployed.
- `uninstall-body.cmd` undoes a Cecil install when a non-Cecil wrapper still sets
  `MANAGED_SUBFOLDER`, `ASSEMBLY_DLL` and `PATCH_MARKER`: the pristine assembly is restored first,
  as for a Cecil mod, then `MANAGED_EXTRAS` come out of the Managed folder.
- `uninstall-body.cmd` reads `installed_by_us` only when the state file's `framework.type` is the
  wrapper's `FRAMEWORK_TYPE`. A Cecil release writes `true` for its own patch, and read as if it
  were about BepInEx it deleted a BepInEx the player had installed.

`scripts/test-loader-switch.ps1` runs both bodies on synthetic game folders: an upgrade with and
without the player's own BepInEx, the launcher's no-receipt route (the new `uninstall.cmd` alone),
and a fresh install. Run it from a short path (`-WorkRoot C:\cul-t`): `findstr` cannot open a state file
past `MAX_PATH`.

Consuming repos: nothing to change unless moving off Cecil. A mod that does keeps
`MANAGED_SUBFOLDER`, `ASSEMBLY_DLL` and `PATCH_MARKER` in its uninstall wrapper, and lists the
old Managed DLLs and logs in `MANAGED_EXTRAS`.

### Added - the config differential runs only when something it depends on changed

A mod's legacy config differential took ten to forty minutes on every release, locally in
`release.ps1` and again on the tag build, re-proving an import whose frozen reader never changes.
`DifferentialGate.psm1`'s `Invoke-ConfigDifferential` records a pass in
`tests/config_differential/passed.json`: every file the differential compiled and read, with a
git blob hash of each (the release version masked in `CMakeLists.txt`, `pixi.toml`, project files
and `launcher-manifest.json`, and `data/config-format.json` hashed as the shared sections plus the
repo's own entries). A later run re-hashes those files and skips the test when none changed, naming
the ones that did when it runs it.

The file list comes from the build that passed: MSBuild's tracking logs for the differential
executable and the libraries it links, `dotnet msbuild -getItem` for a dotnet test project and the
projects it references, and every tracked file plus the submodule commits for any other runner.
All three add `tests/config_differential`, the committed config and the lint's JavaScript and JSON.

- `Invoke-ReleaseTestSuite` (ReleaseWorkflow.psm1) runs `test-unit` and the gate where there is a
  differential, and `pixi run test` elsewhere. `Invoke-VersionCommit` commits the record with the
  version bump, and `Test-CleanGitStatus` does not count it as a change.
- `Publish-NightlyBuild` runs the gate and records, and its clean-tree check ignores the record.
- `release-mod.yml` runs the gate without recording where the mod's core has it, and
  `pixi run test-differential` otherwise.
- conformance's ci-minutes check accepts `Invoke-ReleaseTestSuite` in `release.ps1` and a
  pinned `release-mod.yml` that calls `Invoke-ConfigDifferential`.

Consuming repos with tests/config_differential: replace the full-suite block in
`scripts/release.ps1` with `scripts/templates/release-full-test.ps1`, bump core, and re-pin
`release-mod.yml`. The first release after that still runs the differential once and commits
the record.

### Changed - SplitInjectionCameraTracker holds each camera's Transform

`SplitInjectionCameraTracker` reads a tracked camera's `transform` once, when the camera joins
the target set, instead of six times per camera per frame across `Apply` and `RestorePositions`.
Under Il2CppInterop each of those reads was a call into the IL2CPP runtime that handed back a
managed wrapper. The IL2CPP `UnityEngine.CoreModule` stub also gains `Time.unscaledDeltaTime`.

Consuming repos: nothing to change.

### Added - Physics.SphereCast, SphereCastNonAlloc and RaycastNonAlloc in the Unity stubs

The full seven-argument overloads (origin, [radius,] direction, hit or results, maxDistance,
layerMask, queryTriggerInteraction), signatures read off Unity 2022.3.62's
UnityEngine.PhysicsModule.dll. A Unity mod's lean trace needs them and could not build against the
stubs without.

Consuming repos: nothing to change.

### Added - LineSweep in C#

`CameraUnlock.Core.Processing.LineSweep` is the C# twin of `camera/lean_line_sweep.h`: a
swept sphere for `LeanClamp` built from a `LineCast` delegate (a centre ray, and a ring of rays
each behind a sideways probe), with `LineHit` and `LineSweepSettings`. `LineSweep.Query` is the
`LeanQuery` to hand to `LeanClamp.Apply`, held once so passing it allocates nothing.
`LineSweepTests` ports `lean_line_sweep_tests.cpp` case for case against the same box world.

Consuming repos: nothing to change. A Unity mod's `Physics.SphereCast` misses every collider the
sphere overlaps where it starts; wrapping `Physics.Raycast` in a `LineCast` and taking the
nearer of the two answers covers a surface already inside the standoff.

### Added - ViewMatrixTrackingController clamps the lean against the world

`ViewMatrixTrackingController.LeanQuery` takes the mod's world query (a `Physics.Raycast` or
`SphereCast` from the clean eye) and runs `LeanClamp` in the render hook, after the game has
placed the camera, so the offset is cut before the eye can enter a wall. The query is asked along
the world-space lean both ApplyHeadRotation paths apply (the camera's own axes, with the
processor's -z forward flipped to the transform's +z), and the camera-space offset is scaled by
the fraction the clamp allows. `LeanClamp` is exposed for its Settings (Skin must exceed the
camera's near clip distance) and for InContact and LastQueryFailed. The allowance is reset on a
camera switch, a new tracking session, `ResetState` and `Disable`. `LastTrackingPosition` stays
the lean the tracker asked for.

Consuming repos: nothing to change. With `LeanQuery` left null the controller behaves as before.
easy-delivery-co-headtracking is the first to set it.

### Added - zoom compensation in C#

`CameraUnlock.Core.Processing.ZoomCompensation` is the C# twin of `camera/zoom_compensation.h`:
`FovZoomFactor(tanHalfFov, tanHalfFovBase)` for the factor a lean scales by, and
`ScaleAngleForZoom(angleDeg, factor)` for yaw and pitch. `ZoomCompensationTests` ports
zoom_compensation_tests.cpp case for case.

Consuming repos: nothing to change. A Unity mod whose game narrows its field of view calls these
instead of writing its own.

### Added - a swept sphere for LeanClamp, built out of line casts

`camera/lean_line_sweep.h`. An engine that gives a mod only a line cast (HPL2, HPL3) could check
the centre of the eye's path and nothing around it, so a lean past a door frame's edge or a table
corner a few millimetres off the line put the eye close enough for the near plane to cull the
corner. `LineSweepQuery` is a `LeanQueryFn` that sweeps a sphere of `LineSweepSettings::radius`
along the lean: one ray down the centre, which is exact for a flat surface at any angle, and a
ring of rays around it (8 by default), each started from a short sideways probe so it never starts
inside a surface already beside the eye. The mod supplies only its line cast (`LineCastFn`), which
has to skip the player's own body. The radius must equal the clamp's skin; the query hands back
the sphere's travel plus the radius, which the clamp takes the skin off again. One lean costs
1 + 2 x `ring_rays` casts. A cast that cannot run leaves the whole query unanswered.

Consuming repos: nothing to change. A mod on a line cast can pass `&LineSweepQuery` and a
`LineSweep` to `LeanClamp::Apply` in place of its own query.

### Added - LeanClamp in C#

`CameraUnlock.Core.Processing.LeanClamp` is the C# twin of `camera/lean_clamp.h`, with
`LeanObstruction`, `LeanClampSettings` and a `LeanQuery` delegate in place of the C++ function
pointer and context. The policy is the same line for line (instant tightening, damped release,
a failed query reported rather than absorbed, the allowance dropped on a neutral pose), and
`LeanClampTests` ports `lean_clamp_tests.cpp` case for case. A Unity mod now writes only the
query, a `Physics.SphereCast` or `Raycast` from the clean eye, and hands it to `Apply`.

Consuming repos: nothing to change. A Unity mod carrying its own port of the policy can drop it
for this one; blue-prince-headtracking is the first to.

### Fixed - HeadTrackingSession mode changes are safe from a hotkey thread

`SetMode()` reset the position processor and interpolator inline, so a hotkey calling it (or
`CycleMode()`) from the poller thread raced `Update()` on the render thread, which could be
running on the same state. `SetMode()` now only stores the atomic mode, and `Update()` does the
reset the first time it sees position switched off. `Update()` reads the mode once per frame, and
`CycleMode()` advances with a compare-exchange, so two concurrent presses advance two steps.
`Recenter()` is documented as render-thread only; nothing in core calls it from another thread.

Consuming repos: nothing to change. A mod that defers its mode change to the render thread can
call `CycleMode()` straight from the hotkey callback instead.

### Changed - the header of a rendered file says where to change a setting for every game

The Defaults.ini lines above the settings named the file and its locations but never said to
change a setting there for every game. They gain that sentence and are seven lines, not six. The
last two lines become:

```text
; on macOS. The log names the file it read. Change a setting in Defaults.ini to change it in
; every game that has it set to default, or write a value here instead of default to change it
; for this game only.
```

`generate-readme.mjs`'s config block and `scripts/templates/canonical-config-changelog.md` gain
the same sentence: "Changing a setting in `Defaults.ini` changes it in every game that has it set
to `default`."

Consuming repos: after bumping the pin, re-run `pixi run render-config` and commit the file,
regenerate the README config block, and add the sentence to the conversion bullet of any changelog
section not yet released. A file already on a player's disk keeps the header it was created with,
since `Save` edits only the rows it changes.

### BREAKING - `LightFollowsHead` is retired, and retired keys no longer need a branch

The concept is gone from the schema, from `HeadFollowLightSettings` (C++ `follows_head`, C#
`FollowsHead`), from both config tables and from `PluginConfig` (`flashlightTracking`). A carried
light always follows the head; `LightMultiplier=0` pins the beam to the aim, which is what the
switch turned off ever meant, and it is the next key in the same section.

What to change in a consuming repo:

- A mod reading `config.Light.FollowsHead` / `config.light.follows_head` or
  `PluginConfig::flashlightTracking` no longer compiles. Gate on `Light.Multiplier > 0` where the
  distinction still matters, or drop the gate - the multiplier already carries it.
- A committed canonical file holding `[Light] LightFollowsHead` drops the row. Nothing breaks if it
  does not: the key still resolves, and a file that carries one is logged and ignored.
- The legacy REFramework `[Flashlight] Enabled` key is still read, and `false` now folds into
  `Multiplier=0`, so an existing user's setting survives the upgrade. It is no longer written.

Each entry in the schema's `retired` list now carries an `advice` sentence, generated into
`ConfigKeySchema.RetiredAdvice` (C#) and `RetiredConfigKeyAdvice` (C++). Both flat readers test
`IsRetired` ahead of their switch and log that advice, so a retired concept needs no branch in
either language - which is what let a second one be added at all.

New `cameraunlock/config/retired_keys.h`: `WarnRetiredConfigKey(reader, section, key, log)`, the
same warning for any retired key, for a reader that pulls keys out of an `IniReader` by name.
`value_guards.h`'s narrower `WarnRetiredSmoothingKey` is unchanged and stays - every mod repo's
frozen legacy import compiles that header, ~50 of them call that function, and eight pin the
header and its .cpp by SHA-256 in `tests/config_differential/provenance.txt`. Neither file is
touched by this change.

### Changed - `CollisionEnabled`'s file comment says which games read it

The row's comment now adds "Only games whose mod sweeps the level for walls read this; the rest
ignore it." A committed canonical file that renders the comment picks it up on the next
`render-config`.

### Changed - conformance's `ci-minutes` fails a push build that uploads an artifact

A push or pull request build with an `actions/upload-artifact` step now draws a FAIL. Players get
installers from GitHub Releases, so the artifact only cost Actions storage. Delete the step and any
step that only stages files for it.

### BREAKING - the config differential test runs on the release paths only, and push builds cancel when superseded

A push build ran the whole suite twice in most of the fleet: a "Run unit tests" step ran
`pixi run test`, then `pixi run package`, which depends on `test`, ran it again. In a repo with a
legacy config differential test (`tests/config_differential`) that suite is ten to thirty minutes
of a frozen import on every push, and nothing cancelled a run a newer push had superseded.

Core now runs the differential where a release is made, once:

- `release-mod.yml` runs `pixi run test-differential` after the package task when the repo has
  `tests/config_differential`, and fails the release when that task is missing. Its job joins the
  concurrency group `release-mod-<ref>` and is never cancelled. The default `timeout-minutes` is
  now 60, since the differential alone can take 30.
- `Publish-NightlyBuild` runs `pixi run test-differential` after its package command in the same
  repos, so a dev build does too.
- `release-bepinex-mod.yml` joins a non-cancelling group. It compiles nothing, so it runs no tests.
- `check.yml` groups on `${{ github.workflow }}-${{ github.ref }}`.

New templates: `scripts/templates/build-workflow.yml` (the push build: concurrency, paths-ignore
and one `pixi run package` step), `pixi-test-tasks.toml` (the task split for ctest, run-tests.ps1,
test executables, dotnet test and cargo) and `release-full-test.ps1` (the `pixi run test` gate for
release.ps1).

New conformance check `ci-minutes` (tested by `pixi run test-ci-minutes`, part of
`test-powershell`). It reads the task graph through `pixi task list --json`, so running it needs
pixi on `PATH`.

What each mod repo changes, in one commit with the core pin bump:

1. **pixi.toml**, where the repo has `tests/config_differential`: split `test` into `test-unit`
   (everything but the differential) and `test-differential` (the differential, its provenance
   check and the lint over what it migrated), make `test` depend on both and nothing else, and make
   `package` depend on `test-unit` instead of `test`. A ctest repo labels the differential's tests
   `differential`. A repo without the folder keeps its tasks, with `package` depending on `test`.
2. **.github/workflows/build.yml**: the top-level `concurrency` block and the `paths-ignore`
   lists from the template, and no step that runs a test task `package` already runs.
3. **.github/workflows/release.yml**: pin `release-mod.yml` at this commit or later. A repo with
   its own release workflow runs everything `pixi run test` runs in it (`pixi run test`, or
   package and then test-differential) and does not set `cancel-in-progress: true`.
4. **scripts/release.ps1**: run `pixi run test` before the first file edit
   (`release-full-test.ps1`).

Breaking because a repo with `tests/config_differential` and no `test-differential` task now
fails at release, and at `pixi run release nightly`, once it pins this commit: split the tasks in
the same commit as the pin bump.

### Fixed - `New-ChangelogFromCommits` files a breaking-change subject under its type

A subject with the breaking-change mark (`feat(config)!: ...`, `fix!: ...`, `perf!: ...`) did not
match the type patterns and went under `### Other`. It now goes under Added, Fixed or Changed like
the same subject without the mark.

### Fixed - `New-ChangelogFromCommits` puts the entry above the newest version, and does what release scripts did for themselves

With no `## [Unreleased]`, the generated entry went after the first blank line below
`# Changelog`. In a changelog with no blank line under the header that blank line is the one
inside the newest entry, so the release landed between that entry's heading and its notes
(indiana-jones and wolfenstein describe it). The entry now goes directly above the first `## [`
heading, or at the end of a file with none, with the file's line endings. A file with no `# ` title
line, an empty one included, now throws; it used to be rewritten with no entry in it.

The fleet's release scripts carried their own changelog code for cases core did not handle, each
copy with its own anchor and line endings. Core now handles them:

- `-Maintenance` (a new switch, off by default): where the function would throw because there is
  nothing to list, an empty range or a first release with no user-facing commit, it writes
  `- Maintenance release (no user-facing changes).` under `### Changed`, or `First release.` on a
  first release. This is what `Add-MaintenanceChangelogEntry` wrote in the `-Force` catch of about
  125 release scripts; a script passes `-Maintenance:$Force` and drops the catch.
- On a first release (no `v*` tag) with no curated `## [Unreleased]`, the first `## [x.y.z]`
  section was never released, so its heading is renamed `## [Version] - date` in place and it is
  reported as `Promoted`, as arx-fatalis, thief, viewfinder and superliminal did.
- An entry for the version already there keeps its content, and a `## [Version] - yyyy-mm-dd`
  heading takes today's date, as amnesia-rebirth and soma did. It still returns `AlreadyExists`.

The returned hashtable gains `Maintenance`. No parameter changed position.

### Fixed - a Defaults.ini value only a float reads no longer throws in a game that binds a double

Defaults.ini checked a float concept's value with the float codec, but a C++ row bound to a `double`
field reads it with the double codec, whose bounds are exact. `LocalSmoothing=1.00000001` reads as
a float as 1.0, inside 0 to 1, so the reader accepted it; the double row refused it, and
`ConfigOwner::Load` threw `std::logic_error` at game start (fallout-new-vegas and
bioshock-remastered bind `LocalSmoothing` and `RemoteSmoothing` to double).
`PositionLimitZ=10.0000001` did the same. A float concept's Defaults.ini value now passes only when
both codecs read it, in C++ and C#, so every game takes it or none does: the value is refused, the
row keeps its built-in value, and the log line and in-game message give the double codec's reason.
A C# row cannot bind a double, so no C# game threw, but a C# game now refuses the same values. A
Defaults.ini holding such a value, written by hand, is now refused by games that took the float's
rounding before. No signature changes. Fixture `global/read-float-edges` covers both values.

### Fixed - the REFramework legacy import leaves Insert to an action the legacy file put there

`PluginConfigLegacyImport` left `TrueFreeLookKey` to Defaults.ini always, and its default is
`Insert, Ctrl+Shift+U`. A player whose `HeadTracking.ini` put the toggle, the tracking mode key, the
yaw mode key or the diagnostic marker key on Insert (0x2D) kept that action on Insert, so one press
fired both. For a schema with `trueFreeLook` set, that action now keeps Insert and
`TrueFreeLookKey` is written as `Ctrl+Shift+U` alone, as dying-light and far-cry-6 do.
resident-evil-3 and resident-evil-requiem set `trueFreeLook` and take the fix at their next pin
bump; a player whose legacy file has no action on Insert migrates as before. `PluginConfig::Read`
is untouched.

### Fixed - `FileChanged` and `Reload` no longer throw on a write time that cannot be read

C++ `ConfigOwner::FileChanged` and `Reload` and C# `ConfigOwner.FileChanged` and `Reload` read the
config file's write time and threw when Windows could not read it for a reason other than absence
(`std::system_error` in C++, `UnauthorizedAccessException` or `IOException` in C#), so a watcher
polling `FileChanged` threw at every poll. `FileChanged` now returns true for such a file, since it
needs another look, and `Reload` returns Unreadable with the reason ("... cannot be read: ... The
current settings stay."), handed to the status sink once per episode like a read failure. The write
time stays unrecorded, so the next reload that can read it applies the file. `Load` already deferred
in this case. No signature changes.

### Fixed - `New-ChangelogFromCommits` releases a curated [Unreleased] instead of burying it

`New-ChangelogFromCommits` put the generated `## [x.y.z] - date` entry directly under
`# Changelog`, so in a CHANGELOG with a curated `## [Unreleased]` the release landed above it and
the curated notes were left under no version (dying-light-2, resident-evil-requiem, rv-there-yet).
Now a `## [Unreleased]` section holding any non-blank line becomes the release: its heading is
renamed to `## [x.y.z] - date` where it stands, its content is kept as written, and no commit is
read. An empty `## [Unreleased]` is replaced by the generated entry. A file with none gets the
generated entry under the header as before. No new `## [Unreleased]` is written above the release,
matching the fleet's CHANGELOGs, which start one when the next notes are written.

The returned hashtable gains `Promoted`. For a promoted section `Features`, `Fixes` and `Changes`
count the lines starting `- ` under its `### Added`, `### Fixed` and `### Changed` headings, and
`Generic` is false. A CRLF CHANGELOG keeps CRLF, and a generated entry now reaches one: the
insertion looked for `\n\n` after the header and silently wrote nothing when every line ended
`\r\n`.

A mod whose `release.ps1` moves [Unreleased] into the generated entry itself
(`Move-UnreleasedIntoEntry` in assetto-corsa-evo, bioshock-remastered, black-and-white and
snowrunner) finds no [Unreleased] after the call and leaves the file alone.

### Added - conformance check `release-canonical-since`

`scripts/conformance.ps1` fails a repo whose `launcher-manifest.json` config block carries
`canonical_since` when `scripts/release.ps1` is missing or calls neither
`Assert-ReleaseNotBelowCanonicalSince` nor `New-ReleaseTag` (a call in a comment does not count).
Without either, the only stop on a release below `canonical_since` is validate-manifest in the tag's
CI build, after the tag is pushed. All 81 repos that carry `canonical_since` pass today.

### Fixed - a config that stays unreadable is reported once, and Load no longer throws on its write time

C++ `ConfigOwner::Reload` and C# `ConfigOwner.Reload`: a config file that cannot be opened (held by a
program that denies read sharing, say) leaves its write time unrecorded, so `FileChanged()` stays
true and a watcher reloads it at every poll, which is right, since the file may hold settings newer
than those applied. Each of those reloads also handed the same "cannot be read" message to the status
sink. The sink now gets an Unreadable reason once: again only after a reload that reads the file
(Applied or Unchanged) or when the reason changes. The result still carries the status and reason
every time, and `ConfigReloadStatus` is unchanged.

`Load` read the config file's write time before opening it, and a failure there for any reason but
absence threw out of `Load` (`std::system_error` in C++, `UnauthorizedAccessException` or
`IOException` in C#). It is now Deferred on the table's defaults, as a file that cannot be opened
already was. C++ adds `detail::OwnerTryLastWriteTime`, which returns the Win32 error in place of
throwing; `OwnerLastWriteTime` is unchanged.

### Changed - C# `ConfigOwner` refuses a partly qualified Path or LegacySourcePath

`ConfigOwner<TConfig>` checked `ConfigOwnerOptions.Path` and `LegacySourcePath` with
`Path.IsPathRooted`, which accepts `C:x\CameraUnlock.ini` and `\Games\Foo\CameraUnlock.ini`.
`Path.GetFullPath` resolves those against the process's current drive and folder, so the owner could
read and write a file on another drive than the one meant. Both now take the rule
`DefaultsFile.At` and the C++ owner already apply: a drive letter and a separator, or a UNC path
(elsewhere a leading /), and the constructor throws `ArgumentException` for anything else.

This tightens a precondition. Every converted C# mod builds both paths from `Assembly.Location`,
BepInEx's `Paths.ConfigPath` or `ConfigFile.ConfigFilePath`, and every test from
`Path.GetTempPath()`, all fully qualified, so no caller changes.

### Fixed - C# `CheckedFileWriter` looks for the target after any failed replacement

The writer classed a failed `File.Replace` from `Marshal.GetHRForException`. A corlib that throws
`IOException` without the Win32 code (a Unity Mono build may) got a certain failure, and the
temporary was deleted even when the replacement had already removed the target, leaving no copy of
the file at all. After any failed replacement the writer now looks for the target: when it is gone,
the temporary is moved into place as it was for `ERROR_UNABLE_TO_MOVE_REPLACEMENT`, and a move that
fails keeps the temporary with `OutcomeUncertain` set.

### Fixed - `Assert-LauncherManifestDelivery` reads delivery_mode as Lopari and validate-manifest do

`ReleaseWorkflow.psm1`'s `Assert-LauncherManifestDelivery` compared without case and read a JSON
null or an empty string as an absent field, so `"Manifest"` passed packaging while Lopari installs
it through install.cmd and `validate-manifest.mjs` refuses it, and `"INSTALL_CMD"` with a reason
passed too. It now matches `validate-manifest.mjs` exactly: the key and the value are compared
case-sensitively, a null, an empty string or a value that is not a string is an unknown mode, and
an `install_cmd_reason` that is not a string counts as missing. No manifest in the fleet changes
result: all 131 read the same under both rules.

### Fixed - an interrupted uninstall no longer strands the player's config

`uninstall-body.cmd` sets each `PRESERVE_FILES` entry inside a loader folder aside in
`<game>\CameraUnlock-kept-configs\`, under its path relative to the game folder, while it removes
that folder. A run that stopped before moving the files back left them there: the next uninstall
refused with exit 1 until they were moved back by hand, and no install body looked at the folder,
so a reinstall created a fresh `CameraUnlock.ini` and, in a `legacy` repo, never imported the
legacy file that was also set aside.

- New shared script `scripts/restore-kept-configs.ps1`, staged by `Copy-SharedBundle` as
  `shared/restore-kept-configs.ps1`. It moves every file under `CameraUnlock-kept-configs\` back
  to the same relative path under the game folder, creating parent folders, prints
  `Restored: <path>` for each, and removes the emptied folder. When any of those paths already
  holds a file it moves nothing, names both copies, and exits 1.
- Every `install-body-*.cmd` runs it right after the game-running check, before the loader
  install and before `MOD_SEED_FILES` are seeded, so a restored config is kept rather than
  replaced by the default. `uninstall-body.cmd` runs it in place of the old refusal, whether or
  not the wrapper sets `PRESERVE_FILES`, and then uninstalls as usual. A clash exits 1 under
  `/y` as well; without `/y` the run pauses on the failure as any other does.
- A move-back failure inside `:rmtree_keep` now tells the player to rerun the installer or
  uninstaller once the folder can be written to, instead of moving the files by hand.
- `validate-manifest` counts `shared\restore-kept-configs.ps1` among the files an install_cmd
  package's scripts reach for, so a package that stages the new bodies without it fails.

A mod picks this up at its next core pin bump. A package built with its own list of shared
files rather than `Copy-SharedBundle` has to add the script, or every install and uninstall
fails with "restore-kept-configs.ps1 not found".

### Changed - arx-fatalis, deep-rock-galactic and mudrunner keep their own hotkey rows

`data/config-format.json` `per_game` lists hotkey rows for three repos, approved 2026-09-27, in
trepang2-headtracking's shape, because the fleet chords clash with each game's own bindings:

- **arx-fatalis-headtracking**: `CycleTrackingModeKey=PageUp, Ctrl+Shift+J`. In Arx Fatalis G
  drinks a mana potion, so Ctrl+Shift+G would drink one. `YawModeKey` stays the fleet default.
- **deep-rock-galactic-headtracking**: `CycleTrackingModeKey=PageUp, Ctrl+Shift+J` and
  `YawModeKey=PageDown`. In Deep Rock Galactic G throws a grenade and H toggles the HUD, and the
  game fires them with Ctrl+Shift held.
- **mudrunner-headtracking**: `ToggleKey=End, Ctrl+Shift+U`, `CycleTrackingModeKey=PageUp,
  Ctrl+Shift+J` and `YawModeKey=PageDown, Ctrl+Shift+K`. MudRunner reads Y (text chat), G (horn)
  and H (headlights) without checking Ctrl or Shift, so the fleet chords fire those too; the old
  build used U, J and K. MudRunner has no aim state, so it binds no `TrueFreeLookKey` and U is free.

Each repo's committed file is to hold those values, its table marks the rows `PerGame()`, and
render-config writes them into its descriptor's `per_game`. Until each repo does, its
`config-format` and `config-descriptor` conformance fail.

### Changed - N1 in a Unity import: a key code with no name imports as unbound

Owner ruling of 2026-09-27, recorded in `normalisations.N1` of `data/config-format.json`.
`LegacyNormalisations.KeyCodeToBindings` threw `ArgumentException` for a legacy `KeyCode` value
that `data/keys.json` has no name for, so a Unity map had no way to import one.

- **C#** `LegacyNormalisations.KeyCodeToBindings` gives "" for such a code and adds a
  `KeyCodeOutOfRange` drop with the code in decimal. Its signature does not change; it no longer
  throws `ArgumentException`.
- **C#** `KeyBindings.HasName(int unityKeyCode)` (added): true when `data/keys.json` names the code,
  the Ctrl, Shift and Alt keys included, false for 0 and for any value no `KeyCode` member has.
- **C#** the `KeyCodeOutOfRange` log line is `not carried: [Hotkeys] ToggleKey=999, it is not a key
  code Unity names, so the action is unbound`. No C# import recorded the rule before. The C++ line
  does not change.
- A Unity repo adopts it at its next pin bump: a map that guarded `KeyCodeToBindings` against the
  throw can drop the guard, and a map that appends the action's Ctrl+Shift chord keeps the chord
  when the code is unbound.

### Added - N4: a legacy number outside the row's range is clamped

Approved by the owner on 2026-09-27 and recorded as `normalisations.N4` in
`data/config-format.json`, with `drop_rule` `NumberOutOfRange`. A legacy reader that checked no
range could hand the map a finite number no canonical file holds, and the migration could not write
the row.

- **C++** `DropRule::NumberOutOfRange` (10), `LegacyClampToRange(value, lo, hi, section, key,
  dropped)` for any number type but `bool`, and `LegacyClampToRange<schema::Concept::X>(value,
  section, key, dropped)` on the schema's range. A value outside the range gives the nearest end and
  a `NumberOutOfRange` drop with the value read; a value that is not finite, a bound that is not
  finite, or a low end above the high end throws `std::invalid_argument`.
- **C#** `DropRule.NumberOutOfRange` (10), and `LegacyNormalisations.ClampToRange` for `int`, `float`
  and `double` bounds, and for a `ConceptDescriptor<int>` or `ConceptDescriptor<float>`, throwing
  `ArgumentException`.
- The migration log line is `not carried: [Position] LimitZ=25.0, it is outside the range this
  setting takes, so the nearest end of the range is used`. The numbers of the existing rules do not
  move.
- A map hands `LegacyFollowsDefaultsIni` the value read, not the clamped one, so a clamp that lands
  on the shipped default is still carried as the player's value.
- The changelog template gains a bullet for it. A repo adopts it at its next pin bump by clamping
  each number its frozen reader does not bound, after N2.

### Changed - a legacy number that is not finite (N2) is written `default`

Owner ruling of 2026-09-27. N2 imported a NaN or infinite legacy value as the row's built-in
default and the migration wrote that number, so the row stopped following Defaults.ini although the
player never chose it.

- **C++ and C#** `LegacyFollowsDefaultsIni.Setting(concept, value, shipped)` and the list overload
  leave the row to Defaults.ini when `value` is a float or double that is not finite. A map that
  passes the value as the frozen reader read it gets `default` on that row with no other change;
  one that passes what `LegacyFiniteOrDefault` returned still compares the built-in number. The
  `NonFiniteNumber` drop and its log line do not change.
- `normalisations.N2` in `data/config-format.json` records the ruling, docs/canonical-config.md
  ("The legacy import") describes it, and the changelog template gains a bullet for it.
- A repo adopts it at its next pin bump: check each `Setting` call on a float row passes the value
  as read, and re-run its differential test, where a non-finite corpus input now lists the row in
  `follows_defaults_ini`.

### Added - an untouched default of an older build follows Defaults.ini

Owner ruling of 2026-09-27. `LegacyFollowsDefaultsIni`'s "shipped" is the default of whichever
published build wrote the legacy file, not only the newest one. Where a setting's default changed
between published builds, a value equal to an older build's default is untouched when the import
can tell that build wrote the file, from evidence the file carries; where it cannot, the import
compares with the newest build's value as before.

- **C++** `LegacyFollowsDefaultsIni::Setting(concept, value, const std::vector<T>& shipped)`: the row
  is left to Defaults.ini when the value equals any of the listed values, for a file the evidence
  narrows to several builds whose defaults differ. `Setting(concept, value, {a, b})` binds to it. It
  throws `std::invalid_argument` for an empty list, and for a float or double in it that is not
  finite.
- **C#** `LegacyFollowsDefaultsIni.Setting<T>(ConceptDescriptor concept, T value, IList<T> shipped)`,
  the same, throwing `ArgumentException`.
- **Records.** A repo records each setting whose default changed between published builds in
  `conversion_notes`: each range of versions with the value it shipped, and the evidence the import
  reads (docs/canonical-config.md, "The legacy import"). `data/config-format.json`'s `_comment` says
  so.
- `scripts/templates/canonical-config-changelog.md` words its second Legacy bullet for the build that
  wrote the file.
- Nothing changes for an import that passes the newest build's defaults. A repo with a default that
  changed between published builds adopts it at its next pin bump: it reads the evidence, passes the
  shipped value of the build that wrote the file, and adds a `conversion_notes` entry.

### Changed - update-submodule commits a conventional subject

- `scripts/update-submodule.ps1` commits the pointer and the restamped notices as
  `chore(deps): bump cameraunlock-core to <short sha>` instead of `Update submodule to latest main`.
  `Test-NoiseCommit` drops both from changelogs and release notes: the new one through its `chore`
  prefix, the old one, still in every mod's history, through its `Update submodule` pattern, which
  stays.

### Fixed - validate-manifest checks only the Nexus ZIP of the build it validated

With no arguments, validate-manifest checked the newest `release/*-nexus.zip` of a converted repo
for the config, whatever build it came from. A mod that no longer builds a Nexus ZIP, or did not
build one this time, passed or failed on an older build's file.

- `scripts/validate-manifest.mjs` checks the Nexus ZIP named like the installer ZIP it just
  validated, with `-nexus.zip` for `-installer.zip`. When there is none and `release/` holds another
  Nexus ZIP, it prints a `WARN` line naming it as stale and does not check it. A repo with no Nexus
  ZIP gets no line, as before.

### Fixed - validate-manifest counts a patch tool as declared, and fails a package without it

Lopari runs a Cecil mod's `patches[].tool` in place from the package root, with the tool's folder
as its working directory, and never deploys it. validate-manifest read `tools/BootstrapPatcher.exe`
and the `tools/Mono.Cecil.dll` beside it as binaries no manifest row deploys, and failed
gone-home-headtracking's package, which is correct as it is. It also passed a package whose manifest names
a tool the ZIP does not carry, which Lopari refuses at install with "patch tool ... is not in the
package".

- `scripts/validate-manifest.mjs`, in `manifest` and `manifest_variants` delivery: the file
  `patches[].tool` names, and each file beside it in its folder, is declared payload. At the
  package root only the tool itself counts. A package fails when the tool is missing from the ZIP,
  when `patches` is not an array, when an entry has no nonempty string `target`, `tool` or
  `marker` (Lopari's `PatchSpec` requires all three, and an empty marker never matches), when
  `tool` is absolute or holds `..`, and when a variant carries `patches`, which Lopari reads only
  at the top level. A tool matched only without case passes with the casing warning.
- `scripts/test-validate-manifest.mjs`, `pixi run test-validate-manifest`, part of `pixi run
  check`.
- Mod side: a Cecil mod whose manifest names a tool it does not package now fails
  validate-manifest. Stage the tool at the path the manifest names, as gone-home-headtracking's
  `package-release.ps1` does.

### Changed - a setting the player never changed migrates as default, with LegacyFollowsDefaultsIni

Owner rule of 2026-09-26, from testing assetto-corsa-evo: with Defaults.ini set to position only,
a migrated `HeadTracking.ini` whose `[Position] Enabled=1` was only the old build's shipped default
imported the mode "both", which differed from Defaults.ini, so the migration wrote
`RotationEnabled=true` and `PositionEnabled=true` and the game ignored the global. A value equal to
the old build's default is no player's choice. Now a setting the legacy file does not hold, or holds
at the old build's default, is migrated as `default` and follows Defaults.ini; only a value the
player changed is written explicitly, and still as `default` where it equals what `default` gives.

- **Added, C++** `LegacyFollowsDefaultsIni` (`config/legacy_import.h`): `Setting(concept, value,
  shipped)` (compared with `==`), `Setting(concept, unchanged)`, `NotInLegacy(concept)`,
  `TrackingMode(value, shipped)`, `TrackingMode(unchanged)` and `Concepts()`, which a map passes to
  `ImportResult::Imported` or `Absent` as `follows_defaults_ini`. It throws `std::invalid_argument`
  for either half of the tracking mode outside `TrackingMode`, a concept that is not global or was
  given before, and from `Setting`, a float or double `shipped` that is not finite.
- **Added, C#** `LegacyFollowsDefaultsIni`, the same members, `Concepts` a
  `ReadOnlyCollection<ConceptDescriptor>`, comparing with `EqualityComparer<T>.Default` and throwing
  `ArgumentException`.
- **Changed, owners.** `RotationEnabled` or `PositionEnabled` in `follows_defaults_ini` (C#
  `FollowsDefaultsIni`) now names the tracking mode, and every row of the pair the table has takes
  what `default` gives. Naming `RotationEnabled` for a two-state table, which has no
  `RotationEnabled` row, no longer throws; naming one half on a table with both rows no longer
  leaves the other half at the imported value.
- **Changed, REFramework.** `PluginConfigLegacyImport` compares every row but `DiagnosticMarkerKey`
  with `PluginConfig::SetDefaults` through `LegacyFollowsDefaultsIni`, so a REFramework mod's
  untouched rows migrate as `default`. With Defaults.ini at the built-in values the migrated bytes
  do not change, since the table's defaults are the schema's.
- **README and changelog wording.** `generate-readme.mjs` and
  `scripts/templates/canonical-config-changelog.md` describe the new rule. A converted repo's README
  config block is stale until `generate-readme.mjs --write` runs in it.
- **Every converted repo with a legacy import adopts it**: each map gives every row of its table that
  follows Defaults.ini one `LegacyFollowsDefaultsIni` call against the frozen defaults, and its
  differential test asserts the untouched rows migrate as `default` (docs/canonical-config.md, "The
  legacy import").

### Removed - BREAKING - PositionAllowed and the tracker pivot are not settings

Two owner rulings of 2026-09-26. The tracking mode (`RotationEnabled` with `PositionEnabled`: both,
rotation only, position only) is the only way positional tracking is switched off, so
`PositionAllowed` goes. And the tracker is authoritative over the neck pivot, like the rest of pose
shaping, so `TrackerPivotForward` and `TrackerPivotUp` go too. Not every game shipped a pivot of 0
(repo ships `TrackerPivotForward=0.08`), so, as with a shipped pose-shaping default that is not
identity, a game keeps the pivot it shipped in its own code. `PositionProcessor`'s pivot properties
(C# `TrackerPivotForward` and `TrackerPivotUp`, C++ `Get/SetTrackerPivotForward` and
`Get/SetTrackerPivotUp`) and `HeadTrackingSession`'s pass-throughs do not change.

- **Schema.** `data/config-schema.json` marks all three `canonical: false` and drops their `global`,
  `file_comment` and (on the pivots) `range`. `PositionAllowed` carries the `canonical_reason` `Turn
  positional tracking off with the tracking mode: PositionEnabled=false, or the mode hotkey.`, the
  two pivots `The neck pivot is not a setting.` The pivot aliases stay (`PivotForward`, `NeckPivotForward`, `PivotArm`, `TrackerPivot`,
  `NeckModelForward`, `PivotUp`, `NeckPivotUp`, `NeckModelHeight`), so the deprecated flat readers
  parse every spelling as before. A canonical file holding any of them draws `NonCanonicalConcept`
  with that reason, and the canonical config lint fails it.
- **BREAKING, generated code.** C# `ConfigConcepts.PositionAllowed`, `ConfigConcepts.TrackerPivotForward`
  and `ConfigConcepts.TrackerPivotUp`, and C++ `schema::Concept::PositionAllowed`,
  `schema::Concept::TrackerPivotForward`, `schema::Concept::TrackerPivotUp` and their
  `ConceptTraits`, are gone. Every `schema::Concept` value after `PositionEnabled` moves down by one,
  and every value after `CollisionReleaseSmoothing` by three. A converted mod that names one of them
  no longer compiles: delete it from the table, switch any code that read `PositionAllowed` to the
  tracking mode's position channel, and run `pixi run render-config` and `pixi run test`. They are
  removed rather than deprecated because they were added on 2026-09-24 and no core or mod release
  carries them, and because a descriptor exists only to bind a canonical row: C++ indexes
  `kConcepts` by the `schema::Concept` value, so a retired member left in the enum could bind
  nothing.
- **Config tables and Defaults.ini.** `HeadTrackingConfigTable` has no row for any of the three. The
  canonical set is 24 concepts, 22 of them global. A new Defaults.ini has none of their lines and is
  eight lines shorter; an existing one that holds them is read as before, and those lines are not
  read and draw nothing, like any key of a concept the format does not write.
- **Kept for existing callers, deprecated**: `HeadTrackingConfigData.PositionAllowed`,
  `TrackerPivotForward` and `TrackerPivotUp`, marked `[Obsolete]`, and C++
  `HeadTrackingConfig::position_allowed`, `tracker_pivot_forward` and `tracker_pivot_up`, marked
  `[[deprecated]]`, which `ApplyValues` and `LoadFromFile` still fill. A consumer that reads one gets
  a compiler warning (an error where it builds with warnings as errors, as yapyap and blue-prince
  do). Copying a `HeadTrackingConfig` does not warn.
- **Legacy import.** Two approved changes in `data/config-format.json`, recorded as new DropRules in
  both languages:
  - `position_switch_off`, C++ `DropRule::PositionSwitchOff` and C# `DropRule.PositionSwitchOff` (8).
    A legacy position switch that also kept the mode hotkey off the position modes (the games
    `PositionAllowed` was added for: amnesia-rebirth, soma, bully-scholarship-edition,
    a-plague-tale-innocence, the-painscreek-killings) and is false imports as the rotation-only
    tracking mode, `RotationEnabled` true and `PositionEnabled` false, and is recorded. Nothing else
    carries it. A true value changes nothing and is not recorded. C++ `LegacyPositionSwitch` (over
    the two `bool&` fields) and C# `LegacyPositionSwitch.Record` (over a `HeadTrackingConfigData`)
    do this; a map calls one after it has written the tracking mode. The log line is `not carried:
    [Position] Enabled=false, positional tracking is switched off by the tracking mode now, so the
    mod starts in rotation only and the mode hotkey can turn position back on`.
  - `tracker_pivot`, C++ `DropRule::TrackerPivot` and C# `DropRule.TrackerPivot` (9). C++
    `LegacyTrackerPivot` and C# `LegacyTrackerPivot.Record` compare the effective legacy pivot under
    any spelling with the effective pivot the game shipped, both as the pipeline ran them (0 where
    the game's own switch had compensation off or its code never read the key). A value the player
    changed is dropped and recorded; one equal to the shipped value is not, and the conversion keeps
    the shipped pivot in the mod's code. The log line is `not carried: [Position]
    TrackerPivotForward=0.1, the neck pivot is not a setting now, so a distance you set is not
    carried over`.
- **README and changelog.** The README config block of a legacy repo, rendered by
  `scripts/generate-readme.mjs`, lists two more settings not carried over: `A setting that kept
  positional tracking off whatever the tracking mode said. Where your old file had it off, the mod
  starts in the rotation-only tracking mode instead, and the mode hotkey can now turn positional
  tracking back on.` and `A neck pivot distance you changed from its default. The neck pivot is not
  a setting now.`
  `scripts/templates/canonical-config-changelog.md` carries the same lines and two conditional
  Removed bullets.
- **Fixtures.** `global/Defaults.ini`, `head-tracking/all-concepts.ini` and `all-concepts-fresh.ini`
  lose the three comments-and-rows; the three `head-tracking/apply-*` cases lose the three fields;
  `table/apply-unknown` gains `PositionAllowed`, `TrackerPivotForward` and the alias `PivotUp`, each
  drawing `NonCanonicalConcept`; `global/read-unknown-key` gains a `[Position]` section with the
  three keys, none read; the `readme/values-legacy-*` blocks gain the two lines. The canonical lint's
  mutations gain the three keys, and its line numbers follow the shorter fresh file.
- **Consumers must follow.** a-plague-tale-innocence binds `C::PositionAllowed`; easy-delivery-co,
  obra-dinn, superliminal, the-forest and yapyap bind `ConfigConcepts.TrackerPivotForward` and
  their committed config holds `TrackerPivotForward=default`. Each needs the concept deleted and
  its config re-rendered at the pin bump. easy-delivery-co, obra-dinn and yapyap shipped a pivot of
  0.08 before conversion and repo ships 0.08 today, so each of those keeps 0.08 in code and passes
  it to `LegacyTrackerPivot` as the shipped value. Lopari must drop the three rows from its Defaults.ini
  rows, global-defaults view and tests.

### Removed - BREAKING - aim decoupling is not a setting: aim is always decoupled

Owner ruling of 2026-09-26: decoupled aim is the only supported behaviour. The aim stays with the
mouse or controller while the head moves the view, and no mod offers, reads or documents a way to
couple it to the head. The decoupling machinery (`AimDecoupler`, `AimDecouplingState` and the rest)
is the feature and does not change; only the switch goes.

- **Schema.** `data/config-schema.json` marks `AimDecoupling` `canonical: false`, with the
  `canonical_reason` `Your aim always stays with the mouse or controller while your head moves the
  view.`, and drops its `global` and `file_comment`. Its aliases stay (`DecoupleAim`,
  `AimDecouple`, `EnableAimDecoupling`, `DecoupledAim`), so the deprecated flat readers parse every
  spelling as before. A canonical file holding any of them draws `NonCanonicalConcept` with that
  reason (`[General] AimDecoupling is ignored. Your aim always stays with the mouse or controller
  while your head moves the view.`), and the canonical config lint fails it.
- **BREAKING, generated code.** C# `ConfigConcepts.AimDecoupling` and C++
  `schema::Concept::AimDecoupling` and `ConceptTraits<Concept::AimDecoupling>` are gone, and every
  `schema::Concept` value after `WorldSpaceYaw` moves down by one. A converted mod that names the
  concept in `HeadTrackingConfigTable` no longer compiles: delete it from the list, delete the mod's
  coupled-aim code path so the aim is always decoupled, and run `pixi run render-config` and
  `pixi run test`.
- **Config tables and Defaults.ini.** `HeadTrackingConfigTable` has no `AimDecoupling` row. The
  canonical set is 27 concepts, 25 of them global. A new Defaults.ini has no `AimDecoupling` line
  and is two lines shorter; an existing one that holds the line is read as before, and the line is
  not read and draws nothing, like any key of a concept the format does not write.
- **Kept for existing callers**, and documented as deprecated: `IHeadTrackingConfig` and
  `HeadTrackingConfigData.AimDecouplingEnabled`, C++ `HeadTrackingConfig::aim_decoupling_enabled`,
  which `ApplyValues` and `LoadFromFile` still fill, and `HeadTrackingConfigBase.EnableAimDecoupling`
  and `CachedEnableAimDecoupling`, still bound so a subclass reads a non-null entry. No mod reads
  any of them to couple the aim.
- **Legacy import.** Approved change `coupled_aim` in `data/config-format.json`, recorded as the new
  C++ `DropRule::CoupledAim` and C# `DropRule.CoupledAim` (7). A map records it where the legacy
  switch, under any spelling, is false; a true value changes nothing and is not recorded. The log
  line is `not carried: [General] AimDecoupling=false, aim is always decoupled now, so your aim
  stays with the mouse or controller while your head moves the view`.
- **README and changelog.** The README config block of a legacy repo, rendered by
  `scripts/generate-readme.mjs`, lists `The aim decoupling setting. Aim is always decoupled now, so
  your aim stays with the mouse or controller while your head moves the view, even if your old file
  had decoupling turned off.` among the settings not carried over, and
  `scripts/templates/canonical-config-changelog.md` carries the same line and a Removed bullet.
- **Fixtures.** `global/Defaults.ini`, `head-tracking/all-concepts.ini` and
  `all-concepts-fresh.ini` lose the `AimDecoupling` comment and row; the three `head-tracking/apply-*`
  cases lose the field; `table/apply-unknown` gains `AimDecoupling` and `EnableAimDecoupling`,
  each drawing `NonCanonicalConcept`; `global/read-unknown-key` gains an `AimDecoupling` line that is
  not read; the `readme/values-legacy-*` blocks gain the dropped-settings line.
- **Lopari must follow**: its Defaults.ini rows, its restated Defaults.ini render and header, its
  global-defaults view and its copies of these fixtures.

### Fixed - BREAKING - a Ctrl, Shift or Alt key is never a hotkey's key

Both hotkey codecs read a Ctrl, Shift or Alt key as the key of a binding: `LeftShift` to
`RightAlt` in either dialect, and natively `0x10` to `0x12` and `0xA0` to `0xA5`. Such a key goes
down before the key of any chord made with it, so with `YawModeKey=LeftShift, Ctrl+Shift+H` a player
who pressed Shift, then Ctrl, then H fired the action twice.

- **Parsers**: C++ `input::ParseKeyBindings` and C# `KeyBindings.TryParse` refuse such a key, with
  or without modifiers before it (`Ctrl+LeftControl`, `Ctrl+0x11`), with
  `'<key>' is a Ctrl, Shift or Alt key: expected a key such as End, F9 or A, with Ctrl, Shift or Alt before it`.
  A `CameraUnlock.ini` or `Defaults.ini` row holding one is refused like any other value the codec
  refuses.
- **BREAKING, formatters**: C++ `input::FormatKeyBindings` throws `std::invalid_argument` and C#
  `KeyBindings.Format` throws `ArgumentException` for such a binding, since nothing they could write
  reads back. `FormatVirtualKey` still spells the key. A legacy import that formats a legacy key
  code itself (`{{KeyModifiers::kNone, vk}, chord}`, or C# `new KeyBinding(KeyModifiers.None,
  code)`) now throws on a legacy file that bound one; take the key's text from
  `LegacyVirtualKeyToBindings` (C++) or `LegacyNormalisations.KeyCodeToBindings` (C#) instead, which
  apply N3 below, and append the chord to it.
- **Defaults.ini**: the header lists the 98 key names the file takes, not 104, and a `LeftShift`
  to `RightAlt` key is refused as `<key> is not one of the key names this file takes`. A new
  Defaults.ini's header is one line shorter. Lopari restates the header and must follow.
- `data/fixtures/canonical-ini/keys/cases.tsv` makes every such key `invalid` in both dialects
  (native 208 rows, unity 140). `scripts/lib/key-bindings.mjs` follows, and
  `scripts/generate-config-schema.mjs` refuses a `canonical_default` that names one.

### Added - N3: a legacy hotkey on Ctrl, Shift or Alt alone imports as unbound

Approved by the owner on 2026-09-26 and recorded as `normalisations.N3` in
`data/config-format.json`, with `drop_rule` `ModifierKey`.

- **C++** `DropRule::ModifierKey` (6). `LegacyVirtualKeyToBindings(code)` gives "" for 0x10-0x12
  and 0xA0-0xA5, and the recording overload adds a `ModifierKey` drop with the code in hex. The
  numbers of the existing rules do not move.
- **C#** `DropRule.ModifierKey` (6), and `LegacyNormalisations.KeyCodeToBindings(int unityKeyCode,
  string section, string key, ICollection<DroppedValue> dropped)`: "" for 0 (`KeyCode.None`), "" and
  a `ModifierKey` drop under the key name for `LeftShift` to `RightAlt`, the key's name otherwise,
  and `ArgumentException` for a code with no name, as `KeyBindings.Format` throws.
- The migration log line is `not carried: [Hotkeys] YawModeKey=0x11, it is a Ctrl, Shift or Alt
  key, which goes down before the key of any chord made with it, so it is unbound`.
- The README config block of a legacy repo, rendered by `scripts/generate-readme.mjs`, and
  `scripts/templates/canonical-config-changelog.md` gain a line for it among the settings not
  carried over. A normalisation approved later stops the block rendering until it has a line or
  is marked as having none.
- Core's REFramework import (`PluginConfigLegacyImport`) does not apply N3, as it does not apply
  N1: the frozen `PluginConfig::Read` it calls already replaces a Ctrl, Shift or Alt code with the
  row's default. The README config block of an REFramework repo, and the changelog template for
  one, leave the N3 line out.

### Changed - trepang2-headtracking keeps its own CycleTrackingModeKey and YawModeKey

`data/config-format.json` `per_game` lists `CycleTrackingModeKey` and `YawModeKey` for
trepang2-headtracking, approved 2026-09-26: in Trepang2 the fleet chords clash with the game's own
bindings, Ctrl+Shift+G throws a grenade and Ctrl+Shift+H dual wields. Its committed file is to hold
`CycleTrackingModeKey=PageUp, Ctrl+Shift+J` and `YawModeKey=PageDown`, its table marks both rows
`PerGame()`, and render-config writes both into its descriptor's `per_game`. The repo still holds
`default` on both rows and an empty `per_game`, so its `config-format` and `config-descriptor`
conformance fail until it changes.

### Added - an import can leave a row to Defaults.ini

A map that applied `follows_default` set the field to the table's built-in default, so the
migration wrote that value explicitly wherever Defaults.ini held another one. A player whose
Defaults.ini says `CollisionEnabled=false` got `CollisionEnabled=true` pinned in the new file,
although the legacy file only held the value the build shipped.

- **C++** `ImportResult::follows_defaults_ini` (`std::vector<schema::Concept>`), and a third,
  defaulted parameter of the same type on `ImportResult::Imported` and `ImportResult::Absent`.
- **C#** `ImportResult.FollowsDefaultsIni`, and `Imported` and `Absent` overloads taking
  `IEnumerable<ConceptDescriptor> followsDefaultsIni`.
- **Migration**: `ConfigOwner` gives each named row the value `default` gives it at that `Load`,
  then renders, so the row is written `default`. A concept that is not a row of the table following
  Defaults.ini throws `std::invalid_argument` (C# `ArgumentException`).
- Nothing changes for an import that names no concept. A map using `FollowsDefault` names the
  row's concept wherever it records that drop (docs/canonical-config.md, "The legacy import").

### Changed - a package below `canonical_since` is a pre-release and warns; only a release below it fails

A converted repo writes the config descriptor's `canonical_since` as the version it will be
released in and keeps its last release's version until the release bumps it. Between the two,
`pixi run validate-manifest`, and `pixi run package` wherever packaging runs it, failed with
`config.canonical_since 0.3.0 is above mod_info.version 0.2.0` (seen in snowrunner, roadcraft and
minecraft-bedrock-edition), which also stopped Lopari's go-local, since it runs `pixi run package`.

- **validate-manifest** passes such a package and prints `WARN <zip>: config.canonical_since 0.3.0
  is above mod_info.version 0.2.0, so this package is a pre-release of 0.3.0; releasing any version
  below 0.3.0 fails`. A pre-release version (`1.1.0-dev.3` under `1.1.0`) is treated the same way.
  Every other descriptor rule is unchanged, the tag rule included: `canonical_since` is still above
  every `v*` tag whose committed config has no stamp.
- **A release below `canonical_since` fails.** In a GitHub Actions build for a `v<x.y.z>` tag,
  validate-manifest and `Copy-SharedBundle` (`check-config-descriptor.mjs --package`) fail a
  `canonical_since` above `x.y.z`. The first release at `canonical_since` passes.
- **Added** `scripts/check-config-descriptor.mjs --release <x.y.z> [repo]`, which runs that rule on
  the committed manifest, and `Assert-ReleaseNotBelowCanonicalSince -RepoRoot -Version [-CoreRoot]`
  in `ReleaseWorkflow.psm1`, which runs it and needs `node` on `PATH` where the repo has a
  `launcher-manifest.json`. `New-ReleaseTag` runs it before it creates the tag.
  `check-config-descriptor.mjs` also exports `preReleaseWarning`, `releaseProblems` and
  `releaseVersionFromEnv`, and `repoReport` takes an optional release version.
- **For a converted mod:** a release below `canonical_since` from a release script that does not
  call `New-ReleaseTag` is stopped only in CI, after the tag is pushed, and only once the mod's
  core pin includes this change. Call `Assert-ReleaseNotBelowCanonicalSince -RepoRoot $projectDir
  -Version $newVersion` right after the version is resolved, before any file is written.

### Changed - BREAKING - `CollisionMargin` and `CollisionChannel` are per game in every mod, and `CollisionEnabled` starts on

Collision rows ruling of 2026-09-26. A margin in the engine's own units and a trace channel number
mean something different in every engine, so neither can be one value for every game; whether the
wall check runs is a preference, so it stays global and starts on.

- **Schema.** `data/config-schema.json` gains a `global` field on every canonical concept: `false`
  on `CollisionMargin` and `CollisionChannel`, `true` on the other 26, which were global before.
  `scripts/generate-config-schema.mjs` refuses a canonical concept without a boolean `global`, so
  a new concept cannot arrive undecided, and refuses the field on a concept that is not canonical.
  `CollisionEnabled`
  gains `"canonical_default": true`, which a canonical file and Defaults.ini start with; its
  `default`, the value the deprecated flat readers (`HeadTrackingConfigData.LoadFromFile`,
  `HeadTrackingConfig`'s parser) and the field initialisers use, stays `false`. The generator now
  takes a `canonical_default` on a bool concept, where it must differ from `default`, as well as
  on a hotkey concept.
- **Generated descriptors.** C# `ConceptDescriptor.Global` (new, public) and C++
  `ConceptTraits<Id>::kGlobal` and `ConceptInfo::global`, appended as the struct's last member so
  existing aggregate initialisation still compiles. `ConceptDescriptor.CanonicalDefault` and
  `kCanonicalDefault` are `"true"` for `CollisionEnabled`, and its default text is `true`.
- **Config tables.** The row of a concept that is not global defaults to the table's own value:
  Defaults.ini never reaches it, `default` on it reads the table's default, the fresh render's
  schema-default gate skips it, and `RenderFresh` writes it as `Render` does, as its value
  (`CollisionMargin=10.0`), or commented at its default where the table marks the row `Engine()`
  (`; CollisionChannel=3`). Whether a row is global does not make it an Engine row. So a
  table whose margin is in centimetres or whose channel is its engine's own number renders fresh,
  and the config owners no longer throw from the constructor over it. `PerGame()` on such a row
  throws `InvalidOperationException` (C++ `std::invalid_argument`): `[Position] CollisionChannel is
  not global in data/config-schema.json, so every game keeps its own value and Defaults.ini never
  reaches it; PerGame() is for a global concept`.
- **`HeadTrackingConfigTable`** starts `CollisionEnabled` at `true`, as it starts the hotkey lists
  at their `canonical_default`. `CollisionChannel` stays an `Engine()` row and `CollisionMargin`
  is not one, so `all-concepts-fresh.ini` writes `CollisionMargin=0.1` and `; CollisionChannel=0`
  instead of `default` on both.
- **Defaults.ini.** Core's global table names the 26 global concepts, so a new Defaults.ini has no
  `CollisionMargin` or `CollisionChannel` line and holds `CollisionEnabled=true`. The reader never
  reads either key: a line for one is absent, with no log line and no message.
- **Scripts.** `check-config-format` refuses a `per_game` entry for a concept that is not global.
  The canonical config lint takes a value or the commented Engine form on a `CollisionMargin` or
  `CollisionChannel` row and refuses `default` there, and its message says a committed file holds
  `default` on every global concept row. The README config block lists `CollisionEnabled=true` as
  the built-in value of a `CollisionEnabled=default` row.
- **Fixtures and tests.** `global/Defaults.ini`, `head-tracking/all-concepts*.ini`, the
  `head-tracking/apply-*` cases and the `global/read-alias` and `global/read-refused-values` cases
  follow; the fixture table's `CollisionChannel` row drops `.PerGame()`, which `PerGame()` now
  refuses there, and keeps `.Engine()`. New checks in both languages: the 26 global ids are pinned
  to the schema by name, a table over the two rows renders fresh at engine values, `default` on
  them ignores the effective defaults, `PerGame()` on them throws, `HeadTrackingConfigTable` starts
  `CollisionEnabled` at true and the flat default stays false, and an owner over a table with
  `CollisionMargin` 10 and `CollisionChannel` 3 creates the file with both at those values while
  `CollisionEnabled` follows Defaults.ini. `ConfigSchemaDefaultsTests` (C++ `config_schema_tests`)
  holds every `canonical_default` in the schema to where core's all-concepts table starts the row,
  and names the five concepts that carry one, beside the existing check that `default` matches the
  flat field. The fresh render's gate refuses a `CollisionEnabled` row starting at `false` and
  writes one starting at `true` as `default`, and `global/read-refused-values` gains a refused
  `CollisionEnabled` line whose message names the built-in `true`.

What a consuming repo changes at its pin bump. Keep the `CollisionMargin` and `CollisionChannel`
defaults the mod ships, in whatever unit and channel its engine uses; drop a `PerGame()` on either
row (none is in the fleet today), and keep or add `Engine()` where the row should be written
commented at its default. A table
that binds `CollisionEnabled` defaults it to `true`; a mod without a lean collision sweep does not
bind it. A converted repo whose own table starts the row at `false`, such as a C++ mod's
`ConfigTable` over a field initialised `false`, fails `RenderFresh` at its pin bump with
`[Position] CollisionEnabled defaults to false, and the schema to true. A fresh file writes default
on this row, ...`, and moves that default to `true` or marks the row `PerGame()` under an
owner-approved `per_game` entry. `HeadTrackingConfigTable` starts the row at `true` whatever the
config type's own initialiser holds. A mod whose wall check has not been confirmed in game then runs
it wherever the row follows Defaults.ini, where a new file holds `true`. Re-run
`pixi run render-config`: the committed file holds `CollisionEnabled=default` and
the two rows at the mod's own values, an `Engine()` row commented. The seven converted repos that
were waiting on this (deus-ex-human-revolution, outer-worlds-spacers-choice-edition, ready-or-not,
sniper-elite-v2-remastered, stalker-shadow-of-chornobyl-enhanced-edition, the-forest, thief) have
bumped their pin to def74d7 or later (six at def74d7, thief at 49f5fec), and each starts
`CollisionEnabled` at `true` and commits `CollisionEnabled=default` (read at each repo's HEAD on
2026-09-26).

### Changed - docs/canonical-config.md describes Defaults.ini in full

- **docs/canonical-config.md** gains "The global defaults file": who reads Defaults.ini, that every
  canonical concept follows it unless the table marks the row `PerGame()` with an owner-approved
  `per_game` entry, where it is on Windows, in a packaged app, under Wine and Proton and natively,
  the `default` token, the effective default, what a new file holds, how it is created and read,
  every log line and in-game message, `Reload` and `FileChanged`, saves and toggles, forward
  compatibility, and what has been run under Wine and Mono and what has not. "Values", "Layout",
  "The canonical concept set", "The config owner", "Load", "Save", "Reload and FileChanged", the
  migration's first launch, "For a launcher or another tool that edits the file" and "Changing the
  format" describe the same behaviour where it touches them.
- **`data/fixtures/canonical-ini/README.md`**: the `preferences/` cases are described as what they
  pin, the owner's `Save` of the four preferences and the tracking mode pair, since no launcher
  writes a game file. The `global/` text no longer says nothing reads the file on disk.

Nothing in the code changes. What a converted repo does at its pin bump, the whole Defaults.ini
change in order (each step is the earlier entry it links):

1. **Set where Defaults.ini is.** `DefaultsFile.PerUser()` / `DefaultsFile::PerUser()` in the mod
   (`PluginModDescriptor::defaults` for REFramework), and `DefaultsFile.At` / `DefaultsFile::At`
   with a scratch path in every test that builds an owner or initialises `PluginMod`; a helper
   shared by the mod and its tests takes the `DefaultsFile` as a parameter
   ([the config owners read and create Defaults.ini](#changed---breaking---the-config-owners-read-and-create-defaultsini)).
2. **Move every global concept row's default to the schema's value** (`CollisionEnabled`'s is
   `true`), or get the owner's approval for a `per_game` entry in core's `data/config-format.json`
   (a core commit) and mark the row `PerGame()`, both rows of the tracking mode pair or neither
   ([`PerGame()` and `RenderFresh`](#added---the-default-token-renderfresh--rendercanonicalfresh-and-pergame)).
   `CollisionMargin` and `CollisionChannel` keep the mod's own defaults and need neither
   ([per game in every mod](#changed---breaking---collisionmargin-and-collisionchannel-are-per-game-in-every-mod-and-collisionenabled-starts-on)).
3. **Re-run `pixi run render-config`** and commit the file: `default` on every concept row except
   the `per_game` ones, and the config descriptor's `per_game` filled by encode-seed
   ([committed configs hold `default` rows](#changed---breaking---committed-configs-hold-default-rows-and-per_game-replaces-descriptor_omits-and-hotkey_exceptions),
   [the config descriptor lists per-game rows](#changed---breaking---the-config-descriptor-lists-per-game-rows-not-preference-values)).
4. **Re-render the README config block** with `pixi run readme --write --sections config` and paste
   `pixi run readme --print config` into NEXUS_MODS.md by hand
   ([the README config block explains Defaults.ini](#changed---the-readme-config-block-and-the-changelog-template-explain-defaultsini)).
5. **Take the changelog bullets** for the repo's kind from
   `scripts/templates/canonical-config-changelog.md` into its `[Unreleased]`.

### Changed - the README config block and the changelog template explain Defaults.ini

- **`scripts/generate-readme.mjs`**: where the committed file holds `default` rows, the config block
  says, once, after the file's location: that a setting set to `default` takes its value from
  Defaults.ini, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads,
  that head tracking mods keeping their settings in another file do not, nor (in a `legacy` repo)
  earlier versions of the mod, that writing a value changes that game only, and that a hotkey
  change the mod saves writes a value over `default`, so that row stops following Defaults.ini in
  that game; the three locations the file's header names, and that the mod's log names the file it
  read; that the mod creates Defaults.ini with the built-in values when it finds none, unless
  Windows runs the game as a packaged app, and never changes it afterwards. A `legacy` repo's
  paragraphs add what the import writes (`default` where the imported value equals the row's
  default at that start, the tracking-mode pair together) and that the rows the reset sets to
  `default` follow Defaults.ini. For a repo whose `dialect` is `unity`, the repos whose config
  owner is core's C# one, the block adds that on Linux and macOS without Wine or Proton the mod
  reads its settings and saves none: it creates neither Defaults.ini nor `CameraUnlock.ini`, in a
  `legacy` repo imports the legacy file again at every start, and a change made in game lasts until
  the game closes. Before the file, the block lists the built-in value of each concept row
  set to `default`, from `data/config-schema.json` (the hotkeys at `canonical_default`); a game's
  local row may hold the word as data and is not listed. A committed file with no `default` row
  renders exactly the block it rendered before. `configBlock` is exported and the command line
  runs only when the script is run directly, by its real path, so a run through a junction or
  symlink still runs it (as `check-canonical-config.mjs`, `check-config-descriptor.mjs` and
  `encode-seed.mjs` now do too).
- **`scripts/templates/canonical-config-changelog.md`**: the Legacy bullets gain the import's
  `default` rule and the reset sentence's Defaults.ini clause; an Outside legacy bullet says a new
  `CameraUnlock.ini` sets those rows to `default`; every converted repo takes the Defaults.ini
  bullets (what `default` means and who reads the file, that a saved hotkey change stops a row
  following it, where it is, that the mod creates it and never changes it), and a repo whose
  `dialect` is `unity` the native Linux and macOS read-only bullet for its listing.
- **`pixi run test-generate-readme`**, part of `pixi run check`, renders the block for synthetic
  repo states over the fresh example file and checks the Defaults.ini text and every built-in value
  against `data/fixtures/canonical-ini/global/Defaults.ini`, and over a file of values against the
  blocks captured before this change (`data/fixtures/readme/`).
- `data/config-format.json`'s `_comment` records that `dialect` `unity` is how the block knows a
  repo's owner is C#.

What a consuming repo changes. This is the last core change a converted repo waits on before its
pin bump (C7 to C9). After the bump and `render-config`, run
`pixi run readme --write --sections config`, take the changelog template's new bullets into
`[Unreleased]`, and paste `--print config` into NEXUS_MODS.md by hand. A repo whose committed file
still holds values keeps the block it has.

### Changed - BREAKING - the config descriptor lists per-game rows, not preference values

A launcher now writes no game file: it edits Defaults.ini and reads a game's `CameraUnlock.ini`
for display only. So the `config` block in `launcher-manifest.json` no longer carries the
preference values a launcher once wrote. It holds `path`, `anchor`, `legacy_source`,
`canonical_since` and `per_game`, and nothing else (docs/canonical-config.md, "The config
descriptor").

- **`per_game`** maps each concept id `data/config-format.json` `per_game` lists for the repo to the
  value text the committed file holds on that row, and is `{}` for a repo with none. A launcher
  uses it to show which rows a game keeps as its own and their values, including where the file
  holds `default` or leaves the row out.
- **`scripts/check-config-descriptor.mjs`** refuses `rows` by name, and fails a `per_game` whose ids
  are not exactly the repo's `per_game` rows (a missing id and an extra one both fail), a value
  that is not the committed file's text on the row, a value that is not text, and a value that is
  `default` in any case. A `per_game` row the committed file holds `default` on, or has no line
  for, fails too. A row commented out the way the renderer writes an Engine row marked
  `PerGame()` at its default holds the commented value. The five launcher rows, their `true` or
  `false` rule, the tracking pair against `preference_modes`, the `PositionAllowed=false`
  exception and the rule that a committed `WorldSpaceYaw` away from the schema default needs a
  `per_game` entry are gone: `RenderFresh`'s schema-default gate and the lint's `per_game` rule
  hold a game's own rows now. A fresh render's `default` rows no longer draw a descriptor problem.
  The rules for `path`, `anchor`, `legacy_source`, `canonical_since`, `delivery_mode`, one config
  file and no seed are unchanged. `expectedRows` and `LAUNCHER_ROWS` are replaced by
  `expectedPerGame`.
- **`scripts/encode-seed.mjs`** writes `per_game` in place of `rows`, from `data/config-format.json`
  and the committed file, one row per line, changing no other byte of the manifest; `--check`
  exits 1 when `per_game` is stale. It refuses a block that still has `rows`. `encodeRows` is now
  `encodePerGame`.
- **`validate-manifest`**, conformance's **`config-descriptor`** check and
  **`Assert-LauncherManifestConfig`** (run by `Copy-SharedBundle`) share those rules, so each refuses
  `rows` and a stale `per_game`. `Assert-LauncherManifestConfig` takes the same parameters as before.
  The `ConvertFrom-Json` / `ConvertTo-Json -Depth 10` round trip packaging uses to stamp
  `mod_info.version` keeps `per_game` intact, `{}` included; `pixi run test-config-descriptor` runs
  it.

What a consuming repo changes. No manifest in the fleet carried a `config` block on 2026-09-26, so
nothing has to be migrated. A converted repo delivered by manifest writes its block with
`"per_game": {}` in place of `rows` and runs `render-config`, which fills `per_game` through
encode-seed. `Assert-LauncherManifestConfig` and `validate-manifest` refuse a block with `rows`.

### Changed - BREAKING - committed configs hold `default` rows, and `per_game` replaces `descriptor_omits` and `hotkey_exceptions`

- **The canonical config lint** (`scripts/check-canonical-config.mjs`, which conformance runs as
  `config-format`) reads `default` as `scripts/lib/canonical-ini.mjs` spells it: the value, trimmed,
  equal to `default` without case, on a concept row only. In a committed file every concept row
  holds it, except the rows `data/config-format.json` `per_game` lists for the repo, which hold the
  game's own value and never the token; a `per_game` hotkey row holds a key list in the file's
  dialect. The rule that a hotkey concept holds its `canonical_default` is gone, since those rows
  hold `default` too. `default ; note`, `"default"` and `End, default` are values, not the token.
  On a local row `default` is an ordinary value. A concept row commented out under its own section
  (`; CollisionChannel=3`, the form the renderer gives an Engine row marked `PerGame()` at its
  default) fails unless `per_game` lists it, so a `PerGame()` mark cannot keep a row from
  Defaults.ini without an owner-approved entry. The lint's `exceptions` option is now `perGame`,
  the repo's `per_game` concept ids, and a call without it throws.
- **`data/config-format.json` `per_game`**, per repo in `configs`, a list of `{row, reason,
  approved}`: `row` a canonical concept id, `reason` why the game keeps that row for itself, and
  `approved` the date the owner approved it, never null. The game's table marks each row
  `PerGame()`. A chord a game binds itself is a `per_game` hotkey row whose reason names the chord
  it replaces and the one it uses. An entry added after a repo's first converted release changes
  what `default` means in files already on players' disks, so it is a breaking change for that
  repo. It replaces `descriptor_omits`, whose one entry (subnautica-headtracking, `WorldSpaceYaw`,
  approved 2026-09-25) moved over, and `hotkey_exceptions`, which was empty.
  `scripts/check-config-format.mjs` checks the shape and refuses the two old keys by name, and a
  list that names one of `RotationEnabled` and `PositionEnabled` without the other, which no table
  can match since `PerGame()` marks both or neither.
  `scripts/check-config-descriptor.mjs` leaves a `per_game` launcher row out of `rows` where it
  left a `descriptor_omits` one, with the same meaning, until the descriptor drops `rows`.
- **`config-defaults`**, a new conformance check for converted repos. It fails a tracked C#, C++ or
  header source outside a test folder that names `DefaultsFile.At` / `DefaultsFile::At`; one inside
  a test folder that names `DefaultsFile.PerUser` / `DefaultsFile::PerUser`; and one inside a test
  folder that builds a `ConfigOwner` or initialises `PluginMod` and never names `At`. A test folder
  is a folder named `test` or `tests` in any case, or one whose name ends in `Tests`.
- **`pixi run config-report`** lists each repo's `per_game` rows, with the value its canonical
  committed file holds there, in place of committed concept values that differ from the schema
  default.

What a consuming repo changes. A converted repo's committed file fails the lint until it is
re-rendered after its pin bump: `pixi run render-config` writes `default` on every concept row
except the `per_game` ones. Its tests and mod pass `DefaultsFile.At` and `DefaultsFile.PerUser()`
as `config-defaults` asks, which the pin bump to the Defaults.ini owner needs anyway. The config
descriptor check still reads only `true` or `false`, so it refuses `default` on `PositionAllowed`
and on every launcher row (`EnableOnStartup`, `WorldSpaceYaw`, `RotationEnabled`,
`PositionEnabled`, `TrueFreeLook`), six problems on a fresh render, and the README config block
does not yet explain `default`, until the next core commits, so a converted repo still bumps its
pin after those.

### Added - Defaults.ini probe modes and `pixi run test-linux-probe`

- **Probe modes.** `cameraunlock_tests --probe-defaults-ini <game folder> [--probe-save]` and
  `CameraUnlock.Core.FrameworkTests.exe --probe-defaults-ini <game folder> [--probe-save]
  [--probe-legacy]` (net35 and net472) print tab-separated lines: the resolver's probed inputs, the
  candidates with their kinds and shown paths, whether each file exists, the choice made from that,
  then `Load` of the docs example's table over `CameraUnlock.ini` in the game folder with
  `DefaultsFile.PerUser()`, the status, every log line and status-sink message, with `--probe-save`
  one `Save` of the yaw toggle and its status and reason, and last the path and SHA-256 of every file
  in the game folder and in each candidate's folder. `--probe-legacy` loads the owner scenarios' table
  and legacy import over `HeadTracking.ini` instead. Exit 0 when the probe ran, and 1 with an
  `error` line on an exception. A missing game folder or any other argument prints the usage to
  standard error and exits 2.
  They find and create the player's own Defaults.ini, so no suite runs them; the container task does.
- **`pixi run test-linux-probe`**, not part of `check` since it needs Docker with a Linux engine.
  It builds `containers/linux-probe` (Debian trixie pinned by digest, apt pinned to
  snapshot.debian.org at 2026-09-11, Debian's Wine 10.0 and Mono 6.12, and wine-mono 9.4.0 from its
  MSI pinned by URL and the SHA-256 Wine's own source names), the only step that uses the network.
  Each case then runs in a container with no network, a read-only root, every capability dropped,
  no-new-privileges, a non-root user, tmpfs for the homes and Wine prefixes, and the build outputs
  mounted read-only. Under Wine it runs the C++ probe and both FrameworkTests builds with a home
  whose name is not ASCII: `XDG_CONFIG_HOME` unset, set, relative, below a missing parent (with
  Wine's menu builder off, and as Wine starts by default), the host config folder unwritable, two
  prefixes sharing one home, and a prefix file made before the host file. It also runs both
  FrameworkTests builds over a legacy `HeadTracking.ini` against a hand-written host Defaults.ini,
  where the migrated file writes `default` for the port the two agree on and values for the rest.
  wine-mono runs both builds on its 4.0 runtime. Natively on Mono, which also runs both builds on
  its 4.0 runtime, it runs them over a legacy file, a `CameraUnlock.ini` and nothing, with
  Defaults.ini at `$XDG_CONFIG_HOME`, `~/.config`, `~/Library/Application Support`, the first with
  the second, the second with the third, at none of them, and with `HOME` unset with and without
  `XDG_CONFIG_HOME`. Every case's output is checked against its expectation, including that nothing
  else reached standard output and nothing but Wine's own session-start line reached standard error,
  and any difference fails the task. Output goes to
  `build-linux-probe/`.
- The C++ docs example's config type moved to `cpp/tests/canonical_config_example.h`, so the probe
  loads the same table, and docs/canonical-config.md shows it as its own block. The test binary now
  links bcrypt for the probe's SHA-256.

Nothing for a consuming repo to change.

### Changed - BREAKING - the config owners read and create Defaults.ini

`ConfigOwner` (C# and C++) and core's REFramework `PluginMod` now read Defaults.ini, the file every
concept row not marked `PerGame()` takes its default from, and create it with the built-in values
where none exists. Owner answers of 2026-09-25: all 28 canonical concepts are global, and migration
writes `default` where the imported value equals what `default` gives at that launch.

- **Load** first finds Defaults.ini (the resolver and choice of the previous entries), creates it
  where the choice allows (the `CameraUnlock` folder one level only, then the checked writer with no
  expected bytes, so a file another program created at the same time is read instead), whatever
  becomes of the game's own file, a migrated game included. It never creates it in a packaged app.
  It reads it once, and each concept row not marked `PerGame` that the file gives an accepted value
  for starts from that value, through the row's own codec and setter. `default`, a missing key and
  an invalid value on such a row read that value, or the row's own default where Defaults.ini gives
  none. Every failure to find, create or read it gives the built-in values and one log line; only
  `IOException`, `UnauthorizedAccessException`, `CheckedWriteException` and a Win32 error in C++ are
  caught.
- **A created file** is the table's fresh render (`RenderFresh` / `RenderCanonicalFresh`), `default`
  on every concept row not marked `PerGame`. **A migrated file** writes `default` on such a row where
  the imported value equals what `default` gives at that Load (floats by their bits, hotkey lists by
  their canonical text), and the value otherwise; the tracking mode pair is `default` on both rows
  only when both are equal. The read-back that guards the migration reads over the same Defaults.ini
  values, so a `default` row reads back as the value it replaced.
- **Log lines**, returned with the rest: where Defaults.ini is and what happened to it
  (`Defaults.ini: <path> (read)`, `(created with the built-in values)`, `(created by another program
  at the same time, and read)`, the Wine forms, or one failure line); then, naming the game's file,
  `from Defaults.ini: UdpPort=4242; ToggleKey=End, Ctrl+Shift+Y`, `set in this file, so Defaults.ini
  does not change them: UdpPort, WorldSpaceYaw.` and `built-in, not set in Defaults.ini: ...`; and a
  line for each value Defaults.ini holds that this game would take and cannot use. The Defaults.ini
  paths show `%AppData%` or `~` in place of the profile or home folder.
- **The status sink** gets, after the game file's own message, at most one about Defaults.ini per
  Load or Reload: that it cannot be read, else that values this game would take from it are refused
  (`Defaults.ini: 1 setting cannot be used (ToggleKey=Mouse4), so this game uses its built-in values
  for them. The log has the details.`), else that two Defaults.ini files exist and one is ignored.
- **Save** reads the file over the session's Defaults.ini values, requires every unedited row to keep
  where its value came from, never touches Defaults.ini, and does not depend on it. A save that turns
  a `default` or missing row into a value returns `<path>: WorldSpaceYaw=false is now set for this
  game, and no longer follows Defaults.ini.` in its log, including for `Saved`.
- **Reload** reads Defaults.ini again where Load found it: new readable bytes replace its values, and
  a file that went missing or cannot be read keeps them, with one line and one message. **FileChanged**
  is true when either file's write time changed.
- **C# off Windows**: the owner no longer throws `PlatformNotSupportedException`. When
  `Environment.OSVersion.Platform` is not `Win32NT` it reads `CameraUnlock.ini`, or imports the
  legacy file in memory, or runs on the defaults, reads the first native Defaults.ini candidate,
  creates and writes nothing, and loads with the new status `ConfigLoadStatus.ReadOnly` (6), whose
  message is `Settings are read but not saved on this system: this version saves settings only on
  Windows, including under Wine and Proton. Changes made in game last until the game closes.` Every
  `Save` returns `NotSaved` with `Settings not saved: this version saves settings only on Windows.`
  C++ has no such status: a C++ mod always runs as a Windows program.
- **New public API**: C# `DefaultsFile` (`PerUser()`, `At(path)`), `ConfigOwnerOptions.Defaults`,
  `ConfigLoadStatus.ReadOnly`; C++ `config::DefaultsFile` (`PerUser()`, `At(path)`) in
  `cameraunlock/config/defaults_file.h`, `ConfigOwnerOptions::defaults` and
  `PluginModDescriptor::defaults`, appended as the last member. The load, save and reload results
  gain no member.

What a consuming repo changes at its pin bump. A converted repo does not bump to this commit on its
own: until the lint, the config descriptor check and the README generator read `default` (the
design's C7 to C9, which follow), core's own gates reject the file it commits here.
`check-canonical-config.mjs`, which conformance runs, refuses `ToggleKey=default` and every other
hotkey `default` row, `check-config-descriptor.mjs` refuses `default` on `PositionAllowed` and on
every launcher row, and the generated README block fences `default` rows it does not explain. Bump after C9.

- **Set the Defaults.ini option.** In the mod, `Defaults = DefaultsFile.PerUser()` (C#) or
  `options.defaults = DefaultsFile::PerUser()` (C++). In every test that builds an owner,
  `DefaultsFile.At` / `DefaultsFile::At` with a scratch path. A helper that builds the options for the
  mod and its tests takes the `DefaultsFile` as a parameter, so a test never reads or creates the
  player's own file. The constructor throws `ArgumentException` (C++ `std::invalid_argument`) when it
  is unset, naming both factories.
- **An REFramework mod** sets `PluginModDescriptor::defaults = config::DefaultsFile::PerUser()`.
  `PluginMod::Initialize` throws `std::invalid_argument` without it when `canonicalConfig` is set,
  as it does for `gameName`.
- **A table whose concept row not marked `PerGame` has a default other than the schema's now throws
  from the owner's constructor**, since the owner renders its fresh file there. Move the row's
  default to the schema's value, or, with an owner-approved `per_game` entry, mark it `PerGame()`.
  A table that binds `RotationEnabled` without `PositionEnabled` throws the same way.
- **Re-run `pixi run render-config` and commit the file**: the committed file is now the fresh
  render, `default` on every concept row not marked `PerGame`, which is what the owner creates.
  A render test compares with `RenderFresh` / `RenderCanonicalFresh`.
- **Tests** that compared a created file with `Render` of the defaults compare with the fresh render,
  and a migrated file holds `default` where the imported value equals the built-in one. A test that
  counts the load's log lines counts the Defaults.ini lines too.
- **Write every line of a save's `Log`, including for `Saved`.** Before this commit that log was
  empty for `Saved`, so a mod that writes it only for `NotSaved` and `Uncertain` never writes
  `<path>: WorldSpaceYaw=false is now set for this game, and no longer follows Defaults.ini.`
  Core's `PluginMod` now writes it.
- **The C# owner no longer throws off Windows** and runs read-only there, so a C# mod that caught
  `PlatformNotSupportedException` around it drops the catch.

### Added - where Defaults.ini is, internal to core

The pieces the config owner finds Defaults.ini with (the entry above). Nothing here is public API.

- **The resolver**: C# internal `DefaultsLocation.Resolve(DefaultsProbe)`, C++
  `detail::ResolveDefaults` in `cameraunlock/config/defaults_location.h`. Pure: from what the
  probes found it gives the candidates in order, each with its path, kind, whether it may be
  created and the path as the log shows it (`%AppData%\...`, `~\...`, `~/...`, and with no home
  known the XDG folder by its variable, `$XDG_CONFIG_HOME/...`), or none with the reason. On
  Windows the one candidate is under the roaming AppData known folder, and a packaged process
  never creates it. Under Wine the host's config folder comes first where Wine maps it to
  a drive letter, then the prefix's AppData. Natively, `$XDG_CONFIG_HOME` or `$HOME/.config`, then
  `$HOME/Library/Application Support`, none created.
- **The choice**: C# `DefaultsLocation.Choose`, C++ `detail::ChooseDefaults`. Pure: by which
  candidates' files exist and what became of each creation tried, the file to read or the
  candidate to create next, and then the one log line and, when two files exist, the in-game
  message, in the design's words.
- **The probes**: C# `DefaultsLocation.Probe`, C++ `detail::ProbeDefaults`. C++ finds
  `SHGetKnownFolderPath` and `CoTaskMemFree` through `LoadLibraryW` and `GetProcAddress`, so no
  mod gains a static import of shell32 or ole32; C# asks `Environment.GetFolderPath`. Neither reads
  the `APPDATA` variable. `GetCurrentPackageFullName` and every wine export are found through
  `GetProcAddress`; C# calls the wine exports through cdecl delegates and
  `GetCurrentPackageFullName` through a stdcall one, never a `DllImport` of either.
  Natively the C# probe reads `HOME` and `XDG_CONFIG_HOME` and calls no native code.
- **The folder**: C# `DefaultsLocation.CreateFolder`, C++ `detail::CreateDefaultsFolder`, which
  create only the `CameraUnlock` folder with `CreateDirectoryW`; `ERROR_ALREADY_EXISTS` counts as
  done and a missing parent is a failure. A file named `CameraUnlock` also gives
  `ERROR_ALREADY_EXISTS`, so there it is creating Defaults.ini that fails. C# declares the call
  beside the checked writer's kernel32 imports.
- **Fixture**: `data/fixtures/canonical-ini/global/resolve.tsv`, run by the C++ suite, xunit and
  both FrameworkTests targets, which compare the candidates, shown paths, reads, creations, lines
  and messages. On Windows a test in each language also runs the real probe and checks it creates
  nothing.

### Fixed - Defaults.ini's reasons and lines match in C# and C++ for bytes that are not UTF-8

A Defaults.ini saved as ANSI is read, so a value can hold bytes that are not UTF-8, such as a `£`
typed as a hotkey. C# decoded them with `Encoding.UTF8` and C++ copied them raw, so the refused
value's reason and log line differed between the languages, and C++ wrote bytes that are not
UTF-8 into a UTF-8 log line. Both now write each maximal subpart of an ill-formed sequence as
U+FFFD and keep well-formed UTF-8 as it is. C# does it by hand in the internal
`CodecText.Utf8Text`, because .NET Framework's decoder substitutes differently from that rule
(it reads `F0 80 80` as two U+FFFD, not three). The new `global/read-ansi` case pins it.

Public API: `HotkeyCodec.TryParse` (C#) and the list and color codecs' item errors now decode
through the same function, so on .NET Framework an error quoting such bytes can hold a different
number of U+FFFD than before. What they read is unchanged. The game-file diagnostics are not
changed by this entry, and they still differ between the languages for such bytes.

### Added - Defaults.ini's render and reader, internal to core

The pieces the config owner reads and creates Defaults.ini with (the entry above). Nothing here is
public API.

- **Core's global table and the render**: C# internal `DefaultsIni.Table()` and
  `DefaultsIni.Render()`, C++ `detail::DefaultsIniTable()` and `detail::RenderDefaultsIni()` in
  `cameraunlock/config/defaults_ini.h`. The table is `HeadTrackingConfigTable` naming all 28
  canonical concepts; the render writes its defaults, the four hotkey lists at their
  `canonical_default`, every row as a value (`CollisionChannel` included), under a header of its
  own that lists the 104 key names with a Windows virtual-key code, which is the only hotkey
  dialect the file takes. Both languages give `data/fixtures/canonical-ini/global/Defaults.ini`
  byte for byte, and a test in each holds the header's list to `data/keys.json`.
- **The reader**: C# internal `DefaultsIni.Read(bytes)`, C++ `detail::ReadDefaultsIni(bytes)`. A
  file saved as UTF-16 or holding a NUL is not read, with the owner's words for why. Any other file
  is read whatever its stamp; a newer `ConfigFormat` draws one line. Each canonical concept is found
  by section and key and comes out absent, accepted or refused. A value is refused when the
  concept's codec with the schema's range does not read it, `default` included, and a hotkey list
  also when a key is not one of the 104 names, so `ToggleKey=Mouse4` and `ToggleKey=0x23` are
  refused alike in C# and C++. The tracking-mode pair is checked once on the file's own rows: when
  either row is refused, or the two are both false, both are refused together.
- **Line texts** shared by both languages: `DefaultsIni.RefusedLine` / `detail::DefaultsIniRefusedLine`,
  which takes the game's built-in text, e.g. `Defaults.ini: line 12: [Hotkeys] ToggleKey=Mouse4 is
  not read (Mouse4 is not one of the key names this file takes), so the built-in End, Ctrl+Shift+Y
  is used.`, and `DefaultsIni.PairLine` / `detail::DefaultsIniPairLine` for a refused pair, e.g.
  `Defaults.ini: lines 2 and 4: [General] RotationEnabled=false and [Position]
  PositionEnabled=false are not read (both false is not a tracking mode), so the built-in
  RotationEnabled=true and PositionEnabled=true are used.`
- **Fixtures**: `global/Defaults.ini` and 24 `global/read-*` cases, run by the C++ suite, xunit and
  both FrameworkTests targets. `data/fixtures/canonical-ini/README.md` defines them.
- **Internal to the table**: C# `ConfigTable.RenderValues` and C++ `detail::RenderCanonicalValues`
  write every row as its value under a given header, which the Defaults.ini render uses. `Render`,
  `RenderFresh` and the migration render write the same bytes as before.

### Changed - BREAKING - a table with a concept row not marked `PerGame` renders six lines on `default`

Every render of a config table (C# `ConfigTable.Render`, C++ `RenderCanonical`, and the new fresh
render below) writes six more header lines whenever the table has a concept row that is not marked
`PerGame()`. They come after the hotkeys line, or after the comments line when the table has no
hotkey row, and before the blank line:

```text
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.
```

A migrated file and a file the owner creates carry the lines too: the owner creates with the fresh
render and migrates with the migration render (see the Defaults.ini entry above).

Consuming repos: every converted repo's committed config and its render test compare bytes, so
after bumping the pin, re-run `pixi run render-config` and commit the file. The example, the
all-concepts file and the table fixtures under `data/fixtures/canonical-ini/` gained the lines.

Do not release a mod built on a core pin from before the owner reads Defaults.ini (the entry
above): on such a pin the lines tell players about a file the mod does not read. No converted mod is
released before the Wine and native Mono container runs of the design pass (its C11).

### Added - the `default` token, `RenderFresh` / `RenderCanonicalFresh` and `PerGame()`

Owner answers of 2026-09-25: all 28 canonical concepts are global, and migration writes `default`
where the imported value equals what `default` gives at that launch.

- **The token.** On a concept row, a value equal to `default` in any ASCII letter case, after the
  reader's trimming, reads as the row's default and draws no diagnostic, the way a missing key
  does. `default ; note`, `"default"` and `End, default` are values like any other and go to the
  codec. On a local row the word is data: a string row stores it, a bool row refuses it. Before
  this change the word drew `InvalidValue` on every concept row.
- **`PerGame()`**, C# `ConfigTable.PerGame()` and C++ `ConfigTable::PerGame()`, marks the last or
  selected concept row as one the game keeps: its default stays the table's own, never Defaults.ini's.
  It throws `InvalidOperationException` (C++ `std::invalid_argument`) on a local row. Each use needs
  an owner-approved `per_game` entry for the repo in `data/config-format.json`, which a later core
  commit adds. `RotationEnabled` and `PositionEnabled` are one setting, the tracking mode, so a
  table that binds both marks both or neither. `Apply`, the fresh render and the migration render
  throw `ArgumentException` (C++ `std::invalid_argument`) on a table that marks one alone:
  `[Position] PositionEnabled is marked PerGame() and [General] RotationEnabled is not. The two are
  one setting, the tracking mode, so PerGame() marks both or neither.` Starting the pair from two
  sources could otherwise name no tracking mode from a Defaults.ini pair that is valid on its own.
- **`RenderFresh(header)`**, C++ `RenderCanonicalFresh(table, header)`: the table's defaults with
  every concept row that is not `PerGame` written `Key=default`, an Engine row included; `PerGame`
  and local rows as `Render` writes them. It throws `ArgumentException` (C++ `std::invalid_argument`)
  when such a row defaults to anything but the schema's `default` (a hotkey list's
  `canonical_default`, floats compared by their bits), naming the row:
  `[Network] UdpPort defaults to 5, and the schema to 4242. A fresh file writes default on this
  row, which takes Defaults.ini's value, so the row's own default must be the schema's, or the row
  must be marked PerGame().`, and when the table binds `RotationEnabled` without `PositionEnabled`.
  `HeadTrackingConfigTable` naming every concept passes it
  (`data/fixtures/canonical-ini/head-tracking/all-concepts-fresh.ini`). Seven converted repos
  default `CollisionEnabled`, `CollisionMargin` or `CollisionChannel` off the schema, several in the
  engine's own units; the gate no longer applies to the margin and the channel, which are per game
  in every mod, and `CollisionEnabled` now starts on
  ([per game in every mod](#changed---breaking---collisionmargin-and-collisionchannel-are-per-game-in-every-mod-and-collisionenabled-starts-on)).
- **Internal, for the owner's later adoption**: an `Apply` overload over effective defaults that
  reports where each row's value came from (the file, Defaults.ini or the table), and a migration
  render that writes `default` where a value equals its effective default (C++
  `detail::ApplyCanonicalEffective` and `detail::RenderCanonicalMigration`). The public `Apply` is
  that overload over the table's own defaults, so it differs from before only where a concept row
  holds the token.
- **Schema**: each canonical concept's default as text its codec reads, C# internal
  `ConceptDescriptor.DefaultText`, C++ `ConceptTraits<Id>::kDefaultText` and
  `ConceptInfo::default_text`, appended as the struct's last member so existing aggregate
  initialisation still compiles. `scripts/generate-config-schema.mjs` refuses a key name, alias or
  modifier in `data/keys.json` spelled `default` in any letter case (none is), and
  `data/config-schema.json` says every canonical concept is global, so a concept that must never
  follow Defaults.ini needs a schema change before it is added.
- **Fixtures**: `table/global-*` (the token on each row type and letter case, what is not the token,
  a missing key and an invalid value, a `PerGame` row, local rows, the tracking-mode pair over
  effective defaults, the fresh and migration renders), read by both suites and both
  FrameworkTests targets. The fixture table's `CollisionChannel` row is `PerGame`, which pins that a
  `PerGame` Engine row keeps its commented form.

### Changed - BREAKING - the C# config owner imports a legacy file beside the config and never writes it

Owner decision of 2026-09-25: settings live in `CameraUnlock.ini`, in the folder that holds the
game's legacy file (`BepInEx\config\CameraUnlock.ini` for a BepInEx plugin). `ConfigOwner<TConfig>`
no longer converts a file in place and no longer writes `.pre-canonical` or `.pre-canonical.last`
copies. The C++ owner changes the same way in a following commit.

- **Load**: a file at `Path` is read as canonical, stamped or not; the import never runs and the
  legacy file is never opened. When `Path` is absent and `LegacySourcePath` exists, the legacy
  file is imported and `Path` is created from it, never over a file that appears meanwhile. When
  neither exists, `Path` is created from the defaults. The legacy file is never written, renamed,
  deleted or copied, on any path, deferrals included. Deleting `CameraUnlock.ini` imports the
  legacy file again at the next launch.
- **Log**: when `Path` is read and a legacy file also exists, the log holds
  `<Path>: settings are read from this file. <LegacySourcePath> is left as it was and is not read.`
  An import that completes logs `<Path>: created from <LegacySourcePath>, which is left as it was.`
- **Player message** for a Deferred or LegacyRefused import, including a legacy file that cannot
  be opened:
  `<legacy file name> was not imported into <config file name>: <why>. The mod tries again at the
  next launch and saves nothing this session.`
  When another program created `Path` before the commit, the next launch reads that file and does
  not import, and the message says so:
  `<legacy file name> was not imported into <config file name>: <why>. The mod saves nothing this
  session and reads <config file name>, not <legacy file name>, at the next launch.`
- **Save** no longer refuses an unstamped file; the first save that changes a row stamps it.
  **Reload** reads only `Path` and never runs the import.

Consuming repos (no converted mod has been released):

- Set `Path` to `CameraUnlock.ini` and `LegacySourcePath` to the legacy file (`HeadTracking.ini`,
  `<GUID>.cfg` or whatever the game's last build read). `LegacySourcePath` is now required with
  `Import`: an `Import` without it throws `ArgumentException` from the constructor.
- `LegacyImportInput` takes one path, the legacy file, as `Path`. `LegacyImportInput.LegacySourcePath`
  is gone, so an import that read it no longer compiles; read `input.Path`.
- `ConfigReloadStatus.LegacyReadOnly` (2) is gone. `Unreadable` stays 3.
- Nothing writes `.pre-canonical` or `.pre-canonical.last` any more.

### Changed - BREAKING - the C++ config owner and REFramework mods import the legacy file into `CameraUnlock.ini`

`cameraunlock::config::ConfigOwner<Config>` follows the C# owner above, with the same decisions,
the same log lines and the same player messages. It no longer converts a file in place and writes
no `.pre-canonical` or `.pre-canonical.last` copies.

- **Load**: a file at `path` is read as canonical, stamped or not; the import never runs and the
  legacy file is never opened. When the legacy file also exists (`GetFileAttributesW`, nothing
  opened), the log holds `<path>: settings are read from this file. <legacy_path> is left as it
  was and is not read.` When `path` is absent and `legacy_path` exists, the legacy file is held
  open, imported, checked again, rendered and read back, and `path` is created from it with a
  create-if-absent checked write; a file that appears at `path` meanwhile defers. When neither
  exists, `path` is created from the defaults. The legacy file is never written, renamed, deleted
  or copied.
- **A legacy path outside the ANSI code page**: the published build found no file there and ran
  on its defaults, so those defaults are written to `path` and the legacy file is left as it was.
- **Save** no longer refuses an unstamped file; the first save that changes a row stamps it.
  **Reload** reads only `path` and never runs the import.

Consuming repos (no converted mod has been released):

- A native mod with an import sets `path` to `CameraUnlock.ini` and the new
  `ConfigOwnerOptions::legacy_path` to its legacy file, both fully qualified. `legacy_path` is
  required with an import `run` and refused without one, when it is not fully qualified, and when
  it names the file `path` names (compared without case); each throws `std::invalid_argument`
  from the constructor.
- `ConfigReloadStatus::LegacyReadOnly` (2) is gone. `Unreadable` stays 3.
- `detail::OwnerKeepOriginal` and `detail::OwnerKept` are gone, and so are the hook's `Copy.` and
  `ReadBack` steps.
- **REFramework mods** with `PluginConfigSchema::canonicalConfig` now keep their settings in
  `reframework\plugins\CameraUnlock.ini`, beside the plugin DLL, and import
  `PluginModDescriptor::configFileName` (`HeadTracking.ini` by default) once, while
  `CameraUnlock.ini` is absent. That file is never written. The non-canonical path, and RE8's
  InvertX migration inside it, are unchanged. The mod's `PRESERVE_FILES` must list both
  `CameraUnlock.ini` and the legacy file.

### Changed - `data/config-format.json` records every config as `CameraUnlock.ini`, with its legacy file beside it

Every `installed` path keeps its folder and names `CameraUnlock.ini`
(`BepInEx\config\CameraUnlock.ini`, `reframework\plugins\CameraUnlock.ini`,
`GoneHome_Data\Managed\CameraUnlock.ini`). `legacy_source` is now a bare file name, the file the repo's pre-canonical builds read, in the folder of each
installed path: the BepInEx `<GUID>.cfg`, or the old installed file name (`HeadTracking.ini`,
`MinecraftHeadTracking.ini` for minecraft-bedrock-edition, which has no installed path). It is set
for every repo in `legacy` and null for every other repo, the four unpublished BepInEx repos
included. `schema_version` stays 1: only core's own scripts read the file.

- `check-config-format` fails an installed path not named `CameraUnlock.ini`, and a
  `legacy_source` that holds a folder, names `CameraUnlock.ini`, is null for a repo in `legacy` or
  is set for a repo outside it.
- `check-config-descriptor`: a block's `legacy_source` is the folder of `config.path` and the
  entry's name, and a seed or `files[]` row that lands on the legacy file beside any installed
  path fails. A converted repo delivered by manifest with one stamped recorded config and no block
  now fails, since every entry is recorded under `CameraUnlock.ini`; the exemption for a repo
  converted in place is gone.
- Conformance `config-preserve`: `PRESERVE_FILES` must list every installed path and, for a repo
  in `legacy`, the legacy file in the folder of each one.
- The README config block and `scripts/templates/canonical-config-changelog.md` have one wording
  for a repo in `legacy`: the import from the legacy file, which is never changed, what an older
  version reads, and how to import again or reset. A BepInEx mod adds that ConfigurationManager no
  longer lists (outside `legacy`: does not list) the settings. The `.pre-canonical` wording is gone.

Consuming repos: a converted repo moves its config to `CameraUnlock.ini`, lists the legacy file in
`PRESERVE_FILES`, and re-renders its README block with `pixi run readme --write --sections config`.
The code change comes first, or in the same commit: the README block and a config descriptor
describe `CameraUnlock.ini`, and no check compares either with the path the mod's code reads, so
written first they pass and describe a file the build does not read. The `config-descriptor`
finding for a missing block says so.

### Fixed - conformance `config-preserve` fails a converted install that ships its config

A converted release seeds nothing (owner decision of 2026-09-25), and the install scripts are no
exception. `MOD_SEED_FILES` writes a file only where none is, and an update from a legacy build
has no `CameraUnlock.ini` yet, so a seeded one is written before the mod starts: the mod reads it
and never imports the player's legacy file. The `MOD_DLLS` finding told a converted repo to move
its config to `MOD_SEED_FILES`, which is that bug.

- `config-preserve` fails a converted repo whose `install.cmd` lists, in `MOD_DLLS` or
  `MOD_SEED_FILES`, `CameraUnlock.ini`, the legacy file (`legacy_source`) or the committed config
  file's name, and tells it to take the file out.
- docs/canonical-config.md says the same for `MOD_SEED_FILES`.

`pixi run conformance -All -Check config-preserve`: 20 -> 26 FAIL, the six new ones in the repos
converted in place: fallout-new-vegas, metro-exodus-enhanced-edition, no-mans-sky and starfield
seed the legacy file; a-plague-tale-innocence and sleeping-dogs seed `headtrack.ini`, their
committed file. No unconverted repo is checked.

### Fixed - `validate-manifest` fails a Nexus ZIP that carries the legacy file

`checkNexusConfig` matched a converted repo's Nexus ZIP against the installed paths only. Since
every installed path names `CameraUnlock.ini`, a ZIP carrying the legacy file (`HeadTracking.ini`
and the like) passed, and extracting it replaces the file an older build reads after a rollback,
which is also what the mod imports while `CameraUnlock.ini` is absent.

- `manualZipConfigEntries(state, entries)` in `scripts/check-canonical-config.mjs` gives the ZIP
  entries that land on an installed `CameraUnlock.ini` or on the legacy file in the folder of any
  installed path, at the path or a tail of it. `validate-manifest` fails a converted repo's Nexus
  ZIP on any of them, and its comment no longer describes the in-place conversion.
- `test-config-descriptor` holds the rule to the config, the legacy file beside one or each
  layout's config, a flat ZIP, a BepInEx `.cfg`, an entry with no legacy file, and a file of
  another name.

The newest Nexus ZIP of each of the ten converted repos checked out beside core carries neither
file, before and after.

### Removed - `uninstall-body.cmd` no longer keeps `.pre-canonical` copies

Nothing writes `<file>.pre-canonical` or `<file>.pre-canonical.last` any more. The 19 repos the
canonical config check reports as converted committed their conversion on 2026-09-25, after their
newest GitHub release (`v*` or the `dev` pre-release), so no published build wrote one.
`uninstall-body.cmd` keeps each `PRESERVE_FILES` path and nothing beside it: the folder refusal,
`:is_preserved` and the set-aside around a loader folder removal check the listed path alone. The
template tails are unchanged.

`test-uninstall-preserve.ps1` drops the copies from its fixtures and the folder-named-as-a-copy
case, and its BepInEx fixture keeps `BepInEx\config\CameraUnlock.ini` and the plugin's `.cfg`. Two
cases show a file whose name extends a listed config's (`<file>.bak`, `<file>.bak.old`) goes like
any other unlisted file, each run with `installed_by_us` true, false and `/force`:

- BepInEx, listing `BepInEx\config\CameraUnlock.ini` and the plugin's `.cfg`, with a `.bak`
  beside each and a `.bak.old` beside the `.cfg`. No removal list reaches `BepInEx\config`, so the
  loader folder removal takes the three and the two listed files are set aside and put back.
  Without it all five stay.
- REFramework, listing `reframework\plugins\CameraUnlock.ini` and
  `reframework\plugins\HeadTracking.ini`, with `MOD_LEFTOVERS` naming the two `.bak` files and a
  third file it does not name. The list removes its two on every run, and the loader folder
  removal takes the third.

### Changed - docs/canonical-config.md describes the import into `CameraUnlock.ini` throughout

The sections the earlier entries left alone still described the conversion in place. The owner
table has `legacy_path` / `LegacySourcePath` for every import, not BepInEx alone, and the
constructor's refusals for it; the Load and Reload tables describe the config file and the legacy
file (no `LegacyReadOnly`); "What happens at the first launch" and "When an import does not
happen" describe the import and its player messages; "The copies" is gone; "Rolling back and
forward" says an older build reads the untouched legacy file; the Nexus and seed bullets say a
converted release ships and seeds neither file; the BepInEx section names
`BepInEx\config\CameraUnlock.ini`; the stamp section says the owner reads an unstamped
`CameraUnlock.ini` as canonical; and a launcher edits `CameraUnlock.ini` only, never the legacy
file. No code changes.

### Fixed - the README config block resets to the defaults without deleting the legacy file

The block for a repo in `legacy` told players to delete both files to go back to the defaults. A
Lopari v0.9.0 receipt can record the legacy file as a seed: resident-evil-requiem-headtracking
v0.4.0 installs REFramework and seeds `reframework/plugins/HeadTracking.ini`, and Lopari copies
that record into the receipt of every later install while it owns the loader. With the file
deleted the receipt is never intact again, so Lopari downloads and reinstalls the mod before every
launch, and the reinstall writes no seed because REFramework is already there.

- `generate-readme` and `scripts/templates/canonical-config-changelog.md`: to go back to the
  defaults, replace everything in `CameraUnlock.ini` with the defaults the block prints.
  Deleting only `CameraUnlock.ini` still imports the legacy file again.
- The changelog template's notes and docs/canonical-config.md record why the legacy file is never
  to be deleted.

Consuming repos: a converted repo re-renders its README block with
`pixi run readme --write --sections config`.

### Fixed - a converted repo's manifest fails a seed of its config with or without a config block

The rule that no seed or `files[]` row writes the config or the legacy file ran inside the config
descriptor rules, so it held only for a manifest with a block and one recorded config file. A
converted repo with no block passed `validate-manifest` and conformance while seeding its legacy
file, and one with two recorded files or an unrecorded committed file was never checked.

- `configWriteProblems(man, state)` in `scripts/check-config-descriptor.mjs` fails, for a converted
  repo, every seed (`seed`, `loader.seed`, at the top level and in each variant) and `files[]` row
  whose file name is `CameraUnlock.ini`, a recorded `legacy_source` or the name of a committed
  config file, recorded or not. It matches by name in any folder and at any anchor, so a `mod_home`
  target and an `exe_dir` one for a game `data/games.json` does not list are caught too. The
  descriptor rules no longer check seeds, so a seed is reported once.
- `validate-manifest` runs it on every package built from a repo it can name, block or not.
  Conformance's `config-descriptor` check reports it for the committed manifest.
- `test-config-descriptor` holds the rule to manifests with and without a block, a mod_home seed,
  a seed named like the committed file, a repo with two config files, a top-level `loader.seed`
  beside variants, and an unconverted repo, and runs it through `validate-manifest` and
  conformance.

`pixi run conformance -All -Check config-descriptor`: 5 -> 9 FAIL. The four new ones seed the
legacy file with no block: fallout-new-vegas, metro-exodus-enhanced-edition, no-mans-sky and
starfield. No unconverted repo is checked.

### Fixed - packaging fails a converted repo that seeds its config with no config block

`Assert-LauncherManifestConfig`, which `Copy-SharedBundle` runs in every mod's package, returned
early on a manifest with no config block, so the no-seed rule held there only in
`validate-manifest` and conformance, and most package scripts never call `validate-manifest`.

- `scripts/check-config-descriptor.mjs --package <repo>` reports every problem the default run
  does except a converted repo's missing block, which stays conformance's finding so a converted
  repo can release before its block lands.
- `Assert-LauncherManifestConfig` runs it on every committed `launcher-manifest.json`, block or
  not. Whether a repo is converted is read from its committed config by the checker, so
  packaging any repo with a manifest now needs `node` on `PATH`; GitHub's hosted runners carry it.
- `test-config-descriptor` runs a seed and a `files[]` row of `CameraUnlock.ini` and of the legacy
  file through `Assert-LauncherManifestConfig` with no block, and a converted repo with no block
  and no seed, and an unconverted repo seeding its config, which both pass.

Consuming repos: fallout-new-vegas, metro-exodus-enhanced-edition, no-mans-sky and starfield
seed their legacy file with no block and fail `pixi run package` once they pin this core; take
the seed out of `launcher-manifest.json`. `check-config-descriptor.mjs --package` fails those 4 of
the 131 repos with a manifest and nothing else.

### Removed - `encode-seed.mjs` no longer re-encodes seeds

`encode-seed` rewrote the `content_b64` of every seed whose target is an installed config path.
Every installed path now names `CameraUnlock.ini`, and a converted release seeds nothing, which
`configWriteProblems` fails, so that half wrote nothing in any repo that passes the gates. No
committed `launcher-manifest.json` beside core names `CameraUnlock.ini`.

- `scripts/encode-seed.mjs` writes the config descriptor's `rows` and nothing else; `--check`
  exits 1 when they are stale. `encodeSeeds` is now `encodeRows`, and nothing outside the script
  imported it. `gameRelativeTargets` in `check-config-descriptor.mjs` is no longer exported.
- `scripts/test-encode-seed.mjs`, its pixi task and `data/fixtures/encode-seed/resident-evil-2-headtracking.launcher-manifest.json`
  are gone. `test-config-descriptor` keeps the rows tests on the prey fixture and pins that the
  fixture's seed is left as it was.
- The render-config templates and docs/canonical-config.md describe `rows` only.

Consuming repos: `render-config` runs the same command and no longer touches a seed. A converted
repo takes any seed of its config out of `launcher-manifest.json` by hand.

### Fixed - a converted repo's uninstall wrapper lists neither config file in `MOD_SEED_FILES`

`config-preserve` held `install.cmd`'s `MOD_DLLS` and `MOD_SEED_FILES` to the no-seed rule, and
left the uninstall wrapper's `MOD_SEED_FILES`, which `uninstall-body.cmd` deletes as files the
install seeded, unchecked. A repo that took its config out of `install.cmd` passed with the same
file still listed there, and only `PRESERVE_FILES` stood between the uninstall and the player's
settings.

- `config-preserve` fails a converted repo whose `uninstall.cmd` (on the shared body) lists
  `CameraUnlock.ini`, the legacy file or the committed file's name in `MOD_SEED_FILES`.
- Every `config-preserve` seed finding ends "The mod creates CameraUnlock.ini at first launch;
  list neither file." The seed finding of `configWriteProblems` is one sentence.
- `config-preserve` counts a stamped file that `data/config-format.json` does not record among the
  committed config's names, as `configWriteProblems` does, so a repo converted only by such a file
  fails a listing of its name in either wrapper.
- `MOD_SEED_FILES` stays in the install bodies and wrapper templates that read it (ASI, BeamNG,
  REFramework, shim, shim-forwarder, xNVSE) and in the uninstall body and wrapper: those wrappers
  set it, and unconverted repos still use it.
- `test-config-descriptor` runs `validate-manifest` and conformance's `config-descriptor` over a
  converted repo with no block that seeds `CameraUnlock.ini`, seeds the legacy file, or carries a
  `files[]` row on either, each of which fails, and an unconverted repo seeding its config, which
  passes. It runs `config-preserve` over wrappers listing either file in each of `install.cmd`'s
  `MOD_DLLS` and `MOD_SEED_FILES` and `uninstall.cmd`'s `MOD_SEED_FILES`, and over a repo converted
  only by an unrecorded stamped file that lists it.

`pixi run conformance -All`: 238 -> 244 FAIL (config-preserve 26 -> 32). The six new ones are the
uninstall wrappers of the repos converted in place: a-plague-tale-innocence and sleeping-dogs list
`headtrack.ini`, fallout-new-vegas, metro-exodus-enhanced-edition, no-mans-sky and starfield their
legacy file. No unconverted repo is checked.

Consuming repos: a converted repo takes both files out of `MOD_SEED_FILES` in `install.cmd` and
`uninstall.cmd`, and out of `install.cmd`'s `MOD_DLLS`.

### Changed - docs/canonical-config.md and its example name `CameraUnlock.ini`

- The example fixture is `data/fixtures/canonical-ini/example/CameraUnlock.ini`, renamed from
  `HeadTracking.ini`, and the C++ and C# examples the document excerpts create
  `CameraUnlock.ini`. The bytes are unchanged.
- The C++ example says a mod with an import also sets `options.legacy_path`.
- "When an import does not happen" no longer lists `Windows did not finish replacing ...`. The
  import creates the config file and never replaces one, and only a replacement stops half way.
- The Reload table's note on the unused status 2 no longer refers to the conversion in place.
- The Tooling section lists what `config-preserve` checks now (an unrecorded stamped file counts as
  the committed config, and the uninstall wrapper is checked only on the shared body) and that
  `validate-manifest` fails a converted repo's package that seeds or ships either config file.
- The options table says `legacy_path` / `LegacySourcePath` is the file the game's last
  pre-canonical build read, the entry's `legacy_source`, and gives `HeadTracking.ini` and a BepInEx
  `<GUID>.cfg` as examples only. Many legacy files have other names.

No library code changes.

### Fixed - a legacy file that cannot be read is not reported as read-only

A read that failed with access denied was blamed on the read-only attribute. The C++ owner
checked the attribute on the file it read, and the C# owner on `CameraUnlock.ini` whichever file
it read, so a denied read of a read-only legacy file gave `the file is read-only` in C++ and `it
could not be read (...)` in C#. The attribute never refuses a read. Both owners now report a
failed read as `it could not be read (...)`, or `the file is in use by another program`, and `the
file is read-only` comes only from a write that fails on a read-only file. `detail::OwnerReadWhy` loses its path parameter.
An import only ever creates `CameraUnlock.ini`, never writes over a file, so it never gives that
reason, and docs/canonical-config.md drops it from the import's list. A scenario in both languages
denies the current user read access to a read-only legacy file.

### Added - the `config` descriptor in `launcher-manifest.json`

A converted package can tell a launcher where its canonical file is and which of the launcher's
preference rows the mod binds, with their committed values: a top-level `config` block of `path`,
`anchor`, `legacy_source`, `canonical_since` and `rows` (docs/canonical-config.md, "The config
descriptor"). No mod carries one yet; a repo adds it at or after its conversion.

- **`scripts/check-config-descriptor.mjs`** holds the block's rules: its shape, the tracking pair
  against `preference_modes`, `canonical_since` against `mod_info.version`, and the block against
  the repo's `data/config-format.json` entry, its seeds and `files[]`, and its committed file. A
  package with a block seeds neither its config nor its legacy file, `path` names
  `CameraUnlock.ini`, and an `exe_dir` path has to land on an installed path beside each
  executable `data/games.json` records.
  `pixi run validate-manifest` runs them on a built ZIP whose manifest carries a block.
- **`Assert-LauncherManifestConfig`** in `ReleaseWorkflow.psm1` runs them on the committed
  manifest from `Copy-SharedBundle` when it has a block, so packaging refuses stale `rows` in the
  package scripts that never call validate-manifest. It needs `node` on `PATH` only then.
- **Conformance `config-descriptor`** runs them on the committed manifest, fails a repo delivered
  by manifest whose one recorded config file is stamped and installed as `CameraUnlock.ini` and
  that has no block, and, with tags, a `canonical_since` not above every `v*` tag whose committed
  config lacks the stamp.
- **`scripts/encode-seed.mjs`** also rewrites `config.rows` from the committed file, leaving every
  other byte; `--check` fails stale rows. The `render-config` task command is unchanged.
- **`data/config-format.json` `descriptor_omits`**: per repo, launcher rows the descriptor leaves
  out on purpose, with a reason and an approval date. Only `WorldSpaceYaw` may be listed; it lists
  subnautica-headtracking, whose mod defaults to camera-local yaw because swimming has no stable
  up. A committed `WorldSpaceYaw` away from the schema default fails the descriptor rules unless
  the repo lists it. `check-config-format` validates the key.
- **`pixi run test-config-descriptor`**, part of `pixi run check`.

### Added - `data/fixtures/canonical-ini/preferences/`: a launcher's preferences against the mod

19 cases for the four preferences a launcher manages in a canonical file (tracking mode,
`WorldSpaceYaw`, `TrueFreeLook`, `EnableOnStartup`), over a fixture mod that binds the five rows
with a three-state mode control or the four without `RotationEnabled`. Each `case.tsv` gives, per
preference, the raw value a launcher reads (a value, `invalid` or `missing`) and the value the mod
runs on, and a case with a change has the `expected.ini` the owner's `Save` writes for it. The
fixture README defines the raw decode, including the three-state mode read through
`preference_modes`, false/false as `invalid`, and a two-state file's `RotationEnabled` never read.

`cpp/tests/preferences_fixture_tests.cpp` and `PreferencesFixtures` (xunit, and
`CameraUnlock.Core.FrameworkTests` on .NET Framework 3.5 and 4.7.2) run every case through
`HeadTrackingConfigTable` and `ConfigOwner` on a temporary copy. Tests and data only; no library
code changes.

### Fixed - every wrapper sets every name its template's CONFIG BLOCK sets

The `MOD_SEED_FILES` and `PRESERVE_FILES` fix below closed two names of a wider gap. A wrapper
sets its CONFIG BLOCK before its `setlocal`, so any name the block leaves out keeps whatever
another mod's wrapper set in the same console, and the body acts on it. 32 ASI uninstall wrappers
and 29 ASI install wrappers had no `ASI_SUBDIR` line. Run after a wrapper that set it, the
uninstall looked in that mod's folder, removed nothing of its own and still exited 0, and the
install deployed into that folder, or failed with exit 1 where this game has none. Lopari runs each script in its own `cmd /C` and was never
affected.

The body cannot clear the names itself. It runs after the wrapper's `set` lines and cannot tell
an inherited value from one the wrapper set, so the lines have to be in each wrapper.

- Every template's header now says to keep every CONFIG BLOCK line, blank where it does not
  apply. `install-wrapper-bepinex.cmd` sets `IL2CPP_VENDOR_DIR_NAME`, `IL2CPP_VENDOR_ZIP_NAME`,
  `IL2CPP_PLUGIN_DIR_NAME` and `IL2CPP_MOD_DLLS` blank: `install-body-bepinex.cmd` reads all four
  and no template set them. The template tails are unchanged, so `sync-templates.ps1` rewrites
  nothing.
- **`install-wrapper`** in `conformance.ps1` FAILs a wrapper whose CONFIG BLOCK does not set every
  name its template's CONFIG BLOCK sets, where it checked only `MOD_SEED_FILES` and
  `PRESERVE_FILES` before.
- `scripts/test-uninstall-preserve.ps1` runs the uninstall template and the ASI install template
  from a folder holding `!`, in a console that holds a value for every name each template sets
  blank (`ASI_SUBDIR` naming a folder of another mod's), and expects the same result as from a
  clean console. With the `ASI_SUBDIR` line taken out, the uninstall misses `bin\` and the install
  lands in the other mod's folder, which the harness also asserts.

Consuming repos: the 168 wrappers in 109 repos checked out beside core that lacked a name have had
it added, blank, after the last `set` line of their CONFIG BLOCK. No existing value changed. A
repo not in that sweep adds the names `install-wrapper` lists, or it fails.

### Changed - owner rulings on the Stage 3 open issues (2026-09-25)

- **N1 is approved and back**: a legacy hotkey code outside 0x01-0xFE, 0xFF included, imports as
  unbound. C++ `LegacyVirtualKeyToBindings(long long code)` gives the key name for a code from
  0x01 to 0xFE (`0x` and hex where the key table has no name) and "" for any other code; the
  overload taking a section, key and `dropped` list also records the drop, except for code 0,
  which is how a legacy file says unbound. `DropRule::KeyCodeOutOfRange` is 5 in both languages
  (C# `DropRule.KeyCodeOutOfRange`), after the four existing rules, whose numbers do not move. Its
  log line: `not carried: [Hotkeys] ToggleKey=0x230, it is not a key code from 0x01 to 0xFE, so
  the action is unbound`. No C# import reads virtual-key codes, so C# has the rule and its line
  but no mapping helper. `data/config-format.json` records N1 as approved on 2026-09-25 with
  `drop_rule` `KeyCodeOutOfRange`. The one behaviour change is 0xFF: GetAsyncKeyState reports it
  while VK 0xFF is held, so a player who bound a legacy hotkey to 0xFF loses it. No config the
  fleet ships binds it. The REFramework import still needs no N1: `PluginConfig::Read` already
  keeps the default for such a code.
- **`data/games.json`**: abzu's `display_name` is `ABZU`. The canonical renderer writes the display
  name into the file header and accepts printable ASCII only. `pixi run validate-games` now fails a
  `display_name` that is not printable ASCII or that starts or ends with a space.
- **`data/config-format.json` `conversion_notes`**: per repo in `configs`, facts the owner decided
  that the repo's conversion and differential test follow, each with `text` and an `approved` date.
  `check-config-format` requires a repo in `configs`, a non-empty array and both fields. The first
  entry is Requiem's: its position sensitivity is 1.0, as v0.4.0's installer and Nexus ZIPs ship.
  The 2.0 in v0.4.0's launcher seed was drift and was re-encoded from the shipped file on
  2026-08-30. A file Lopari seeded from v0.4.0 has its 2.0 dropped as `PoseShaping` at conversion.
  The REFramework conversion test now pins that outcome only: the seed's three position
  sensitivities are logged as dropped against Requiem's schema, and the shipped file drops none.
- **Tracker pivot range confirmed**: `TrackerPivotForward` and `TrackerPivotUp` keep the canonical
  range 0 to `config::kMaxPositionLimit` (10).
- **Unit scales a player can edit leave player config**, by the doctrine rule that axis signs and
  scales are correct in code. The eight spellings are group `PositionScale` of
  `non_canonical_keys` (below), so a table refuses one as a local row, applying a canonical file
  reports one as `NonCanonicalConcept` with `The mod converts your head movement to the game's
  units itself, so the scale is not a setting.`, and the lint fails a file holding one. A
  conversion folds the shipped scale into the mod's code as a constant and passes the legacy value
  through `LegacyPoseShaping`, so a scale the player changed is dropped as `PoseShaping`. The
  approval and every player-facing line name scales now: `approved_changes.pose_shaping` in
  `data/config-format.json`, the README line `generate-readme.mjs` renders (`A sensitivity, scale,
  deadzone, response curve or axis inversion you changed from its default.`), the three bullets of
  `scripts/templates/canonical-config-changelog.md`, and the `PoseShaping` log line in both
  languages, now `sensitivity, scales, deadzones, response curves and axis inversion are set in the
  tracker now, not in this mod`. No mod has converted, so no README or log a player has seen
  changes. metaphor-refantazio's scale is a bare `[Position] Scale`, which no group can list
  because section-less matching would also take `[FieldOfView] Scale` and `[Reticle] Scale`. The
  lint refuses a bare `Scale` in a canonical file; the table and apply do not name it, so that
  conversion folds and drops the scale by hand through `LegacyPoseShaping`.

### Fixed - every wrapper sets `MOD_SEED_FILES` and `PRESERVE_FILES`, blank where unused

A wrapper sets its CONFIG BLOCK before its `setlocal`, so a name its CONFIG BLOCK leaves out keeps
whatever another mod's wrapper set in the same console, and the body cannot tell an inherited
value from one the wrapper meant. An install wrapper without `MOD_SEED_FILES` that inherits another
mod's list fails with exit 1 on a seed its own package does not ship; an uninstall wrapper without
`PRESERVE_FILES` keeps files at the paths another mod listed, and one without `MOD_SEED_FILES`
removes files by another mod's names. Lopari runs each script in its own `cmd /C` and was never
affected.

- `scripts/templates/uninstall-wrapper.cmd` sets `PRESERVE_FILES` in its CONFIG BLOCK, blank, after
  `MOD_SEED_FILES`, with a comment saying why the line stays when blank. The install templates whose
  body reads `MOD_SEED_FILES` (ASI, BeamNG, REFramework, shim, shim-forwarder, xNVSE) already set
  it. The template tail is unchanged, so `sync-templates.ps1` rewrites nothing.
- **`install-wrapper`** in `conformance.ps1` now FAILs a wrapper whose CONFIG BLOCK does not set
  `MOD_SEED_FILES` or `PRESERVE_FILES` where its template does. `sync-templates.ps1` never writes
  a CONFIG BLOCK, so this is what catches a wrapper that drops the line.
- `scripts/test-uninstall-preserve.ps1` runs the uninstall wrapper template from a folder holding
  `!`, from a console that already holds another mod's `PRESERVE_FILES` naming a file the template
  removes, and expects the file removed. The template before this change keeps it.
- The `uninstall-body.cmd` header points at the template line; the body is otherwise unchanged.

Consuming repos: every wrapper repo checked out beside core has had the lines added to its
CONFIG BLOCK by hand, blank except where `install.cmd` already seeds (amnesia-the-dark-descent's
`uninstall.cmd` now lists `HeadTracking.ini` in `MOD_SEED_FILES`, which its `MOD_DLLS` already
removed). A repo not in that sweep adds `set "PRESERVE_FILES="` to `uninstall.cmd`, and
`set "MOD_SEED_FILES="` to either wrapper whose template sets it, or `install-wrapper` fails.

### Changed - pose-shaping sections and more pose-shaping spellings are refused

The deadzone shape most of the fleet reads, a bare `Yaw`, `Pitch` and `Roll` under `[Deadzone]`
(black-and-white, black-mesa, dorfromantik, fallout-new-vegas, half-life-2, portal, portal-2,
subnautica, titanfall-2), got through: a table accepted a local row in `[Deadzone]`, applying such
a file drew only `UnknownSection`, and the lint passed `[Deadzone] Threshold` and gave `[Deadzone]
Yaw` only the bare-noun hint. A bare key under `[Sensitivity]` or `[Inversion]` drew no key
diagnostic either. And several pose-shaping spellings the fleet and core use were in no list.

- A `non_canonical_keys` group now has `sections`, sections that hold only its settings:
  `Sensitivity` lists `[Sensitivity]`, `Inversion` lists `[Inversion]` and `Deadzone` lists
  `[Deadzone]`. The generator checks them (PascalCase, not `[CameraUnlock]`, not a schema section
  with a canonical concept, each listed once) and emits C++ `schema::kNonCanonicalSections` and C#
  `ConfigConcepts.NonCanonicalSectionReasons`. A config table refuses a local row in one, applying
  a canonical file reports every key in one that nothing else names as `NonCanonicalConcept` with
  the group's reason (beside the section's `UnknownSection`), and the lint fails the section and
  each key in it with that reason.
- New spellings: `RotScale` (group `Sensitivity`); `SignYaw`, `SignPitch`, `SignRoll`, `SignX`,
  `SignY`, `SignZ` (group `Inversion`), which dead-rising-2 and mafia-ii-definitive-edition read as
  `[tuning] rot_scale` and `sign_*`; `DeadzoneMin`, `DeadzoneMax` and the `Deadband` forms (group
  `Deadzone`); `SensitivityCurve` and `CurveStrength` (group `ResponseCurve`), the names core's own
  `AxisConfig` profile format writes.
- More sensitivity spellings in group `Sensitivity`: `YawGain` and `PitchGain`
  (assassins-creed-origins reads them under `[ExtendedView]`), `PositionSensitivity`
  (south-park-the-stick-of-truth, `[General]`), and `RollGain`, the bare `Sensitivity`,
  `RotationSensitivity`, `RotationMultiplier`, `RotationGain`, `PositionMultiplier`, `PositionGain`
  and `LeanScale`, which no fleet config file reads. Before this a canonical file or table could
  carry any of them as a game-local row with no diagnostic. The gates still match listed spellings
  only, so a sensitivity under a name no group lists passes them; the group's doc and
  docs/canonical-config.md say so.
- New group `PositionScale`: `PositionScale`, `PositionScaleUU`, `PosScale`, `WorldScale`,
  `UnitsPerMeter`, `UnitsPerMetre`, `WorldUnitsPerMeter`, `WorldUnitsPerMetre`, with the fleet
  repos that read each in its doc. Converting the tracker's metres to the game's units is the
  mod's boundary code; a scale the player can edit is a position sensitivity under another name.
  `PositionScale`, `WorldScale` and `UnitsPerMeter` leave `deliberately_unaliased`, whose reason
  (a concept still to come) pointed the other way.
- `deliberately_unaliased` and the `Deadzone` group's doc no longer give conflicting counts: the
  nine repos that read `[Deadzone] Yaw` are named, and they all read `[Sensitivity] Yaw` too. The
  `DeadzoneDeg` and `ResponseCurve` docs say who reads what.
- `data/fixtures/canonical-ini/table/apply-unknown` adds `[Sensitivity] Pitch`, `[Deadzone] Yaw`
  and `Threshold`, a `[Tuning]` section with `rot_scale`, `SignYaw`, `YawGain`,
  `PositionSensitivity` and `LeanScale`, and `[Network] WorldScale`.
- The shared table fixture's `[Position] LeanScale` row, a position multiplier under another
  name, is `[Position] LeanTraceLength` now (float, 0 to 2, default `1.0`), and `LeanDelayMs`'s
  comment reads `Milliseconds before a lean starts, and the metres its wall trace reaches.` A port
  that declares the fixture table renames the row with it.
- The REFramework import listed `[Position] InvertX`, `InvertY` and `InvertZ` in `pose_shaping`
  for every schema, though Requiem's `Read` never reads them (`positionInvertKeys` false), against
  the list's contract of settings the frozen reader read. It lists them only where
  `positionInvertKeys` is set, so Requiem's import gives six values and RE2, RE3, RE4, RE7 and RE8
  nine.

None of these spellings is an alias, so the deprecated flat readers and `HeadTrackingConfigBase`
read exactly what they read before.

### Added - pose shaping leaves the canonical format: deadzone and curve keys, and the fold record

The tracker owns pose shaping, so a canonical file carries no sensitivity multiplier (rotation or
position), deadzone, response curve or user-facing axis inversion. The sensitivities and
inversions were already non-canonical concepts; deadzones and curves were never concepts, so
nothing stopped a mod writing them as game-local rows.

- `data/config-schema.json` gains `non_canonical_keys`: groups of spellings with a
  `canonical_reason`, for settings no canonical file carries that are not concepts. `Deadzone`
  lists the fleet's deadzone spellings (`Deadzone`, `DeadzoneDeg`, `DeadzoneYaw`, `DeadzonePitch`,
  `DeadzoneRoll`, `YawDeadzone`, `PitchDeadzone`, `RollDeadzone`, `EnableDeadzone`) and
  `ResponseCurve` the plain curve names (`ResponseCurve`, `YawCurve`, `PitchCurve`, `RollCurve`).
  They are not aliases, so the deprecated flat readers never resolve them and behave as before.
  `DeadzoneDeg` and `DeadzoneYaw` leave `deliberately_unaliased`, whose reason for them (a concept
  still to come) no longer holds.
- The generator validates the groups (a spelling that the alias table resolves, or that two groups
  list, fails) and emits C++ `schema::kNonCanonicalKeys` and C# `ConfigConcepts.NonCanonicalKeyReasons`.
- A config table refuses a game-local row named by one of these spellings, applying a canonical
  file reports such a key as `NonCanonicalConcept` with its reason in any section, and the
  canonical config lint fails a file holding one. `data/fixtures/canonical-ini/table/apply-unknown`
  carries a `ResponseCurve` and a `[Deadzone] DeadzoneYaw` line.
- C++ `PoseShapingValue` and `LegacyPoseShaping` (bool, float, double) in `config/legacy_import.h`,
  and C# `PoseShapingValue` and `LegacyPoseShaping.Record`. A map passes each pose-shaping value its
  frozen reader read, with the value the game shipped. The call records both in the new
  `ImportResult::pose_shaping` (C# `ImportResult.PoseShaping`), written as the canonical codecs
  write them, and `folded` when they are equal as numbers: the conversion moves a folded value
  into the mod's axis code, and a differential test asserts that against it. A value the player
  changed is also dropped as `PoseShaping`, which the migration logs. `ImportResult::Imported` and
  `Absent` take the list as a second argument, defaulting to empty (C#: new overloads).
- The REFramework import records the pose-shaping values `PluginConfig::Read` reads (the three
  multipliers, the three position sensitivities and, for a schema with `positionInvertKeys`, the
  three position inversions) through `LegacyPoseShaping` against `PluginConfig::SetDefaults`. A
  dropped float is now written as the float codec writes it, so Requiem's seed logs `not carried:
  [Position] SensitivityX=2.0, ...` where it logged `=2`. Its differential test compares the
  pose-shaping list on every corpus input and checks that each shipped file folds every value
  listed and drops nothing.
- `HeadTrackingConfigTable` binds no pose shaping, as before; its documentation now says the
  sensitivity and inversion fields keep the defaults instance's values.

`LightMultiplier` is not pose shaping and stays canonical. `SensitivitySettings` and
`DeadzoneSettings` stay on the public API, bound to no canonical row. The legacy import support,
`ImportResult` included, has no consumer yet, so its new member and factory argument change no
mod.

### Added - `TrueFreeLook` and `TrueFreeLookKey` in the canonical concept set

Aiming down sights has two lean modes in a shooter with positional tracking (the
`shooter-ads-handling` skill): sights locked, the default, and true free look. The setting and its
toggle are now schema concepts, so every mod spells them the same way.

- `[Position] TrueFreeLook`, bool, default false, no aliases. false keeps the eye on the sight line
  while aiming; true leaves the lean in full while the weapon stays put in the world. It is in
  `[Position]` because the lean is all it changes and it exists only where positional tracking
  does, and a key goes in the section of its subject, as `CollisionEnabled` does. It is a persisted
  preference like `WorldSpaceYaw`: the toggle saves it, so a mod marks the row Writable.
- `[Hotkeys] TrueFreeLookKey`, a key list with `canonical_default` `Insert, Ctrl+Shift+U`. No flat
  reader reads it, so its `default` and field initialisers are that list too, where the other
  hotkey concepts keep the single key the flat readers have always shipped. The generator accepts a
  list `default` only when it is the concept's own `canonical_default`.
- Fields `HeadTrackingConfigData.TrueFreeLook` and `TrueFreeLookKeyName`, and
  `HeadTrackingConfig::true_free_look` and `true_free_look_key_name`, which come after every
  existing C++ member. Both `HeadTrackingConfigTable`s bind them, and their defaults instance starts
  `TrueFreeLookKey` at its `canonical_default`. Neither concept is on `IHeadTrackingConfig`.
- The deprecated flat readers (`HeadTrackingConfigData.ApplyValues` and `LoadFromFile`,
  `HeadTrackingConfig::ApplyValues` and `LoadFromFile`) do not read either concept, so their
  behaviour and defaults are unchanged: the C# one skips both before its duplicate-spelling
  warning, as it did while they resolved to no concept. `true_free_look` is read only by a mod's legacy import:
  section-less matching resolves it to `TrueFreeLook`, so in a canonical file it draws
  `MisplacedKey` and is not read, and the lint reports it as spelled `TrueFreeLook`.
- The generated tables gain `ConfigConcepts.TrueFreeLook`, `ConfigConcepts.TrueFreeLookKey`, their
  `ConfigKeySchema.Keys` and `config_keys` constants, and `schema::Concept::TrueFreeLook` and
  `TrueFreeLookKey`. The two are inserted in the schema's concepts order, after `PositionAllowed`
  and after `YawModeKey`, which is where the renderer writes them, so the later values of
  `schema::Concept` move up. That enum has no consumer yet.
- `data/fixtures/canonical-ini/head-tracking/` carries both concepts: `all-concepts.ini` and the
  three apply cases.

The canonical hotkey defaults are now the four lists of the controls table: `ToggleKey`
`End, Ctrl+Shift+Y`, `CycleTrackingModeKey` `PageUp, Ctrl+Shift+G`, `YawModeKey`
`PageDown, Ctrl+Shift+H` and `TrueFreeLookKey` `Insert, Ctrl+Shift+U`. A chord is an ordinary item
of its action's list, so the canonical config lint now refuses any key whose name starts with
`Chord` (`ChordToggle`, `ChordToggleKey` and the rest), and a mod's legacy import folds such a row
into the action's list. `ShowReticle`, `ReticleToggleKey` and `PositionToggleKey` stay
non-canonical.

A search of the sibling repos' INI, cfg, JSON, TOML, C++, C#, Lua, Rust and Markdown files and
their 41 decoded launcher manifest seeds found no `TrueFreeLook` or `true_free_look`, so no
config already on disk changes meaning.

### Added - `docs/canonical-config.md`: the canonical config format, documented

The design lived only in untracked notes, so nothing committed described the format that every
converted mod and any tool editing a mod's config depend on.
[docs/canonical-config.md](docs/canonical-config.md) now does: the file's layout, grammar, value codecs, hotkey lists and the two dialects, the stamp
and `ConfigFormat`, the canonical concept set and why the sensitivity, inversion, reticle,
position-toggle and recenter concepts have no row; config tables, Writable rows and
`HeadTrackingConfigTable`; the config owner's Load, Save and Reload results and its threading
rules; what a conversion does and what players are told, the `.pre-canonical` copies, deferrals
and rollback; BepInEx's `.ini` beside the `.cfg` and the `Config.Reload()` rule for its import;
the shared fixtures and their TSV format for ports; and the tooling. README.md links it from the
pipeline porting paragraph and from Configuration.

- **The examples are compiled and run.** The C++ and C# code in the document is taken from
  `cpp/tests/canonical_config_example_tests.cpp` and
  `csharp/src/CameraUnlock.Core.Tests/Config/CanonicalConfigExample.cs`, which build a table over
  a config type derived from core's with one local row, create the file through `ConfigOwner`,
  save a yaw toggle and read it back. The C# file also runs in `CameraUnlock.Core.FrameworkTests`,
  so the example is C# 7.3 that builds on net35. Both render
  `data/fixtures/canonical-ini/example/HeadTracking.ini`, a new hand-written fixture.
- **`pixi run check-doc-examples`** (`scripts/check-doc-examples.mjs`, now part of `pixi run check`)
  fails when a code block in `docs/*.md` preceded by `<!-- excerpt: <path> -->` is not a run of
  consecutive lines of that file (compared after taking off the run's common indentation), when one
  preceded by `<!-- file: <path> -->` is not that whole file, and when a `cpp` or `csharp` block
  carries neither. Every other fenced block must be tagged `ini` or `text`, so code cannot get past
  the check untagged or under another tag such as `cs` or `c++`, and fences indented in a list
  item are checked like any other. The untagged byte dump in `docs/hcam-inband-protocol.md` is
  now tagged `text`.

### Added - seed re-encoding, render-config templates and the Nexus config check

Tooling a mod repo uses when it converts to the canonical config format (design 5.1).

- **`scripts/encode-seed.mjs`** rewrites the `content_b64` of every `launcher-manifest.json` seed
  that writes the repo's config, from the committed file's bytes. It reads the same seed shapes as
  `Assert-ManifestSeedsMatchShipped` (`loader.seed`, a top-level `seed`, `variants[].loader.seed`),
  and a seed writes the config when its target, resolved against its anchor (`exe_dir` through
  `data/games.json`'s `executable_relpath` and `xbox_executable_relpath`), is an `installed` path
  `data/config-format.json` records for the repo. Other seeds (BepInEx.cfg, a marks file) are left
  alone, and only the `content_b64` strings change: every other byte of the manifest is kept, and
  the script checks that the rewritten manifest parses to the old one with only those values
  replaced. It refuses a committed file without the `[CameraUnlock]` stamp, a config entry with no
  committed path, an unknown anchor, and two seeds that share a blob but need different contents.
  `--check` exits 1 when a seed is stale and writes nothing. No manifest, or no seed that writes a
  config, is reported and exits 0. `pixi run test-encode-seed`, now part of `pixi run check`, runs
  it against unchanged copies of resident-evil-2-headtracking's and prey-headtracking's manifests in
  `data/fixtures/encode-seed/`.
- **`scripts/templates/render-config-task-cpp.toml`** and **`render-config-task-csharp.toml`**: the
  `render-config` pixi task a converted repo adds. C++ runs the test binary's `--render-config
  <path>` mode, C# runs the render test with `CAMERAUNLOCK_RENDER_CONFIG=write`, and both then run
  encode-seed. Each file states what the repo's test has to do for its mode.
- **`scripts/templates/canonical-config-changelog.md`**: the fleet's standard changelog bullets for
  a conversion release, in an in-place and a BepInEx variant, worded as the README config block
  words them, plus the Removed bullets for the reticle toggle and settings and for the
  sensitivity, deadzone, response curve and axis inversion settings, which the tracker app now
  owns.
- **`scripts/validate-manifest.mjs`**, run with no arguments or with `.` in a converted repo, also
  opens the newest `release/*-nexus.zip` and fails when it carries a file at a path
  `data/config-format.json` records as the config's `installed` path, or at a tail of one (a flat
  ZIP extracted into the exe folder). A Nexus update extracted over the game folder would otherwise
  replace the player's file with the stamped default, and no migration would run. A repo that is
  not converted is not checked, so today's Nexus ZIPs that carry the config (abzu, prey,
  resident-evil-requiem and others) keep passing until their conversion takes it out of the Nexus
  staging.

### Added - the README config block, rendered from the committed config

A repo converted to the canonical config format documents its config in a block that
`scripts/generate-readme.mjs` renders inside the hand-written Configuration section, between a
`<!-- cameraunlock:config -->` line and a `<!-- /cameraunlock:config -->` line
(`scripts/templates/readme.md`). The block is built from `data/config-format.json` and the committed
file only, so a converted repo has one and an unconverted repo has none.

- **What it says**: where the file is installed, one path or the list of store layouts, and that
  the mod creates it when it finds none. For a repo in `legacy`: the one-time conversion at first
  launch, the original kept as `<file>.pre-canonical` and `<file>.pre-canonical.last` as the file
  before the most recent conversion, comments and keys the mod never read not carried over, nor
  the settings each `approved_changes` entry drops (one line per entry: a changed sensitivity,
  deadzone, response curve or axis inversion; reticle settings and a reticle toggle key; the
  setting of a feature shipped switched off while untested, which now follows the mod's default),
  an older version of the mod possibly misreading the new layout (a key that moved reads as its
  default, a hotkey or other value now written as a name can be misread), and copying
  `.pre-canonical` back first as the way to go back to an older version. An `approved_changes`
  entry with no line in the generator stops the block rendering. For a BepInEx repo (one whose
  entry has a `legacy_source`): that ConfigurationManager does not list the settings, and in a
  `legacy` repo that the settings now live in `BepInEx\config\<GUID>.ini`, the `.cfg` is left as
  it was and an older version still reads it, the same list of what is not carried over, and a
  reset deletes both files. Then the committed file itself in an `ini` code fence, CRLF turned to
  LF.
- **`pixi run readme`** checks the block with the other generated sections, and `--write` (which now
  applies `config` by default beside `opentrack` and `community`) inserts it after Configuration's
  lead prose, before its first `###` subsection, adds the section after Controls when there is
  none, keeps it current, and removes one from a repo that is not converted. A converted repo whose block cannot be rendered (a config
  file unstamped or unrecorded, markers that do not pair, or a marker above the first section or
  under an H2 other than Configuration) is reported and exits 1 in both modes.
  Unconverted repos without a block see no change: `pixi run readme --all` prints the same output as
  before.
- **`--print config [repo]`** prints one repo's block for NEXUS_MODS.md, which is untracked and so is
  updated by hand from it.
- **`--json --roots-file <file>`** prints each repo's result per section for conformance.
- **Conformance `readme`** FAILs a converted repo whose README has no config block or one that
  differs from the rendered block, an unconverted repo whose README has one, and a block that cannot
  be rendered. It runs `generate-readme.mjs --json --sections config` once for every repo.
- `scripts/check-canonical-config.mjs --json` now carries each file's `no_installed_reason`.

### Added - the canonical config lint and three config conformance checks

Conformance now holds each mod repo to the canonical config format. `scripts/conformance.ps1`
gains `config-format`, `config-legacy-reader` and `config-preserve`, backed by a new lint,
`scripts/check-canonical-config.mjs`, which reads `data/config-schema.json`, `data/keys.json` and
`data/config-format.json`. A repo counts as converted when a committed config file that
`data/config-format.json` records carries the `[CameraUnlock]` stamp, or, where its entry records
no committed path yet, when any tracked `.ini` or `.cfg` file does; nothing records it separately.
A checkout is matched to its entry by folder name, and by the name its `origin` remote gives where
the folder name is not listed (a worktree, a clone under another name), ignoring ASCII case.

- **`scripts/lib/canonical-ini.mjs`**: the canonical INI grammar for core's scripts, the rules of
  `ParseCanonicalIni` / `CanonicalIni.Parse` and `HasCanonicalStamp` / `CanonicalIni.HasStamp`.
  `scripts/lib/key-bindings.mjs` is the hotkey binding codec over `data/keys.json`, native and Unity
  dialects. `pixi run check-canonical-ini-js` (`scripts/test-canonical-config.mjs`, part of
  `pixi run check`) runs every case in `data/fixtures/canonical-ini/reader/` through the first and
  `keys/cases.tsv` through the second, and holds the lint to its rules: the two rendered fixture files
  pass it, and each rule fails a copy of `head-tracking/all-concepts.ini` edited to break it.
- **The lint**, on each stamped committed file: the reader finds nothing to report, every line ends
  in CRLF, there is no byte order mark and no byte above 0x7F, every key line is written
  `Key=value`, and every section header is written `[Name]` and appears once; `[CameraUnlock]` holds `ConfigFormat=1` and nothing else; a concept is written only at
  the schema's section and key, spelled as the schema spells it, and an alias, another spelling or
  another section fails; a concept the canonical format does not write fails with its
  `canonical_reason`, a retired key fails, and so does `[Sensitivity]`, `[Inversion]` or `[Reticle]`,
  the schema sections with no canonical concept; a game-local key is not a bare noun from
  `deliberately_unaliased` (`Enabled`, `Yaw`, `Position` and the rest), section and key names are
  PascalCase ASCII letters and digits, and no key name is used in two sections; every value in
  `[Hotkeys]` is a key list in the file's dialect (hex codes only in native files); each canonical
  hotkey concept holds its `canonical_default`, apart from a binding `hotkey_exceptions` replaces
  for the repo; the file is tracked by git and `git check-attr text` reports it unset (`-text`).
- **`node scripts/check-canonical-config.mjs [repo ...]`** prints each file's problems and exits 1
  on one; `--json` prints what conformance reads, and `--roots-file <file>` takes the repo paths
  one per line, which conformance uses so no fleet size runs into the Windows command-line limit; `pixi run config-report` (`--report`) prints the
  fleet report: game-local `(section, key)` pairs shared by three or more canonical repos, the
  game-local section names in use, and concept values in committed files that differ from the
  schema default.
- **`config-format`** FAILs a lint problem; a converted `legacy` repo with no `src/legacy_config/`
  or `Legacy/` folder; a repo outside `legacy` with one; a repo outside `legacy` and `exempt` whose
  committed file is missing, unrecorded or unstamped, or that `data/config-format.json` does not list
  at all; a converted `legacy` repo with an unstamped file left; and a recorded committed file the
  repo does not have; and a tracked file carrying the stamp where `data/config-format.json` records
  no committed path, since nothing is linted until the conversion records it. A repo whose
  `install.cmd` dispatches to `install-body-reframework.cmd` needs no legacy folder: its import is
  core's `PluginConfigLegacyImport`. It WARNs once for a `legacy` repo not yet converted.
  Predecessor repos are not checked.
- **`config-legacy-reader`** FAILs a converted repo whose tracked C, C++, C# or Rust source outside
  its legacy folder uses `GetPrivateProfile*`, `WritePrivateProfile*`, `IniReader`, `IniWriter`,
  `ParseIniConfig`, `ParseIniFile`, or a `.Bind(` call in a C# file that names BepInEx (reported as
  `ConfigFile.Bind`), unless `allow_legacy_symbols` lists that symbol in that file. `vendor`,
  `extern`, `third_party`, `cameraunlock-core`, `bin`, `obj`, `build`, `out`, `release`, `dist` and
  `target` folders are skipped, and so is `tests/config_differential/`, whose oracle compiles the
  published build's reader. A `Legacy/` folder under it is not a legacy import folder either.
- **`config-preserve`** FAILs a converted repo whose `install.cmd` lists a config file in `MOD_DLLS`,
  or whose `uninstall.cmd`, where it dispatches to `uninstall-body.cmd`, leaves an installed path or
  a `legacy_source` out of `PRESERVE_FILES`.
- **`config-block`** now FAILs a parenthesis in `PRESERVE_FILES`, which `uninstall-body.cmd` expands
  inside `for %%k in (...)`.
- **`scripts/check-config-format.mjs`** also checks `hotkey_exceptions`: the key is a hotkey concept
  with a `canonical_default`, `replaces` is one of its bindings, and `with` is one binding, written
  as the codec writes it in each dialect the repo's configs use and not already in the default. The
  `_comment` of `data/config-format.json` now says so, and how `allow_legacy_symbols` names a symbol.

On landing, no repo is converted, so every one of the 87 `legacy` repos draws the `config-format`
WARN, and every repo outside `legacy` and `exempt` FAILs it: the 40 unpublished movers until each
converts, and the head-tracking repos checked out beside core that `data/config-format.json` does
not list. That is the conversion queue, not a regression.

### Added - `data/config-format.json`: which repos migrate, and where their configs live

A new data file for the canonical config migration, and `pixi run check-config-format`
(`scripts/check-config-format.mjs`, part of `pixi run check`) to gate it. Nothing reads it yet: the
config conformance checks and each game's conversion will.

- **`legacy`**: the 87 repos that published a pre-canonical build, meaning a `v*` release or the
  rolling `dev` pre-release on GitHub. Only these carry a frozen legacy import. The list was built
  on 2026-09-24 from `gh release list` over every org repo whose name contains "head", draft
  releases excluded, and matches the 87 of design section 8 name for name. Entries are keyed by the
  current repo name; `renamed_from` gives the old name for `pathologic-2-headtracking`
  (`pathalogic-2-headtracking`) and `stormworks-build-and-rescue-headtracking`
  (`stormworks-headtracking`), which local checkouts still carry. `predecessors` names earlier
  repos of the same mod with published releases, whose shipped files are differential-test inputs:
  `fallout-new-vegas-headtracking-delete`, `obra-dinn-headtracking-old2`, `peak-headtracking-old`
  and `resident-evil-requiem-headtracking-dev`. `obra-dinn-headtracking-old` is not among them: its
  only release, v1.0.0, is a draft, so it never published a build. The set is frozen. The checker
  pins the SHA-256 of the sorted names, so adding, removing or renaming one fails `check`.
- **`exempt`**: the seven repos that cannot move to a canonical reader (cyberpunk-2077,
  the-pathless, beamng-drive, fusion-360, minecraft-java-edition, firewatch, outer-wilds) and the two
  with no config (green-hell, ni-no-kuni-wrath-of-the-white-witch), each with its reason.
- **`configs`**: for each of the 127 migrating repos, each config file: `committed` (the repo path
  of the rendered file, null where the repo tracks none today), `installed` (every path relative to
  the game folder, one per store layout, from `data/games.json`'s `executable_relpath` and
  `xbox_executable_relpath` where the reader resolves the exe or module folder), `legacy_source`
  (`BepInEx\config\<GUID>.cfg` for the 18 BepInEx repos, whose canonical file is
  `BepInEx\config\<GUID>.ini`) and `dialect` (`native`, or `unity` for the BepInEx and Cecil repos,
  which read key names only). mass-effect-legendary-edition has three files. minecraft-bedrock-edition
  has no path in the game folder, since its file sits beside the mod DLL in Lopari's mod_home, so its
  `installed` is empty with a `no_installed_reason`, the only form the checker accepts an empty list
  in. Game paths use backslashes, as install scripts and `PRESERVE_FILES` write them; repo paths use
  forward slashes, as git writes them.
- **`normalisations`**: N2 (a non-finite float imports as the row's default), approved by the owner
  on 2026-09-24, with `drop_rule` `NonFiniteNumber`, and N1 (a legacy hotkey code outside 0x01-0xFE
  imports as unbound), approved on 2026-09-25 with `drop_rule` `KeyCodeOutOfRange` and the probe
  result. A normalisation with `approved` null would carry a `pending` text and may not name a
  DropRule.
- **`approved_changes`**: the three docs-survey decisions a differential test also allows, each with
  its text, its decision number, the date 2026-09-24 and its DropRule: `reticle` (decision 2),
  `pose_shaping` (decision 3) and `follows_default` (decision 4).
- **`hotkey_exceptions`** and **`allow_legacy_symbols`**, both empty. A game that binds a chord
  letter itself records `{ "<hotkey key>": { "replaces", "with", "reason" } }`; a use of a symbol the
  legacy-reader check bans that does not read the config records `{ "symbol", "file", "reason" }`.

The checker fails on an unknown or missing key, a duplicated key anywhere in the file (which
`JSON.parse` would drop silently), a repo named twice across `legacy`, its predecessors and old
names, and `exempt`, an exempt repo with a `configs` entry, a `legacy` repo without one, a path that
is absolute, uses the other separator or holds an empty, `.` or `..` segment, one installed path
listed twice in a repo, a DropRule named twice, and any change to the `legacy` names.

### Added - `MOD_SEED_FILES` in the REFramework install body

`scripts/install-body-reframework.cmd` reads the optional CONFIG BLOCK list `MOD_SEED_FILES`
that the ASI, shim, shim-forwarder, xNVSE and BeamNG bodies already read: files copied into
`reframework\plugins\` only when that folder does not already hold one of the same name, so a
script install or update keeps the values a player tuned. Each file comes from the package's
`plugins\` folder, or its root, the same places `MOD_DLLS` come from, and is copied before
`MOD_DLLS`. The install prints `Deployed default <name>` or `Kept your existing <name>`. A seed
missing from the package, or one that cannot be copied, fails the install with exit 1 and no
state file, as a failed `MOD_DLLS` copy does.

The uninstall side is unchanged: `uninstall-body.cmd` already removes `MOD_SEED_FILES` from
`reframework\plugins\`, and a path listed in `PRESERVE_FILES` stays through that and through the
removal of `reframework\`.

`scripts/templates/install-wrapper-reframework.cmd` now sets `MOD_SEED_FILES` in its CONFIG
BLOCK, blank, as the ASI, BeamNG, shim, shim-forwarder and xNVSE templates do. CONFIG BLOCK
values carry over within one console (see `PRESERVE_FILES` below), so an REFramework wrapper
without the line, run from a console that already ran another mod's wrapper that set it, reads
that mod's list. Unless its own package ships those names, the install then fails with "not
found in installer package" and exit 1, after REFramework has been extracted and with no state
file written.

Unset or empty, the install behaves exactly as before. `scripts/test-uninstall-preserve.ps1`
now also installs, reinstalls, uninstalls and installs again through a real console into
synthetic REFramework trees, with both the package and the game under a path holding `!`,
with `installed_by_us` true and with `/force`; given `-ReferenceInstallBody`, it checks the
install without the list against an older body. It also runs the wrapper template itself from a
console that already holds another mod's `MOD_SEED_FILES`, and expects a clean install.

Consuming repos: every REFramework repo's `install.cmd` now sets `MOD_SEED_FILES`, blank until it
converts to the canonical config format (see the fix at the top of this section). The conversion
then moves `HeadTracking.ini` from `MOD_DLLS` to
`MOD_SEED_FILES` in `install.cmd` and, as the uninstall template asks, lists it the same way in
`uninstall.cmd`. Until then every script install overwrites that INI, as it always has.

### Added - `PRESERVE_FILES`: uninstall.cmd can keep a mod's config

`scripts/uninstall-body.cmd` reads a new optional CONFIG BLOCK list, `PRESERVE_FILES`:
config files the uninstall leaves in place, so a player's settings survive an uninstall and
reinstall. Entries are paths relative to the game folder, space-separated, quoted when one
holds a space (`"Eternal Afternoon_Data\Managed\HeadTracking.cfg"`). Each listed path's
`<path>.pre-canonical` and `<path>.pre-canonical.last`, the copies the config migration keeps
of the file it converted, are kept with it without being listed.

- Every per-file removal (`MOD_DLLS`, `LEGACY_DLLS`, `MOD_SEED_FILES`, `MOD_LEFTOVERS`,
  `ROOT_EXTRAS`, `MANAGED_EXTRAS`, the loader files) skips a listed path, compared as a full
  path without regard to case, and prints `Kept: <name>`.
- Removing a loader folder (`BepInEx\`, BepInEx's `dotnet\`, `MelonLoader\`, `reframework\`,
  a UE4SS mod folder) first moves each listed file inside it to `CameraUnlock-kept-configs\`
  in the game folder under the same relative path, removes the folder, recreates the file's
  parent and moves it back. A file that cannot be set aside leaves the folder in place; a
  file that cannot be moved back stays in `CameraUnlock-kept-configs\`, which the uninstall
  names. Either way the run ends "Uninstall Incomplete", exit 1, with the state file kept.
- An uninstall that finds `CameraUnlock-kept-configs\` already there refuses with exit 1
  before it touches anything, since that folder holds a config an earlier run did not put
  back.
- An entry with a wildcard, a drive, a leading or trailing `\`, a `..`, a `/`, a `!`, a
  parenthesis, `&`, `^`, `<`, `>` or `|`, or an empty entry, fails with exit 1 and a message
  naming `PRESERVE_FILES`. Inside a quoted entry those last five are also unsafe in the
  wrapper's own `set "PRESERVE_FILES=..."` line, where the entry's quotes leave them exposed
  and cmd.exe drops the value before the body can see it.
- An entry, or one of its two copies, that is a folder in the game folder fails with exit 1
  before anything is touched: only files are set aside, so a listed folder would go with the
  loader folder around it.
- The value is inherited like every CONFIG BLOCK variable, because the wrapper sets its CONFIG
  BLOCK before its `setlocal`. A wrapper with no `PRESERVE_FILES` line, run from a console that
  already ran a converted mod's uninstall, keeps any of its own files at the paths that mod
  listed. Lopari runs each script in its own `cmd /C` and is not affected. The uninstall wrapper
  template sets the line, blank (see the fix at the top of this section).

Unset or empty, the uninstall behaves exactly as before. `scripts/test-uninstall-preserve.ps1`
runs the body through a real console against synthetic game trees under a path holding `!`
and a space, including every list type, BepInEx, REFramework and UE4SS trees with
`installed_by_us` true and false and `/force`, the failure paths and the pause on failure;
given `-ReferenceBody`, it also checks the cases without the list against an older body.

Consuming repos: every uninstall wrapper now sets `PRESERVE_FILES` blank (see the fix at the top
of this section). A repo fills it in when it converts to the canonical config format.

### Added - REFramework configs on the canonical format, behind `PluginConfigSchema::canonicalConfig`

An RE game converts by setting `canonicalConfig` and `PluginModDescriptor::gameName`. Nothing in
the fleet sets it yet, and with it unset every RE mod reads, migrates, writes and binds hotkeys
exactly as before, `ApplyIniEdits` and its RE8 migration included (runbook G7 still applies to
those builds).

- **`PluginConfigSchema::canonicalConfig`** (bool, default false), appended after `modId`,
  since every mod initialises the schema positionally. **`PluginModDescriptor::gameName`**: the
  game's name as data/games.json spells it, written at the top of the canonical file; required
  with `canonicalConfig`, and `PluginMod::Initialize` throws `std::invalid_argument` without it.
- **`PluginConfig::Read(path, schema)`**: `SetDefaults`, every read and `Validate`, exactly as
  `Load` did them, with no log line and no migration; false, on the defaults, when there is no
  file. `Load` is now `Read`, its two log lines and the RE8 migration, unchanged in behaviour.
  `Read` is the legacy import of all six RE mods, so it is frozen from here on, and with it
  `SetDefaults` and `PluginConfig`'s field initialisers, which are what it gives a key an old file
  lacks. A default that changes for new files changes in `PluginConfigTable`'s defaults instead.
  The C++ suite pins `Read` with hand-written values on each fixture and on out-of-range,
  unparseable, empty and missing inputs, so a change to it fails even though `Load`, which the
  import is compared with, calls it too.
- **`PluginConfig` hotkey lists**, appended: `toggleKeyBindings` (`End, Ctrl+Shift+Y`),
  `cycleTrackingModeKeyBindings` (`PageUp, Ctrl+Shift+G`), `yawModeKeyBindings` (`PageDown,
  Ctrl+Shift+H`) and `diagnosticMarkerKeyBindings` (`F9`), as canonical text. In canonical mode
  `PluginMod` sets the int codes (`toggleKey`, `positionToggleKey`, `yawModeKey`,
  `diagnosticMarkerKey`) to 0, so a `registerExtraHotkeys` callback still reading one registers
  nothing instead of a default over the player's setting. resident-evil-village's F9 callback
  moves to `diagnosticMarkerKeyBindings` in its own conversion.
- **`cameraunlock/reframework/plugin_config_table.h`**: `PluginConfigTable(schema)`, the
  canonical table over `PluginConfig` with defaults from `SetDefaults(schema)`: `[Network]
  UdpPort`; `[General] EnableOnStartup` and `WorldSpaceYaw` (Writable); `[Smoothing]
  LocalSmoothing`, `RemoteSmoothing`; `[Position] PositionEnabled` (Writable), `PositionLimitX`,
  `PositionLimitY`, `PositionLimitZ`, `PositionLimitZBack`; `[Hotkeys] ToggleKey`,
  `CycleTrackingModeKey` (the legacy `PositionToggleKey`, the PageUp action) and `YawModeKey`,
  with the local row `DiagnosticMarkerKey` when `schema.diagnosticMarkerKey`; `[Light]
  LightFollowsHead` and `LightMultiplier` when `schema.flashlight`. No sensitivity or inversion
  rows (the tracker shapes the pose), so `schema.positionSensitivity` stays each game's constant;
  no `RotationEnabled` (the mode cycle is two-state), no `PositionLimitYDown` (`PluginMod` mirrors
  `PositionLimitY` downwards, and the row's comment says so), no `ConfigVersion`.
  `PluginConfigLegacyImport(schema)`: `Read` on the ANSI path, the in-memory half of the RE8
  `InvertX` correction, then each field the table carries; each hotkey code becomes a list of that
  key plus the Ctrl+Shift chord the legacy bootstrap registered beside it (Y, G, H; none for
  `DiagnosticMarkerKey`), so the chords move into the lists with no change in what fires. A
  sensitivity or inversion away from its `SetDefaults` value is recorded as dropped
  (`PoseShaping`). `Imported`, or `Absent` when `Read` finds no file (so a path the ANSI code page
  cannot hold is `Absent`, as the legacy build found no file there). Writes nothing. `keys` lists
  every key `Read` reads for the schema. No N1 rule is needed: `Read` refuses any hotkey code
  outside 0x01-0xFE (and the modifiers) and keeps the default, so every code it gives has a
  canonical spelling.
- **`PluginMod` in canonical mode**: `LoadConfig` builds a `ConfigOwner<PluginConfig>` on the wide
  path of `CameraUnlock.ini` beside the plugin DLL with that table and import, and `legacy_path`
  the `configFileName` beside it (`HeadTracking.ini` by default), so with neither file the owner
  creates `CameraUnlock.ini` (`Created`, the old Save-on-missing) and a legacy file is imported
  once while `CameraUnlock.ini` is absent, and never written. The owner's log lines are
  logged at once (logging is up by then), as warnings with the player's reason when the load was
  not usable. `RequestCycleTrackingMode`, on the hotkey thread, computes the next two-state mode
  from the mode the render thread last applied, stores it as desired, requests the apply and
  saves `PositionEnabled`; two presses before a frame still move one step. `ToggleYawMode` saves
  `WorldSpaceYaw`. The End toggle never saves. A save that is `NotSaved` or `Uncertain` logs its
  row, reason and the owner's lines, which name the path and the step. `RequestCycleTrackingMode`
  is now defined out of line, same signature.
- **`InitializePlugin` in canonical mode** registers Toggle, the mode action and the yaw toggle
  from the three lists with `input::RegisterKeyBindings`, and no hard-coded chord: the chords are
  items of the lists, rebindable like any key.
- **Fixtures**: data/fixtures/reframework-legacy holds the HeadTracking.ini each of
  resident-evil-2, -3, -4, -7, -village and -requiem shipped at its newest published build, byte
  for byte, plus the older file Requiem v0.4.0's launcher manifest seeds. The C++ suite runs
  `PluginConfig::Load` against the import on each and on its whole corpus (about 2,000 inputs a
  file, floats bitwise), checks the import leaves its copy untouched, imports RE8's and
  Requiem's corpora through the owner into `CameraUnlock.ini`, and imports each shipped file into a
  `CameraUnlock.ini` that reads back as imported, is not rewritten by a second load, and edits one
  line per `PositionEnabled` or `WorldSpaceYaw` save. Every owner import leaves `HeadTracking.ini`
  byte for byte as it was, with nothing but the two files in the folder. Requiem v0.4.0's
  installer ships position sensitivity 1.0 and its launcher seed 2.0, and core cannot tell a
  seeded 2.0 from a player's.
  The owner ruled on 2026-09-25 that 1.0 stands (`data/config-format.json` `conversion_notes`),
  so the seed's 2.0 is logged as dropped.

### Added - the C++ config owner and migration driver

`cameraunlock::config::ConfigOwner<Config>` in `config/config_owner.h` is the C++ twin of the C#
`ConfigOwner<TConfig>` below: the one reader and writer of a game's canonical config file, writing
only through `WriteFileChecked`. It has the same statuses with the same numbers, the same player
messages and the same decisions, including the two the C# entry records: a missing file at `Save`
is `NotSaved`, and a file that appears while `Load` creates one is `Deferred` on the defaults.
Nothing in the fleet uses it yet; each native game's conversion wires it. The statuses, options
and results compile everywhere; the owner itself is Windows only, as the writer is.

- **Options by member assignment**: `ConfigOwnerOptions<Config>` with `path` (a fully qualified
  `std::wstring`, `CameraUnlock.ini`; there is no ANSI path), `table`, `import`
  (`LegacyImport<Config>`, whose empty `run` means the game never published a pre-canonical
  build), `legacy_path` (the fully qualified legacy file the import reads, `HeadTracking.ini`
  beside `CameraUnlock.ini`), `header` and `status_sink`. The constructor throws
  `std::invalid_argument` for an empty or relative path, a table with no rows, an import that
  names keys but has no run, an import `run` with no `legacy_path`, a `legacy_path` with no `run`,
  a `legacy_path` that is not fully qualified or names the file `path` names (compared without
  case), a table that has both `RotationEnabled` and `PositionEnabled` and marks only one Writable,
  or a header the renderer refuses.
- **`Load()`** returns `ConfigLoadResult<Config>` (`ConfigLoadStatus`: `Canonical` 0, `Migrated`
  1, `Created` 2, `Deferred` 3, `LegacyRefused` 4, `Unreadable` 5) with the `config`, the
  `diagnostics`, a UTF-8 `log` and the player's `reason`. It must not run under the loader lock:
  call it from the game's init thread, never from `DllMain`. A file at `path` is read as
  canonical, stamped or not, and the legacy file is never opened; when one exists
  (`GetFileAttributesW`) the log says settings are read from `path` and the legacy file is left as
  it was and not read. With no file at `path`, the legacy file is imported into a new file at
  `path`, or with no legacy file either, `path` is created from the defaults.
- **Import** holds the legacy file with `CreateFileW(GENERIC_READ, FILE_SHARE_READ |
  FILE_SHARE_WRITE)`, no `FILE_SHARE_DELETE`, from its snapshot through the import to a second
  read, then closes it before the commit. The import is handed a `LegacyInput`: the wide path, its
  ANSI form from `WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, ...)`, and `ansi_lossy` when a
  character had no ANSI form (or the form does not convert back). An import that reports `Absent`
  while the owner holds the file is deferred with both paths in the log, unless the ANSI form was
  lossy: then the published build, handed that form, never saw the file and ran on its defaults,
  so the defaults the import gave are written to `path`, the legacy file is left as it was, and the
  log names the case. Every import of a lossy path logs it, so a per-key import that simply read
  nothing is named too. The render is read back through the table and `path` is created with a
  create-if-absent checked write. The legacy file is never written, renamed, deleted or copied.
  Verification, the not-carried and dropped-value log lines, and every deferral match the C# owner.
- **`Save(const std::function<void(Config&)>&)`** returns `ConfigSaveResult`: `Saved` 0,
  `NotSaved` 1 with the `reason` and the Win32 `error`, or `Uncertain` 2 whose reason names the
  file and the kept `temporary_path`. A changed row the table does not mark Writable throws
  `std::logic_error` naming it; the mode pair, the stamp and `ConfigFormat` repair, the read-back
  through the table and the no-op save are as in C#.
- **`Reload()`** returns `ConfigReloadResult<Config>` (`Unchanged` 0, `Applied` 1, `Unreadable`
  3; 2 is not used; `config` is a `std::optional`), reads only `path` and never writes.
  **`FileChanged()`** compares the last write time from `GetFileAttributesExW` with the recorded
  one; a missing file counts as 0. When `GetFileAttributesExW` refuses a file that is there (one
  pending deletion refuses it with access denied), the time comes from `FindFirstFileW`, as .NET's
  `File.GetLastWriteTimeUtc` reads it, so `Load` defers on such a file rather than throwing.
- One `std::mutex` around `Load`, `Reload`, `Save` and `FileChanged`; the status sink runs after it
  is released. `Save` is synchronous: call it from the HotkeyPoller thread, never per frame.
- `ConfigLoadStatusName`, `ConfigSaveStatusName` and `ConfigReloadStatusName` give the C#
  spellings.

The tests run the C# scenario list against real files, the held-handle test (GetPrivateProfileStringA,
`fopen`, `_wfopen` and `std::ifstream` read a held file; an open denying read sharing, a delete and
a rename fail), a legacy file in a folder named outside the ANSI code page, and a child of the
test binary killed at each of the 11 steps its hook names in an import
(`--config-owner-interrupt <step>`), after which the legacy file is unwritten, `path` is absent or
whole, and the next launch ends byte for byte where an uninterrupted one does.

### Deprecated - the C++ flat config readers

`ParseIniConfig`, `HeadTrackingConfig::LoadFromFile` and `HeadTrackingConfig::ApplyValues` are
deprecated in their comments, so the mods that call them build as before. They stay until a major
version. A converted native game reads its config through `config::ConfigOwner` with
`config::HeadTrackingConfigTable`. `IniReader` and `IniWriter` are not deprecated here:
`ini_reader.h` is frozen byte for byte for the legacy imports that call it.

### Added - the C# config owner and migration driver

`ConfigOwner<TConfig>` in `CameraUnlock.Core.Config` is the one reader and writer of a game's
canonical config file, `CameraUnlock.ini`: while that file is absent it imports the game's legacy
file once through the game's frozen import into a new file, or creates the file from the defaults;
it saves the rows the table marks Writable and reloads. It never writes the legacy file, and
writes `CameraUnlock.ini` only through `CheckedFileWriter`. Nothing in the fleet uses it yet; each
game's conversion wires it. The C++ twin comes next and follows the same statuses and decisions.

- **Options by property**: `ConfigOwnerOptions<TConfig>` with `Path` (absolute, `CameraUnlock.ini`),
  `Table`, `Import` (null for a game that never published a pre-canonical build),
  `LegacySourcePath` (the legacy file the import reads, beside `Path`), `Header` and `StatusSink`.
  The owner copies them when it is built and throws `ArgumentException` for a missing path, table
  or header, a relative path, an `Import` without a `LegacySourcePath`, a `LegacySourcePath`
  without an `Import` or naming `Path`, a table that has both `RotationEnabled` and
  `PositionEnabled` and marks only one of them Writable (a mode change writes the pair), or a
  header the renderer refuses.
- **`Load()`** returns `ConfigLoadResult<TConfig>`: `Status` (`ConfigLoadStatus`: `Canonical` 0,
  `Migrated` 1, `Created` 2, `Deferred` 3, `LegacyRefused` 4, `Unreadable` 5), the `Config` the
  session runs on, the reader's and table's `Diagnostics`, a `Log` of lines naming the file for the
  game to write once its logger is up, and the player's `Reason`. A file at `Path` is read as
  canonical, stamped or not (`Unreadable` when it is UTF-16 or holds a NUL), and stamped by the
  first save that changes a row; the import never runs and the legacy file is never opened, and
  when a legacy file exists the log says settings are read from `Path` and the legacy file is left
  as it was and not read. With no file at `Path` and a legacy file, the legacy file is imported
  into a new file at `Path`. With neither, the file is created from the table's defaults, never
  over a file that appears meanwhile (that is `Deferred` on the defaults, and nothing retries).
- **Import**: the owner holds the legacy file open for reading, sharing read and write but not
  delete, from its snapshot through the import to a second read of its bytes, so no program can
  newly lock, rename or delete it meanwhile and a write is caught. It renders the imported settings,
  reads the render back through the table and requires every row to equal the import's (floats
  bitwise), then creates `Path` with a create-if-absent checked write. The legacy file is never
  written, renamed, deleted or copied. The log lists the import's dropped values and every key
  line of the legacy file the import does not read (`not carried: [General] Smoothng=0.3 on line
  5, this build does not read it`). A process killed at any step leaves `Path` absent or whole.
- **Deferral**: a legacy file in use by another program, a folder that cannot be written, a legacy
  file changed during the import, a commit that fails, a value no codec writes or a render that
  does not read back (`[Light] LightMultiplier=7.5 cannot be converted`), and an import that
  reports `Undecodable` or `Absent` create no file at `Path` and give `Deferred`; an import that
  refuses the file gives `LegacyRefused`. The session runs on what the import gave, the player is
  told once through the status sink, nothing is saved that session and the next launch imports
  again, unless another program created `Path` meanwhile, which the next launch reads instead.
- **`Save(Action<TConfig> change)`** returns `ConfigSaveResult`: `Saved` 0, `NotSaved` 1 (with the
  `Reason` and the unchanged `Error`) or `Uncertain` 2 (Windows did not finish the replacement; the
  reason names the file and `TemporaryPath`, the kept temporary). The change runs on the settings
  read from the file as it is now; a changed row the table does not mark Writable throws
  `InvalidOperationException` naming it, so End, which changes only the session, never writes
  `EnableOnStartup`. When `RotationEnabled` or `PositionEnabled` changes both are written. It saves
  only a readable file whose `ConfigFormat` is not newer than this build's, stamping an unstamped
  one in the same edit (a stamp with no `ConfigFormat`, or one that is not a number, gets
  `ConfigFormat=1` the same way), and reads the edited bytes back
  through the table before writing: only the changed rows may differ. A change that leaves every
  row as it was writes nothing.
  Never rolls back or retries.
- **`Reload()`** returns `ConfigReloadResult<TConfig>`: `Unchanged` 0 (the file holds the bytes
  the owner last created, imported or saved), `Applied` 1 (any other file at `Path`, read as
  canonical, stamped or not) or `Unreadable` 3 (a missing file or one the canonical reader cannot
  read; the game keeps its settings). 2 is not used. It reads only `Path`, never imports and never
  writes. **`FileChanged()`** compares the
  file's last write time with the one recorded at the last Load, Reload or save.
- One lock around Load, Save and Reload; the status sink runs after it is released. A Unity mod
  calls them on the main thread: a save is one synchronous write per key press.

Decisions the design left open, each in the API's own documentation:

- **A missing file at `Save` is `NotSaved`** ("the settings file is missing; it is created again at
  the next launch"), not created: creating it outside `Load` would pre-empt the import from the
  legacy file.
- **A file `Load` cannot open** (another program holds it denying read sharing) is `Deferred` on the
  table's defaults and the import does not run, since it runs only while no file is at `Path`.
- **The import reads only `LegacySourcePath`**: a file at `Path` is always read as canonical, and
  one that lost its stamp is stamped again at the next save. Deleting it imports the legacy file
  again at the next launch.
- **After a `Deferred`, `LegacyRefused` or `Unreadable` load**, every save that session is
  `NotSaved`, until a `Reload` applies a readable file.
- An import that throws, or returns no result, is a bug: the exception reaches the caller.

A BepInEx import, documented on `Import`: BepInEx's `ConfigFile` reads the `.cfg` in its
constructor (`if (File.Exists(ConfigFilePath)) Reload();` in 5.4.23.5 and be.785), before the
owner holds the file, so the import sets `SaveOnConfigSet = false`, then calls `Config.Reload()`,
then binds. The fleet's BepInEx plugins compile against their own BepInEx assemblies, not core's
`csharp/stubs/BepInExStubs.cs`, and core calls none of those members, so the stubs are unchanged.

Not yet run: the owner on Unity Mono or under BepInEx 6 on CoreCLR. The scenarios run on .NET 8,
CLR 2 (net35) and CLR 4 (net472); the first converted game of each runtime checks it in game.

### Deprecated - the C# flat config readers and `HeadTrackingConfigBase`

`ConfigParsingUtils.ParseIniFile`, `HeadTrackingConfigData.LoadFromFile` and
`HeadTrackingConfigData.ApplyValues` (`CameraUnlock.Core`), and `HeadTrackingConfigBase`
(`CameraUnlock.Core.Unity.BepInEx`) are deprecated in their documentation, not with `[Obsolete]`, so
the mods that call them build as before. They stay until a major version. A converted game reads
its config through `ConfigOwner` with `HeadTrackingConfigTable`, and a BepInEx game reads its `.cfg`
only through its frozen legacy import.

### Added - legacy import support, normalisations N1 and N2, and the differential corpus, in C# and C++

What a game's legacy import hands the migration driver, the normalisations its map applies, and the
corpus generator its differential test runs. Nothing in the fleet uses them yet:
the config owners (next) drive the imports, and each game's conversion writes its own.

- **The import contract.** C++ `cameraunlock/config/legacy_import.h`: `ImportStatus` (`Imported`
  0, `Refused` 1, `Undecodable` 2, `Absent` 3), `ImportResult` with factories that hold each status
  to its fields (a reason for `Refused` and `Undecodable`, dropped values for `Imported` and
  `Absent`), `LegacyInput`, `LegacyKey` and `LegacyImport<Config> { run; keys; }`. C#
  `CameraUnlock.Core.Config`: `ImportStatus`, `ImportResult`, `LegacyImportInput`, `LegacyKey`,
  the delegate `LegacyImportRun<TConfig>` and `LegacyImport<TConfig>`. Two changes from the
  design's sketch, which had `run(Config&)` alone: `run` also takes the input, because the driver
  owns the file's path views and an import never recomputes them (C++ `LegacyInput`: the wide
  path, the ANSI path and whether the ANSI conversion lost characters; C# `LegacyImportInput`: the
  config path and, for BepInEx, the separate `.cfg` it migrates from); and the import carries
  `keys`, every section and key the frozen reader reads, so the driver can log every other key
  line of the old file as not carried and the corpus generator takes the same list. An empty
  section in a `LegacyKey` means any section, for the readers that ignore sections.
- **Dropped values.** `DroppedValue` (rule, section, key, the value as the import read it) and
  `DropRule`: `NonFiniteNumber` 1 (N2), `PoseShaping` 2 (a sensitivity, deadzone, curve or
  inversion set away from the shipped default), `Reticle` 3, `FollowsDefault` 4 (a feature
  shipped off pending verification that now takes the mod's default) and `KeyCodeOutOfRange` 5
  (N1). `PoseShaping`, `Reticle` and `FollowsDefault` are the docs-survey decisions; `FollowsDefault` is added to the design's list so decision 4's changes
  reach the log too. C++ `DescribeDroppedValue` and C# `DroppedValue.Describe()` give the log
  line, e.g. `not carried: [Smoothing] RemoteSmoothing=nan, it is not a finite number, so the
  default is used`.
- **N1**, C++ only, approved by the owner on 2026-09-25: `LegacyVirtualKeyToBindings(long long
  code)` gives the key name for a code from 0x01 to 0xFE (hex where the table has no name) and ""
  (unbound) for any other code; an overload records the drop, except for code 0, which is how a
  legacy file says unbound. A chord switch folds into the same list through
  `input::FormatKeyBindings`. No C# import reads virtual-key codes, so C# has no N1 helper.
- **N1 probe.** `pixi run probe-n1` runs `cameraunlock_tests --probe-getasynckeystate`, outside
  `check` because it injects a key press with SendInput and needs an interactive desktop. On
  Windows 11 Pro 10.0.26200 (2026-09-24), with F24 held, GetAsyncKeyState reported F24 (0x87) down
  and reported up for 0x187, 0x287, 0x10087, -121, 0, 0xFF, 0x100 and -1, so no out-of-range code
  reports a key in range. But 0xFF reports on its own: it is the code the SDK's kbd.h gives to
  scan codes a layout leaves unmapped (`VK__none_`), and with VK 0xFF itself held
  GetAsyncKeyState(0xFF) reported down. N1 therefore unbinds a legacy 0xFF hotkey that could fire,
  a change for a player who bound one, which the owner approved; no config the fleet ships binds
  0xFF.
- **N2**: C++ `LegacyFiniteOrDefault` (float and double), C#
  `LegacyNormalisations.FiniteOrDefault`: a NaN or infinite legacy value gives the runtime row's
  default and records the drop; a non-finite default throws.
- **The differential corpus generator**: C++ `GenerateIniMutations` in the header-only
  `cameraunlock/config/testing/ini_mutations.h`, and C# `IniMutations.Generate` in
  `csharp/testing/IniMutations.cs`, which no shipped assembly compiles: a test project links it
  with `<Compile Include="..\cameraunlock-core\csharp\testing\IniMutations.cs"
  Link="IniMutations.cs" />` (the path relative to the project). C# 7.3, and free of nullable
  warnings in a project that enables them. It takes a legacy file, the import's own key list
  (`LegacyImport` keys) and a descriptor for each of those keys (section, key, an alternate valid
  value, one out-of-range value per refused range, whether it is a hotkey and the chord switches
  that fold into it), and refuses the call when the list and the descriptors name different keys,
  so a game's corpus cannot leave out a key its import reads. A descriptor with an empty section
  is a section-less key, matched anywhere in the file, which covers the section-less C# readers
  and header-less files. It returns every mutation design 6.2 lists, per key, per pair of keys,
  per section and per file, as named outputs in a fixed order.
  `data/fixtures/canonical-ini/README.md` defines each byte for byte, and seven fixture cases
  under `data/fixtures/canonical-ini/mutations/`, two of them section-less, pin every output's
  name and SHA-256 in both languages.
  Choices the design left open: a key the file lacks is first added at its alternate value; the
  invalid value is `abc`; the listed `""` is both the empty value and a literal pair of quotes; the
  repeated section block carries each key's alternate value; the 1100-character value is the digit
  1 repeated; the 0x1A and NUL bytes are lines of their own after the first key; NUL padding is 64
  bytes; the UTF-16 output decodes the file as UTF-8 where it is UTF-8 and as code page 1252
  otherwise; a section-less key the file lacks goes after the last line above the first header,
  gets no section case swap, and moves to another section under a new `[Elsewhere]` at the end;
  "before the first header" and "UTF-8 mark before a header" apply only where the file has a
  header above the key or at all.

### Added - core's config table over its own config types, in C# and C++

`HeadTrackingConfigTable` builds the config table for a game that keeps its settings in core's
config type: C++ `HeadTrackingConfigTable({...})` in `cameraunlock/config/head_tracking_config_table.h`
over `HeadTrackingConfig`, C# `HeadTrackingConfigTable.Create(...)` over `HeadTrackingConfigData`.
Nothing in the fleet uses it yet; elite-dangerous and system-shock-2-25th-anniversary-remaster are
to run on it.

- **The game names the concepts it implements**, and core binds each; the game then appends its
  local rows and modifiers (`Select(...).Writable()` and the rest). A list rather than every
  concept, because a canonical file carries only what the game binds: a game with no carried light
  has no `[Light]`. It also means a concept core adds later never enters an existing game's file,
  and so never fails its render test, until the game names it. An empty list, a concept named
  twice, or (C++) a value that is not a canonical concept throws `std::invalid_argument` /
  `ArgumentException`; a null list or item throws `ArgumentNullException`.
- **A derived config type** carries the game's own fields for its local rows:
  `HeadTrackingConfigTable<ModConfig>({...})` where `ModConfig` derives from `HeadTrackingConfig`,
  and `HeadTrackingConfigTable.Create<ModConfig>(...)` where it derives from
  `HeadTrackingConfigData` and has a public parameterless constructor.
- **Defaults**: the config type's own defaults, with `ToggleKey`, `CycleTrackingModeKey` and
  `YawModeKey` at their schema `canonical_default`: `End, Ctrl+Shift+Y`, `PageUp, Ctrl+Shift+G`
  and `PageDown, Ctrl+Shift+H`. The field initialisers the flat readers use are unchanged (`End`,
  empty, `PageDown`).
- **Bindings**: `LocalSmoothing` and `RemoteSmoothing` set the top-level value and the position
  settings' copy (C# recomposes `Position` with `WithSmoothing`, as `ApplyValues` does).
  `PositionLimitY` never sets `PositionLimitYDown`; the flat readers keep their mirror.
  `CollisionMargin` and `CollisionReleaseSmoothing` are C++ `lean_clamp.skin` and
  `lean_clamp.release_smoothing`. `LightFollowsHead` and `LightMultiplier` are `light` in C++; in
  C# they, like the position limits, replace `Light` or `Position` with a copy holding the new
  value, so a `HeadFollowLightSettings` another config shares is not changed.
- **`CollisionChannel` is an Engine row**: the channel is data about the game, not a taste, so at
  its default it is written as the comment `; CollisionChannel=0` and a later build's corrected
  default reaches every user who never set it. Any other value is written as an active line.

`data/fixtures/canonical-ini/head-tracking/all-concepts.ini` is the defaults with every canonical
concept rendered, and both languages must produce it byte for byte; three apply cases hold every
field each concept reaches.

### Added - config tables, applying a canonical file and rendering one, in C# and C++

A config table lists the rows of one game's canonical config file and binds each to a field of
the game's config. Applying it reads a parsed file into the config; rendering it writes a config
as a canonical file. Nothing in the fleet uses them yet: core's own table and the config owners
build on them.

- **Concept rows** take their section, key, codec, range and comment from
  `data/config-schema.json`, and only a canonical concept can be named. C++ checks the field
  type at compile time: `bool` for a bool concept, an integral type other than `bool` whose
  limits hold the concept's range (a `std::uint16_t` holds `UdpPort`, not `DataFreshnessMs`),
  `float` or `double` for a float concept, `std::string` for a hotkey list. In C# the concept's
  descriptor fixes the accessors' type.
- **Local rows** name their own section, key, codec and comment. A row's default is its field's
  value in the defaults instance the table is built with. Modifiers on the last row: `Comment`
  (replaces a concept's comment), `Range` (int, float and double local rows), `Engine` (at its
  default the row is written as `; Key=value`), `Writable` (the owner's Save may change it), and
  `Select`, which picks a concept row a helper added.
- **Checks when a row is added**, which throw and leave the table as it was: one key name per
  file, counting the `ConfigFormat` key core writes in `[CameraUnlock]`; PascalCase local sections and keys; no local row in `[CameraUnlock]` or in a schema
  section with no canonical concept (`[Sensitivity]`, `[Inversion]`, `[Reticle]`); a local key
  may not be any concept's key or alias under the schema's normalisation; a local row with no
  comment must follow a local row of its section; a default must render; `RotationEnabled` and
  `PositionEnabled` may not both default to false.
- **Apply**: every row starts from its default, so an absent key reads as the default with no
  diagnostic; an invalid value keeps the default with a diagnostic naming the line, the value and
  what was expected; an unknown section or key draws one diagnostic, none in `[CameraUnlock]`; a
  key naming a row of the table in another section, or a concept row by an alias, names the row
  the mod reads it as, a key naming a retired concept says so, and one naming a concept the
  canonical format does not write gives the schema's reason, all in any section; `RotationEnabled=false` with
  `PositionEnabled=false` takes both defaults with one diagnostic naming both lines. No key takes
  its value from another. Fields no row binds are left alone.
- **Render**: the header (`; <display name> head tracking settings.`, the comments line, and the
  hotkey line when a row holds hotkeys), `[CameraUnlock]` with `ConfigFormat=1`, the schema
  sections in the schema's order with concept rows in its concepts order and then the local rows
  of that section, then the local sections; comments as `; text`; a blank line between sections;
  CRLF with a final CRLF; ASCII, no byte order mark. The display name must be printable ASCII.
- **Hotkey codec**: a hotkey list held as its canonical text. It reads any spelling the binding
  codec reads and gives the canonical text (`ctrl+shift+y,end` reads as `Ctrl+Shift+Y, End`),
  and renders only canonical text. C++ reads the native dialect, C# the Unity one.

C++ (`cameraunlock/config/config_table.h`, `hotkey_codec.h`, pure): `ConfigTable<Config>`,
`ApplyCanonical`, `RenderCanonical`, `ApplyReport`, `RenderHeader`, `FieldHoldsConcept<Id,
Field>()` and `HotkeyCodec`; failed checks throw `std::invalid_argument`. The generated
`cameraunlock/config/config_concepts.g.h` holds `schema::Concept`, `schema::ValueFamily`,
`schema::ConceptTraits<Id>` (section, key, family, range, file comment, canonical default),
`schema::kConcepts`, `schema::kSections` and `schema::kNonCanonicalConcepts`.

C# (`CameraUnlock.Core.Config`): `ConfigTable<TConfig>` (`Concept`, `Local`, the modifiers,
`Apply`, `Render` returning bytes), `ApplyReport`, `RenderHeader`, `HotkeyCodec`,
`ConceptDescriptor`, `ConceptDescriptor<T>`, `ConceptValueFamily`, and the generated
`ConfigConcepts` with one descriptor per canonical concept. A failed check throws
`ArgumentException`; a modifier on a row it does not apply to throws
`InvalidOperationException`.

Also added, for the reader: `CanonicalDiagnosticKind` gains `InvalidValue` (11),
`UnknownSection` (12), `UnknownKey` (13), `RetiredKey` (14), `NonCanonicalConcept` (15),
`NoTrackingMode` (16) and `MisplacedKey` (17), with their sentences; `CanonicalDiagnostic` gains
`detail` / `Detail`, the codec's expectation, the schema's reason or the row a misplaced key
belongs to; `CanonicalSection` gains `line` / `Line`, its
first header's line.

The fixtures in `data/fixtures/canonical-ini/table` hold both languages to the same applied
values, diagnostics and rendered bytes.

### Added - the canonical value codecs, in C# and C++

How each kind of value is written in a canonical config file and read back. Nothing reads a
config through them yet; the canonical config tables will. Every codec has one shape: parse
a value as the canonical reader hands it over (already trimmed) into the value or an error
naming what was expected, render the value as canonical text, and compare two values for
verification, floats bit for bit. Rendering throws for a value that would not read back as
itself.

- `bool`: writes `true` / `false`; reads `true false 1 0 yes no on off`, ASCII
  case-insensitive.
- `int`: writes decimal, `-` only when negative, no leading zeros; reads `-?[0-9]+` within
  the field type and an optional inclusive range.
- `hex32`, `hex64`: write `0x` and upper-case digits without padding (`0x404`, `0x0`); read
  `0x` or `0X` and 1 to 8 or 1 to 16 digits of either case.
- `float`, `double`: of the `%.Ng` texts (N 1 to 9, or 1 to 17) that read back to the same
  bits, the one with the smallest N that has no exponent, else the one with the smallest N,
  with `.0` appended when it has neither `.` nor `e`: `1.0`, `10.0`, `0.15`, `1e-05`,
  `-0.0`. Preferring the text without an exponent is what keeps 10 from being written
  `1e+01`, which the smallest N alone would give. Reads
  `-?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?` within an optional inclusive range; no `inf`,
  `nan`, leading `+`, leading or trailing `.`, comma or hex float, and a number too large
  for the type, or one that is not zero but rounds to zero, is invalid.
- `string`: C++ keeps the bytes, whatever their encoding; C# reads strict UTF-8 and treats
  anything else as invalid. Neither reads a byte below 0x20 other than tab.
- `enum`: PascalCase tokens, checked when the codec is built, read ASCII case-insensitively
  and written in the declared spelling, never as a number.
- `color`: four comma-separated floats in [0,1], each trimmed; written `r, g, b, a`.
- `list<hex32>`, `list<hex64>`, `list<string>`: split at `,`, items trimmed of spaces and
  tabs and non-empty; an empty value is an empty list; written joined by `, `. A string
  item that is empty or holds `,` cannot be written.

C++ (`cameraunlock/config/value_codecs.h`, pure, no `<windows.h>`): `CodecParseResult<T>`
(`value`, `error`, `ok()`), `BoolCodec`, `IntCodec<Int>` over every integral type but
`bool`, `Hex32Codec` and `Hex64Codec` (`HexCodec<std::uint32_t>` / `<std::uint64_t>`),
`FloatCodec` and `DoubleCodec` (`FloatingCodec<F>`), `StringCodec`, `EnumToken<E>` and
`EnumCodec<E>`, `ColorCodec` (`std::array<float, 4>`), and `Hex32ListCodec`,
`Hex64ListCodec` and `StringListCodec` (`ListCodec<Item>`). Each has `Value`, `Parse`,
`Render` and `Equal`; a range or token list that makes no sense, and a render that would not
read back, throw `std::invalid_argument`. Floats go through `std::to_chars` and
`std::from_chars`, so a consumer needs a standard library whose floating-point `<charconv>`
is correctly rounded down to the subnormals; verified with MSVC 19.50 and libstdc++ from GCC
12.5 and 13.5. `value_codecs.cpp` refuses to compile against libstdc++ 11, whose
`std::from_chars` reports every subnormal result as out of range.

C# (`CameraUnlock.Core.Config`): `IValueCodec<T>` (`TryParse(byte[], out T, out string)`,
`Render(T)` returning bytes, `Equal`), `BoolCodec`, `IntCodec` (int), `Hex32Codec` (uint),
`Hex64Codec` (ulong), `FloatCodec`, `DoubleCodec`, `StringCodec`, `EnumToken<TEnum>` and
`EnumCodec<TEnum>`, `ColorCodec` (a four-element float array), `Hex32ListCodec`,
`Hex64ListCodec` and `StringListCodec`. On .NET Framework a float can be written with a
different last digit than C++ writes (1234.5677490234375 as `1234.5678`), which C++ reads
as the same float. Neither `float.Parse` nor `double.Parse` is correctly rounded there, so a
double written there can read as a neighbouring double elsewhere, and so can a hand-typed
float text with more digits than core writes: `1.00000005960464477539062500001` reads as 1.0
there and as 1.0000001 in C++. The float texts core writes read the same in both; a
cross-check of 1.5 million of them found no difference. On every runtime a value reads back to the
same bits where it was written.

`data/fixtures/canonical-ini/codecs/cases.tsv` (397 rows) holds both languages to the same
text, run by the C++ suite, xunit on .NET 8 and `CameraUnlock.Core.FrameworkTests` on .NET
Framework 3.5 and 4.7.2, and each language sweeps over 200,000 random finite floats and
doubles plus zeros, denormals, the limits and the powers of ten, reading every render back
bit for bit.

### Added - the canonical concept set in the config schema, and `DataFreshnessMs` and `PositionAllowed`

`data/config-schema.json` now says which concepts the canonical config format writes, and
what it writes for them. Every field is new and optional to a reader that does not know it,
and nothing changes for the flat readers: every concept, alias and default parses as before.

- `canonical` on every concept. The ones no canonical file carries are `false` with a
  one-line `canonical_reason` for players: `RecenterKey` (the tracker app owns the centre),
  `ShowReticle`, `ReticleColor` and `ReticleToggleKey` (a mod draws a reticle only where
  the game shows none and the interaction point would otherwise be ambiguous, and never
  makes it a setting), the six sensitivities and six inversions (the mod applies the pose
  as the tracker sends it), and `PositionToggleKey` (the tracking-mode key is
  `CycleTrackingModeKey`).
- `file_comment` on every canonical concept: one or two lines of printable ASCII, without
  `"` or `\`, written above the key for players. `doc` stays the developer text.
- `range` (`min`, `max`, either optional) on the canonical numbers, equal to what the flat
  readers accept: `UdpPort` 1-65535, `DataFreshnessMs` 1-2147483647, the smoothing pair and
  `CollisionReleaseSmoothing` 0-1, the five position limits and the two tracker pivots 0 to
  `config::kMaxPositionLimit` (10), `LightMultiplier` 0 to `effects::kMaxLightMultiplier`
  (5), `CollisionMargin` 0 with no upper bound. `CollisionChannel` has none. The C# flat
  reader has always refused a tracker pivot outside 0-10; the C++ one takes any finite
  number, and the range follows the C# side. The owner confirmed the pivots' 0-10 range for
  both languages on 2026-09-25; the deprecated C++ flat reader keeps taking any finite pivot.
- `codec: "hotkey"` on the canonical key lists, and `canonical_default`, the binding list a
  canonical file starts with: `ToggleKey` `End, Ctrl+Shift+Y`, `CycleTrackingModeKey`
  `PageUp, Ctrl+Shift+G`, `YawModeKey` `PageDown, Ctrl+Shift+H`. The `default` values and
  the field initialisers keep the single key names they have always had.
- `scripts/generate-config-schema.mjs` checks all of it: `canonical` is a boolean on every
  concept; `canonical_reason` exactly on the non-canonical ones and `file_comment` exactly
  on the canonical ones; `range` only on canonical int and float concepts, with numbers of
  the concept's type, min not above max and the default inside; `codec` only `hotkey` and
  required on canonical string concepts; `canonical_default` only on hotkey concepts, and
  it must read in both the native and the Unity dialect of `data/keys.json`, spelled as
  the codecs write it. A concept field the generator does not know is an error. It also
  emits `cpp/tests/concept_ranges.g.h`, which the C++ test holds to the guard constants
  the way `ConfigSchemaDefaultsTests.SchemaRanges_MatchTheGuards` does in C#.

Two new concepts, each parsed by both flat readers:

- `[General] DataFreshnessMs`, int, default 500, no aliases: how long, in milliseconds, the
  newest tracker packet counts as current. Fields `HeadTrackingConfigData.DataFreshnessMs`
  and `HeadTrackingConfig::data_freshness_ms`. A value below 1 is refused with a log line
  and the previous value stands, because a window of 0 or less never counts a packet as
  current. Core only parses it. `DataFreshnessMs` leaves `deliberately_unaliased`.
- `[Position] PositionAllowed`, bool, default true, no aliases: false means the game never
  applies positional tracking and the tracking-mode control skips the position modes.
  Fields `HeadTrackingConfigData.PositionAllowed` and `HeadTrackingConfig::position_allowed`.
  Core only parses it.

Neither is on `IHeadTrackingConfig`, which consumers may implement. The generated tables
gain `ConfigKeySchema.Keys.DataFreshnessMs`, `ConfigKeySchema.Keys.PositionAllowed`,
`config_keys::kDataFreshnessMs` and `config_keys::kPositionAllowed`. A sweep of the sibling
repos' INI, cfg and JSON files and the decoded launcher manifest seeds found no file naming
`PositionAllowed`, and none of the three repos that read through core's flat readers
(the-painscreek-killings, elite-dangerous, system-shock-2-25th-anniversary-remaster) ships
`DataFreshnessMs`, so no config already on disk changes meaning.

### Added - key names and the hotkey binding codec, in C# and C++

One vocabulary for hotkey values: `End`, `End, Ctrl+Shift+Y`, or empty for unbound. Nothing
reads a config through it yet; the canonical config tables will.

- `data/keys.json` (schema_version 1) lists 334 keys by their written name, which is Unity's
  `KeyCode` member name, with the Windows virtual-key code and the Unity `KeyCode` value
  each carries. Every `KeyCode` member is there but `None`, Unity's value for no key. The
  names and values were read off the shipped `UnityEngine` assemblies of 18 games, Unity
  5.3.4 to 6000.3.15, which agree on every value. `LeftApple` and `LeftMeta` read as
  `LeftCommand`, and `RightApple` and `RightMeta` as `RightCommand` (one value, three
  names). A virtual-key code is paired with a name only where both mean the same key on
  every layout: letters, digits (`Alpha0`-`Alpha9`), `F1`-`F24`, the nav cluster, arrows,
  keypad digits and operators, `Escape`, `Space`, `Tab`, `Return`, `Backspace`, `Pause`,
  `Print`, `Numlock`, `CapsLock`, `ScrollLock`, left and right Shift, Control and Alt, the
  Windows keys and `Menu`. The `VK_OEM_*` punctuation codes have no name.
  `scripts/generate-config-schema.mjs` validates it and emits
  `cameraunlock/input/key_names.g.h` and `CameraUnlock.Core.Input.KeyNames` (internal);
  `pixi run check-config-schema` covers both.
- C++ (`cameraunlock/input/key_bindings.h`, pure, no `<windows.h>`): `KeyModifiers`
  (`kCtrl` 1, `kShift` 2, `kAlt` 4, with `|`, `&` and `HasModifiers`), `KeyBinding`
  (`modifiers`, `vk`), `ParseKeyBindings(std::string_view)` returning
  `KeyBindingsParseResult` (`bindings`, `error`, `ok()`), `FormatKeyBindings` and
  `FormatVirtualKey(int)`. Native values also read `0x` and one or two hex digits from
  0x01 to 0xFE, so every code a legacy file holds can be written; a code with no name is
  written that way (`0xBA`).
- C# (`CameraUnlock.Core.Input`): `KeyModifiers` (the same numbers), `KeyBinding`
  (`Modifiers`, `UnityKeyCode`), `KeyBindings.TryParse(string, out KeyBinding[], out string)`
  and `KeyBindings.Format(IList<KeyBinding>)`. Unity values read names only: a `KeyCode`
  value is not a virtual-key code, so a number would mean another key than it does in a
  native mod.
- The syntax: items split at `,`, each an optional `Ctrl`, `Shift` and `Alt` in any order,
  then one key, joined by `+`; ASCII case-insensitive; a binding listed twice is invalid;
  an error says what was expected. The canonical text writes `Ctrl+Shift+Alt+` in that
  order, the key's table name and `, ` between items, so `end,shift+ctrl+y` is written
  `End, Ctrl+Shift+Y`. `Format` and `FormatKeyBindings` throw for what would not read back
  (a code with no name in C#, a code outside 0x01-0xFE in C++, a repeated binding).
  `data/fixtures/canonical-ini/keys/cases.tsv` holds both languages to it.
- Registration. C++ `cameraunlock/input/key_binding_registration.h` (Windows):
  `RegisterKeyBindings(HotkeyPoller&, const std::vector<KeyBinding>&, std::function<void()>)`
  adds one hotkey per distinct key and returns those ids in the order each key first
  appears. Bindings that share a key share its hotkey, so one key press runs the action
  once however many items of the list it matches (`End, Ctrl+End` on Ctrl+End), as
  `IsTriggered` reports it once. C# `CameraUnlock.Core.Unity.Extensions.KeyBindingInput.IsTriggered(IList<KeyBinding>)`
  asks `Input.GetKeyDown` and `Input.GetKey`. Both apply one rule: a binding with modifiers
  fires when its key goes down while every modifier it names is held, either side; one
  without does not fire while Ctrl and Shift are both held, which is `NavGuarded`'s rule.
  Chords are ordinary items of the list, so `ToggleKey=End, Ctrl+Shift+Y` needs no second
  binding path. `KeyBindingInput.cs` is also in `csharp/il2cpp/CameraUnlock.Core.Unity.Il2Cpp.props`.
- `csharp/stubs/UnityStubs.cs` declares every `KeyCode` member with an explicit value (the
  values are unchanged), and a test holds each to `data/keys.json`.

### Added - the canonical INI reader, in C# and C++

One byte-level reader for the canonical config format, the dialect every converted mod
will read. It works at the level of sections and keys and leaves values as raw bytes;
nothing reads values into a config yet. Pure in both languages: no file I/O, and nothing in
the bytes makes it throw. C# rejects a null array with `ArgumentNullException`.

- C++ (`cameraunlock/config/canonical_ini.h`, namespace `cameraunlock::config`):
  `kConfigFormat` (1), `ParseCanonicalIni(std::string_view)` returning `CanonicalIni`
  (`status`, `unreadable_line`, `format_version`, `sections`, `diagnostics`, `Find`,
  `FindSection`), `HasCanonicalStamp(std::string_view)`, `CanonicalReadStatus`,
  `CanonicalDiagnosticKind`, `CanonicalDiagnostic`, `CanonicalSection`, `CanonicalValue`,
  `CanonicalReadStatusName`, `CanonicalDiagnosticKindName` and
  `DescribeCanonicalDiagnostic`. No `<windows.h>`; it compiles warning-free with
  `g++ -std=c++17 -Wall -Wextra -Werror`.
- C# (`CameraUnlock.Core.Config`): `CanonicalIni.Parse(byte[])`,
  `CanonicalIni.HasStamp(byte[])`, `CanonicalIni.ConfigFormat`, `CanonicalReadStatus`,
  `CanonicalDiagnosticKind`, `CanonicalDiagnostic` (with `Describe()`),
  `CanonicalSection` and `CanonicalValue`. Names and values are `byte[]`; lookups take a
  string and compare its UTF-8 bytes.

The rules: a UTF-16 byte order mark or any NUL makes the document unreadable and nothing
in it is read. CRLF, LF and a lone CR end a line; a UTF-8 byte order mark at offset 0 is
skipped; lines are trimmed of spaces and tabs only; blank lines and lines starting `;` or
`#` are skipped. A `[` line is a header named by the text up to its first `]`, trimmed;
text after the `]` is ignored with a diagnostic, a header with no `]` or an empty name
leaves the lines below it outside any section until the next header, and each key there is
reported. A key line splits at its first `=`. There are no inline comments, quotes or
escapes, so `B=true ; c` has the value `true ; c`. Repeated headers of one name are one
section, a repeated key keeps its last occurrence and one diagnostic names every line.
Section and key names compare ASCII case-insensitively and nothing else is folded; 0x1A
and every byte above 0x7F are ordinary bytes.

`[CameraUnlock] ConfigFormat` gives the format version. Missing, or not digits, reads as
`kConfigFormat` with a diagnostic; so does `0`, because formats are numbered from 1. A
number above `kConfigFormat` is kept as read (capped at 2147483647) with a diagnostic.
The section's other keys are reserved for core.

The stamp is a line that opens a section named `CameraUnlock` under the reader's own
header rule, so `[CameraUnlock] ; note` is stamped and `; [CameraUnlock]` is not, and the
stamp and the reader can never disagree about a readable file. A file starting with a
UTF-16 byte order mark is searched in its UTF-16 decoding, the only place core decodes
UTF-16, so a canonical file re-saved as UTF-16 still counts as stamped; a stamped file
can still be unreadable.

Diagnostics are returned, never logged, ordered by first line and then kind, and each
has a sentence for the player. `CanonicalDiagnosticKind` numbers 1 to 10 are the same in
both languages; later kinds are appended and no number changes.

The shared fixtures are `data/fixtures/canonical-ini/reader/<case>/input.ini` and
`expected.tsv`, 59 cases, with the row format and byte escape in
`data/fixtures/canonical-ini/README.md`. The C++ suite, the xunit suite and
`CameraUnlock.Core.FrameworkTests` on net35 and net472 all run them.

### Changed - the REFramework config migration edits through the INI editor and checked writer

`PluginConfig::Load`'s ConfigVersion migration (RE8's `[Position] InvertX` correction and
the `[General] ConfigVersion` stamp) used to rewrite the INI by opening it with
`std::ios::trunc`. It now builds the new bytes with `EditIni` and commits them with
`WriteFileChecked`, so the live file is never truncated. The version check, the InvertX
correction, the stamp and a later deliberate `InvertX=true` behave as before. No public
signature changed. What a migrated file looks like changes in these cases:

- A replaced line keeps the file's own key spelling, the spacing around `=` and any inline
  comment. `invertx = true` becomes `invertx = false`, not `InvertX=false`.
- The stamp is inserted after the section's last setting, not after its last non-blank
  line, so a comment closing `[General]` stays below it.
- A file whose last line had no newline got the stamp joined onto that line
  (`AutoEnable=trueConfigVersion=1`), which left the file unstamped and migrated it again
  on every launch. The stamp now goes on its own line, and the file still ends without a
  newline.
- Inserted lines use the file's dominant line ending, CRLF on a tie. It used to be CRLF
  whenever the file held any CRLF.
- The file is edited as GetPrivateProfileStringA, the mod's reader, reads it. A byte
  that is not UTF-8, such as an ANSI comment with an accented letter, is kept as it is.
  A UTF-8 byte order mark is part of the first line to that reader, so a header right
  behind it is not a header, and the stamp goes in a `[General]` appended at the end, as
  the old code did. The first of a repeated `InvertX` or `ConfigVersion` is the one
  edited, as before. A stamp for a repeated `[General]` goes under the first header,
  which is the only one the reader reads; the old code put it under the last, so that
  file was never stamped.
- A file the editor cannot edit as the reader reads it is left untouched: UTF-16, a
  control byte other than tab, LF and CR anywhere (the reader skips each byte from 0x00
  to 0x1F but those three before or after a key, before a header, just inside a header's
  brackets and at the start of a value, and the editor keeps them as part of the line),
  a `[` line with no `]` (the reader opens a section there and the editor none), and a
  key the migration edits whose first occurrence sits under a later header of a repeated
  section (the reader reads only the first block, so an edit there changes nothing it
  sees). A lone CR is refused as well, although in every shape probed
  GetPrivateProfileStringA ends a line there just as the editor does. The old code
  edited all of these, and a UTF-16 file came out corrupted.
- The editor writes no inline comments of its own, so the migration carries a replaced
  line's comment across itself (from the first `;` or `#` outside quotes, with the white
  space in front of it, to the end of the line). A comment it cannot write back leaves
  the file untouched: one holding a tab, a control byte or a byte above 0x7F, or ending
  in a space. The old code
  migrated such a file, rewriting the whole line and dropping the comment with it.
- A config that is read-only, held open without delete sharing, or changed on disk
  during the edit is left untouched too. In every refused or failed case the correction
  applies for the session only, the stamp is not claimed, the error log names the
  refusal and its line or the failed step and its Windows error, and the next launch
  tries again. When Windows stops partway through replacing the config and the writer
  cannot finish it, the log also says where the edited copy is, and when the config was
  gone and moving that copy into its place failed, it gives that move's Windows error.
- A config deleted between the migration's read and its write is reported and not
  recreated. The old code wrote it back from the copy it had read.

### Added - a checked file writer, in C# and C++

Writes new bytes over a file only while it still holds the bytes the caller built them
from. It is the file-system half of saving a preference; the INI editor below produces
the bytes. Windows only.

- C#: `CameraUnlock.Core.Config.CheckedFileWriter.Write(string path, byte[] expected,
  byte[] candidate)` returning `CheckedWriteOutcome`. A null `expected` means the file
  is expected to be absent. A failed step throws `CheckedWriteException`, an
  `IOException` whose inner exception is the original error, with `Step`, `TargetPath`,
  `TemporaryPath`, `TemporaryRemoved`, `OutcomeUncertain`, `CleanupError` and
  `CompletionError`. Builds on every target, net35 included, and throws
  `PlatformNotSupportedException` off Windows.
- C++: `cameraunlock/config/checked_file_writer.h`, with `WriteFileChecked(const
  std::wstring&, const std::optional<std::string>&, const std::string&)` returning
  `CheckedWriteResult` (`status`, `failed_step`, `error`, `outcome_uncertain`,
  `completion_error`, `temporary_path`, `temporary_removed`, `cleanup_error`), plus
  `CheckedWriteStatusName` and `CheckedWriteStepName`.
- `CheckedWriteOutcome` / `CheckedWriteStatus` are `Committed`, `TargetChanged`,
  `TargetAppeared`, `TargetMissing` and `TargetReplaced`, with the same numbers in both
  languages. C++ adds `Failed` where C# throws. `CheckedWriteStep` is `ReadTarget`,
  `CreateTemporary`, `WriteTemporary`, `FlushTemporary`, `CloseTemporary`,
  `RecheckTarget`, `Commit` and `RemoveTemporary`, numbered 1 to 8 in both, and C++ adds
  `None = 0`.

The writer reads the target (bytes, and identity as volume serial plus file index) and
stops with a conflict unless it matches `expected`. It creates
`<file name>.<32 hex digits>.tmp` beside the target with CREATE_NEW / `FileMode.CreateNew`,
writes the candidate, calls FlushFileBuffers and closes it, checking each result. Then it
reads the target again and goes ahead only if the bytes still match and it is still the
same file. An existing target is swapped with ReplaceFileW (`File.Replace` in C#, no
backup file), which keeps its hidden and system attributes. An absent one is created by
renaming the temporary without replace-existing (`File.Move` in C#,
MoveFileExW(MOVEFILE_WRITE_THROUGH) in C++), so a file that appeared after the check is
reported as `TargetAppeared` and left alone.

The target is never opened for writing, truncated or deleted. A read-only target fails
with Windows' access-denied error and keeps its attribute. After a conflict or a failure
the writer deletes only the temporary it created, by the exact name it recorded. It
never deletes by pattern, and it does not count a file already sitting at its chosen name
as its own. The exception is ReplaceFileW's ERROR_UNABLE_TO_MOVE_REPLACEMENT (and `_2`),
which Microsoft documents as able to leave the target missing or renamed. When nothing is
at the target path afterwards, the writer finishes the replacement itself with the same
rename it uses to create a file (`File.Move` / MoveFileExW(MOVEFILE_WRITE_THROUGH), still
without replace-existing), and a rename that succeeds is `Committed`. Without that the
next launch would find no file and write defaults. When a file is there, or the rename
fails, the write is reported as `OutcomeUncertain` / `outcome_uncertain` and the
temporary, which may be the only copy of the new contents, is kept. The rename's own error
is `CompletionError` (an exception, null when no rename was tried) in C# and
`completion_error` (a Win32 error, 0 when none was tried or it succeeded) in C++. The
final check and the swap are two operations, so another program writing the file between
them is overwritten; this is not compare-and-swap. The caller serializes its own saves.

Both test suites run the same scenarios against real files. Every step fails in turn
through an internal fault hook. The target is edited, swapped for a copy with identical
bytes, deleted, or created between the two reads, and created again just before the
rename. They also cover a handle open without FILE_SHARE_DELETE, an exclusive handle, a
read-only target, hidden and system attributes, a taken temporary name, a removal that
fails, an unfinished replacement with the target deleted, renamed or still in place, a
finishing rename that fails, unrelated `.tmp` and `.bak` files beside the target, and a
non-ASCII path. The C++ suite, and the new `CameraUnlock.Core.FrameworkTests` console on net35 and net472,
also kill a child process at the start of each step and check that the target is
unchanged and at most the child's own temporary is left behind. `pixi run
test-framework` runs that console on CLR 2.0 and CLR 4. `pixi run check` runs the net472
half only, because the net35 half needs the Windows .NET 3.5 feature and the CI image
has not been checked for it. None of this has run under Unity's Mono yet.

### Added - a pure batch INI editor for the canonical format, in C# and C++

Sets values in a canonical INI document's bytes and leaves every other byte where it was.
It does no file I/O: the caller passes the bytes in (empty for an absent file) and gets
the new bytes back, or a typed refusal and no bytes. It edits the grammar the canonical
reader reads, and it never decodes, so a byte in any encoding it does not edit is copied
through.

- C#: `CameraUnlock.Core.Config.IniEditor.Edit(byte[] original, IList<IniEdit> edits)`
  returning `IniEditResult` (`Succeeded`, `Refusal`, `Bytes`, `Section`, `Key`,
  `Lines`). `IniEdit(section, key, value, insertIfAbsent)` and
  `IniEdit(section, key, value, insertIfAbsent, firstOccurrenceWins)`. `Bytes` throws
  `InvalidOperationException` on a refused result. Builds on every target, net35
  included.
- C++: `cameraunlock/config/ini_editor.h`, with `EditIni(const std::string&,
  const std::vector<IniEdit>&)` returning `IniEditResult`, and
  `IniEditRefusalName`.
- `IniEditRefusal` is `None` (0), `Utf16` (1), `NulByte` (2) and `KeyNotFound` (3), with
  the same numbers in both languages. `Utf16` and `NulByte` are the two documents the
  canonical reader cannot read; `Lines` names the NUL's line, counted as the reader
  counts.

Lines are read as the canonical reader reads them: CRLF, LF and a lone CR each end a line,
a UTF-8 byte order mark at offset 0 is skipped and kept, lines are trimmed of spaces and
tabs only, a `[` line is a header named up to its first `]`, and a `[` line with no `]`
ends the section above it and names none. Repeated headers of one name are one section.
Sections and keys match ignoring ASCII case, and a replaced line keeps the file's own
spelling. An edit's section, key and value are printable ASCII, so an edit never matches a
key holding any other byte; SUB (0x1A), a vertical tab, a form feed, invalid UTF-8 and
cp1252 bytes are ordinary bytes of the lines around it.

A replacement rewrites everything after the `=` and the spaces and tabs after it, up to
the line terminator: the grammar has no inline comments, so `B = true ; note` becomes
`B = false`. The white space around `=` and the terminator stay. A repeated key has its
last occurrence replaced, the one the reader keeps, and the earlier ones stay. With
`insertIfAbsent`, a missing key goes after the last key line of its section's last block,
or straight after that block's header when it has none, and a missing section is appended
at the end of the file after a blank line. New lines take the file's most common line
ending, CRLF on a tie and then LF, except where that ending would pair with a CR before
it or an LF after it into one CRLF and lose a line: there the line gets CRLF. A file
whose last line has no terminator still has none afterwards. Keys above the first header belong to no section and are never matched.

An edit marked first-occurrence-wins (`IniEdit.FirstOccurrenceWins` in C#,
`IniEdit::first_occurrence_wins` in C++) is for a reader that takes the first of a
repeated key, as GetPrivateProfileStringA does. It replaces the first occurrence in the
document, and it inserts an absent key into the first block of a repeated section. Only
the REFramework migration (the entry above) uses it.

An edit that would not read back as given throws `ArgumentException` /
`std::invalid_argument`: a section, key or value holding anything outside printable ASCII
(0x20 to 0x7E, so a tab too), an empty section or key, a leading or trailing space on any
of the three, `]` in a section, `=` in a key or a key starting `[`, `;` or `#`, and two
edits of one key. A value may be empty, and may hold `;`, `#`, `=` and quotes.

Both implementations run the same byte fixtures in `data/fixtures/canonical-ini/editor`,
67 cases, with their `case.tsv` format in `data/fixtures/canonical-ini/README.md`. The C++
suite, the xunit suite and `CameraUnlock.Core.FrameworkTests` on net35 and net472 all run
them, and read every successful case back through the canonical reader: the edited keys
read their new values, every other key and every unedited line reads as before, and the
diagnostics differ only on edited lines.

Its only caller is that migration; nothing outside core calls it. It has not been
released, so this entry describes it as it first ships; while unreleased it was retargeted
from the flat readers' grammar to the canonical one, and `IniEditRefusal` was renumbered.

### Added - the tracking-mode mapping to `RotationEnabled` / `PositionEnabled`

`data/pipeline-conformance.json` gains a top-level `preference_modes` block beside
`constants`. `preference_modes.tracking_mode` lists the three modes in cycle order
(`both`, `rotation`, `position`) with the `RotationEnabled` / `PositionEnabled` pair
each is stored as: true/true, true/false, false/true. Any other pair, false/false
included, names no mode. The vector runner and `PipelineConstantsTests` read only
`vectors` and `constants`, so neither sees the new block.

The runtime is hand-written, not generated:

- C#: `CameraUnlock.Core.Tracking.TrackingModeChannels`, with
  `Encode(TrackingMode, out bool rotationEnabled, out bool positionEnabled)` and
  `TrackingMode? Decode(bool rotationEnabled, bool positionEnabled)`.
- C++: `cameraunlock/tracking/tracking_mode.h`, with `TrackingModeChannels`,
  `EncodeTrackingMode(TrackingMode)` and
  `std::optional<TrackingMode> DecodeTrackingMode(bool, bool)`.

Decode returns null / `std::nullopt` for a pair no mode writes, and does not map
it onto a mode. There is no new cycle helper, because `HeadTrackingSession.CycleMode`
already steps through the modes in this order in both languages. The tests check
that order against the file.

`TrackingMode` itself is unchanged. In C++ its definition moved from
`head_tracking_session.h` into `tracking_mode.h`, which `head_tracking_session.h`
includes, so every existing include still finds it.

`TrackingModeChannelsTests` reads the JSON directly. The C++ test compares against
`cpp/tests/preference_modes.g.h`, which `scripts/generate-config-schema.mjs` emits
from the block, and `pixi run check-config-schema` fails when that header is stale.
Only the tests include the header. Editing the JSON alone, or either language's
runtime alone, fails a test.

### Added - the `RotationEnabled` config concept, and a C++ gate on schema defaults

`data/config-schema.json` gains `RotationEnabled`: canonical `[General]
RotationEnabled`, bool, default `true`, with no aliases. A bare `Enabled` still
resolves to nothing. Both config halves parse it the way they parse
`PositionEnabled`, into `HeadTrackingConfigData.RotationEnabled` and
`HeadTrackingConfig::rotation_enabled`, and the generated tables gain
`ConfigKeySchema.Keys.RotationEnabled` and `config_keys::kRotationEnabled`. Core
only parses the key. Nothing in core reads either field yet, so what it does is
up to the mod.

A sweep of the sibling repos' INI, cfg and JSON files and the decoded launcher
manifest seeds found no file naming `RotationEnabled`, so no config already on
disk changes meaning.

`config_key_schema.g.h` now also carries each concept's declared default as
`cameraunlock::kConfigConceptDefaults` (with `ConfigConceptDefault`,
`ConfigValueType` and `kConfigConceptDefaultCount`). `config_schema_tests.cpp`
holds a default-constructed `HeadTrackingConfig` to that table, which is the C++
twin of `ConfigSchemaDefaultsTests`. A concept with no C++ field bound in the
test, or a C++ initialiser that disagrees with the schema, now fails
`pixi run test-cpp`.

The `WorldSpaceYaw` entry in `default_conflicts` is rewritten to match the
fleet today: dying-light-2 and dishonored-2 now default to world-locked yaw,
quake-ii-rtx and sleeping-dogs default true, and sonic-racing-crossworlds,
pixeljunk-monsters-2 and battlefield-bad-company-2 still start camera-local.

### Removed - BREAKING - the aim-down-sights mode cycle

Aiming down sights now has one behaviour across the fleet: head tracking carries
straight on through the aim and the weapon stays on the aim, with no setting and
no key (the `shooter-ads-handling` skill). The cycle's shared code is gone:
`cameraunlock/ads/ads_mode.h`, `entry_pose.h` and `ads_blend.h`, their C# twins
`AdsMode` / `AdsModes`, `AdsEntryPose`, `AdsPose` and `AdsPoseBlend`, the
`ads_mode` field on `HeadTrackingConfig` and `AdsMode` on
`HeadTrackingConfigData`, and the `AdsMode` key (with the `ADS` section) in
`data/config-schema.json` and the generated key tables. An ini that still carries
`AdsMode` loads as before; the key resolves to nothing and is ignored.

`AdsFade` stays, re-documented as the transition a mod rides to ease a positional
lean out while the sights are up. The `AimMarker` renderers stay too: mods draw
hip-fire reticles with them.

Consuming repos: remove the mode cycle, its `Insert` / `Ctrl+Shift+U` binding,
the ADS marker, the entry pose and the ADS branch of the tracking gate, per the
`shooter-ads-handling` prompt. A mod that still references any removed symbol
stops compiling when it bumps this pin.

### Added - install.cmd and uninstall.cmd ask for the game folder when detection finds nothing

`find-game.ps1` takes an `-Interactive` switch. With it, a run that detects no
install stops and asks for the folder instead of failing, re-asking until it is
given one that holds the executable `games.json` names for that game, or an
empty line to cancel. It accepts what Explorer's "Copy as path" and a
drag-and-drop produce - quoted, with a trailing separator - and resolves a
relative path before anything records it.

Every install body and the uninstall body pass the switch on the no-path branch
and only when the caller did not pass `/y`. The launcher passes a path and `/y`
and so never reaches the prompt; a path that was given and did not resolve is
still a hard error.

This is the only route into a game that publishes nothing to detect: one that
arrived as a zip from itch.io, Game Jolt or a direct download has no registry
key, no store manifest and no library folder, so its `games.json` entry carries
an `env_var` and nothing else, and before this the installer's whole answer was
to tell the user to re-run it with an argument.

`scripts/test-install-prompt.ps1` drives the cases through a real cmd.exe.

Consuming repos: nothing to change. Bodies ship from here, so a submodule bump
picks this up.

### Changed - the DX9 overlay hooks Present outright instead of waiting for the game's device

`DX9Overlay::Install()` now reads the shared `IDirect3DDevice9` vtable off a
windowed probe device it creates and releases on the spot, and hooks Present
there - the same route the DX11 and DX12 overlays here already take. Waiting for
the game's own `IDirect3D9::CreateDevice` made `Install()` an ordering
constraint on the consumer: called after the game has created its device, the
hook never fires again, and the overlay never draws with no error and no log
line to say why. An ASI cannot always be sure of arming first, and nothing about
the old behaviour told it when it had not.

The probe is windowed, is released before `Install()` returns, and passes
`D3DCREATE_FPU_PRESERVE`, so it takes no exclusive mode, holds no adapter, and
does not switch the running game's FPU to single precision. The CreateDevice
hook is still armed, and is still what fires `SetDX9DeviceReadyCallback`; it is
now also the fallback for a machine where the probe device cannot be created.

Consuming repos: nothing to change. A consumer that armed early enough to win
the old race keeps working, and one that did not now works too.

### Added - the aim marker on Direct3D 9

`rendering/aim_marker_dx9.h` binds `AimMarker<Traits>` to the existing DX9
overlay, so a D3D9 game gets the same mark, in the same place, as one on D3D11
or D3D12. `AimMarkerDX9` is the alias to use; define
`CAMERAUNLOCK_DX9_OVERLAY_IMPLEMENTATION` and
`CAMERAUNLOCK_AIM_MARKER_DX9_IMPLEMENTATION` in one TU, as with the other two.

Two things a consumer has to know, both documented at the top of the header and
both forced by how D3D9 is reached rather than chosen: `Ensure()` must be called
during initialisation and not from the frame that first wants the marker,
because the overlay arms itself by hooking `IDirect3D9::CreateDevice` and the
game calls that once, early; and `Ready()` therefore means the hook is armed
rather than that a device exists. `AimMarkerStyle`'s colours are `0xAABBGGRR`
while D3DCOLOR is `0xAARRGGBB`, which the shipped white-on-black style is blind
to and a coloured one would not be.

### Added - the loader components the notices gate could not see

`legal/` gains `miniz.txt`, `injector.txt`, `memorymodule.txt` and
`d3d8to9.txt`: the third-party sources Ultimate ASI Loader compiles into the
`dinput8.dll` every ASI mod vendors. The x64 asset carries MinHook, injector and
miniz; the 32-bit asset carries those plus MemoryModule (MPL-2.0) and d3d8to9.
None of them was named in `legal/README.md` or in any consuming mod's notices.

`scripts/validate-notices.mjs` now sees them. `ARCHIVE_COMPONENTS` rules can
key on the `- Asset:` line of the vendor README, because both loader assets
unpack to the same filename; a `vendor/ultimate-asi-loader/` whose README
resolves to no rule fails instead of passing on the loader's own MIT text. For
MPL-2.0 components the gate also requires the notices to name the copy of the
source we hold (the itsloopyo/MemoryModule fork), which is the section 3.2
obligation a reproduced text does not discharge. And it now finds the MinHook a mod links out of
`cameraunlock-core/vendor/minhook` when `CAMERAUNLOCK_BUILD_HOOKS` is on,
which the scan of the mod's own dependency directories never reached.

Consuming repos: every mod vendoring Ultimate ASI Loader needs sections for
miniz and injector in `THIRD-PARTY-NOTICES.md`, plus MemoryModule (with the
source offer) and d3d8to9 where the vendored asset is `Ultimate-ASI-Loader.zip`.
The gate reports which are missing.

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
