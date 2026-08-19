# CameraUnlock Core

A cross-platform head tracking library for games. Provides complete tracking data processing, aim decoupling, and UI compensation with implementations in both C# (Unity/.NET) and C++ (native). Consumed as a git submodule by game head-tracking mod repos.

## Features

- **OpenTrack Protocol Support** - UDP receiver for OpenTrack's 48-byte packet format
- **Full Processing Pipeline** - Offset, deadzone, smoothing, and sensitivity (rotation and position)
- **Aim Decoupling** - Separates aim direction from camera rotation for natural FPS gameplay
- **UI Compensation** - Reticle positioning that accounts for head tracking offset
- **Thread-Safe Design** - Background UDP receiver safe to poll from the game thread
- **Broad Compatibility** - Supports .NET 3.5 through .NET Standard 2.0, Unity 2018+, IL2CPP (source-shared)
- **Release Tooling** - PowerShell modules, install script templates, and a reusable CI workflow shared by every mod repo

## Directory Structure

```
cameraunlock-core/
├── csharp/
│   ├── src/
│   │   ├── CameraUnlock.Core/                    # Core library (multi-target)
│   │   ├── CameraUnlock.Core.Unity/              # Unity extensions
│   │   ├── CameraUnlock.Core.Unity.BepInEx/      # BepInEx integration
│   │   ├── CameraUnlock.Core.Unity.Harmony/      # Harmony IL patching utilities
│   │   ├── CameraUnlock.Core.Tests/              # xUnit tests (net8.0)
│   │   └── CameraUnlock.Core.Unity.Tests/        # xUnit tests for the Unity half (net8.0)
│   ├── il2cpp/                                   # Source-shared helpers for IL2CPP mods (see its README.md)
│   └── CameraUnlock.Core.sln
├── cpp/
│   ├── include/cameraunlock/                     # Public headers
│   ├── src/                                      # Implementation
│   ├── tests/                                    # C++ tests
│   └── CMakeLists.txt
├── powershell/                                   # 7 reusable .psm1 modules for mod release pipelines
├── scripts/
│   ├── templates/                                # install.cmd / uninstall.cmd templates copied into mod repos
│   └── *.ps1, install-body-*.cmd                 # Game detection, packaging, release-note scripts
├── data/
│   └── games.json                                # Game detection metadata (single source of truth)
├── .github/workflows/
│   └── release-bepinex-mod.yml                   # Reusable release workflow (workflow_call)
└── pixi.toml                                     # Build/test task definitions
```

## Core Components

### Data Structures

| Type | Description |
|------|-------------|
| `TrackingPose` | Immutable struct: Yaw, Pitch, Roll + timestamp |
| `PositionData` | Immutable struct: X, Y, Z head position + timestamp |
| `Vec3` | 3D vector |
| `Quat4` | Quaternion |
| `SensitivitySettings` | Per-axis multipliers and invert flags |
| `DeadzoneSettings` | Per-axis deadzone values |
| `PositionSettings` | Position scaling and travel limits |

### Protocol

**OpenTrackReceiver** - Thread-safe UDP receiver
```csharp
var receiver = new OpenTrackReceiver();
receiver.Start(port: 4242);

// In game loop:
var pose = receiver.GetLatestPose();
if (receiver.IsDataFresh())
{
    // Apply tracking
}
```

### Processing

**TrackingProcessor** - Full processing pipeline
```csharp
var processor = new TrackingProcessor();
processor.Sensitivity = new SensitivitySettings(yaw: 1.0f, pitch: 0.8f, roll: 0.5f);
processor.LocalSmoothing = 0.0f;    // same-machine (loopback) tracker
processor.RemoteSmoothing = 0.15f;  // remote device on the network
processor.IsRemoteConnection = receiver.IsRemoteConnection;  // fed each update

// In game loop:
var processed = processor.Process(rawPose, deltaTime: Time.deltaTime);
```

**PositionProcessor** / **PositionInterpolator** provide the same pipeline for
positional tracking, and **PoseInterpolator** interpolates between UDP packets
for frame-rate independent smoothness.

### Aim Decoupling

**AimDecoupler** - Computes aim direction independent of camera rotation
```csharp
// Camera rotates with head tracking, but aim stays stable
var aimDirection = AimDecoupler.ComputeAimDirectionLocal(trackingRotation);
```

### Higher-Level Components

