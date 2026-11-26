# Vendored MinHook - upstream baseline and local changes

This directory is a vendored copy of MinHook, not a pristine upstream release.
It is recorded here so the attribution in each consuming mod's
`THIRD-PARTY-NOTICES.md` can be verified against upstream rather than taken on
trust.

Every mod that builds with `CAMERAUNLOCK_BUILD_HOOKS=ON` links this copy, so a
change here reaches all of them at once. That is the point of the directory
existing - before it, the fleet provisioned MinHook five incompatible ways
across three versions - but it means a change made for one game ships in every
game. Only put something here that is right for all of them.

## Baseline

- Upstream: https://github.com/TsudaKageyu/minhook
- Base: `v1.3.4`, the tagged release
- Every file here is byte-identical to that tag except the one listed below.
  `LICENSE.txt` and `AUTHORS.txt` are byte-identical to upstream, UTF-8 BOM
  included.

Upstream's own `CMakeLists.txt` at `v1.3.4` still declares
`MINHOOK_PATCH_VERSION 3`, so `MINHOOK_VERSION` computes to `1.3.3` on a v1.3.4
tree. That is upstream's, not ours, and it is left as it is: correcting it would
put a divergence in the tree for a constant nothing here reads.

## Local changes

### `src/hook.c` - use the process heap

`MH_Initialize` calls `GetProcessHeap()` instead of `HeapCreate(0, 0, 0)`, and
`MH_Uninitialize` no longer calls `HeapDestroy`. Allocations still pair with
`HeapFree`, so nothing leaks; the library just does not stand up a private heap
inside the game process.

This arrived from two mods that had each vendored their own MinHook and each
made this same edit independently - `dishonored-headtracking` and
`fallout-4-headtracking` - which is what settled it as the shape every consumer
should get rather than a per-game preference. Note what it is and is not: a
precaution against a private heap in a process whose allocator the mod does not
own, not a fix for a diagnosed crash. Nobody has recorded a failure that
`HeapCreate` caused. If a consumer ever needs the private heap back, that is a
reason to make the choice configurable, not to fork the file again.

## Do not edit the licence files

`LICENSE.txt` carries the BSD-2-Clause terms for both Tsuda Kageyu (MinHook)
and Vyacheslav Patkov (Hacker Disassembler Engine 32/64), and both must travel
with every source and binary redistribution. `AUTHORS.txt` is upstream's own
credit list. Neither is ours to change.

BSD-2-Clause permits the modification above and does not require it to be
marked, so a consumer's notices are not wrong for omitting it. They are better
for naming it, and the wording to use is that this is v1.3.4 with the local
change recorded in `cameraunlock-core/vendor/minhook/LOCAL-CHANGES.md`.

## Verifying

```bash
git clone https://github.com/TsudaKageyu/minhook /tmp/minhook
git -C /tmp/minhook checkout v1.3.4
diff -r --strip-trailing-cr /tmp/minhook/src vendor/minhook/src
diff -r --strip-trailing-cr /tmp/minhook/include vendor/minhook/include
diff --strip-trailing-cr /tmp/minhook/LICENSE.txt vendor/minhook/LICENSE.txt
diff --strip-trailing-cr /tmp/minhook/AUTHORS.txt vendor/minhook/AUTHORS.txt
```

Only `src/hook.c` should report a difference, and only in the two hunks
described above.
