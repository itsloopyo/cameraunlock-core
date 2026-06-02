# CameraUnlock IL2CPP shared source

Unity-coupled helpers for IL2CPP (BepInEx 6 + Il2CppInterop) mods, shared as **source**
rather than as a compiled assembly.

## Why source, not a binary

IL2CPP mods compile against interop proxy assemblies generated from the game's IL2CPP
metadata (`libs/UnityEngine.CoreModule.dll` etc. produced by Il2CppInterop). Those
proxies are binary-incompatible with the real UnityEngine reference assemblies that
`CameraUnlock.Core.Unity` targets, so IL2CPP mods cannot reference the
`CameraUnlock.Core.Unity*` DLLs - only the framework-agnostic `CameraUnlock.Core`.

Sharing the Unity-coupled pieces as source lets each IL2CPP mod compile them against
its own game's proxies while keeping a single canonical implementation here.

## Usage

In the mod's `.csproj`, after the `CameraUnlock.Core` ProjectReference:

```xml
<PropertyGroup>
  <!-- optional: include the dev-only intro-video skipper -->
  <CameraUnlockFastBoot>true</CameraUnlockFastBoot>
</PropertyGroup>
<Import Project="..\..\cameraunlock-core\csharp\il2cpp\CameraUnlock.Core.Unity.Il2Cpp.props" />
```

## Contents

| File | Namespace | Purpose |
|------|-----------|---------|
| `SplitInjectionCameraTracker.cs` | `CameraUnlock.Core.Unity.Il2Cpp` | Multi-camera split injection for camera-relative pipelines (HDRP): rotation via worldToCameraMatrix, position via transform with local-space restore. |
| `FastBootBehaviour.cs` (opt-in) | `CameraUnlock.Core.Unity.Il2Cpp` | Dev tool: disables splash/intro VideoPlayers on every scene load. |
| `../src/CameraUnlock.Core.Unity/Extensions/ChordHotkeys.cs` | `CameraUnlock.Core.Unity.Extensions` | Ctrl+Shift+letter chord hotkeys (whitelisted Core.Unity file, proxy-compatible). |

## Constraints on files in this folder

- Only use Unity APIs known to exist in Il2CppInterop proxies (value-type math, Camera,
  Transform, Input, Time). Anything exotic must be verified in a real game build first.
- Files must be nullable-clean under `#nullable enable` and compile under C# 10 with
  `TreatWarningsAsErrors` (mods enable both).
- MonoBehaviours must declare the `(IntPtr ptr)` constructor and avoid Il2Cpp event
  subscriptions (not supported from injected types) - poll instead.
- Core.Unity files whitelisted in the props must additionally keep compiling in the
  `CameraUnlock.Core.Unity` assembly (C# 7.3, net35/net472/net48, real UnityEngine).
