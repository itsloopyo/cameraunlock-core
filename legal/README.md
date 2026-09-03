# Canonical third-party licence texts

Verbatim upstream licence text for every component the mods bundle, vendor or
statically link. This directory is the source of truth: when a mod writes its
`THIRD-PARTY-NOTICES.md`, the licence block is copied from here rather than
retyped, so a notice cannot drift from what upstream actually says.

## Why the full text, every time

MIT, BSD-2-Clause and BSD-3-Clause all require the copyright notice, the
permission or conditions text, and the disclaimer to accompany a binary
distribution. Naming the licence and quoting the copyright line satisfies none
of them. Apache-2.0 additionally requires the LICENSE and any NOTICE to travel
with the distribution. LGPL-2.1 requires attribution plus a route to the
source.

Every ZIP we publish is a binary distribution. That includes the Nexus ZIP,
which is easy to forget because it holds nothing but the payload subtree.

## Adding a component

1. Take the text from the upstream `LICENSE` verbatim. Strip a UTF-8 BOM if
   present; change nothing else.
2. Read the whole file before assuming one holder. `minhook.txt` carries a
   second, separate copyright for Hacker Disassembler Engine 32/64 (Vyacheslav
   Patkov) below Tsuda Kageyu's. Attributing only the headline project drops a
   rights holder.
3. Record the version the mod actually builds against, not the tag you
   remember. For a submodule, `git submodule status` is the authority. A mod
   that takes MinHook from `cameraunlock-core/vendor/minhook` builds against
   v1.3.4 (commit `c3fcafdc10146beb5919319d0683e44e3c30d537`); one that still
   supplies its own target builds against whatever that target pins.

## Files

| File | Component | Licence |
|------|-----------|---------|
| `bepinex.txt` | BepInEx | LGPL-2.1 |
| `cameraunlock-core.txt` | cameraunlock-core | MIT |
| `cyber-engine-tweaks.txt` | Cyber Engine Tweaks | MIT |
| `d3d8to9.txt` | d3d8to9 | BSD-2-Clause |
| `dear-imgui.txt` | Dear ImGui | MIT |
| `fabric-loader.txt` | Fabric Loader | Apache-2.0 |
| `glm.txt` | OpenGL Mathematics | MIT / Happy Bunny |
| `harmonyx.txt` | HarmonyX | MIT |
| `inih.txt` | inih | BSD-3-Clause |
| `injector.txt` | injector (ThirteenAG) | zlib |
| `kiero.txt` | Kiero | MIT |
| `melonloader.txt` | MelonLoader | Apache-2.0 |
| `memorymodule.txt` | MemoryModule | MPL-2.0 |
| `minhook.txt` | MinHook, incl. HDE32/HDE64 | BSD-2-Clause |
| `miniz.txt` | miniz | MIT |
| `mono-cecil.txt` | Mono.Cecil | MIT |
| `monomod.txt` | MonoMod | MIT |
| `red4ext.txt` | RED4ext | MIT |
| `reframework.txt` | REFramework | MIT |
| `tweakxl.txt` | TweakXL | MIT |
| `ue4ss.txt` | UE4SS | MIT |
| `ultimate-asi-loader.txt` | Ultimate ASI Loader | MIT |
| `unity-doorstop.txt` | UnityDoorstop | LGPL-2.1 |

A loader archive is not one component. The BepInEx `win_x64` zip we vendor
carries UnityDoorstop (`winhttp.dll`), HarmonyX (`0Harmony.dll`), Mono.Cecil
and MonoMod alongside BepInEx itself. Shipping that zip redistributes all of
them, so each needs its own entry here and its own block in the mod's notices.
Reading only the loader's headline licence drops four rights holders.

Ultimate ASI Loader is the same shape, with one more trap: its two release
assets unpack to the same `dinput8.dll` and are not the same binary. Per
`premake5.lua` at v9.7.2 through v9.7.4, `Ultimate-ASI-Loader_x64.zip` compiles
MinHook (via `external/injector/minhook`), injector's `FunctionHookMinHook.cpp`
and `external/miniz/miniz.c` into the DLL. `Ultimate-ASI-Loader.zip` (32-bit)
compiles those and also `external/MemoryModule/*.c` and
`external/d3d8to9/source/*.cpp`. The `- Asset:` line in the mod's vendored
README says which one it ships; the DLL's filename does not.

MemoryModule is MPL-2.0, and that licence asks for something a reproduced text
cannot give. Section 3.2 requires whoever distributes Covered Software in
Executable Form to tell recipients how to obtain its Source Code Form. That duty is
the distributor's, and pointing only at someone else's repository leaves it
resting on a copy they can delete. So a mod that ships the 32-bit loader names
our own fork, https://github.com/itsloopyo/MemoryModule, at the commit the
loader pins (`5f83e41c3a3e7c6e8284a5c1afa5a38790809461` at every tag above),
with the upstream repository and the loader's tree as further locations,
alongside the full licence text. `validate-notices.mjs` checks that the fork
is named. The fork must stay public for as long as any release ZIP carrying
the 32-bit loader can be downloaded.

`bepinex.txt` and `unity-doorstop.txt` are both LGPL-2.1 and their operative
terms are byte-identical; they differ only in what each upstream appended after
"END OF TERMS AND CONDITIONS" (BepInEx a project NOTICE, Doorstop the standard
"How to Apply These Terms" appendix). A notices file may reproduce the licence
body once and name both components against it.

OpenTrack is deliberately absent. The mods implement its UDP wire format and
link none of its code, so its ISC licence triggers no notice obligation. It is
credited in each mod's notices as the origin of the protocol.
