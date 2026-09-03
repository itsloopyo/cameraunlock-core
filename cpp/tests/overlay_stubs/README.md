# Overlay compile stubs

The three overlay headers (`dx9_overlay.h`, `dx11_overlay.h`, `dx12_overlay.h`)
and the two aim-marker bindings on top of them are header-only, and a consuming
mod instantiates them in one TU by defining
`CAMERAUNLOCK_DX{9,11,12}_OVERLAY_IMPLEMENTATION`. Their one real dependency,
MinHook, is vendored per mod rather than here, so nothing in this repo ever
compiled the bodies. Everything outside the `#ifdef` was the only part any build
had ever seen.

`cameraunlock_overlay_compile` expands each implementation block so it is
typechecked.

## What this catches, and what it does not

It catches syntax errors, type errors, and signature drift against the stub
surface. That is all a compiler can do here.

It would **not** have caught the two worst defects these headers have shipped:
`MH_DisableHook(nullptr)` (which disables every MinHook hook in the process,
other mods' included) is a well-typed call, and a D3D12 fence race is well-typed
code. Both are logic bugs. The value of this target is that a header
nobody compiles rots freely and cannot be refactored with any confidence - not
that it substitutes for review or for tests.

Known gaps in the stubs themselves:

- `MH_CreateHook` is stubbed as a **template accepting any callable**, because
  the real signature takes `void*` and MSVC permits function-pointer-to-`void*`
  as an extension while gcc does not. The cost is that a `__stdcall`/`__cdecl`
  mismatch or a wrong detour signature - a genuine, compiler-catchable class of
  bug in exactly this code - compiles clean here.
- The target is guarded on `if(WIN32)` and this repo has no C++ CI, so it runs
  only when a developer runs `pixi run build-cpp` on Windows.

## About the stubs

They are:

- **Not** a functional test. Every stub returns success and does nothing. The
  target links nothing and runs nothing; it is `add_library(... OBJECT)` on
  purpose.
- **Not** a substitute for the real headers. Only the surface the overlay
  headers touch is declared, so adding a new MinHook call means adding it here
  too. That failure is the point - it fails at build time in this
  repo rather than in a mod six weeks later.

The D3D headers themselves are real, from the Windows SDK.
