# The canonical config format

Every head-tracking mod converted to it keeps its settings in one INI dialect, read and written
by one reader, one renderer and one editor per language, all in this library. This document is
the format and the contract around it: what the file looks like, what a mod binds, what the
config owner does at load, save and reload, what a player sees when an older file is imported,
the shared fixtures a port in another language runs, and the tooling that keeps the fleet on it.

The data behind the format is checked in and generated from, never restated by hand:

| File | What it holds |
|------|---------------|
| `data/config-schema.json` | Every config concept: section, key, type, default, range, the comment written above it, and whether the canonical format writes it |
| `data/keys.json` | Every key name a hotkey value can use, with its Windows virtual-key code and its Unity `KeyCode` value |
| `data/config-format.json` | Which repos convert, which carry a legacy import, where each config file is committed and installed, and the approved normalisations and changes |
| `data/fixtures/canonical-ini/` | Byte fixtures that the C++ suite, the C# suite and any port run unchanged |

The C++ API is in `cpp/include/cameraunlock/config/` and `cpp/include/cameraunlock/input/`, the
C# API in the `CameraUnlock.Core.Config` and `CameraUnlock.Core.Input` namespaces. The two
languages follow the same rules, with the same enum numbers, and are held to the same fixtures.

## The file

### Where it lives

Every converted mod keeps its settings in `CameraUnlock.ini`, in the folder its old config sat in:
`BepInEx\config\CameraUnlock.ini` under BepInEx, `reframework\plugins\CameraUnlock.ini` under
REFramework. A repo that published a pre-canonical build leaves its old file, the legacy file,
where it is: the mod imports it once while `CameraUnlock.ini` is absent and never writes, renames
or deletes it, so an older build still reads it after a rollback. A new repo puts
`CameraUnlock.ini` in the folder its loader loads the mod from.

`data/config-format.json` records every config file of every converting repo: `committed`, the
repo path of the rendered file; `installed`, each path the file takes relative to the game
folder, one per store layout, every one named `CameraUnlock.ini`; and `legacy_source`, the bare
name of the legacy file in the folder of each installed path, set exactly for a repo in `legacy`.
A game with several config files gives each its own entry and its own owner.

### What it looks like

This is the file the C++ and C# examples [below](#what-a-mod-binds) create at first launch, byte
for byte:

<!-- file: data/fixtures/canonical-ini/example/HeadTracking.ini -->
```ini
; Example Game head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=4242

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=true
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=true
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=true

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=true

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=End, Ctrl+Shift+Y
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=PageUp, Ctrl+Shift+G
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=PageDown, Ctrl+Shift+H

[Logging]
; true: write HeadTracking.log beside the game's executable.
WriteLog=false
```

### Layout

One renderer writes every file, so every file has the same shape:

1. A header of comment lines: `; <display name> head tracking settings.`, where the display name
   is the game's name as `data/games.json` spells it; a line saying comments go on their own line;
   and, when the file holds a hotkey row, a line on how hotkeys are written.
2. `[CameraUnlock]`, a comment asking the player to leave it in place, and `ConfigFormat=1`.
3. The schema sections the mod has rows in, in the order of the schema's `sections` array
   (Network, General, Smoothing, Position, Hotkeys, Light), each with its concept rows in the order
   of the schema's `concepts` array and then the mod's own rows in that section, in table order.
4. The mod's own sections, in table order.

Each row is its comment lines (`; text`) and then `Key=value`. A blank line separates sections.
Lines end in CRLF, including the last, and everything the renderer writes is ASCII with no byte
order mark.

A file holds only the rows the mod binds: a mod with no carried light has no `[Light]`, and one
whose mode control has two states has no `RotationEnabled`. Every bound row is written, so a
converted file states every setting explicitly, and a default in code only decides a key the
player deleted or one a later version added.

An **Engine** row holds data about the game rather than a taste: an address, an offset, a vtable
slot, a collision channel. At its default it is written as a comment showing the value,
`; PovOffset=0x404`, which the reader skips, so the row reads its default and a later build that
corrects the default reaches every player who never set it. Any other value is written as an
active line.

### Line grammar

The reader, the editor and core's scripts (`scripts/lib/canonical-ini.mjs`) share one grammar,
defined on bytes:

- **Unreadable files.** A file starting with a UTF-16 byte order mark (`FF FE` or `FE FF`), or
  holding a NUL byte anywhere, is unreadable, and nothing in it is read.
- **Encoding.** Nothing is decoded. A UTF-8 byte order mark at offset 0 is skipped. Bytes above
  0x7F and 0x1A are ordinary bytes in comments and string values.
- **Lines** end at CRLF, LF or a lone CR. Each line is trimmed of spaces and tabs, nothing else.
- **Comments.** A line whose first byte after trimming is `;` or `#`. There are no inline
  comments: `B=true ; note` has the value `true ; note`, which the bool codec then refuses.
- **Section headers.** A line starting `[` is a header named by the text up to its first `]`,
  trimmed. Text after the `]` is ignored with a diagnostic. A `[` line with no `]`, or with an
  empty name, opens no section: the keys below it, up to the next header, are reported and not
  read. A key above the first header is reported and not read.
- **Key lines** split at the first `=`. The key is the text before it, the value the text after
  it, both trimmed. A value may be empty. A line with no `=`, or an empty key, is reported and
  skipped. There are no quotes and no escapes.
- **Names** compare ASCII case-insensitively, sections and keys alike. Nothing else is folded.
- **Repeats.** Headers of one name are one section. A key set more than once in a section keeps
  its last occurrence, and one diagnostic names every line.

### Values

Every row has one codec, which both reads and writes its value. A value the codec
does not read keeps the row's default and draws a diagnostic naming the line, the value and what
was expected. Nothing is clamped: a number outside the row's range is invalid in the same way.
The reader never refuses a file over a value, so one bad value never costs the player the rest of
the file.

| Codec | Written as | Read |
|-------|------------|------|
| bool | `true`, `false` | `true false 1 0 yes no on off`, ASCII case-insensitive |
| int | decimal, `-` only when negative, no leading zeros | `-?[0-9]+`, within the field's type and the row's range |
| hex32, hex64 | `0x` and upper-case digits, no padding: `0x404` | `0x` or `0X` and 1 to 8, or 1 to 16, digits of either case |
| float, double | the shortest `%.Ng` text in the C locale that reads back to the same bits, preferring one without an exponent, with `.0` added when it has no `.` or `e`: `1.0`, `10.0`, `0.15`, `1e-05` | `-?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?`, correctly rounded; no `inf`, `nan`, leading `+`, leading or trailing `.`, comma or hex float; too large for the type, or not zero but rounding to zero, is invalid |
| string | as stored | the value's bytes, with no byte below 0x20 other than tab; C# also requires strict UTF-8 |
| enum | the row's PascalCase token: `UpdateCamera` | the row's tokens, ASCII case-insensitive; never a number |
| color | four floats `r, g, b, a`, each 0 to 1 | four comma-separated floats, each trimmed and 0 to 1 |
| list of hex32, hex64 or string | items joined by `, ` | split at `,`, each item trimmed and non-empty; an empty value is an empty list |
| hotkey | a key list, see [Hotkeys](#hotkeys) | see [Hotkeys](#hotkeys) |

`data/fixtures/canonical-ini/codecs/cases.tsv` holds both languages to every codec, with three
values left out because .NET Framework disagrees with the rules there. Its float formatting is
not always correctly rounded, so it can write a float's last digit differently from C++ and
.NET 8: the float 1234.5677490234375 as `1234.5678` where the rule gives `1234.5677`, and
3451485.25 as `3451485.3` where the rule gives `3451485.2`. C++ reads either text as the same
float. Its float parser is not correctly rounded either, so a hand-typed float text with more
digits than core writes can read one step away from what C++ reads. .NET Framework 3.5 also reads
the double `3e-324` as 0. Each language's own tests hold those three values.

### Hotkeys

A hotkey value is a list of bindings separated by commas, and empty means unbound:
`ToggleKey=End, Ctrl+Shift+Y`. A binding is any of `Ctrl`, `Shift` and `Alt`, each at most once
and in any order, then one key, joined by `+`. Names read ASCII case-insensitively, and the same
binding twice in one list is invalid. The canonical text writes the modifiers as
`Ctrl+Shift+Alt+` in that order, the key as `data/keys.json` spells it, and `, ` between items,
so `end,shift+ctrl+y` is written `End, Ctrl+Shift+Y`.

The key names are Unity's `KeyCode` member names (`End`, `PageUp`, `F9`, `Alpha1`, `Keypad0`,
`UpArrow`), 334 of them in `data/keys.json`. A name has a Windows virtual-key code only where both
mean the same key on every layout, so punctuation keys and mouse buttons have none. The two
dialects differ in one way:

