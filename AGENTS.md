<!-- managed by ticketing app - edit via Agents in the sidebar; changes here are overwritten on the next sync -->

<!-- agent: Code quality minimums -->
## Code quality minimums

These apply to every line of code in every repo. They override convenience.

### Fail fast, don't fail silent

If something fails, let it throw. No swallowed exceptions. No silent
fallbacks that mask the underlying problem. No retry loops that paper
over a broken contract. The error message is the diagnostic; if you
catch and rewrite it, you've thrown the diagnostic away.

The narrow exception: validate at system boundaries (user input,
external APIs, file system). Everything inside the boundary trusts the
contract.

### No fallbacks for impossible cases

Don't add error handling, validation, or fallbacks for scenarios that
can't happen. Trust internal code and framework guarantees. A `Result`
that's always `Ok` doesn't need to be a `Result`. A null check on a
value that's constructed three lines earlier is noise.

### No over-engineering

Don't add features, refactor, or introduce abstractions beyond what the
task requires. A bug fix doesn't need surrounding cleanup. A one-shot
operation doesn't need a helper function. Don't design for hypothetical
future requirements. Three similar lines is better than a premature
abstraction.

No half-finished implementations either. If you can't complete it, say
so and stop, don't leave a stub that compiles but lies.

### No decorative comments

Default to writing zero comments. Only add a comment when the WHY is
non-obvious: a hidden constraint, a subtle invariant, a workaround for
a specific bug, behavior that would surprise a reader. If removing the
comment wouldn't confuse a future reader, don't write it.

Don't explain WHAT the code does (well-named identifiers do that).
Don't reference the current task, fix, or callers ("used by X", "added
for the Y flow", "fixes #123"). Those belong in the PR description and
rot as the codebase evolves.

### No backwards-compat hacks for unused code

If something is unused, delete it completely. Don't rename to `_unused`,
don't add `// removed` comments, don't re-export removed types as
aliases. Backwards compatibility is for shipped public APIs (see the
Libraries category rule); internal scaffolding gets cut clean.


<!-- agent: Library API stability -->
## Library API stability

A library is a contract with its consumers. The two libraries here are
consumed by different consumer sets, but the same discipline applies:

- **cameraunlock-core** is consumed by ~19 head-tracking mod repos and
  by lopari.
- **quickfeed** is consumed by headcam-ios as a submodule, and its wire
  protocol is consumed by obs-quickfeed-plugin.

### Public API is everything an external consumer can reference

Public API = exported types, functions, traits, classes, enums, wire
protocol bytes, schema fields, file paths in shipped artifacts,
environment variable names, exit codes. If a downstream repo can write
code or scripts against it, it's API.

### Renaming is a breaking change, deprecate before remove

Don't rename a public type or function and "fix the callers". The
callers are in repos you may not control and they break silently on
next pull. The disciplined path:

1. Add the new name as the canonical thing.
2. Keep the old name as a thin re-export or wrapper, marked deprecated
   (or commented as such if the language doesn't support deprecation
   attributes).
3. Wait at least one release cycle for consumers to migrate.
4. Remove the old name in a major version bump.

### Adding is safe, removing or changing is breaking

You can add a new method, a new field, a new enum case, a new packet
type, a new games.json key. None of those break consumers (assuming
non-exhaustive matching).

You CAN'T:
- Remove an existing thing (consumers compile-error or runtime-fail).
- Change the type of an existing thing.
- Renumber an enum case (the wire protocol implodes).
- Tighten a precondition (consumers that were already calling correctly
  now hit the new check).
- Loosen a postcondition (consumers that were relying on the old
  guarantee now miss it).

### Wire formats and schemas are the strictest API

Source-level API breaks are caught by compile errors in consumer repos.
Wire-format breaks (UDP packet layout, JSON schema, state file shape)
are caught at runtime, often only on a user's machine, often only on
some users' machines. Treat them like a public API constant: never
renumber, never reorder, never remove. Add new packet types or new
fields with safe defaults.

### Test the API surface, not just internals

A library's tests should exercise the public API as a consumer would.
If the public API has 20 methods and your test file imports
`internal::*`, the public API isn't actually tested. The internals
are.

### Versioning matters because consumers pin

Both libraries here use SemVer. Major bumps are explicit "go look at
the migration notes". Don't sneak a breaking change into a minor or
patch bump just because "no one's using that yet" - someone is, and
they pinned to your minor version trusting you wouldn't.


<!-- agent: cameraunlock-core specifics -->
## cameraunlock-core specifics

### You're inside the shared core, not a game mod

This repo is the submodule that ~19 game head-tracking mods consume. The
head-tracking doctrine (data flow, view-matrix vs transform-save, aim
decoupling, reticle projection, framework dispatch, install.cmd contract,
vendoring) lives in the Mods:Head Tracking category rule and is the
contract we provide *to* those mods. Reference here, not duplicated.

Layout:
- `csharp/` - .NET solution (5 projects, see Multi-targeting below)
- `cpp/` - CMake static library `cameraunlock`
- `powershell/` - 5 reusable .psm1 modules consumed by every mod's release pipeline
- `scripts/templates/` - install.cmd / install-*.cmd / uninstall.cmd templates copied verbatim into each mod
- `data/games.json` - game-id to detection metadata, single source of truth
- `.github/workflows/release-bepinex-mod.yml` - reusable workflow called via `workflow_call` from mod repos