| Type | Description |
|------|-------------|
| `HeadTrackingSession` | Per-frame pipeline (receiver → interpolators → processors) with tracking-loss hold and mode cycling |
| `MultiPlayerTrackingManager` | One `HeadTrackingSession` per local player, each with its own UDP port (split-screen multiplayer) |
| `StaticHeadTrackingCore` | Static receiver + processor core that survives Unity lifecycle events |
| `CenterOffsetManager` | Recentering state |
| `AxisTransform` (`MappingConfig`, `MappingPreset`, `SensitivityCurve`) | Axis remapping and non-linear sensitivity curves |
| `Config.Profiles` (`ProfileManager`, `ProfileSerializer`) | Named configuration profiles |
| `HotkeyHandler` | Framework-agnostic hotkey dispatch |
| `PerformanceMonitor` | Per-frame processing statistics |

## Processing Pipeline

```
Raw UDP Packet (48 bytes)
    │
    ▼
OpenTrackReceiver (thread-safe parsing)
    │
    ▼
TrackingProcessor Pipeline:
    1. Convert to quaternion, subtract center offset (recentering)
    2. Apply per-axis deadzone (ignore small movements)
    3. Apply per-axis Euler smoothing (exponential moving average;
       deliberately not quaternion SLERP, which causes phantom roll)
    4. Apply per-axis sensitivity
    │
    ▼
Processed TrackingPose → Game patches
```

Mods that cannot link this library and reimplement the pipeline instead (CET Lua,
UE4SS Lua, Rust ASI, Python) do not receive fixes made here. The invariants such
a port has to reproduce, each with a check that fails on the wrong
implementation, are in [docs/porting-the-pipeline.md](docs/porting-the-pipeline.md).
An audit of four independent ports found three had made the same angle-handling
mistake, which is why that document exists.

## Unity Integration

### AimDecouplingState

Singleton managing aim decoupling state:
```csharp
// Update with current tracking
AimDecouplingState.Instance.UpdateTracking(trackingQuaternion, trackingEuler);

// Get decoupled aim direction for a camera
var aimDir = AimDecouplingState.Instance.GetAimDirection(camera);

// Get screen offset for UI positioning
var offset = AimDecouplingState.Instance.GetScreenOffset(camera);
```

### BaseRotationTracker

Separates game rotation from head tracking:
```csharp
var tracker = new BaseRotationTracker();

// In camera patch:
tracker.Update(cameraTransform, gameWantedRotation, headTrackingRotation);
var baseRotation = tracker.BaseRotation;       // Game's intended rotation (world space)
var combined = tracker.CombinedRotation;       // What the camera actually shows
```

### SelfHealingModBase

MonoBehaviour that survives scene changes:
```csharp
public class MyMod : SelfHealingModBase
{
    protected override void Initialize()
    {
        // Setup code
    }
}

// Create it once:
SelfHealingModBase.CreateMod<MyMod>();
```

### Other Unity Components

| Area | Types |
|------|-------|
| Tracking | `ViewMatrixModifier`, `ViewMatrixTrackingController`, `TrackingLossHandler`, `CameraLifecycleManager`, `CameraRotationComposer`, `PositionApplicator` |
| Rendering / UI | `IMGUIReticle`, `RenderPipelineHelper`, `NotificationUI`, `StatusIndicatorUI`, `CanvasCompensation`, `UIElementOffsetController` |
| Input | `UnityHotkeyHandler`, `ChordHotkeys` |
| Utilities | `PerFrameCache`, `CrosshairUtility`, `GameUIFinder`, `FramerateHelper` |

### IL2CPP Games

IL2CPP mods (BepInEx 6 + Il2CppInterop) cannot reference the `CameraUnlock.Core.Unity*`
DLLs. Unity-coupled helpers are shared as **source** instead via
`csharp/il2cpp/CameraUnlock.Core.Unity.Il2Cpp.props` - see
[csharp/il2cpp/README.md](csharp/il2cpp/README.md).

## Configuration

Implement `IHeadTrackingConfig`:
```csharp
public interface IHeadTrackingConfig
{
    int UdpPort { get; }
    bool EnableOnStartup { get; }
    SensitivitySettings Sensitivity { get; }
    string RecenterKeyName { get; }
    string ToggleKeyName { get; }
    bool AimDecouplingEnabled { get; }
    bool ShowDecoupledReticle { get; }
    float[] ReticleColorRgba { get; }
    float LocalSmoothing { get; }
    float RemoteSmoothing { get; }
}
```

BepInEx integration available via `CameraUnlock.Core.Unity.BepInEx`.

## C++ Library

A CMake static library `cameraunlock` (C++17, CMake 3.20+) mirroring the C# core
plus native-only modules for in-process game modding.