- **Native mods** (C++ `input::ParseKeyBindings`) read the names that have a virtual-key code, and
  also `0x` with one or two hex digits from `0x01` to `0xFE`, for a key the table cannot name. A
  code with no name is written that way (`0xBA`).
- **Unity mods** (C# `KeyBindings.TryParse`) read the names that have a `KeyCode` value, and no
  numbers: a `KeyCode` value is not a virtual-key code (`End` is 279 to Unity and 0x23 to Windows),
  so a number would name another key than it does in a native mod.

A binding with modifiers fires when its key goes down while every modifier it names is held, on
either side. A binding without modifiers does not fire while Ctrl and Shift are both held. The
chords are ordinary items of the list, so a player can rebind or remove them like any other key,
and no key of its own names a chord: a canonical file has no key whose name starts with `Chord`,
and a mod's legacy import folds an old chord switch or chord letter row into its action's list.
C++ `input::RegisterKeyBindings` puts a list on a `HotkeyPoller`, one hotkey per distinct key, so
one press runs the action once however many items it matches; C#
`KeyBindingInput.IsTriggered` (`CameraUnlock.Core.Unity`) asks Unity's input the same question.

### The stamp and ConfigFormat

`[CameraUnlock]` is the stamp. A file is stamped when it has a line that opens a section named
`CameraUnlock` under the header rule above (so `[CameraUnlock] ; note` is stamped and
`; [CameraUnlock]` is not). The owner reads `CameraUnlock.ini` as canonical with or without it and
adds it at the next save that changes a row; core's tooling tells a repo's committed canonical
file from a legacy one by it. In a file that starts with a UTF-16 byte order mark the stamp is
looked for in its UTF-16 text, the one place core decodes UTF-16.

`ConfigFormat` inside it is the dialect version, and 1 is the only version. It changes only when
the grammar or a codec changes in a way an older reader would misread. Adding concepts, rows, keys
or key names never changes it.

- Missing, not digits, or `0`: read as the current version with a diagnostic, and the next save
  writes `ConfigFormat=1`.
- Above the reader's version (a file written by a newer build, read after a rollback): read, with
  a diagnostic naming the value, and every save is refused, so an older build never rewrites a
  newer file.

The section's other keys are reserved for core: the reader draws no unknown-key diagnostic for
them, and a mod cannot put a row there.

### The canonical concept set

The concepts are the settings every mod spells the same way. This is core's table naming all 28
of them at their defaults, which both languages render byte for byte
(`data/fixtures/canonical-ini/head-tracking/all-concepts.ini`). The comments are the schema's
`file_comment`, and a mod can replace one where its unit or behaviour differs.

<!-- file: data/fixtures/canonical-ini/head-tracking/all-concepts.ini -->
```ini
; Fixture Game head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=4242

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=true
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=true
; true: your aim stays with the mouse or controller while your head moves the view.
AimDecoupling=true
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=true
; Milliseconds a tracker packet stays current. Once the tracker has sent nothing
; for this long, the mod stops following it until data arrives again.
DataFreshnessMs=500

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=0.0
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=0.15

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=true
; false: head tracking runs rotation only, whatever RotationEnabled and PositionEnabled say,
; and the mode hotkey skips the modes that use position.
PositionAllowed=true
; false: while you aim down the sights, leaning keeps your eye on the sights.
; true: the weapon stays put and your head moves freely around it (true free look).
TrueFreeLook=false
; How far, in metres, leaning left or right can move the view.
PositionLimitX=0.3
; How far, in metres, raising your head can move the view.
PositionLimitY=0.2
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=0.2
; How far, in metres, leaning forward can move the view.
PositionLimitZ=0.4
; How far, in metres, leaning back can move the view.
PositionLimitZBack=0.1
; true: leaning stops at walls instead of moving the view through them.
CollisionEnabled=false
; How far the view is held off a wall when you lean into it, in the game's own units.
CollisionMargin=0.1
; Which of the game's collision channels the wall check tests against.
; CollisionChannel=0
; How gently the view eases back out after a wall stopped a lean.
; 0 is the quickest, 1 the slowest.
CollisionReleaseSmoothing=0.9
; Metres from the pivot of your neck forward to the point the tracker follows.
; Used to remove the lean that turning your head adds. 0 here and in TrackerPivotUp turns it off.
TrackerPivotForward=0.0
; Metres from the pivot of your neck up to the point the tracker follows.
TrackerPivotUp=0.0

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=End, Ctrl+Shift+Y
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=PageUp, Ctrl+Shift+G
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=PageDown, Ctrl+Shift+H
; Switches between keeping your eye on the sights and true free look (TrueFreeLook).
TrueFreeLookKey=Insert, Ctrl+Shift+U

[Light]
; true: a light you carry points where you look instead of where you aim.
LightFollowsHead=true
; How far the light turns for each degree your head turns.
; 1 matches the view, 0 keeps the light on your aim.
LightMultiplier=1.5
```

The ranges come from the schema's `range` field: `UdpPort` 1 to 65535, `DataFreshnessMs` 1 to
2147483647, the smoothing pair and `CollisionReleaseSmoothing` 0 to 1, the five limits and the two
tracker pivots 0 to 10, `LightMultiplier` 0 to 5, `CollisionMargin` 0 with no upper bound (its
unit is the engine's own), and `CollisionChannel` none. The four hotkey lists start at the
schema's `canonical_default`. The `default` field and core's field initialisers, which the older
flat readers use, keep `End` for `ToggleKey` and `PageDown` for `YawModeKey`, and leave
`CycleTrackingModeKey` empty. No flat reader reads `TrueFreeLookKey`, so its `default` and field
initialisers are its `canonical_default`.

The tracking mode at startup is the pair `RotationEnabled` and `PositionEnabled`: both true is
rotation and position, `true, false` rotation only, `false, true` position only. Both false names
no mode, so the table reads both as their defaults and reports both lines. A mode change always
writes the pair.

`TrueFreeLook` is the lean while aiming down sights, in a shooter with an aim state and positional
tracking. `false`, sights locked, keeps the eye on the sight line; `true`, true free look, leaves
the lean in full while the weapon stays put in the world. It is in `[Position]` because the lean is
all it changes and it exists only where positional tracking does, and a key belongs in the section
of its subject, as `CollisionEnabled` does. A mod without positional tracking binds neither it nor
`TrueFreeLookKey`. It has no alias: `true_free_look` is read only by a mod's legacy import, and in a
canonical file it draws `MisplacedKey` and is not read. The older flat readers read neither
`TrueFreeLook` nor `TrueFreeLookKey`.

The schema's other concepts stay in the schema, so the older flat readers still parse them, and
the canonical format never writes them. Each carries a `canonical_reason`, which the table's
`NonCanonicalConcept` diagnostic gives when a canonical file holds one:

| Concepts | Why the canonical format has no row for them |
|----------|----------------------------------------------|
| `YawSensitivity`, `PitchSensitivity`, `RollSensitivity`, `PositionSensitivityX/Y/Z` | The mod applies the head pose as the tracker sends it, with no sensitivity of its own |
| `InvertYaw`, `InvertPitch`, `InvertRoll`, `InvertPositionX/Y/Z` | The mod applies the head pose as the tracker sends it, with no axis inversion of its own |
| `ShowReticle`, `ReticleColor` | Whether a reticle is shown, and its colour, are not settings |
| `ReticleToggleKey` | The reticle has no toggle key |
| `PositionToggleKey` | `CycleTrackingModeKey` is the hotkey that switches positional tracking |
| `RecenterKey` | The mod keeps no centre of its own: the player centres the view in the tracker app |

So the schema sections `[Sensitivity]`, `[Inversion]` and `[Reticle]` hold no canonical concept,
and no canonical file has them. The retired `Smoothing` key (and its alias `SmoothingFactor`) is
not written either.

Deadzones, response curves, unit scales and axis signs a player can edit, and some sensitivity
spellings, were never schema concepts, so the schema lists their spellings under
`non_canonical_keys` instead. These are not aliases: neither flat reader resolves them, and no field is bound to them. A canonical file
holding one draws the same `NonCanonicalConcept` diagnostic with the group's `canonical_reason`,
in any section. A group's sections hold nothing else, so every key in one draws the group's
reason whatever it is spelled, beside the section's own `UnknownSection`:

| `non_canonical_keys` | Sections | Spellings | Reason |
|----------------------|----------|-----------|--------|
| `Sensitivity` | `[Sensitivity]` | `RotScale`, `YawGain`, `PitchGain`, `RollGain`, `PositionSensitivity`, `Sensitivity`, `RotationSensitivity`, `RotationMultiplier`, `RotationGain`, `PositionMultiplier`, `PositionGain`, `LeanScale` | The mod applies the head pose as the tracker sends it, with no sensitivity of its own |
| `Inversion` | `[Inversion]` | `SignYaw`, `SignPitch`, `SignRoll`, `SignX`, `SignY`, `SignZ` | The mod applies the head pose as the tracker sends it, with no axis inversion of its own |
| `Deadzone` | `[Deadzone]` | `Deadzone`, `DeadzoneDeg`, `DeadzoneYaw`, `DeadzonePitch`, `DeadzoneRoll`, `YawDeadzone`, `PitchDeadzone`, `RollDeadzone`, `EnableDeadzone`, `DeadzoneMin`, `DeadzoneMax`, `Deadband`, `YawDeadband`, `PitchDeadband`, `RollDeadband` | The mod applies the head pose as the tracker sends it, with no deadzone of its own |
| `ResponseCurve` | | `ResponseCurve`, `YawCurve`, `PitchCurve`, `RollCurve`, `SensitivityCurve`, `CurveStrength` | The mod applies the head pose as the tracker sends it, with no response curve of its own |
| `PositionScale` | | `PositionScale`, `PositionScaleUU`, `PosScale`, `WorldScale`, `UnitsPerMeter`, `UnitsPerMetre`, `WorldUnitsPerMeter`, `WorldUnitsPerMetre` | The mod converts your head movement to the game's units itself, so the scale is not a setting |

Matching is the schema's: ASCII case and `_` and `-` are ignored, so `deadzone_yaw` is
`DeadzoneYaw` and `rot_scale` is `RotScale`; a section matches ASCII case-insensitively. The bare
`Yaw`, `Pitch` and `Roll` that mods wrote under `[Sensitivity]` and `[Deadzone]` are in no list,
because section-less they would be both; they draw the reason of the section they are in.

The lists match these spellings and no others. A pose-shaping setting under a name no group lists
passes the table, apply and the lint as a game-local row, so a conversion that meets a new
spelling adds it to its group in core's schema, never as a list of the mod's own.

A bare noun cannot go into a group. metaphor-refantazio reads its unit scale as `[Position] Scale`,
and section-less matching would take spec-ops-the-line's `[FieldOfView] Scale` and rv-there-yet's
`[Reticle] Scale` with it, which are not pose shaping. The lint refuses a bare `Scale` in any
canonical file, but a table accepts it as a game-local row and apply reports a stray one as
`UnknownKey`, so that conversion folds the scale into code and drops the legacy value through
`LegacyPoseShaping` by hand, with no gate behind it.

The conversion from the tracker's metres to the game's units is the mod's boundary code, like its
axis signs. A scale the player can edit is a position sensitivity under another name, so, by the
owner's ruling of 2026-09-25, a conversion folds the shipped value into code as a constant and
drops a value the player changed, as it does a sensitivity.

`LightMultiplier` is not pose shaping: it turns a carried light, not the view, and stays a
canonical concept.

### Game-local rows

Anything a mod reads that is not a concept is a local row: engine data, diagnostics, logging, a
feature of one game. The table refuses, when the row is added:

- a key name used anywhere else in the file, whatever the section, compared ASCII
  case-insensitively and counting `ConfigFormat`;
- a key that is any concept's key or alias under the schema's normalisation (case and `_` and `-`
  ignored), canonical, non-canonical or retired, or a spelling `non_canonical_keys` lists;
- a section or key that is not PascalCase ASCII letters and digits;
- a row in `[CameraUnlock]`, in `[Sensitivity]`, `[Inversion]`, `[Reticle]` or `[Deadzone]`, or in a section
  spelled like a schema section or an earlier local section with other letter case;
- a row with no comment, unless an earlier local row of its section is written above it and its
  comment covers both.

A local row belongs in the schema section of the same subject when there is one (a position
feature under `[Position]`), and otherwise in a section of its own. The canonical config lint also
refuses a local key that is one of these bare nouns from the schema's `deliberately_unaliased`
list: `Enabled`, `Enable`, `Amount`, `Factor`, `Scale`, `Limit`, `Multiplier`, `Yaw`, `Pitch`,
`Roll` and `Position`, and any key whose name starts with `Chord` (see [Hotkeys](#hotkeys)). It
refuses no other spelling from that list.

## What a mod binds

### Config tables

A table lists the rows of one config file and binds each to a field of the mod's config type:
C++ `config::ConfigTable<Config>` (`config/config_table.h`), C# `ConfigTable<TConfig>`, where
`TConfig` is a class. A row's default is its field's value in the defaults instance the table is
built with, so the defaults live in the config type, as they always have.

- **Concept rows** name a concept and take its section, key, codec, range and comment from the
  schema. C++ checks the field's type at compile time (`FieldHoldsConcept`): `bool`, an integral
  type whose limits hold the concept's range (`std::uint16_t` holds `UdpPort`), `float` or
  `double`, or `std::string` for a key list. In C# the generated `ConfigConcepts` descriptor fixes
  the accessors' type.
- **Local rows** name their section, key, codec and comment.
- **Modifiers** apply to the last row added, or to the concept row `Select` names: `Comment`
  replaces a concept's comment, `Range` bounds an int, float or double local row, `Engine` makes
  the row an Engine row, `Writable` marks a row the owner's `Save` may change.

`ApplyCanonical(doc, table, config)` / `table.Apply(doc, config)` reads a parsed file into a
config: every row starts from its default, fields no row binds are left alone, and it returns the
table's diagnostics (an invalid value, an unknown section or key, a key in the wrong section or
spelled as an alias, a retired or non-canonical concept, a pair that names no tracking mode).
`RenderCanonical(table, config, header)` / `table.Render(config, header)` writes a config as a
canonical file.

### HeadTrackingConfigTable

A mod that keeps its settings in core's config type does not write concept rows itself. C++
`config::HeadTrackingConfigTable<Config>({...})` (`config/head_tracking_config_table.h`) over
`HeadTrackingConfig`, and C# `HeadTrackingConfigTable.Create<TConfig>(...)` over
`HeadTrackingConfigData`, take the list of concepts the game implements and bind each to the
field core's type holds it in. The mod then adds its local rows, on a config type derived from
core's, and its modifiers.

The list is explicit because a file carries only what the mod binds, and because a concept core
adds later then reaches a mod's file only when that mod names it, so it never breaks an existing
mod's committed file. An empty list or a concept named twice throws.

The defaults are the config type's own, with the four hotkey lists at their `canonical_default`.
`CollisionChannel` is an Engine row. `LocalSmoothing` and `RemoteSmoothing` also set the copy the
position settings carry. `PositionLimitY` never sets `PositionLimitYDown`: no key takes its value
from another. The sensitivity and inversion fields of core's types have no row, so a canonical
file never sets them and they keep the defaults instance's values.

### Writable rows

The owner's `Save` changes only rows the table marks Writable, and a change to any other row
throws, naming it. That is how a mod states which of its controls persist:

- A tracking-mode control writes `RotationEnabled` and `PositionEnabled` together, so a table
  that has both must mark both Writable or neither; the owner refuses a table that marks one.
- The yaw-mode control writes `WorldSpaceYaw`, and the true free look toggle writes `TrueFreeLook`.
- The on/off toggle (End) does not persist: it changes only the session. `EnableOnStartup` is
  Writable only in a mod with a separate control that saves it, and the toggle still never calls
  `Save` for it.

### The committed file

A converted repo commits the file its table renders from its defaults, at the `committed` path
`data/config-format.json` records, marked `-text` in `.gitattributes`. The repo's render test
compares the two byte for byte, so the committed file cannot drift from the code. The owner
creates the same bytes at first launch wherever the runtime writes every value's text as the test
did: a C# mod on .NET Framework can differ in the last digit of a float, as [Values](#values)
describes. `pixi run render-config` rewrites it after a change
to a row, a comment or a default (see [Tooling](#tooling)).

### C++ example

A native mod with core's config type and one local row. The code below is compiled and run by
`cpp/tests/canonical_config_example_tests.cpp`, and `pixi run check` fails when it no longer
matches that file.

<!-- excerpt: cpp/tests/canonical_config_example_tests.cpp -->
```cpp
struct ModConfig : cameraunlock::HeadTrackingConfig {
    bool write_log = false;
};

ConfigTable<ModConfig> ModConfigTable() {
    ConfigTable<ModConfig> table = HeadTrackingConfigTable<ModConfig>(
        {Concept::UdpPort, Concept::EnableOnStartup, Concept::WorldSpaceYaw, Concept::RotationEnabled,
         Concept::PositionEnabled, Concept::ToggleKey, Concept::CycleTrackingModeKey, Concept::YawModeKey});
    table.Select(Concept::WorldSpaceYaw).Writable()
        .Select(Concept::RotationEnabled).Writable()
        .Select(Concept::PositionEnabled).Writable();
    table.Local("Logging", "WriteLog", &ModConfig::write_log, BoolCodec(),
                "true: write HeadTracking.log beside the game's executable.");
    return table;
}
```

The owner is built once, before anything reads the file, with the file's full path (in the test,
`dir` is a scratch folder; a mod uses the folder its loader loads it from). `Load` runs on the
mod's init thread, never in `DllMain`, and `Save` runs where the hotkey fired, after the mod has
applied the new value to its running state:

<!-- excerpt: cpp/tests/canonical_config_example_tests.cpp -->
```cpp
ConfigOwnerOptions<ModConfig> options;
options.path = (dir / L"HeadTracking.ini").wstring();
options.table = ModConfigTable();
options.header.display_name = "Example Game";
ConfigOwner<ModConfig> owner(std::move(options));

ConfigLoadResult<ModConfig> loaded = owner.Load();
const ModConfig& config = loaded.config;
cameraunlock::input::KeyBindingsParseResult toggle = cameraunlock::input::ParseKeyBindings(config.toggle_key_name);

ConfigSaveResult saved = owner.Save([](ModConfig& c) { c.world_space_yaw = false; });
```

On the first launch `loaded.status` is `Created` and the folder holds the file shown
[above](#what-it-looks-like). The save changes the `WorldSpaceYaw` line and nothing else, and the
next launch loads `Canonical` with the saved value. A mod with a published pre-canonical build
also sets `options.import` (see [The legacy import](#the-legacy-import)).

### C# example

The same mod in C#, compiled and run by
`csharp/src/CameraUnlock.Core.Tests/Config/CanonicalConfigExample.cs` under xunit on .NET 8 and in
`CameraUnlock.Core.FrameworkTests` on .NET Framework 3.5 and 4.7.2, so it is C# 7.3 that builds on
net35:

<!-- excerpt: csharp/src/CameraUnlock.Core.Tests/Config/CanonicalConfigExample.cs -->
```csharp
public sealed class ModConfig : HeadTrackingConfigData
{
    public bool WriteLog { get; set; }
}

public static ConfigTable<ModConfig> ModConfigTable()
{
    return HeadTrackingConfigTable.Create<ModConfig>(ConfigConcepts.UdpPort, ConfigConcepts.EnableOnStartup,
            ConfigConcepts.WorldSpaceYaw, ConfigConcepts.RotationEnabled, ConfigConcepts.PositionEnabled,
            ConfigConcepts.ToggleKey, ConfigConcepts.CycleTrackingModeKey, ConfigConcepts.YawModeKey)
        .Select(ConfigConcepts.WorldSpaceYaw).Writable()
        .Select(ConfigConcepts.RotationEnabled).Writable()
        .Select(ConfigConcepts.PositionEnabled).Writable()
        .Local("Logging", "WriteLog", c => c.WriteLog, (c, v) => c.WriteLog = v, new BoolCodec(),
            "true: write HeadTracking.log beside the game's executable.");
}
```

<!-- excerpt: csharp/src/CameraUnlock.Core.Tests/Config/CanonicalConfigExample.cs -->
```csharp
var owner = new ConfigOwner<ModConfig>(new ConfigOwnerOptions<ModConfig>
{
    Path = Path.Combine(dir, "HeadTracking.ini"),
    Table = ModConfigTable(),
    Header = new RenderHeader("Example Game"),
});

ConfigLoadResult<ModConfig> loaded = owner.Load();
ModConfig config = loaded.Config;
bool parsed = KeyBindings.TryParse(config.ToggleKeyName, out var toggle, out var error);

ConfigSaveResult saved = owner.Save(c => c.WorldSpaceYaw = false);
```

A Unity mod calls `Load`, `Save` and `Reload` on the main thread, where its hotkeys fire.

### REFramework mods

REFramework mods read their config through core's `PluginConfig`, so their table and their legacy
import are core's too. A mod converts by setting `PluginConfigSchema::canonicalConfig` (appended
last, since every mod initialises the schema positionally) and `PluginModDescriptor::gameName`,
which `PluginMod::Initialize` then requires. `PluginConfigTable(schema)`
(`reframework/plugin_config_table.h`) binds `UdpPort`, `EnableOnStartup`, `WorldSpaceYaw`
(Writable), the smoothing pair, `PositionEnabled` (Writable), four position limits, the three
hotkey lists, a local `DiagnosticMarkerKey` when the schema has a diagnostic marker key, and the
light rows when the schema has a flashlight. The mode control has two states, so there is no
`RotationEnabled`. `PluginConfigLegacyImport(schema)` is the import, and `PluginConfig::Read`,
which it calls, is frozen. With the flag set, `PluginMod` keeps the settings in
`reframework\plugins\CameraUnlock.ini`, beside the plugin DLL, and imports
`PluginModDescriptor::configFileName` (`HeadTracking.ini` by default) from the same folder while
`CameraUnlock.ini` is absent; that file is never written. With the flag unset, a mod reads,
migrates and writes `configFileName` as it did before.

## The config owner

`ConfigOwner<Config>` (C++ `config/config_owner.h`) and `ConfigOwner<TConfig>` (C#) are the one
reader and writer of a config file. Each file has one owner, built before anything reads the file,
and nothing else reads or writes it. The owner writes only the config file, never the legacy file, and only through
the checked file writer, which never opens the live file for writing: it writes a temporary beside
it and swaps it in whole, or creates the file only where none is. The
owner runs on Windows only; the reader, the table, the codecs and the renderer are pure and run
anywhere.

The options are filled by member (C++) or property (C#), never positionally, so an option added
later changes no mod's code:

| C++ | C# | |
|-----|----|-|
| `path` | `Path` | The file's full path. Required. C++ takes it as a wide string, with no ANSI overload |
| `table` | `Table` | The mod's table. Required |
| `import` | `Import` | The frozen legacy import, empty (C++) or null (C#) for a mod that never published a pre-canonical build |
| `legacy_path` | `LegacySourcePath` | The legacy file the import reads, as a full path, normally in the folder of `path`: `HeadTracking.ini` or a BepInEx plugin's `<GUID>.cfg`. Required with an import and refused without one |
| `header` | `Header` | What the renderer writes above the settings: the display name. Required |
| `status_sink` | `StatusSink` | Optional. Shows the player a one-line message, run after the owner releases its lock |

The constructor throws for a missing or relative path, a table that has both `RotationEnabled`
and `PositionEnabled` and marks only one Writable, a header the renderer refuses, an import without
a legacy path, a legacy path without an import, or a legacy path naming the config file itself
(compared without case). The C++ owner also throws for a table with no rows.

### Load

`Load()` returns the status, the config the session runs on, the reader's and table's
diagnostics, the lines for the mod's log and, when the load was not usable, the message for the
player. The owner logs nothing itself: the lines come back so a mod can load before its logger is
up and write them afterwards. The status numbers are the same in both languages.

| Status | When | The session runs on | Saves this session |
|--------|------|---------------------|--------------------|
| `Canonical` (0) | The config file exists and is readable, stamped or not (for an unstamped one the log says the next save stamps it) | the file | yes |
| `Migrated` (1) | There was no config file, and the legacy file was imported into a new one this launch | the imported settings | yes |
| `Created` (2) | There was no config file and no legacy file, and the rendered defaults were written, never over a file that appeared meanwhile | the defaults | yes |
| `Deferred` (3) | An import or creation could not finish, or the config file or the legacy file could not be opened | what the import gave, or the defaults | no |
| `LegacyRefused` (4) | The import refused the legacy file, as the published build did | what the import gave; the mod does what its published build did on that refusal | no |
| `Unreadable` (5) | The config file is saved as UTF-16 or holds a NUL byte | the defaults | no |

While the config file exists the import never runs and the legacy file is never opened; when a
legacy file is also present, the log says so:
`<path>: settings are read from this file. <legacy path> is left as it was and is not read.`
A config file the owner cannot open is deferred on the defaults, and nothing is imported.

### Save

`Save(change)` hands `change` the settings read from the file as it is now; the change sets the
new values. The owner then checks that only Writable rows changed, builds one edit (both mode rows
when either changed), runs the editor, reads the edited bytes back through the table, where only
the changed rows may differ, and writes exactly those bytes, only if the file still holds the
bytes it read. That last check and the replacement are two operations, not a compare-and-swap, so
a program that writes the file between them is overwritten. A change that leaves every row as it
was writes nothing and reports `Saved`.

| Status | Meaning |
|--------|---------|
| `Saved` (0) | The file holds the new values |
| `NotSaved` (1) | Nothing was written, and the file is as it was. The result has the reason and the OS error |
| `Uncertain` (2) | Windows started replacing the file and did not finish, and the checked writer could not finish it either. The reason names the file and the kept temporary that holds the new contents |

It saves only a readable file whose `ConfigFormat` is not newer than the build's; an unstamped
one gets its `[CameraUnlock]` section in the same write. The legacy file is never edited. A missing
file is not created by `Save`: the next `Load` creates it. After a `Deferred`, `LegacyRefused` or
`Unreadable` load every save is `NotSaved` until a `Reload` applies a readable file. `Save` never
rolls back and never retries: the mod applies the new value first, and a save that fails leaves
the session running on it.

### Reload and FileChanged

`FileChanged()` compares the file's last write time with the one the owner recorded at its last
load, reload or save, for a mod that watches its file. `Reload()` reads only the config file, never
writes, and never runs the import:

| Status | Meaning |
|--------|---------|
| `Unchanged` (0) | The file holds the bytes the owner last created or saved |
| `Applied` (1) | The file was read as canonical, stamped or not, and the result holds its settings |
| `Unreadable` (3) | The file is missing or could not be read. The mod keeps the settings it has |

There is no status 2: `LegacyReadOnly`, which read a legacy file mid-session, went with the
conversion in place.

### Threading

- One lock serialises `Load`, `Save`, `Reload` and `FileChanged`. It does not coordinate separate
  processes. The status sink runs after the lock is released.
- `Load` imports and creates files, so it must not run under the loader lock: call it from the mod's init
  thread, never from `DllMain`.
- `Save` is synchronous. Call it from the `HotkeyPoller` thread or another thread that is not
  drawing a frame, never from a per-frame path. A Unity mod calls it on the main thread, one write
  per key press.
- Where a mode change is applied on the render thread (through `input/deferred_actions.h`), the
  hotkey callback on the poller thread computes the next mode from the one the render thread last
  applied, stores it as the desired mode, requests the apply, and saves. Computing from the
  applied mode keeps two presses before one frame to one step. Core's REFramework `PluginMod` does
  exactly this.

## Migration

### Which repos convert

`data/config-format.json` names three groups:

- **`legacy`**: the 87 repos that published a pre-canonical build, a `v*` release or the rolling
  `dev` pre-release on GitHub. Only these carry a legacy import, because only their players hold a
  file an older build wrote. The set is frozen: `pixi run check-config-format` pins its names.
- **`exempt`**: nine repos that cannot move to a canonical reader or have no config, each with its
  reason.
- Every other repo carries no import: the 40 unpublished repos that convert, and every new repo,
  which is canonical from its first build.

### The legacy import

The import is the repo's own config reading code as it was at the commit before the conversion,
moved into `src/legacy_config/` (C++) or `Legacy/` (C#), with its own frozen copy of that commit's
config struct and defaults. It reads the old file exactly as the published build did, through the
same file API, fills the frozen struct, and maps it field by field into the runtime config: hotkey
codes and chord switches into key lists, numbers into enum tokens, and the rules below. It writes
nothing. It stays for the life of the repo, since a player can update from any older build.

- C++ `LegacyImport<Config>` (`config/legacy_import.h`) holds `run`, called with a `LegacyInput`
  (the wide path, its ANSI form, and whether the ANSI form lost a character) and the config to
  fill, and `keys`, every section and key the frozen reader reads. C#
  `LegacyImport<TConfig>(run, keys)` takes a `LegacyImportRun<TConfig>` delegate called with a
  `LegacyImportInput`. A `LegacyKey` with an empty section is a key the reader finds in any
  section.
- `run` returns an `ImportResult`: `Imported` (0) and `Absent` (3) carry the values it dropped and
  the pose-shaping values it read (below), `Refused` (1) and `Undecodable` (2) carry a reason. An import that throws is a bug and the
  exception reaches the caller.
- A dropped value is a `DroppedValue` with its `DropRule`. The rules are the only differences a
  conversion may make between what the published build ran on and what the new file holds, and
  `data/config-format.json` records each one the owner approved:

| DropRule | Recorded as | What is dropped |
|----------|-------------|-----------------|
| `NonFiniteNumber` (1) | normalisation N2 | A NaN or infinite float, which imports as the row's default (C++ `LegacyFiniteOrDefault`, C# `LegacyNormalisations.FiniteOrDefault`) |
| `PoseShaping` (2) | approved change `pose_shaping` | A sensitivity, unit scale, deadzone, response curve or axis inversion a player set away from the shipped default. A shipped unit scale, and a shipped default that is not identity, belong to the mod's axis conversion, so the conversion moves them into the mod's own code |
| `Reticle` (3) | approved change `reticle` | Reticle settings and a reticle toggle key |
| `FollowsDefault` (4) | approved change `follows_default` | The setting of a feature shipped switched off while untested, which now follows the mod's default |
| `KeyCodeOutOfRange` (5) | normalisation N1 | A hotkey code outside 0x01-0xFE, 0xFF included, which imports as unbound (C++ `LegacyVirtualKeyToBindings`; no C# import reads virtual-key codes). Code 0, a legacy file's unbound, stays unbound and is not recorded |

`conversion_notes` in `data/config-format.json` holds what the owner decided for one repo's
conversion, such as which of two shipped values is the default; the repo's conversion and its
differential test follow it.

A map passes every sensitivity, unit scale, deadzone, response curve and axis inversion its
frozen reader read through C++ `LegacyPoseShaping` or C# `LegacyPoseShaping.Record` (bool, float and double),
with the effective legacy value and the value the game shipped. Each call adds a `PoseShapingValue` to the
result's `pose_shaping` (C# `PoseShaping`): section, key, both values written as the canonical codecs
write them (`true`, `1.0`, `0.5`; `nan`, `inf` or `-inf` for a legacy value that is not finite),
and `folded`, true when the two are equal as numbers. A folded value is what the game shipped, and
the conversion moves it into the mod's own axis code, so the mod behaves as before with no setting.
A value that is not folded is one the player changed, and the call also adds it to the dropped
values as `PoseShaping`, which the migration logs as `not carried: [Section] Key=value, sensitivity,
scales, deadzones, response curves and axis inversion are set in the tracker now, not in this mod`. The map
sets no runtime field from either, and a shipped value that is not finite throws. Core's REFramework
import lists the ones `PluginConfig::Read` reads (the three multipliers, the three position
sensitivities and, for a schema with `positionInvertKeys`, the three position inversions) against
`PluginConfig::SetDefaults`, where an RE mod keeps the shaping it
ships and still applies.

Core keeps two pieces of import code, frozen, because several repos share them: the Win32 helpers
the native imports call (`config/ini_reader.h` and `config/value_guards.h`, pinned by
`cpp/tests/win32_profile_semantics_tests.cpp` and `cpp/tests/frozen_ini_helper_tests.cpp`), and
REFramework's `PluginConfig::Read`. A C# import that calls another core reader carries its own copy
of that reader in its legacy folder, so a later change to the public class cannot move a
migration.

A repo proves its import with a differential test: the published build's reader and the import
run on the shipped file and on every output of the corpus generator (C++ `GenerateIniMutations`
in `config/testing/ini_mutations.h`, C# `IniMutations.Generate` in `csharp/testing/IniMutations.cs`,
which a test project links as source), and their results may differ only as the rules above
allow. For pose shaping that means two things. On the shipped file every `pose_shaping` entry is
folded, which is where the test holds the conversion's axis code to the shipped value it replaces.
Where one build shipped two values for a setting, only one can be the fold, and
`conversion_notes` in data/config-format.json records which. Requiem v0.4.0's installer ships
position sensitivity 1.0 and its launcher seed carried 2.0; the owner ruled 1.0 on 2026-09-25, so
nothing is folded and the seed's 2.0 is dropped as `PoseShaping`.
On every corpus input, a pose-shaping value the published build ran on and the new config does not
hold is an expected difference exactly when the result lists it in `pose_shaping` with that value
and in `dropped` as `PoseShaping`.

### What happens at the first launch

When `Load` finds no config file and the legacy file exists:

1. It opens the legacy file for reading, sharing read and write but not delete, and holds it open
   while it reads the bytes, runs the import on that path and reads the bytes again. No program
   can newly lock, rename or delete the file meanwhile, and a write in between defers the import.
2. It renders the imported settings, reads the render back through the table and requires every
   row to equal the import's (floats bit for bit).
3. It creates the config file with the rendered bytes, only if no file has appeared at its path.

The legacy file is never written, renamed, deleted or copied, whatever happens. A process killed
at any point leaves the config file absent or whole, and a launch that finds it absent imports
again. Once the config file exists, the legacy file is not read again; deleting only the config
file makes the next launch import the legacy file again.

The log names the file on every line. It says where the settings came from
(`<path>: created from <legacy path>, which is left as it was.`) and lists every dropped value and
every key line of the legacy file the import does not read, for example:

```text
C:\Games\Example\HeadTracking.ini: not carried: [General] Smoothng=0.3 on line 5, this build does not read it
C:\Games\Example\HeadTracking.ini: not carried: [Smoothing] RemoteSmoothing=nan, it is not a finite number, so the default is used
```

Hand-written comments are not carried either: the new file's comments are the renderer's.

### When an import does not happen

The config file is not created, the legacy file is left as it was, the session runs on what the
import gave (or on the defaults), nothing is saved that session, and the player is told once
through the status sink. The message is `<legacy file> was not imported into <config file>:
<why>. The mod tries again at the next launch and saves nothing this session.`, where `<why>` is
one of:

- `the file is in use by another program`
- `the file is read-only`
- `the folder cannot be written`
- `it could not be read (...)` or `it could not be written (...)`, for any other I/O error, with
  the error in the brackets: `Windows error N:` and the system's text in C++, the exception's
  message in C#
- `the file was changed by another program while it was read`
- `[Section] Key=value cannot be converted`, for a value no codec writes or a render that does not
  read back
- `another program created the file at the same time`, when a file appeared at the config path
  before the owner created it
- `Windows did not finish replacing <path>, so it may be missing; the new settings are in <temp>`
- `the old settings reader could not find the file`, when the import reports the legacy file
  absent while the owner holds it open
- the import's own reason, for `Undecodable`, and for `LegacyRefused`, where the mod then does
  what its published build did on that refusal

When another program created the config file first, the next launch reads that file and does not
import, and the message ends `The mod saves nothing this session and reads <config file>, not
<legacy file>, at the next launch.` instead.

In C++ the import is also handed the legacy path in the ANSI code page. When that form lost a
character and the import reports the file absent, the published build, handed the same ANSI path,
never saw the file and ran on its defaults. The owner then writes those defaults to the config
file instead of deferring, leaves the legacy file as it was, and logs the case.

A config file `Load` cannot open at all, or cannot create, gives `<file> cannot be read: <why>.` or
`<file> was not created: <why>.`, then `The mod runs on its default settings this session.`

A config file that is unreadable gives `<file> cannot be read: it is saved as UTF-16; save it as
ANSI or UTF-8. The mod runs on its default settings and saves nothing until the file is fixed.`
(or `line N holds a NUL byte`). A save that fails gives `Settings not saved: <why>.`, and an
unfinished replacement `Settings may not be saved: <why>.`

### What players are told

A converted repo's README carries a config block that `scripts/generate-readme.mjs` renders from
`data/config-format.json` and the committed file (see [Tooling](#tooling)). What it says between
the file's location and the committed file depends on the config entry and on whether the repo is
in `legacy`:

- **A repo in `legacy`**: earlier versions kept these settings in the legacy file, in the same
  folder; the first start that finds no `CameraUnlock.ini` reads the settings from the legacy
  file and writes them into `CameraUnlock.ini`, never changes the legacy file, and does not read
  it again while `CameraUnlock.ini` exists; comments, keys the mod never read and the settings
  each approved change drops are not carried over; an older version of the mod reads the legacy
  file and never `CameraUnlock.ini`, so a setting changed after updating is not in the legacy
  file; deleting only `CameraUnlock.ini` imports the legacy file again at the next start; and
  replacing everything in `CameraUnlock.ini` with the file the block ends with gives the defaults.
  The block never tells a player to delete the legacy file: a Lopari v0.9.0 receipt can record it
  as a seed (resident-evil-requiem-headtracking v0.4.0 seeds `reframework/plugins/HeadTracking.ini`
  beside REFramework), and with that file gone Lopari reinstalls the mod before every launch.
- **Outside `legacy`**: nothing beyond the location and the file.
- **A BepInEx mod**, installed under `BepInEx\config\`, adds that BepInEx's ConfigurationManager
  no longer lists these settings, or for a repo outside `legacy`, does not list them.

`scripts/templates/canonical-config-changelog.md` holds the matching changelog bullets for a
conversion release of a repo in `legacy`. The untracked NEXUS_MODS.md is updated by hand from
`pixi run readme --print config`.

### Rolling back and forward

An older build reads the legacy file, which the canonical build never writes, so rolling back
needs no step: the older build runs on the settings the legacy file held when the player updated.
A setting changed after updating is in `CameraUnlock.ini` only, and the older build does not read
it. An older build that saves writes the legacy file as it always did.

Rolling forward, `CameraUnlock.ini` is read as it was left, so a setting an older build saved to
the legacy file in between is not carried over unless `CameraUnlock.ini` is deleted first, which
imports the legacy file again at the next start.

### Install, uninstall and manual packages

- **`MOD_SEED_FILES`** in an install wrapper's CONFIG BLOCK lists files copied only when the game
  folder does not already hold one of the same name. The ASI, shim, shim-forwarder, xNVSE, BeamNG
  and REFramework bodies read it. A converted repo lists no config in it or in `MOD_DLLS`, which
  `copy /y` overwrites on every install: not `CameraUnlock.ini`, not the legacy file and not the
  committed file under another name. The owner creates `CameraUnlock.ini` at first launch. An
  update from a legacy build finds no `CameraUnlock.ini`, so a seeded one would be written before
  the mod starts and the mod would never import the player's legacy file.
- **`PRESERVE_FILES`** in an uninstall wrapper's CONFIG BLOCK lists config paths, relative to the
  game folder, that `uninstall-body.cmd` leaves in place, including inside a loader folder the
  uninstall removes. A converted repo lists every `installed` path, and a repo in `legacy` also
  lists the legacy file in the folder of each one.
- **Manual (Nexus) ZIPs** carry neither `CameraUnlock.ini` nor the legacy file. Extracted over the
  game folder, the first would replace the player's settings with the defaults, and on an update
  from a legacy build would stop the import; the second would replace the file an older build
  reads, which is also what the mod imports while `CameraUnlock.ini` is absent.
  `pixi run validate-manifest` fails a converted repo's Nexus ZIP that carries either.
- **Launcher seeds**: a converted release seeds nothing in `launcher-manifest.json`. No seed and
  no `files[]` row writes `CameraUnlock.ini`, the legacy file or a file named like the committed
  config, in any folder, with a config block or without one; the owner creates `CameraUnlock.ini`
  at first launch. Lopari v0.9.0 records a seeded file's hash in its receipt and, once the file has
  changed or is gone, downloads and reinstalls the mod before launching. A `files[]` row is copied
  over whatever is there at every deploy. `pixi run validate-manifest` fails such a package and
  conformance's `config-descriptor` the committed manifest.

### BepInEx

A BepInEx mod is a C# mod like any other: core's owner and reader on
`BepInEx\config\CameraUnlock.ini`, with the plugin's `BepInEx\config\<GUID>.cfg` as its legacy
file. The plugin binds nothing through BepInEx's `ConfigFile` at runtime, so BepInEx's
ConfigurationManager no longer lists its settings, and the `.cfg` is never written again.

- `LegacySourcePath` names the `.cfg`. When `CameraUnlock.ini` is absent and the `.cfg` exists,
  the import reads the `.cfg` and the owner creates `CameraUnlock.ini` from it. A file at
  `CameraUnlock.ini` is always read as canonical. Neither file present is `Created`.
- The import is the plugin's own `Bind` calls, frozen, run on the plugin's `Config`. BepInEx's
  `ConfigFile` read the `.cfg` in its constructor, before the owner held the file, so the import
  sets `Config.SaveOnConfigSet = false`, then calls `Config.Reload()`, then binds. Without the
  `Reload` its values come from a read the owner's snapshot does not cover; with `SaveOnConfigSet`
  off, no `Bind` writes the `.cfg`.
- The `.cfg` stays byte for byte as the last pre-canonical build left it, so an older build still
  reads it. Deleting only `CameraUnlock.ini` imports the `.cfg` again at the next start; deleting
  both gives the defaults.

## Shared fixtures

`data/fixtures/canonical-ini/` holds byte fixtures that core's C++ suite, its xunit suite and
`CameraUnlock.Core.FrameworkTests` (.NET Framework 3.5 and 4.7.2) all run unchanged, and that a
reader, editor or codec in any other language is held to. Everything under `data/fixtures/` is
`-text` in git, because the line endings, byte order marks and stray bytes are what the cases
test. `data/fixtures/canonical-ini/README.md` defines every file byte for byte.

| Directory | What it pins | Files |
|-----------|--------------|-------|
| `reader/` | the reader and the stamp | per case, `input.ini` and `expected.tsv` with `status`, `format`, `stamp`, `key` and `diagnostic` rows |
| `editor/` | the editor | per case, `case.tsv` of `set`, `set_or_insert`, `set_first`, `set_or_insert_first`, `rejects` and `refused` directives, `input.ini`, and `expected.ini` for a case that succeeds |
| `keys/` | the hotkey binding codec, both dialects | `cases.tsv`: dialect, input, `canonical` or `invalid`, canonical text |
| `codecs/` | the value codecs | `cases.tsv`: codec, input, `canonical` or `invalid`, canonical text, and the IEEE 754 bits for floats |
| `table/` | tables, apply and render | a fixture table both suites declare, and per case `input.ini` with `expected.tsv`, or `values.tsv` with `expected.ini` |
| `head-tracking/` | `HeadTrackingConfigTable` | `all-concepts.ini` and three apply cases |
| `preferences/` | a launcher's four preferences against the mod: what it reads, what the mod runs on, what the owner's `Save` writes | per case, `case.tsv` of `binds`, `preference` and `change` rows, `input.ini`, and `expected.ini` for a case with a change |
| `mutations/` | the differential corpus generator | per case `input.ini`, `keys.tsv` and `expected.tsv` of output names and SHA-256 hashes |
| `example/` | the examples in this document | `HeadTracking.ini` |

The TSV files share one shape: ASCII, one row per LF-terminated line, fields separated by one tab,
and a line that is empty or starts with `#` is a note. Section, key and value fields use one byte
escape: a byte from 0x20 to 0x7E other than `\` stands for itself, `\` is `\\`, and every other
byte is `\x` and two upper-case hex digits (`\x09` for a tab, `\xE9` for a cp1252 `é`). A port
parses the rows, runs its own implementation, renders its result as the same rows and compares
the lists exactly.

## For a launcher or another tool that edits the file

The owner is not the only program that may edit a canonical file. A launcher or any other tool
that edits one follows the owner's rules:

- Edit only `CameraUnlock.ini`, never the legacy file, and only when it carries the stamp and a
  `ConfigFormat` the tool implements. The legacy file is what an older build reads, and the mod
  imports it only while `CameraUnlock.ini` is absent.
- Change values with the editor's rules and nothing else: every other byte is kept, a replaced
  line keeps its key's spelling and the white space around `=`, a repeated key has its last
  occurrence replaced, a missing key goes after the last key line of its section, a missing
  section at the end, and new lines take the file's most common line ending. The editor refuses a
  UTF-16 file and one holding a NUL byte. `editor/` pins all of it.
- Read with the reader's rules (`reader/`) and read the edited bytes back before writing them.
- Expect no protection from the running mod. The owner's lock covers one process, and its check
  that the file still holds the bytes it read is a separate operation from its replacement, so a
  tool's edit that lands between the two is overwritten by the mod's save. Edit the file while the
  game is not running, or accept losing that edit.
- Take section and key names from `data/config-schema.json`. A hotkey value written by a tool
  must be one the mod's dialect reads.

### The config descriptor

A package tells a launcher where its canonical file is, and which of the launcher's preference
rows the mod binds, with a top-level `config` block in `launcher-manifest.json`:

```text
"config": {
  "path": "BepInEx/config/CameraUnlock.ini",
  "anchor": "game_root",
  "legacy_source": "BepInEx/config/com.cameraunlock.valheim.headtracking.cfg",
  "canonical_since": "1.1.0",
  "rows": {
    "EnableOnStartup": true,
    "WorldSpaceYaw": true,
    "RotationEnabled": true,
    "PositionEnabled": true,
    "TrueFreeLook": false
  }
}
```

The paths and version above show the shape; each repo's come from its own entry in
`data/config-format.json` and its own history.

- `path` is the file, relative to `anchor`, with `/` between segments. The file is named
  `CameraUnlock.ini`.
- `anchor` is `game_root` (the default when absent), `exe_dir` or `mod_home`, as for a seed.
- `legacy_source` is the legacy file the import reads, in the folder of `path`, present exactly
  where `data/config-format.json` records one.
- `canonical_since` is the first version of the mod that shipped the canonical file, present
  exactly when the repo is in `legacy`. A launcher can warn before installing an older version.
- `rows` maps `data/config-schema.json` concept ids to the committed file's values. The ids a
  launcher reads are `EnableOnStartup`, `WorldSpaceYaw`, `RotationEnabled`, `PositionEnabled`
  and `TrueFreeLook`.

There is no format field: the file's `[CameraUnlock] ConfigFormat` names the dialect. A package
with more than one config file carries no block, and no variant carries one.

`scripts/check-config-descriptor.mjs` holds every rule, and `pixi run validate-manifest` runs them
on the built ZIP against the repo it was built from:

- The block has those five fields and no other; `path` and `legacy_source` are relative, with no
  `\`, drive, root, empty, `.` or `..` segment; `anchor` is one of the three.
- `path` names `CameraUnlock.ini`, the fleet's one config name. No `v*` release from before the
  canonical format reads a file of that name, so a launcher that manages it leaves alone the file
  an older version of the mod reads after a rollback.
- `rows` names only the five concepts, each `true` or `false`; `RotationEnabled` only beside
  `PositionEnabled`; and the two of them together are a pair `preference_modes` in
  `data/pipeline-conformance.json` lists.
- `canonical_since` is written `x.y.z` and is not above `mod_info.version`. A pre-release sorts
  below its release, so a `1.1.0-rc1` build cannot carry `canonical_since` `1.1.0`.
- `delivery_mode` is `manifest` or `manifest_variants`.
- The repo is converted and `data/config-format.json` records one config file for it. `game_root`
  needs exactly one `installed` path, and `path` is it; `exe_dir` needs `path` to be the tail of
  every `installed` path, and `path` beside each executable `data/games.json` records for the game
  to be one of them, which is the anchor for a file with one path per store layout; `mod_home` is
  for a file with no `installed` path. `legacy_source` is the folder of `path` and the name the
  entry records.
- The manifest seeds no config and ships none through `files[]`. That rule holds for every
  converted repo, with a block or without one (Launcher seeds, under "Install, uninstall and
  manual packages").
- `rows` holds every one of the five concepts the committed file has as a line, with the
  committed value (the renderer writes `true` or `false`), and no other. Two exceptions. When the
  committed file has `PositionAllowed=false`, `rows` has neither `RotationEnabled` nor
  `PositionEnabled`. And a row `data/config-format.json` `descriptor_omits` lists for the repo is
  left out, so a launcher never sets it: that list holds `WorldSpaceYaw` alone, for a game whose
  default differs from the fleet's on purpose because it has no stable up (Subnautica, where the
  player swims), each with its reason and the date the owner approved it. An omission has to be
  listed there, so a row dropped by accident still fails, and a committed `WorldSpaceYaw` away
  from the `data/config-schema.json` default fails unless it is listed, so a game that differs is
  not handed to a launcher's global.

`path`, `anchor`, `legacy_source` and `canonical_since` are written by hand at the conversion,
with `"rows": {}`. `scripts/encode-seed.mjs`, which `render-config` runs, then writes `rows` from
the committed file and changes no other byte of the manifest; `--check` exits 1 when `rows` is
stale.

No rule compares the block with what the mod's code reads: the rules compare it with
`data/config-format.json`, which records `CameraUnlock.ini`. A repo whose build still reads its
legacy file moves its owner to `CameraUnlock.ini`, with the legacy file as the owner's legacy
path, before it adds the block or re-renders its README config block, or in the same change.
Either one written first passes every check and describes a file the build does not read.

Conformance's `config-descriptor` check runs the same rules on the committed manifest, except
the one against `mod_info.version`, which packaging stamps. It also fails a converted repo
delivered by manifest whose one config file `data/config-format.json` records as stamped, and
which has no block (a stamped file the entry does not record is config-format's finding), and, in
a clone with its tags, a `canonical_since` that is not above every `v*` tag whose committed config
carries no stamp. A shallow clone has no tags, and the check warns that it did not run.

Packaging stamps `mod_info.version` by reading the manifest with `ConvertFrom-Json` and writing it
with `ConvertTo-Json -Depth 10`; `pixi run test-config-descriptor` runs that round trip over a
block and compares the result. `Copy-SharedBundle` runs the same rules on the committed manifest
through `Assert-LauncherManifestConfig` whenever it has a block, so a package script that never
calls validate-manifest still refuses stale `rows`; that needs `node` on `PATH`, as
`render-config` does.

## Tooling

In core:

| Command | What it does |
|---------|--------------|
| `pixi run check-config-schema` | Fails when the C++ and C# files generated from `data/config-schema.json` and `data/keys.json` are stale. `node scripts/generate-config-schema.mjs` regenerates them |
| `pixi run check-config-format` | Checks the shape of `data/config-format.json` and pins its `legacy` names |
| `pixi run check-canonical-ini-js` | Runs the reader and key fixtures through core's script grammar (`scripts/lib/canonical-ini.mjs`, `scripts/lib/key-bindings.mjs`) and holds the lint to its rules |
| `pixi run check-doc-examples` | Fails when a C++ or C# block in `docs/` is not a run of lines of the test it names, or an ini block is not the fixture it names |
| `pixi run test-encode-seed` | Runs encode-seed against copies of real manifests |
| `pixi run test-config-descriptor` | Checks that a good config descriptor passes and one mutation per rule fails, and runs encode-seed's `rows`, validate-manifest and conformance on it |
| `pixi run config-report` | The fleet report: game-local keys three or more canonical repos share, local section names in use, and concept values in committed files that differ from the schema default |

In a mod repo, and in conformance:

- **`render-config`**, the pixi task from `scripts/templates/render-config-task-cpp.toml` or
  `render-config-task-csharp.toml`, rewrites the committed file from the table (the C++ test
  binary's `--render-config <path>` mode, or the C# render test with
  `CAMERAUNLOCK_RENDER_CONFIG=write`) and then runs encode-seed.
- **`scripts/encode-seed.mjs`** rewrites the `content_b64` of every `launcher-manifest.json` seed
  that writes the repo's config from the committed file's bytes, and the config descriptor's
  `rows` from its values, leaving every other byte of the manifest as it was. `--check` exits 1
  when a seed or `rows` is stale.
- **`node scripts/check-canonical-config.mjs [repo ...]`** lints each stamped committed file: the
  reader finds nothing to report; CRLF endings, no byte order mark, ASCII only; `Key=value` and
  `[Name]` written plainly, each section once; `[CameraUnlock]` holding `ConfigFormat=1` alone;
  every concept at the schema's section and key, spelled as the schema spells it and not as an
  alias; no non-canonical or retired concept, no spelling `non_canonical_keys` lists, and no
  `[Sensitivity]`, `[Inversion]`, `[Reticle]` or `[Deadzone]` section, and no key in one of the
  sections `non_canonical_keys` lists; a schema section spelled as the schema spells it; local sections and keys PascalCase,
  each local key used once in the file, none of the bare nouns above and none starting with
  `Chord`; every hotkey concept, and
  every key in `[Hotkeys]`, a key list in the file's dialect, with the canonical hotkey concepts at
  their `canonical_default` unless `hotkey_exceptions` records the repo's reason; the file tracked
  by git and `-text`. It does not check comments.
- **Conformance** (`pixi run conformance`) runs the lint as `config-format`, which also fails a
  converted `legacy` repo with no legacy folder (an REFramework repo needs none: its import is
  core's `PluginConfigLegacyImport`), a repo outside `legacy` with one, and a repo outside
  `legacy` and `exempt` whose committed file is missing, unrecorded or unstamped.
  `config-legacy-reader` fails a converted repo whose source outside its legacy folder uses
  `GetPrivateProfile*`, `WritePrivateProfile*`, `IniReader`, `IniWriter`, `ParseIniConfig`,
  `ParseIniFile` or BepInEx's `ConfigFile.Bind`, unless `allow_legacy_symbols` records a use that
  reads no config. `config-preserve` fails `CameraUnlock.ini`, the legacy file or the committed
  file's name in `MOD_DLLS` or `MOD_SEED_FILES`, and an installed path or the legacy file beside
  one missing from `PRESERVE_FILES`. `readme` fails a converted repo whose README config block
  is missing or differs from the rendered one, and an unconverted repo that has one.
  `config-descriptor` holds the committed manifest to the config descriptor's rules (see
  "The config descriptor") and fails a converted repo's manifest that seeds or ships its config.
- **The README config block** sits between `<!-- cameraunlock:config -->` and
  `<!-- /cameraunlock:config -->` in the Configuration section. From core's own checkout,
  `pixi run readme --write <repo>` inserts and updates it, and `pixi run readme --print config
  <repo>` prints it for NEXUS_MODS.md, where `<repo>` names a sibling checkout. With no repo name,
  `scripts/generate-readme.mjs` works on the folder above core, which is the mod when it runs from
  the mod's `cameraunlock-core` submodule.
- **`pixi run validate-manifest`**, in a converted repo, also fails when the newest
  `release/*-nexus.zip` carries a file at an `installed` path of the config or at the legacy file
  beside one, or at the tail of either.
  A ZIP whose manifest carries a config descriptor is held to its rules.

## Changing the format

- **Adding** a concept, a key name, an alias or a local row is safe and never changes
  `ConfigFormat`. A new concept goes into `data/config-schema.json` with a field on both
  `HeadTrackingConfigData` and `HeadTrackingConfig` and a binding in both `HeadTrackingConfigTable`s
  in the same change: `HeadTrackingConfigTable` throws for a canonical concept it has no binding
  for, and the tests that build it with every concept fail. The concept reaches a mod's file only
  when that mod names it.
- **Removing** a concept or an alias, or moving an alias between concepts, changes what files on
  players' disks mean. Changing a default in core's types is breaking too: it changes every new
  file, and every file that leaves the key out.
- **A changed grammar or codec** that an older reader would misread needs a new `ConfigFormat`
  and a conversion from the old one, in core, for every mod at once. Nothing like that exists yet.
- The frozen code (`ini_reader`, `value_guards`, `PluginConfig::Read` and every repo's legacy
  folder) does not change: each import reads an old file as its published build did.
