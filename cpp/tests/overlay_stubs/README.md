# Overlay compile stubs

The three overlay headers (`dx9_overlay.h`, `dx11_overlay.h`, `dx12_overlay.h`)
are header-only templates that a consuming mod instantiates in one TU by
defining `CAMERAUNLOCK_DX{9,11,12}_OVERLAY_IMPLEMENTATION`. Their real
dependencies (ImGui, kiero, MinHook) are vendored per mod, not here, so nothing
in this repo ever compiled the bodies. Everything outside the `#ifdef` was the
only part any build had ever seen.

That is not a theoretical gap. It is how `MH_DisableHook(nullptr)` (which
disables *every* hook in the process, including other mods') and the DX12 fence
race shipped: neither is reachable by any test, and neither is visible to a
compiler that never expands the block.

These stubs exist purely so `cameraunlock_overlay_compile` can expand each
implementation block and typecheck it. They are:

- **Not** a functional test. Every stub returns success and does nothing. The
  target links nothing and runs nothing; it is `add_library(... OBJECT)` on
  purpose.
- **Not** a substitute for the real headers. Only the surface the overlay
  headers touch is declared, so adding a new ImGui/MinHook/kiero call means
  adding it here too. That failure is the point - it fails at build time in this
  repo rather than in a mod six weeks later.

The D3D headers themselves are real, from the Windows SDK.
