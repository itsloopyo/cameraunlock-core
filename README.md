# CameraUnlock Core

A cross-platform head tracking library for games. Provides complete tracking data processing, aim decoupling, and UI compensation with implementations in both C# (Unity/.NET) and C++ (native).

## Features

- **OpenTrack Protocol Support** - UDP receiver for OpenTrack's 48-byte packet format
- **Full Processing Pipeline** - Offset, deadzone, smoothing, and sensitivity
- **Aim Decoupling** - Separates aim direction from camera rotation for natural FPS gameplay
- **UI Compensation** - Reticle positioning that accounts for head tracking offset
- **Thread-Safe Design** - Lock-free UDP receiver with atomic operations
- **Broad Compatibility** - Supports .NET 3.5 through .NET Standard 2.0, Unity 2018+

## Directory Structure

```
cameraunlock-core/
├── csharp/
│   ├── src/
│   │   ├── CameraUnlock.Core/                    # Core library (multi-target)
│   │   ├── CameraUnlock.Core.Unity/              # Unity extensions
│   │   ├── CameraUnlock.Core.Unity.BepInEx/      # BepInEx integration
│   │   ├── CameraUnlock.Core.Unity.Harmony/      # Harmony IL patching utilities
│   │   └── CameraUnlock.Core.Tests/              # xUnit tests
│   └── CameraUnlock.Core.sln
├── cpp/
│   ├── include/cameraunlock/                     # Public headers
│   ├── src/                                    # Implementation
│   ├── tests/                                  # C++ tests
│   └── CMakeLists.txt
└── powershell/                                 # Build & deployment automation
```

## Core Components

### Data Structures

| Type | Description |
|------|-------------|
| `TrackingPose` | Immutable struct: Yaw, Pitch, Roll + timestamp |
| `Vec3` | 3D vector |
| `Quat4` | Quaternion |
| `SensitivitySettings` | Per-axis multipliers and invert flags |
| `DeadzoneSettings` | Per-axis deadzone values |

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
processor.SmoothingFactor = 0.3f;

// In game loop:
var processed = processor.Process(rawPose);
```

### Aim Decoupling

**AimDecoupler** - Computes aim direction independent of camera rotation
```csharp
// Camera rotates with head tracking, but aim stays stable
var aimDirection = AimDecoupler.ComputeAimDirectionLocal(trackingRotation);
```

## Processing Pipeline

```
Raw UDP Packet (48 bytes)
    │
    ▼
OpenTrackReceiver (thread-safe parsing)
    │
    ▼
TrackingProcessor Pipeline:
    1. Subtract center offset (recentering)
    2. Apply deadzone (ignore small movements)
    3. Apply smoothing (SLERP interpolation)
    4. Apply sensitivity (per-axis multipliers)
    │
    ▼
Processed TrackingPose → Game patches
```

## Unity Integration

### AimDecouplingState

Singleton managing aim decoupling state:
```csharp
// Update with current tracking
AimDecouplingState.Instance.UpdateTracking(trackingRotation);

// Get decoupled aim direction
var aimDir = AimDecouplingState.Instance.GetAimDirection();

// Get screen offset for UI positioning
var offset = AimDecouplingState.Instance.GetScreenOffset();
```

### BaseRotationTracker

Separates game rotation from head tracking:
```csharp
var tracker = new BaseRotationTracker();

// In camera update:
tracker.Update(camera.rotation, trackingRotation);
var baseRotation = tracker.BaseRotation; // Game's intended rotation
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
}
```

BepInEx integration available via `CameraUnlock.Core.Unity.BepInEx`.

## Building

### C# (.NET)

```bash
cd csharp
dotnet build CameraUnlock.Core.sln
```

### C++ (CMake)

```bash
cd cpp
cmake -B build
cmake --build build
```

## Target Framework Compatibility

| Project | Targets | Notes |
|---------|---------|-------|
| CameraUnlock.Core | net35, net40, net472, net48, netstandard2.0 | Conditional compilation for framework differences |
| CameraUnlock.Core.Unity | net35, net472, net48 | Unity 2018+ Mono compatibility |
| CameraUnlock.Core.Unity.BepInEx | net472, net48 | BepInEx requires .NET 4.x |
| CameraUnlock.Core.Unity.Harmony | net35, net472, net48 | Harmony IL patching (via Lib.Harmony 2.2.2) |

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
| `SmoothingUtils` | GetEffectiveSmoothing (adjusts for latency), Smooth (exponential MA) |
| `DeadzoneUtils` | Apply (axial deadzone with smooth activation) |

## PowerShell Modules

Located in `powershell/`:

| Module | Purpose |
|--------|---------|
| `AssemblyPatching.psm1` | IL patching utilities (Mono.Cecil) |
| `GamePathDetection.psm1` | Game installation detection |
| `ModDeployment.psm1` | Mod deployment helpers |
| `ModLoaderSetup.psm1` | BepInEx/mod loader setup |
| `ReleaseWorkflow.psm1` | Release automation |

## Dependencies

### C# NuGet
- Lib.Harmony 2.2.2 (CameraUnlock.Core.Unity.Harmony only)
- xUnit 2.4.2 (testing only)
- Microsoft.NET.Test.Sdk 17.6.0 (testing only)

### C++
- Winsock2 (Windows UDP)
- MinHook (optional, for function hooking)

### Runtime
- Unity assemblies provided by consuming projects (weak references)
- BepInEx provided by mod loader

## License

MIT License - see [LICENSE](LICENSE) for details.
