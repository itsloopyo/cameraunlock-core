# Canonical INI fixtures

Byte fixtures for the canonical INI format: `reader/` for the reader, `editor/` for the
editor, `keys/` for the hotkey binding codec, `codecs/` for the value codecs, `table/` for
config tables, `head-tracking/` for core's table over its own config types, `global/` for
Defaults.ini and where it is, `preferences/` for the owner's save of the four preferences, `mutations/` for
the differential corpus generator and `example/` for the examples in docs/canonical-config.md. Core's C++ suite runs them unchanged
(`cpp/tests/canonical_ini_tests.cpp`, `cpp/tests/ini_editor_tests.cpp`,
`cpp/tests/key_bindings_tests.cpp`, `cpp/tests/value_codecs_tests.cpp`,
`cpp/tests/config_table_tests.cpp`, `cpp/tests/head_tracking_config_table_tests.cpp`,
`cpp/tests/defaults_ini_tests.cpp`, `cpp/tests/defaults_location_tests.cpp`,
`cpp/tests/preferences_fixture_tests.cpp` and
`cpp/tests/ini_mutations_tests.cpp`), and so does its C# suite (`CanonicalIniFixtures`,
`IniEditorFixtures`, `KeyBindingFixtures`, `ValueCodecFixtures`, `ConfigTableFixtures`,
`HeadTrackingConfigTableFixtures`, `DefaultsIniFixtures`, `DefaultsLocationFixtures`,
`PreferencesFixtures` and `IniMutationFixtures`, under xunit on
net8.0 and in
`CameraUnlock.Core.FrameworkTests` on .NET Framework 3.5 and 4.7.2). The expected files are
written by hand from the rules, never produced by an implementation, except under
`mutations/`, whose hashes come from a third implementation of its rules, in Python, that
neither language shares code with.