| Module | Contents |
|--------|----------|
| `data/`, `math/`, `processing/`, `protocol/` | Same pipeline as C#: tracking pose, vec/quat math, deadzone/smoothing, OpenTrack UDP receiver (threaded and polling variants) |
| `config/` | INI file reader |
| `input/` | Hotkey polling, chord hotkeys, deferred actions |
| `logging/`, `diagnostics/` | File logging, crash handler |
| `memory/` | Pattern scanner, PE fingerprinting, RTTI/vtable inspection |
| `rendering/` | Crosshair/aim projection, DX11/DX12 overlays, GUI marker compensation |
| `time/` | QPC clock, frame clock |
| `tracking/` | `HeadTrackingSession` - full session orchestration |
| `hooks/` | Function hooking (optional module) |
| `discovery/` | Camera address discovery via float classification (optional module) |
| `unreal/` | Unreal Engine runtime helpers: UE5 LWC math, GUObjectArray/FName reflection (optional module) |
| `reframework/` | RE Engine (REFramework) utilities: camera chain, TDB inspection, game state probing (optional module) |

Optional modules (all `OFF` by default):

| CMake option | Target | Requires |
|--------------|--------|----------|
| `CAMERAUNLOCK_BUILD_HOOKS` | `cameraunlock_hooks` | MinHook target provided by consumer |
| `CAMERAUNLOCK_BUILD_DISCOVERY` | `cameraunlock_discovery` | `CAMERAUNLOCK_BUILD_HOOKS=ON` |
| `CAMERAUNLOCK_BUILD_UNREAL` | `cameraunlock_unreal` | Windows |
| `CAMERAUNLOCK_BUILD_REFRAMEWORK` | `cameraunlock_reframework` | REFramework headers from consumer (C++20) |

Tests build by default (`CAMERAUNLOCK_BUILD_TESTS=ON`) into `cameraunlock_tests`.

### Forcing a known camera vfunc

`CameraDiscovery` normally picks the per-frame camera update by call-count heuristic:
during the probe window it ranks each hooked vfunc by how close its call count sits to a
per-frame rate. That is non-deterministic when the real camera update has not begun firing
inside the window (a probe that lands on a load-in), and it can latch onto a high-frequency
getter instead, so the mod "never starts" on that launch.

When the correct index is already known from reversing, pin it:

```cpp
cameraunlock::discovery::DiscoveryConfig config;
config.candidate_names = {"CCustomCamera", "CCamera"};
config.forced_vfunc_index = 2;  // default -1 = auto-discover, unchanged behaviour
```

The index applies to the first (most-specific) candidate only. Probing still runs, so the
instance pointer is captured as usual, and the slot is committed only once it is being
called at a per-frame *rate* rather than having merely accumulated calls. That wait is not
bounded by `probe_frames` (a long load-in must not hand the choice back to the heuristic)
and logs a line every ~5s while it waits. An index that does not exist on the class actually
found is reported as an error once and then ignored, leaving auto-discovery to run; it is
not reported as discovery failure, which consumers treat as transient and retry on.

The decision itself lives in `discovery/probe_selection.h` as a free function,
`SelectProbeSlot`, with no MinHook or live-process dependency, so both paths are unit
tested. It is built into the base `cameraunlock` library, not the optional discovery module.

## Building

### With pixi (recommended)

```bash
pixi run build          # dotnet build csharp -c Release
pixi run test           # dotnet test csharp
pixi run check          # debug build + quick tests
pixi run pack           # dotnet pack to dist/
pixi run test-powershell # vendoring soak tests (Windows only)
```

### C# (.NET)

```bash
cd csharp
dotnet build CameraUnlock.Core.sln
```

The Unity-coupled projects compile against Unity/BepInEx reference DLLs they do
not vendor. Consuming mod repos provide them by setting `UnityEnginePath` /
`BepInExPath` (usually in a `Directory.Build.props`). When building this repo
standalone, the projects fall back to sibling checkouts of
`gone-home-headtracking` (pre-2017.3 Unity DLLs, needed for net35) and
`valheim-headtracking` (modern Unity + BepInEx); override with
`-p:UnityEnginePath=...` / `-p:BepInExPath=...` if those aren't present.

### C++ (CMake)

```bash
cd cpp
cmake -B build
cmake --build build
ctest --test-dir build
```

## Target Framework Compatibility

| Project | Targets | Notes |
|---------|---------|-------|
| CameraUnlock.Core | net35, net40, net472, net48, netstandard2.0 | Conditional compilation for framework differences |
| CameraUnlock.Core.Unity | net35, net472, net48 | Unity 2018+ Mono compatibility |
| CameraUnlock.Core.Unity.BepInEx | net472, net48 | BepInEx requires .NET 4.x |
| CameraUnlock.Core.Unity.Harmony | net35, net472, net48 | Harmony IL patching (via Lib.Harmony 2.2.2) |
| CameraUnlock.Core.Tests | net8.0 | A passing test here does not prove Unity Mono compatibility - build the full solution |
| CameraUnlock.Core.Unity.Tests | net8.0 | Compiles the Unity classes under test from source against `UnityStubs.cs`; the shipped UnityEngine assemblies cannot be loaded in a test host |

