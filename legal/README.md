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
   remember. For a submodule, `git submodule status` is the authority.

## Files

| File | Component | Licence |
|------|-----------|---------|
| `bepinex.txt` | BepInEx | LGPL-2.1 |
| `cameraunlock-core.txt` | cameraunlock-core | MIT |
| `cyber-engine-tweaks.txt` | Cyber Engine Tweaks | MIT |
| `dear-imgui.txt` | Dear ImGui | MIT |
| `fabric-loader.txt` | Fabric Loader | Apache-2.0 |
| `glm.txt` | OpenGL Mathematics | MIT / Happy Bunny |
| `harmonyx.txt` | HarmonyX | MIT |
| `inih.txt` | inih | BSD-3-Clause |
| `kiero.txt` | Kiero | MIT |
| `melonloader.txt` | MelonLoader | Apache-2.0 |
| `minhook.txt` | MinHook, incl. HDE32/HDE64 | BSD-2-Clause |
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

`bepinex.txt` and `unity-doorstop.txt` are both LGPL-2.1 and their operative
terms are byte-identical; they differ only in what each upstream appended after
"END OF TERMS AND CONDITIONS" (BepInEx a project NOTICE, Doorstop the standard
"How to Apply These Terms" appendix). A notices file may reproduce the licence
body once and name both components against it.

OpenTrack is deliberately absent. The mods implement its UDP wire format and
link none of its code, so its ISC licence triggers no notice obligation. It is
credited in each mod's notices as the origin of the protocol.