Lopari's Rust codec is to run the same files. Of the `editor/` cases, those whose `case.tsv`
holds a `set_first` or `set_or_insert_first` directive exist for REFramework's
`ApplyIniEdits` (`cpp/src/reframework/plugin_config.cpp`), which edits a file that
GetPrivateProfileStringA reads, and that reader takes the first occurrence of a repeated
key. A port whose editor implements only the canonical occurrence rule (an edit replaces
the last occurrence of a repeated key, and an absent key goes into the last block of a
section whose header repeats) runs every other `editor/` case, and its reader runs every
`reader/` case. The port's runner skips those cases by directive, never by name, so a new
case without one is run as soon as it lands. An editor that also implements the
first-occurrence rule (`first_occurrence_wins` in C++, `FirstOccurrenceWins` in C#) is held
to every `editor/` case, as core's C++ and C# suites are.

Everything under `data/fixtures/` is marked `-text` in `.gitattributes`, so git keeps the
bytes as they are: the CRLF, LF and lone CR endings, the byte order marks, the NUL and
0x1A bytes and the missing final newlines are what the cases test.

## reader/

One directory per case, holding `input.ini` (the document's bytes) and `expected.tsv`
(what `ParseCanonicalIni` / `CanonicalIni.Parse` read from it, and what
`HasCanonicalStamp` / `CanonicalIni.HasStamp` say about it).

`expected.tsv` is ASCII, one row per line, each line ending in LF. A line that is empty or
starts with `#` is a note and is not compared. The other lines are rows whose fields are
separated by one tab, in this order:

| Row | Fields | Meaning |
|-----|--------|---------|
| `status` | `Readable`, `Utf16`, or `NulByte` and the 1-based line of the first NUL | The read status |
| `format` | an integer | The format version; 0 when the document is unreadable |
| `stamp` | `true` or `false` | Whether the document carries the `[CameraUnlock]` stamp |
| `key` | section, key, value, line, earlier lines | One key kept in a section: its last occurrence |
| `diagnostic` | kind, lines | One diagnostic |

`status`, `format` and `stamp` come first, once each. Then one `key` row for every key
the reader kept, in ascending order of its line, then one `diagnostic` row for every
diagnostic in the order the reader returns them (by first line, then by kind number).
An unreadable document has no `key` or `diagnostic` rows. A runner renders its own result
as these rows and compares the lists exactly.

In a `key` row the section is spelled as its first header spells it and the key as its
kept line spells it. Lines are 1-based. A list of lines is written comma-separated with no
spaces, and an empty list as `-`. Diagnostic kinds are the `CanonicalDiagnosticKind`
enumerator names (`DuplicateKey`, `KeyOutsideSection` and so on).

Section, key and value fields are bytes, written with one escape:

- a byte from 0x20 to 0x7E, other than `\`, stands for itself;
- `\` is written `\\`;
- every other byte is written `\x` and two upper-case hexadecimal digits, so a tab is
  `\x09`, 0x1A is `\x1A` and a cp1252 `é` is `\xE9`.

An empty value is an empty field.

## editor/

One directory per case, holding `case.tsv`, usually `input.ini` (the document's bytes;
when it is absent the editor is handed no bytes, as for a file that does not exist) and,
for a case that succeeds, `expected.ini` (the edited document, byte for byte). The C++
suite runs them through `EditIni` and the C# suites through `IniEditor.Edit`.

`case.tsv` is ASCII with the same note rule as `expected.tsv`. Each other line is a
directive and its fields, separated by one tab. Section, key and value fields use the byte
escape above; each is the UTF-8 text of an argument, which the C# runners decode into a
string. An empty value is an empty last field.

| Directive | Fields | Meaning |
|-----------|--------|---------|
| `set` | section, key, value | Replace the key's value; `KeyNotFound` when it is absent |
| `set_or_insert` | section, key, value | Replace it, or insert it when absent |
| `set_first` | section, key, value | `set`, with the first occurrence winning |
| `set_or_insert_first` | section, key, value | `set_or_insert`, with the first occurrence winning |
| `rejects` | section, key, value | This edit alone, with insertion, must throw against `input.ini` |
| `refused` | refusal, section, key, lines | The whole batch is refused, naming these |

The `set` directives, in file order, are one batch. With no `refused` row the batch must
produce `expected.ini`; with one, it must produce no bytes and the `IniEditRefusal` named
(`Utf16`, `NulByte` or `KeyNotFound`), with the refused edit's section and key as given in
the batch (empty for a refusal of the whole document) and its lines (`-` for none).

A runner also reads each successful case back through the canonical reader, before and
after. Every key the batch did not edit reads as it did, and every line the batch did not
replace or insert is the same bytes as before. Without a `_first` directive, each edited key
reads its new value, and the diagnostics differ only on edited lines. With one, the line
holding each edited key's new value is the one the batch changed.

## keys/

`cases.tsv` holds hotkey values and what the binding codec reads from them. The key names
come from `data/keys.json`. It is ASCII with the same note rule as `expected.tsv`, and each
other line is a row whose fields are separated by one tab:

| Field | Meaning |
|-------|---------|
| dialect | `native` for the C++ `ParseKeyBindings`, which reads names with a Windows virtual-key code and `0x` codes; `unity` for the C# `KeyBindings.TryParse`, which reads names with a Unity `KeyCode` value and no codes |
| input | the value, with the byte escape above; the C# runner decodes it as UTF-8 |
| result | `canonical` or `invalid` |
| canonical | only after `canonical`: the text the codec writes for what it read, with the byte escape; empty for an empty list |

The C++ suite runs the `native` rows and the C# suites the `unity` rows. For a `canonical`
row the codec must read the input, write exactly the canonical text, and read that text
back as the same bindings. For an `invalid` row it must refuse the input, with an error and
no bindings.

The syntax both dialects share: a value that is empty after trimming spaces and tabs is an
empty list, which means unbound. Otherwise it is split at `,` into items, each trimmed and
non-empty. An item is split at `+` into trimmed tokens: any of `Ctrl`, `Shift` and `Alt`,
each at most once and in any order, then exactly one key. Names and modifiers read ASCII
case-insensitively; nothing else is folded. The same binding twice in one list is invalid.
The canonical text writes the modifiers as `Ctrl+Shift+Alt+` in that order, the key as
`data/keys.json` spells it (an alias as the key's name), a native code with no name as `0x`
and upper-case hex without padding, and joins the items with `, `.

## codecs/

`cases.tsv` holds values and what each value codec reads from them and writes back. It is
ASCII with the same note rule as `expected.tsv`, and each other line is a row whose fields
are separated by one tab:

| Field | Meaning |
|-------|---------|
| codec | one of the names below |
| input | the value as the reader hands it over, already trimmed, with the byte escape above |
| result | `canonical` or `invalid` |
| canonical | only after `canonical`: the text the codec writes for what it read, with the byte escape; empty for an empty string or list |
| bits | only for `float`, `double` and `float[0,1]`: the value read, as `0x` and 8 or 16 upper-case hex digits of its IEEE 754 bits |

For a `canonical` row the codec must read the input, write exactly the canonical text, read
that text back as an equal value (floats bit for bit) and write it again unchanged. For an
`invalid` row it must refuse the input with an error, which names what was expected.

| Codec | What it is |
|-------|------------|
| `bool` | writes `true` or `false`; reads `true false 1 0 yes no on off`, ASCII case-insensitive |
| `int` | a 32-bit signed whole number: writes decimal, `-` only when negative, no leading zeros; reads `-?[0-9]+` |
| `int[1,65535]` | `int` within 1 to 65535 |
| `hex32`, `hex64` | writes `0x` and upper-case digits without padding; reads `0x` or `0X` and 1 to 8, or 1 to 16, digits of either case |
| `float`, `double` | IEEE 754 binary32 and binary64, every finite value; see below |
| `float[0,1]` | `float` within 0 to 1 |
| `string` | the bytes as they are; refuses a byte below 0x20 other than tab |
| `enum` | the tokens `Never`, `MenusOnly`, `AllDialogue` and `AllOverlays`: read ASCII case-insensitively, written in that spelling |
| `color` | four `float[0,1]` separated by commas, each trimmed; written `r, g, b, a` |
| `list<hex32>`, `list<hex64>`, `list<string>` | an empty value is an empty list; otherwise items split at `,`, each trimmed of spaces and tabs, non-empty and read by the item codec; written joined by `, ` |

Floats. The text written is chosen among `%.Ng` in the C locale, N from 1 to 9 for `float`
and 1 to 17 for `double`, keeping only the texts that read back to the same bits: the one
with the smallest N that has no exponent, or, when every one has an exponent, the one with
the smallest N. `.0` is appended when the text has neither `.` nor `e`. So 1 is `1.0`, 10 is
`10.0` rather than `1e+01`, 0.15 is `0.15`, 0.00001 is `1e-05` and -0 is `-0.0`. A value is
read with correct rounding, ties to even, from the grammar
`-?[0-9]+(\.[0-9]+)?([eE][+-]?[0-9]+)?`; a number too large for the type, or one that is not
zero but rounds to zero, is invalid. .NET Framework's `float.Parse` and `double.Parse` are
not correctly rounded (it reads `1.00000005960464477539062500001` as the float 1.0, where the
rule gives 1.0000001), and no row holds a text it reads wrongly.

The rows are hand-written, and every float and double row was also checked against an
independent reference that parses with exact rational arithmetic and formats with Python's
`%g`. The C# string codec reads strict UTF-8 where the C++ one keeps any bytes, so the
fixtures hold no string that is not UTF-8. Three values are kept out because .NET Framework
disagrees with the rules there: it writes the float 1234.5677490234375 as `1234.5678` (the
rule gives `1234.5677`) and the float 3451485.25 as `3451485.3` (the rule gives `3451485.2`,
the tie rounded to even), and on .NET Framework 3.5 it reads `3e-324` as 0 (the rule gives
the smallest denormal). Each language's own tests hold them.

## table/

One fixture table and the config it binds, defined identically in both suites, and one
directory per case. The table is what a game declares; the cases hold `ApplyCanonical` /
`ConfigTable.Apply` and `RenderCanonical` / `ConfigTable.Render` to it. A port runs them by
declaring the same table.

The fixture config's fields, in table order, with the row each is bound to and its default.
The C++ suite binds `UdpPort` to a `std::uint16_t` and `LocalSmoothing` to a `double` reached
through a getter and a setter; the C# suite binds `int` and `float`. Both write and read the
same text.

| Field | Row | Codec | Default | Modifiers |
|-------|-----|-------|---------|-----------|
| ToggleKey | concept ToggleKey | hotkey | `End, Ctrl+Shift+Y` | Writable |
| UdpPort | concept UdpPort | int, 1 to 65535 | `4242` | |
| PositionEnabled | concept PositionEnabled | bool | `true` | Writable |
| RotationEnabled | concept RotationEnabled | bool | `true` | Writable |
| EnableOnStartup | concept EnableOnStartup | bool | `true` | |
| LocalSmoothing | concept LocalSmoothing | float, 0 to 1 | `0.0` | |
| PositionLimitX | concept PositionLimitX | float, 0 to 10 | `0.3` | Comment: `How far, in metres, leaning sideways moves the view.` and `The fixture's own wording.` |
| CollisionChannel | concept CollisionChannel | int | `3` | none: the concept is not global, so the row is an Engine row whatever the table marks |
| CycleTrackingModeKey | concept CycleTrackingModeKey | hotkey | `PageUp, Ctrl+Shift+G` | |
| Mode | local [Camera] Mode | enum `ControlRotation`, `UpdateCamera` | `UpdateCamera` | comment `ControlRotation or UpdateCamera (decoupled).` |
| LeanDelayMs | local [Position] LeanDelayMs | int | `50` | comment `Milliseconds before a lean starts, and the metres its wall trace reaches.`; Range(0, 1000) |
| LeanTraceLength | local [Position] LeanTraceLength | float | `1.0` | no comment; Range(0, 2) |
| NearClip | local [Camera] NearClip | double | `0.1` | comment `Near clip distance, in the game's units.` |
| UpdateCameraSlot | local [Camera] UpdateCameraSlot | int | `196` | comment `Engine values. The commented lines show the built-in values.` and `Delete the ; to pin your own.`; Engine |
| PovOffset | local [Camera] PovOffset | hex32 | `0x404` | no comment; Engine |
| CleanCameraReader | local [Camera] CleanCameraReader | hex64 | `0x1402A0B10` | no comment; Engine |
| HookOffsets | local [Camera] HookOffsets | list of hex32 | `0x10, 0x2A` | comment `Offsets the camera hook patches.` |
| AimCallers | local [Camera] AimCallers | list of hex64 | empty | comment `Return addresses whose aim is left alone. Empty for none.` |
| WidgetNames | local [Camera] WidgetNames | list of string | `Crosshair, Compass` | comment `Widgets that follow the head.` |
| MarkerColor | local [Camera] MarkerColor | color | `1.0, 0.5, 0.0, 1.0` | comment `Marker colour: red, green, blue and opacity, each 0 to 1.` |
| LogPath | local [Logging] LogPath | string | `HeadTracking.log` | comment `Log file, beside the game's executable.` |
| WriteLog | local [Logging] WriteLog | bool | `false` | comment `true: write the log.`; Engine |
| ReloadKey | local [Logging] ReloadKey | hotkey | `F10` | comment `Reads this file again.` |

A `\n` in a comment above separates its lines. The render header's display name is
`Fixture Game`. Every hotkey value in the cases reads the same in the native and the Unity
dialect, so the C++ suite's native `HotkeyCodec` and the C# suite's Unity one agree on each.

`CollisionChannel` is the table's one concept row that does not follow Defaults.ini: the schema
marks it `"global": false`, which makes it an Engine row whose default is the table's own. Every
other concept row follows Defaults.ini, so the table passes the fresh render's gate, its renders
carry the six header lines on `default`, and the cases pin that such a row keeps the commented
form at its default.

A case directory holds `input.ini`, or one or more of the render files below:

- `input.ini` and `expected.tsv`: the table applied to `input.ini` as the reader parses it,
  onto a config whose fields hold other values beforehand. `expected.tsv` has one `field` row
  per field in table order (name, and the field's value as its codec writes it) and one
  `diagnostic` row per diagnostic Apply returns, in its order (kind, lines comma-separated,
  and the sentence DescribeCanonicalDiagnostic / `Describe` gives). The reader's own
  diagnostics are not listed.
- `input.ini`, `effective.tsv` and `expected.tsv`: the same, through the Apply that takes
  effective defaults (C++ `detail::ApplyCanonicalEffective`, C# the internal
  `ConfigTable.Apply(doc, config, effective, fromDefaultsIni)`). `effective.tsv` has a `field`
  row, as in `values.tsv`, for each field whose effective default differs from the table's
  default, and a `defaults_ini` row naming each concept whose effective default Defaults.ini
  gave. `expected.tsv` then also has one `source` row per field in table order, after the
  `field` rows: the name and `file`, `defaults_ini` or `built_in`, where the row's value came
  from. Only these cases list sources.
- `values.tsv` and `expected.ini`: `values.tsv` has a `field` row for each field that differs
  from its default, the value as its codec writes it; the config holding them renders exactly
  as `expected.ini`.
- `fresh.ini`: the table's fresh render (C++ `RenderCanonicalFresh`, C# `RenderFresh`), which
  reads back through Apply as the defaults with no diagnostic.
- `values.tsv`, `effective.tsv` and `migration.ini`: the migration render (C++
  `detail::RenderCanonicalMigration`, C# the internal `ConfigTable.RenderMigration`) of the
  values over the effective defaults, which reads back through the effective Apply as the values
  with no diagnostic.

The TSV files are ASCII with the note rule above, fields separated by one tab, and values and
sentences in the byte escape. For every `input.ini` and `expected.ini` case a runner also renders
the config the case ends with, parses and applies that, and requires no diagnostic from the
reader or the table, the same field values, and the same bytes when rendered again. Each runner
also checks that a member of the config no row binds keeps its value through Apply.

The `global-` cases hold the `default` token, effective defaults and the fresh and migration
renders:

| Case | What it holds |
|------|---------------|
| `global-default` | `default` on an int, a bool, a float and two hotkey rows reads each row's effective default: Defaults.ini's where it gave one (`UdpPort=5000`, `EnableOnStartup=false`, `LocalSmoothing=0.5`, `ToggleKey=F8`), the table's for `CycleTrackingModeKey`. `PositionLimitX=0.25` in the file wins over Defaults.ini's 0.35 |
| `global-default-case` | `Default`, `DEFAULT` and `default` with spaces the reader trims are the token |
| `global-default-not-token` | `"default"` and `default ; note` go to the codec, which refuses them: the effective default and an InvalidValue |
| `global-missing-invalid` | a missing key and an invalid value both read the effective default; the invalid value also draws InvalidValue |
| `global-per-game` | `default` on `CollisionChannel`, which is not global, reads the table's own 3 though the effective defaults hold 7; on `PositionLimitX` it reads Defaults.ini's 0.35 |
| `global-local` | on a local row the word is data: `LogPath=default` stores it, `WriteLog=default` is an InvalidValue |
| `global-pair-one-default` | `RotationEnabled=default` reads the effective false beside `PositionEnabled=false`: no tracking mode, so both take the effective pair, rotation off and position on, and NoTrackingMode names only the line that set a value |
| `global-pair-off` | both false, with an effective pair of rotation off and position on: both take the effective pair |
| `global-render-fresh` | the fresh render (`fresh.ini`: every concept row but `CollisionChannel` is `default`, and `CollisionChannel` is commented at 3) and a values render of the same table (`values.tsv` and `expected.ini`) |
| `global-migration` | the migration render: rows equal to their effective default are `default`, the others values; `RotationEnabled` equals its effective default and `PositionEnabled` does not, so both are values; `CollisionChannel` and the local rows are written as Render writes them |
| `global-migration-pair` | imported values equal to the effective defaults, the pair included, migrate to the bytes of `fresh.ini` |

## head-tracking/

Core's `HeadTrackingConfigTable` naming every canonical concept, over C++ `HeadTrackingConfig`
and C# `HeadTrackingConfigData`. The render header's display name is `Fixture Game`.

- `all-concepts.ini`: the table's defaults instance rendered. It holds every canonical concept at
  its default, the four hotkey lists and `CollisionEnabled` at their `canonical_default`, so it
  pins each concept's default rendering in both languages. `CollisionMargin` and
  `CollisionChannel` are not global, so they are Engine rows, the commented lines
  `; CollisionMargin=0.1` and `; CollisionChannel=0`.
- `all-concepts-fresh.ini`: the same table's fresh render, every global concept `Key=default` and
  the two Engine rows commented as above. It proves core's own table passes the fresh render's
  gate: every global concept defaults to the schema's `default` (its `canonical_default` where it
  has one), and the table binds `PositionEnabled` beside `RotationEnabled`.
- `apply-values/`, `apply-position-off/`, `apply-empty/`: `input.ini` and `expected.tsv`, whose
  rows are `field`, a name and the value as the concept's codec writes it. The names are the 28
  concepts in the schema's order, then `PositionLocalSmoothing` and `PositionRemoteSmoothing`,
  the copy of the smoothing pair the position settings carry (C++ `position.local_smoothing`,
  C# `Position.LocalSmoothing`). A runner reads each field straight off the config, not through
  the table.

A runner applies each `input.ini` twice, onto a new config and onto the config `apply-values`
produces, and requires `expected.tsv` both times with no diagnostic from the reader or the table.
Before applying, it sets fields no row binds (the recenter key, the position X sensitivity and
the position Y inversion) and requires them unchanged afterwards. It also renders the result,
reads that back and requires the same fields and bytes. `apply-empty` is the defaults, and across
the three cases every field is off its default at least once.

## global/

Defaults.ini, the file a game's global concept rows take their default from when they are not
marked `PerGame()`. Core renders a new one and reads one with the internal C++
`detail::RenderDefaultsIni` and `detail::ReadDefaultsIni` (`cameraunlock/config/defaults_ini.h`)
and C# `DefaultsIni.Render` and `DefaultsIni.Read`, which the config owners use to create and read
the file on disk (docs/canonical-config.md, "The global defaults file").

`Defaults.ini` is what core writes as a new Defaults.ini: core's global table,
`HeadTrackingConfigTable` naming every global concept, at its defaults, with the four hotkey
lists and `CollisionEnabled` at their `canonical_default`. Every row is written as its value. The
rows, sections and comments are those of `head-tracking/all-concepts.ini` without
`CollisionMargin` and `CollisionChannel`, which are not global and have no line here. The header
is its own, and carries none of the six game-file lines on `default`: four lines saying what the
file is and who reads it, the comments line, the hotkeys line, and six lines listing the key names
the file takes, the 104 names in `data/keys.json` that have a `vk`, with `A to Z`,
`Alpha0 to Alpha9`, `F1 to F24` and `Keypad0 to Keypad9` standing for their ranges. A runner
renders the table and requires these bytes, reads them back and requires every global concept
accepted at the table's default, and the two that are not global absent, with nothing else to
say, and requires the header's key list, ranges written out, to be exactly the names in
`data/keys.json` with a `vk`.

Each `read-*` directory holds `input.ini`, Defaults.ini's bytes, and `expected.tsv`, what the
reader makes of them, in the TSV shape and byte escape above:

| Row | Fields | Meaning |
|-----|--------|---------|
| `unreadable` | reason | The file is not read: `it is saved as UTF-16; save it as ANSI or UTF-8`, or `line N holds a NUL byte`. It is the only row |
| `format` | line | The one line for a ConfigFormat above this build's |
| `value` | concept, `accepted`, line, value | A concept whose value passed every check |
| `value` | concept, `refused`, line, section, key, value, reason | A concept whose value failed a check |
| `line` | concept, text | The log line for a refused value, except the tracking-mode pair's |
| `pair` | text | The one log line for a refused tracking-mode pair |

The rows come in that order: `format` when there is one, then a `value` row for each concept that
is not absent in the schema's concepts order, then a `line` row for each refused concept in the
same order, then `pair`. A concept with no `value` row is absent. The section is spelled as its
first header spells it, the key as its kept line spells it, and the value is its bytes. The
built-in value a `line` or `pair` row names is the global table's, as its codec writes it, which a
runner takes from reading `Defaults.ini`'s render. A runner reads `input.ini`, renders its own rows
and compares the lists exactly.

The rules the cases hold:

- **Unreadable.** A file starting with a UTF-16 byte order mark, or holding a NUL, is not read at
  all, whatever lines come before the NUL.
- **The stamp.** The file is read whether or not it has `[CameraUnlock]`, and a `ConfigFormat`
  that is missing, zero or not a number draws nothing. One above the build's draws
  `Defaults.ini: line N: ConfigFormat=V was written by a newer version of the mod. This version reads format 1.`,
  with the key and value as the file spells them, and the file is still read.
- **Which line.** A concept is its section and key, compared ASCII case-insensitively, and the last
  occurrence wins, across repeated headers too. An alias, the key in another section, and a key or
  section no concept has are not read, and draw nothing.
- **A value** is refused when the concept's codec, with the schema's range, does not read it
  (`codecs/`), and the reason is the codec's. `default`, in any letter case, is refused the same
  way.
- **A hotkey value** is first split at `,` into items. The key of an item is its text after the
  last `+`, trimmed of spaces and tabs. The first item whose key is not empty, not `Ctrl`, `Shift`
  or `Alt`, and not one of the 104 names with a `vk` (ASCII case-insensitively; an alias does not
  count) refuses the value with `<key> is not one of the key names this file takes`, the key as
  the file spells it, so `Mouse4` and `0x23` are refused alike. A value that passes goes to the
  codec, whose grammar and reasons both dialects share for these names (`keys/`).
- **The tracking-mode pair.** Each of `RotationEnabled` and `PositionEnabled` is its accepted value,
  or the schema's default, `true`, when absent. When either is refused, the reason is
  `Key: reason` for each refused row, `RotationEnabled` first, joined by `; `, then
  `, and the two are read together as the tracking mode`. Otherwise, when the pair is not a mode
  `preference_modes` in data/pipeline-conformance.json lists (false/false), the reason is
  `both false is not a tracking mode`. Either way each of the two that is not absent becomes
  `refused` with that reason, and the pair has one `pair` row and no `line` rows.

The line texts:

- A refused value: `Defaults.ini: line N: [Section] Key=value is not read (reason), so the built-in B is used.`
- A refused pair with one row in the file:
  `Defaults.ini: line N: [Section] Key=value is not read (reason), so the built-in RotationEnabled=R and PositionEnabled=P are used.`
- With both, in line order:
  `Defaults.ini: lines N and M: [S1] K1=v1 and [S2] K2=v2 are not read (reason), so the built-in RotationEnabled=R and PositionEnabled=P are used.`

A reason and a line are UTF-8 whatever bytes the value holds, since an ANSI save is readable.
Where the value, or the part of it a reason quotes, is well-formed UTF-8 its bytes are kept.
Each maximal subpart of an ill-formed sequence, as Unicode defines it for U+FFFD substitution, is
written as U+FFFD (`EF BF BD`): a lone `A3` is one, the truncated `E9 A3` is one, and `F0 80 80`
is three, since `80` cannot follow `F0`. The `value` row's value field stays the file's bytes.

| Case | What it holds |
|------|---------------|
| `read-empty` | an empty file: every concept absent, nothing said |
| `read-ansi` | bytes that are not UTF-8 in a bool, two floats and hotkey values, reaching the value, a key-name reason and a codec reason: lone, truncated, overlong, surrogate and above-U+10FFFF sequences each written as U+FFFD by the rule above, beside well-formed 2, 3 and 4 byte sequences kept as they are |
| `read-utf16`, `read-nul` | the two unreadable files |
| `read-no-stamp`, `read-format-missing`, `read-format-zero`, `read-format-not-a-number` | read with no line |
| `read-format-newer` | `ConfigFormat=2`: the format line, and the file read |
| `read-refused-values` | codec and range refusals on int, bool and float rows and a value with `; note`, each with its line; one value beside them accepted; invalid `CollisionMargin` and `CollisionChannel` lines, not read at all since neither is global |
| `read-hotkey-mouse4`, `read-hotkey-code` | `ToggleKey=Mouse4` and `ToggleKey=0x23` on line 12, refused alike, with the line the design gives |
| `read-hotkeys` | names in any letter case with modifiers, an empty list, a grammar error with the codec's reason, and `Clear`, a Unity key with no `vk` |
| `read-default-token` | `DEFAULT`, `Default` and `default` on an int, a bool, a float and a hotkey row |
| `read-pair-both-false` | false/false: both refused, one line |
| `read-pair-one-invalid` | one row the codec refuses takes the valid one with it; the line names the rows in line order |
| `read-pair-both-invalid` | both reasons in one line |
| `read-pair-one-absent-one-false` | `PositionEnabled=false` alone is rotation only with the built-in `RotationEnabled`: accepted |
| `read-pair-one-absent-one-invalid` | the pair's line naming its one row |
| `read-repeated-key` | the last of three occurrences wins across a repeated section |
| `read-case` | section and key in other letter case: read, and the line spells them as the file does |
| `read-wrong-section` | concept keys in other sections, one of them invalid, beside a key in its own section: only that one is read |
| `read-alias` | aliases, one after its key in the same section: not read, nothing said; `CollisionMargin` and its alias `CollisionRadius`, not read since it is not global |
| `read-unknown-key` | an unknown section, a concept the format does not write and an unknown key: nothing said |

### resolve.tsv

Where Defaults.ini is: which places a game looks in, and what it does with them. Core finds it
with the internal C++ `detail::ResolveDefaults` and `detail::ChooseDefaults`
(`cameraunlock/config/defaults_location.h`) and C# `DefaultsLocation.Resolve` and
`DefaultsLocation.Choose`. Both are pure. The probes that fill the resolver's input and the
creation of the folder call the operating system and are not in the file; the conversions a probe
makes under Wine are given as inputs. The config owners run the probes, this resolver and this
choice at every `Load`.

`resolve.tsv` is ASCII with the note rule of `expected.tsv`, fields separated by one tab, and
every text field is the byte escape above of UTF-8 text. It holds cases one after another. A case
starts with a `case` row and runs to the next `case` row; inside it, a `choice` row starts one
choice over the case's candidates, which runs to the next `choice` or `case` row.

| Row | Fields | Meaning |
|-----|--------|---------|
| `case` | name | The case's name, once in the file |
| `input` | name, value | One input to the resolver, from the list below. An input with no row is empty |
| `unix` | text | Under Wine, the host folder's Unix text the probe converts. No row when there is none |
| `host` | reason | Under Wine, why there is no host candidate. No row when there is one, and off Wine |
| `candidate` | kind, create, path, shown | One candidate, in the order they are tried: `windows`, `wine_host`, `wine_prefix` or `native`; `yes` or `no` for whether it may be created; the Defaults.ini path; the path as the log shows it |
| `none` | reason | There is no candidate, and why |
| `choice` | | Starts a choice |
| `exists` | index | The file of the candidate with this 0-based index exists |
| `outcome` | index, kind, why | What became of creating that candidate: `created`, `appeared` (another program created it at the same time), `parent_missing`, or `folder_failed` or `file_failed` followed by why, in the owner's words |
| `read` | index or `-` | The candidate whose file is read |
| `create` | index or `-` | The candidate to create next; the caller creates it and chooses again with its outcome |
| `line` | text | The one log line. No row while `create` names a candidate |
| `message` | text | The in-game message. No row when there is none |

A case holds `case`, its `input` rows, then `unix`, `host`, and either the `candidate` rows or
`none`, then its choices. A choice holds `choice`, its `exists` and `outcome` rows, then `read`,
`create`, `line` and `message`. A runner takes the `case`, `input`, `exists` and `outcome` rows as
they are, renders every other row from its own resolver and choice, and compares the case's rows
exactly.

| Input | Value |
|-------|-------|
| `platform` | `windows`, `wine` or `native` |
| `known_folder` | The roaming AppData known folder (`FOLDERID_RoamingAppData`) |
| `package` | What `GetCurrentPackageFullName` returned for a zero length, in decimal. No row when kernel32 has no such function |
| `wine_version` | What `wine_get_version` returned |
| `host` | The system name `wine_get_host_version` gave. No row when ntdll has no such function |
| `WINEHOMEDIR`, `WINE_HOST_XDG_CONFIG_HOME`, `XDG_CONFIG_HOME`, `HOME` | The environment variables of those names |
| `codepage` | `failed`: the Unix text could not be turned into bytes in Wine's Unix code page |
| `dos_file_name` | What `wine_get_dos_file_name` returned for those bytes. No row when it returned NULL |

The rules the cases hold. A folder's parent is the folder up to its last separator.

- **Windows** reads `known_folder` and `package`. With no known folder there is no candidate,
  `Windows reported no roaming AppData folder`. Otherwise the one candidate is
  `<known_folder>\CameraUnlock\Defaults.ini`, shown `%AppData%\CameraUnlock\Defaults.ini`. It may be
  created unless `package` is there and is not 15700: any other answer, 122 included, is a
  packaged process.
- **Wine** reads everything but `package` and `HOME`. The host candidate comes first when there is
  one, then the prefix candidate, which is the Windows candidate for `known_folder`, may be
  created, and is left out when the known folder is empty. The host candidate's folder:
  - With no `host`: none, `Wine did not report the host system`.
  - With `host` `Darwin`: the DOS home, then `\Library\Application Support\CameraUnlock`.
  - With any other `host`, the first of `WINE_HOST_XDG_CONFIG_HOME` and `XDG_CONFIG_HOME` that
    starts with `/`, with the `/` at its end removed, then `/CameraUnlock`, is the Unix text, and
    `$<variable>/CameraUnlock` names it in a reason. With `codepage` failed there is none,
    `$<variable>/CameraUnlock could not be converted to the Unix code page`; with no
    `dos_file_name`, none, `Wine could not convert $<variable>/CameraUnlock to a Windows path`;
    with one that does not start with a drive letter, `:` and `\`, none,
    `$<variable>/CameraUnlock has no drive letter in this Wine prefix`; otherwise `dos_file_name`.
  - With neither variable starting with `/`: the DOS home, then `\.config\CameraUnlock`.
  - Where the DOS home is needed: with no `WINEHOMEDIR` there is none, `WINEHOMEDIR is not set`,
    and with no DOS home none, `the home folder has no drive letter in this Wine prefix`.

  The DOS home is `WINEHOMEDIR` after a leading `\??\`, with the `\` at its end removed, when
  that starts with a drive letter, `:` and `\`, and there is none otherwise (`\??\unix\...`).
  A host path is shown with its leading part replaced by `~` when that part equals the DOS home
  and the path ends there or goes on with `\`; any other host path is shown as it is. With no
  DOS home, a host folder from `dos_file_name` is shown with its parent replaced by
  `$<variable>`, as in `$XDG_CONFIG_HOME\CameraUnlock`. With no candidate at all the reason is
  `Windows reported no roaming AppData folder`.
- **Native** reads `XDG_CONFIG_HOME` and `HOME`, each counted only when it starts with `/` and
  taken with the `/` at its end removed. The first candidate is `<XDG_CONFIG_HOME>/CameraUnlock/Defaults.ini`,
  or `<HOME>/.config/CameraUnlock/Defaults.ini` when `XDG_CONFIG_HOME` does not count; the second
  is `<HOME>/Library/Application Support/CameraUnlock/Defaults.ini`. A candidate that needs `HOME`
  is left out when it does not count, and with no candidate the reason is
  `HOME is not set to an absolute path`. None may be created. A path is shown with its leading part
  replaced by `~` when that part equals `HOME` and the path ends there or goes on with `/`. When
  `HOME` does not count, the `XDG_CONFIG_HOME` candidate is shown with that folder replaced by
  `$XDG_CONFIG_HOME`. A `HOME` or `XDG_CONFIG_HOME` that is empty once the `/` at its end is
  removed replaces nothing.

The choice, where `Settings` stands for ` Settings set to default use the built-in values.`, a
candidate's name is its shown path with ` (this Wine prefix)` added for the prefix candidate, and
`<W>` is `Wine <wine_version> on <host>`, or `Wine <wine_version>` with no `host`:

- With no candidate: `Defaults.ini: no location: <reason>.`, then under Wine the host sentence
  below, then `Settings`.
- Otherwise, when a candidate's file exists, the first such is read. When a later one exists too,
  the line is `Defaults.ini: <name> is read, and <other name> is not.` and the message
  `Two Defaults.ini files: this game reads <name> and ignores <other name>.` Otherwise the line
  is the found line below with `read`.
- Otherwise natively: `Defaults.ini: no file at <shown>; on this system the mod reads Defaults.ini but does not create it.`
  and `Settings`, the shown paths joined by ` or `.
- Otherwise each candidate that may be created, in order: with no outcome it is the one to create;
  `created` reads it, with the found line and `created with the built-in values`; `appeared`
  reads it, with `created by another program at the same time, and read`; a failure moves on.
- When none is left, on Windows: for a candidate that may not be created,
  `Defaults.ini: not created, because this game runs as a packaged app (GetCurrentPackageFullName returned <package>); <shown> is created by the next game that is not packaged, or by Lopari.`,
  and otherwise `Defaults.ini: <failure>` and `Settings`. Under Wine: `Defaults.ini: `, the prefix
  candidate's failure with its name's ` (this Wine prefix)` after the shown folder or path, or
  `no location: Windows reported no roaming AppData folder.` with no prefix candidate, then the
  host sentence and `Settings`.

The found line is `Defaults.ini: <shown> (<what>)` on Windows and natively,
`Defaults.ini: <shown> (<W>, the host's config folder, <what>)` for the host candidate, and
`Defaults.ini: <shown> (<W>, this Wine prefix, <what>): the host's config folder <host folder> could not be used: <host why>.`
for the prefix candidate. The host sentence is
` The host's config folder <host folder> could not be used: <host why>.` The host folder is the
host candidate's shown folder, or `none`. The host why is the `host` reason with no host
candidate; with one, `<shown parent> does not exist` after `parent_missing`,
`it could not be created: <why>` after `folder_failed`, `Defaults.ini was not created there: <why>`
after `file_failed`, and `it holds no Defaults.ini, and one there would be shared by every Wine prefix`
when it was not tried. A failure is `<shown folder> was not created, because <shown parent> does not exist.`,
`<shown folder> could not be created: <why>.` or `<shown> was not created: <why>.`

The cases cover Windows with and without a known folder, packaged at 15700, 122, 87 and -1 and
with no such function; under Wine each XDG spelling, both set, a relative value before an absolute
one, both relative, a `/` at the end, a home with a non-ASCII name and a Unix text holding
characters outside ASCII and outside the BMP, a path beside the home, no `WINEHOMEDIR` with and
without XDG, a `\??\unix` home, a failed code-page conversion, a failed path conversion, a
`\\?\unix\` result, Darwin with and without `WINEHOMEDIR`, a host other than Linux and Darwin, no
host and an empty one, no known folder, and no location at all; every combination of existing
host and prefix files and every creation outcome at each; and natively XDG set, unset, relative
and outside the home, `/` at the end of both, `HOME` unset and relative, no location, and both
native files present.

## preferences/

The config owner's `Save` of four preferences: the tracking mode pair, world-space yaw, true free
look and launch-enabled. In-game hotkeys save the first three; launch-enabled (`EnableOnStartup`)
is saved only by a mod that marks that row Writable, since End never persists. The cases pin the
save of each, with what the file holds for it and what the mod runs on before and after. No launcher writes a
game file: a launcher edits Defaults.ini, and reads a game's `CameraUnlock.ini` for display only.
One directory per case, holding `case.tsv`, `input.ini` and, for a case with a change,
`expected.ini`. Every input carries the `[CameraUnlock]` stamp, so no save adds one.

The fixture mod is `HeadTrackingConfigTable` binding one of two row sets, each row at the
schema's default (`true`, except `TrueFreeLook`, `false`):

| Row set | Rows |
|---------|------|
| `three-state` | `[General] EnableOnStartup`, `[General] WorldSpaceYaw`, `[General] RotationEnabled`, `[Position] PositionEnabled`, `[Position] TrueFreeLook` |
| `two-state` | the same without `RotationEnabled`: a mode control with two states, as REFramework mods have |

`case.tsv` is ASCII with the note rule of `expected.tsv`. Each other line is a row whose fields
are separated by one tab:

| Row | Fields | Meaning |
|-----|--------|---------|
| `binds` | `three-state` or `two-state` | The row set. First, once |
| `preference` | preference, raw, mod | What the file holds for the preference and the value the mod runs on. One row per preference, in the order `tracking_mode`, `world_space_yaw`, `true_free_look`, `launch_enabled` |
| `change` | preference, value | Optional, last: the value a save changes the preference to |

A tracking mode is written as its name in `preference_modes.tracking_mode` of
`data/pipeline-conformance.json`: `both` (`true, true`), `rotation` (`true, false`) or
`position` (`false, true`). The other three preferences are `true` or `false`. A raw field may
also be `invalid` or `missing`; a mod field is always a value.

The raw value, what the file holds:

- A row is found by its section and key under the reader's rules (names compare ASCII
  case-insensitively, and the last occurrence of a key wins) and read with the `bool` codec of
  `codecs/`. It is the value read, `invalid` when the key is there but its value is not a bool
  (an empty value included), or `missing` when the key is not there.
- `world_space_yaw`, `true_free_look` and `launch_enabled` are the raw values of
  `WorldSpaceYaw`, `TrueFreeLook` and `EnableOnStartup`.
- `tracking_mode` on `three-state`: an invalid row makes the mode `invalid`, whatever the other
  row holds; otherwise a missing row makes it `missing`; otherwise the pair is read through
  `preference_modes`, and a pair it does not list, false/false, is `invalid`.
- `tracking_mode` on `two-state`: `PositionEnabled` true is `both` and false is `rotation`;
  invalid and missing stay so. `RotationEnabled` is never read, even when the file holds one.

The mod's value is what the table applied to the file gives. An invalid or missing row reads as
its default, and then on `three-state` a false/false pair reads as both defaults
(docs/canonical-config.md, on the tracking mode at startup), so the mod always runs on a listed
mode. A `two-state` mod's rotation is always on.

The change is saved through `ConfigOwner`'s `Save` on a copy of `input.ini`, with the table
marking every row of the set Writable. A tracking mode is set as its `preference_modes` pair,
both rows on `three-state` and `PositionEnabled` alone on `two-state`. The owner writes both mode
rows when either changes, so on `three-state` a mode change always writes the pair, even a row
whose value stays, and every value is written `true` or `false`. `expected.ini` is the file
after the save, byte for byte. The new value always differs from the mod's value: a save that
changes no row writes nothing, even over an invalid or missing row, so no case saves the value
the mod already runs on.

For every case a runner reads `input.ini`, renders its own four `preference` rows (the raw values
by the rules above, the mod's values from the table applied to a new config) and compares them
with `case.tsv` exactly. Core's runners then load a copy of `input.ini` through the owner, whose
Defaults.ini is a scratch file it creates with the built-in values, so a row's default there is
the schema's, and require `Canonical`, the file unchanged and the same rows from the loaded config. With a change,
they save it and require `Saved` and the bytes of `expected.ini`, and require `expected.ini` to
read as the case's rows with the changed preference's raw and mod values both the new value. A
case whose change is outside the preference's values (`two-state` has no `position`) or equals
the mod's value fails. A port of the owner runs the same files, and its save of `input.ini` must
give `expected.ini`.

The cases: every mode on both row sets (`three-state-both`, `-rotation`, `-position`,
`two-state-both`, `-rotation`); false/false (`three-state-false-false`); the input of
`table/apply-mode-off-one-invalid` below a stamp (`three-state-one-invalid`); a missing row,
inserted by the change (`three-state-missing-position`, `three-state-missing-rotation` in LF, and
`two-state-missing-section`, whose change appends the section); an invalid row beside a missing
one (`three-state-invalid-and-missing`, the one case without a change); `yes` and `on`
(`three-state-yes-on`); each single bool (`world-space-yaw`, `true-free-look`, `launch-enabled`)
and all three invalid (`single-bools-invalid`); a single bool changed beside mode rows spelled
`yes` and `nope`, which the save leaves byte for byte (`single-bool-keeps-mode`); a repeated key
(`repeated-key`); lone CR endings (`cr-only`); and a stray `RotationEnabled` in a two-state file
(`two-state-stray-rotation`). Each `case.tsv` says what its case shows in its notes.

## example/

`CameraUnlock.ini`: the file the examples in docs/canonical-config.md create at first launch, which
is the example table's fresh render (`RenderCanonicalFresh` / `RenderFresh`) with the display name
`Example Game`: the six header lines on `default`, and `default` on every concept row. The table is
`HeadTrackingConfigTable` naming `UdpPort`, `EnableOnStartup`, `WorldSpaceYaw`, `RotationEnabled`,
`PositionEnabled`, `ToggleKey`, `CycleTrackingModeKey` and `YawModeKey`, plus one local row,
`[Logging] WriteLog`, a bool defaulting to false with the comment
`true: write HeadTracking.log beside the game's executable.`
`cpp/tests/canonical_config_example_tests.cpp` and `CanonicalConfigExample` (xunit and
`CameraUnlock.Core.FrameworkTests`) render it and create it through the config owner, and
`pixi run check-doc-examples` holds the document's copy of it to this file.

## mutations/

The differential corpus (design 6.2): C++ `GenerateIniMutations` in
`cameraunlock/config/testing/ini_mutations.h` and C# `IniMutations.Generate` in
`csharp/testing/IniMutations.cs`, whose every output a game's differential test runs through its
legacy import and its migration. The generator takes the import's list of the keys it reads
(`LegacyImport` keys) and a descriptor for each, and refuses the pair when the two name different
keys. One directory per case:

- `input.ini`: the base, a legacy file's bytes.
- `keys.tsv`: the key descriptors in order. A `key` row has the section (empty for a reader that
  ignores sections), key, alternate valid value and `true` or `false` for a hotkey. Each `range` row after it adds one out-of-range value
  to that key, and each `chord` row adds a chord switch: section, key, the value that turns it on
  and the value that turns it off.
- `expected.tsv`: one row per output in order, its name and the lower-case SHA-256 of its bytes.

Fields use the byte escape above. A runner generates the outputs from `input.ini` and `keys.tsv`,
passing the `key` rows' sections and keys as the import's list, and requires `expected.tsv`
exactly: the same count, order, names and hashes.

### How the generator reads the base

A UTF-8 byte order mark at the start is set aside and written back in front of every output
unless a mutation says otherwise. The rest splits into lines at CRLF, LF and a lone CR, each line
keeping its ending; a last line with no ending keeps none. The dominant ending is the most common
of CRLF, LF and CR among the lines, ties going to CRLF, then LF; with no endings it is CRLF. It is
fixed from the base and used for every line the generator inserts.

On a line trimmed of spaces and tabs: a line starting `[` is a header, named by the trimmed text
up to its first `]`; a header with no `]` or an empty name opens no section. A line starting `;`
or `#` is a comment. Any other line with an `=` and a non-empty trimmed key before it is a key
line. A key line belongs to the section of the last header above it, and a key line under no
section is never matched by a key with a section. Names compare ASCII case-insensitively.

A key's line is the first key line with its section and key. A section-less key (empty section)
is a key a reader looks up in every section, and its line is the first key line with its key
anywhere in the file, above every header or under any header, named or not. Its prefix is the line up to and
including the first `=` and any spaces and tabs after it; its value is the text after the `=`,
trimmed. Setting a value makes the line its prefix and the new value.

Inserting a line gives it the dominant ending, except at the end of a file whose last line has no
ending: that line takes the dominant ending and the new last line takes none, so the file still
ends without one. A section's insert point is just after the last non-blank line of its first
block (its first header up to the next header of any kind). Adding a key to a section inserts
`Key=value` at that point, or, when no header names the section, inserts `[Section]` and then the
key line at the end of the file. Adding a section-less key inserts it just after the last
non-blank line above the first header of any kind (in the whole file when it has no header), or
at the start of the file when there is no such line.

Before any mutation, every descriptor key with no line gets one, `Key=alternate`, added to its
section in descriptor order. That file, the full base, is where every output starts.

### The outputs, in order

For each key in descriptor order, named `[Section] Key: ` (`Key: ` for a section-less key) and
then:

1. `removed`: its line deleted.
2. `duplicate before, another value` and `duplicate after, another value`: its prefix and the
   alternate value inserted before, then after, its line.
3. `valid, then an invalid duplicate`: its prefix and `abc` inserted after its line;
   `invalid, then a valid duplicate`: the same inserted before it.
4. `key case swapped`: every ASCII letter before the line's `=` changes case.
5. `section case swapped`: in the header above the line, every ASCII letter between the `[` and
   the first `]` after it changes case. Not for a section-less key.
6. `moved to another section`: the line deleted, then inserted at the insert point of the first
   section in the file other than its own; with none, `[Elsewhere]` and the line are inserted at
   the end (`[Other]` when its own section is `Elsewhere`). A section-less key has no section of
   its own, so `[Elsewhere]` and its line are always inserted at the end.
7. `before the first header`: the line deleted, then inserted just before the first header of any
   kind. Only when that header is above the line, which it always is for a key with a section.
8. `'; x' appended`, `';x' appended`, `' # x' appended`: the text between the quotes added to the
   end of the line.
9. `in double quotes`, `in single quotes`: the value set to itself inside `"` or `'`.
10. `value <label>`: the value set to each of `empty` (nothing), `space` (one space), `""` (two
    double quotes), `abc`, `nan`, `inf`, `-inf`, `1e400`, `0,15`, `0x10`, `010`, `-1`, `+1`,
    `space then 1` (a space, then 1), `1 then space`, `1abc`, `True`, `TRUE`, `tRue`, `yes`, `on`,
    `2`, and `1100 characters` (the digit 1, 1100 times).
11. `out of range <v>`: the value set to each of the key's out-of-range values.
12. For a hotkey, `hotkey 0x230`, `hotkey 0` and `hotkey End`: the value set to that; then, for
    each chord switch, `chord [S] K on` and `chord [S] K off`: the switch set to its on or off
    value, added to its section when it has no line.

Then for each ordered pair of different keys A and B, named `[SA] KA alternate, [SB] KB` and then
` removed` (A set to its alternate value and B's line deleted) or ` invalid` (A set to its
alternate value and B set to `abc`).

Then for each section with a named header, in the order its first header appears and spelled as
that header spells it, named `[Section]: ` and then:

1. `header with spaces`, `header with a comment`, `header not closed`: its first header line
   replaced by `[ Section ]`, `[Section] ; c`, `[Section`.
2. `section repeated`: a copy of its first block inserted right after that block, every descriptor
   key of the section, and every section-less descriptor key, in the copy set to its alternate
   value. When the block's last line has no ending it takes the dominant ending, and the copy's
   last line keeps none.

Then, named `file: ` and then:

1. `UTF-8 mark before a header`: EF BB BF and the full base from its first header on. Only when
   the full base has a header.
2. `UTF-8 mark before a comment`: EF BB BF, `; comment`, the dominant ending, then the full base
   without its own mark.
3. `CRLF`, `LF`, `lone CR`: every line that has an ending given that one.
4. `mixed line endings`: the lines that have an ending given CRLF, LF, CR, CRLF and so on in turn.
5. `no final newline`: trailing lines with no content deleted, and the last line's ending removed.
6. `CRLF, a 199-character line`, and the same for 200, 254 and 255: every ending CRLF, and the
   first descriptor key's line rewritten as its key, `=`, spaces, then its value, that many
   characters long before the CRLF.
7. `0x1A byte`, `NUL byte`: a line holding just that byte inserted after the first descriptor
   key's line.
8. `trailing NUL padding`: the full base followed by 64 NUL bytes.
9. `tab separators`: on every key line, the spaces and tabs around the first `=` replaced by a tab
   on each side.
10. `#Key= lines`, `;Key= lines`: after each descriptor key's line, `#` or `;`, the key and
    `=alternate` inserted.
11. `indented continuation lines`: after each descriptor key's line, four spaces and its alternate
    value inserted.
12. `UTF-16 LE with a mark`: FF FE and the full base without its mark as UTF-16 LE, decoded as
    UTF-8 where the bytes are strict UTF-8 and as Windows code page 1252 otherwise (the five codes
    1252 leaves undefined become the same code point, as .NET Framework's code page 1252 table
    gives them).
13. `cp1252 byte in a comment`: the line `; caf` and the byte E9 inserted before the first
    descriptor key's line.
14. `cp1252 byte in a value`: the byte E9 added to the end of every descriptor key's line.

The cases: `legacy-lf` (LF, a key written `Key = value`, a key the reader does not read, a header
with a comment, keys and a section the base lacks), `utf8-mark-crlf` (a mark, CRLF, a lower-case
header and key, UTF-8 in a comment with a code point above U+FFFF, an unclosed header over a key,
a header with spaces inside its brackets, no final newline, chord switches the base lacks),
`cp1252-lone-cr` (lone CR, cp1252 bytes, a trailing blank line, one section named `Elsewhere`),
`tie-crlf-lf` (a CRLF and LF tie, keys above every header), `tie-lf-cr` (an LF and CR tie),
`no-header-crlf` (no header and `Key = value` lines, the shape of gone-home's first-run file, in
CRLF; only section-less keys, one the base lacks, and a section-less chord switch the base lacks)
and
`sectionless-lf` (section-less keys above every header and under one, one the base lacks, a key
with a section beside them, no final newline).