### Framework Notes

- **net35** - Old Unity games (e.g., Return of the Obra Dinn)
- **net472** - Unity Mono with C# 7.3 language features
- **net48** - Modern C# 9.0 features
- **netstandard2.0** - .NET Core compatible

## Math Utilities

| Component | Functions |
|-----------|-----------|
| `MathUtils` | Clamp, Clamp01, Lerp |
| `QuaternionUtils` | FromYawPitchRoll, Multiply, Slerp, Normalize, Inverse |
| `AngleUtils` | NormalizeAngle, ShortestAngleDelta, ToRadians, ToDegrees |
| `SmoothingUtils` | GetEffectiveSmoothing (selects local vs remote by connection), Smooth (exponential MA) |
| `DeadzoneUtils` | Apply (axial deadzone with smooth activation) |

## PowerShell Modules

Located in `powershell/`, imported by every mod repo's `release.ps1` / `update-deps.ps1`:

| Module | Purpose |
|--------|---------|
| `AssemblyPatching.psm1` | Mono.Cecil IL patching (`Invoke-HeadTrackingPatch`, `New-ScreenCenterPatcher`) |
| `DevDeploy.psm1` | Local dev-deploy pipelines per loader (`Invoke-DevDeployBepInEx`, `-Cecil`, `-MelonLoader`, `-ASILoader`, `-REFramework`, `-Shim`) |
| `GamePathDetection.psm1` | Game install detection across Steam/GOG/Epic/Ubisoft/Xbox/registry (`Find-GamePath`, `Get-GameConfig`) |
| `ModDeployment.psm1` | Mod file deployment, backups, verification (`Copy-ModFiles`, `Test-ModDeployment`) |
| `ModLoaderSetup.psm1` | BepInEx/MelonLoader/UE4SS install and vendored loader update (`Install-BepInEx`, `Update-VendoredLoader`) |
| `NightlyRelease.psm1` | Rolling GitHub pre-release publishing for dev builds (`Publish-NightlyBuild`) |
| `ReleaseWorkflow.psm1` | Release automation: versioning, changelog, tagging, submodule sync (`Get-CsprojVersion`, `New-ChangelogFromCommits`, `Update-CameraUnlockCoreToRemoteTip`) |

## Install Scripts & Templates

`scripts/templates/` is the source of truth for every mod repo's `install.cmd` /
`uninstall.cmd` (plus per-loader variants: ASI, Cecil, MelonLoader, REFramework,
shim, UE4SS). Templates are copied verbatim into mod repos; only the CONFIG BLOCK
differs per mod. Supporting scripts:

- `find-game.ps1` - bridges `install.cmd` to `GamePathDetection.psm1`
- `package-bepinex-mod.ps1` - builds the installer ZIP for releases
- `check-loader-arch.ps1` - detects x86/x64 loader mismatches
- `generate-release-notes.ps1` - changelog from git history

Install scripts never reach the network: vendored loaders committed under each
mod's `vendor/` directory are the install-time source of truth. The only path
that does reach the network is `pixi run update-deps`, and it will not take a
GitHub release that has been public for less than 14 days
(`Invoke-FetchLatestLoader -MinimumAgeDays`), so a compromised upstream publish
has a window to be caught before it can be vendored.

## Game Detection Metadata

`data/games.json` (schema v1) maps game ids to detection metadata: `display_name`,
`env_var`, `executable_relpath`, Steam/GOG/Epic/Ubisoft/Xbox/registry lookup keys,
and an optional `data_folder` for Cecil-patched mods. Consumed by
`GamePathDetection.psm1` and `find-game.ps1` in every mod repo.

## Reusable CI Workflow

`.github/workflows/release-bepinex-mod.yml` is called via `workflow_call` from mod
repos. It validates that the git tag version matches the csproj version, packages
the installer ZIP, generates release notes, and publishes the GitHub release.

## Dependencies

### C# NuGet
- Lib.Harmony 2.2.2 (CameraUnlock.Core.Unity.Harmony only)
- xUnit 2.4.2 (testing only)
- Microsoft.NET.Test.Sdk 17.6.0 (testing only)

### C++
- Winsock2 (Windows UDP)
- MinHook (optional, for hooks/discovery modules)
- REFramework headers (optional, for reframework module)

### Runtime
- Unity assemblies provided by consuming projects (weak references)
- BepInEx provided by mod loader

## License

MIT License - see [LICENSE](LICENSE) for details.