### C# multi-targeting is load-bearing, do not narrow it

`CameraUnlock.Core` targets `net35;net40;net472;net48;netstandard2.0`.
Per-TFM constraints:

- net35, net40, net472: C# 7.3 only, no nullable reference types, no
  `is null` patterns, no records, no init-only setters.
- net48, netstandard2.0: C# 9.0 with nullable enabled.
- Conditional symbols: NET35, NET40, NET472, NET48, NETSTANDARD2_0,
  UNITY_MONO (net472), NULLABLE_ENABLED.

`CameraUnlock.Core.Unity` is net35;net472;net48 (no netstandard, Unity
Mono will not load it). `CameraUnlock.Core.Unity.BepInEx` is
net472;net48. `CameraUnlock.Core.Unity.Harmony` is net35;net472;net48.
`CameraUnlock.Core.Tests` targets only net6.0.

A passing test on net6.0 does NOT prove a feature works on Unity Mono.
Build the full solution to catch C# 7.3 vs 9.0 syntax violations on
older TFMs.

### Specific public API surfaces that consumers depend on

- `CameraUnlock.Core`: `TrackingPose`, `Vec3`, `Quat4`, `SensitivitySettings`,
  `DeadzoneSettings`, `OpenTrackReceiver`, `TrackingProcessor`,
  `PoseInterpolator`, `PositionProcessor`, `PositionInterpolator`,
  `AimDecoupler`, `ScreenOffsetCalculator`, `SmoothingUtils.BaselineSmoothing`
- `CameraUnlock.Core.Unity`: `StaticHeadTrackingCore`, `SelfHealingModBase<T>`,
  `ViewMatrixModifier`, `AimDecouplingState.Instance`, `BaseRotationTracker`,
  `TrackingLossHandler`, `PerFrameCache`

Defaults that mods rely on staying constant: `BaselineSmoothing = 0.15f`,
`IntervalBlend = 0.3f`, `MaxExtrapolationFraction = 0.5f`,
`LimitZ = 0.40m`, `LimitZBack = 0.10m`, `LimitX = 0.30m`, `LimitY = 0.20m`.
Changing a default is a breaking change. Add a new field instead.

### scripts/templates/ is the install-script source of truth

Every mod's `scripts/install.cmd` and `scripts/uninstall.cmd` are
verbatim copies of the matching template here. Edits are confined to
the CONFIG BLOCK. The arg parser block (`:parse_args` with `/y`, `-y`,
`--yes`, `/force`) is canonical and identical across every mod.

If you change anything outside the CONFIG BLOCK:
1. Update the template here.
2. Re-sync every dependent mod repo's install.cmd / uninstall.cmd.
3. Run `unix2dos` on every regenerated `.cmd` (`.cmd` files must be
   CRLF or they silently fail on Windows).
4. Test against at least one mod-repo install on a clean game install.

Lopari drives `install.cmd "<path>" /y` programmatically. Exit codes
are contract: 0=success, 1=user-fixable, 2=unknown/malformed argument.
Breaking the arg parser is a launcher-breaking bug.

### powershell/ modules are public API

Every mod's `release.ps1` and `update-deps.ps1` import these:

- `GamePathDetection.psm1`: `Get-GamePath`, `Get-BepInExPluginsPath`, `Get-GameDataFolder`
- `ModDeployment.psm1`: `Copy-ModFiles`, `Backup-GameFiles`, `Verify-Deployment`
- `ModLoaderSetup.psm1`: `Refresh-VendoredLoader`, `Invoke-FetchLatestLoader`
- `AssemblyPatching.psm1`: `Patch-Assembly`, `Get-ManagedPath`
- `ReleaseWorkflow.psm1`: `Get-CsprojVersion`, changelog generation

`Refresh-VendoredLoader` enforces version-prefix pinning (e.g. BepInEx
5.4.x only). Bumping a major upstream version is a deliberate per-mod
change. Don't add positional parameters to existing functions; add
named parameters with safe defaults.

### data/games.json is schema v1

Required keys per game: `display_name`, `env_var`, `steam_app_id`,
`steam_folder`, `executable_relpath`, `data_folder` (optional, Cecil-
patched mods only). Adding a key is safe. Renaming or removing a key
silently breaks detection in mods that have not been re-synced. If
schema needs to evolve, bump `schema_version` and migrate all consumers
in the same change.

### Reusable CI workflow

`.github/workflows/release-bepinex-mod.yml` is called via `workflow_call`
from mod repos. Inputs (csproj path, mod name, output dlls) are the
contract. Validates that git tag version matches csproj version, builds
Release, produces installer ZIP. Don't widen inputs without bumping every
caller. Pin JS action majors (`actions/checkout@v6`,
`actions/upload-artifact@v7`, `microsoft/setup-msbuild@v3`) to keep CI
off Node 20's deprecation path.

### install.cmd never reaches out to the network

Vendored is the install-time source of truth. `pixi run update-deps` is
the only path that touches the network, runs manually by the dev,
produces a commit. CI never refreshes; it consumes what is committed
under `vendor/<loader-slug>/`. If you change vendoring behavior here,
every mod's `update-deps.ps1` inherits the change on next sync.
