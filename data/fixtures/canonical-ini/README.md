# Canonical INI fixtures

Byte fixtures for the canonical INI format: `reader/` for the reader, `editor/` for the
editor, `keys/` for the hotkey binding codec, `codecs/` for the value codecs and `table/` for
config tables. Core's C++ suite runs them unchanged (`cpp/tests/canonical_ini_tests.cpp`,
`cpp/tests/ini_editor_tests.cpp`, `cpp/tests/key_bindings_tests.cpp`,
`cpp/tests/value_codecs_tests.cpp` and `cpp/tests/config_table_tests.cpp`), and so does its C#
suite (`CanonicalIniFixtures`, `IniEditorFixtures`, `KeyBindingFixtures`, `ValueCodecFixtures`
and `ConfigTableFixtures`, under xunit on net8.0 and in
`CameraUnlock.Core.FrameworkTests` on .NET Framework 3.5 and 4.7.2). Lopari's Rust codec is to run the same files, so nothing
here is specific to one language: a reader or editor in any language is held to every case. The expected files are written by hand from the rules, never produced by an
implementation.

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
| CollisionChannel | concept CollisionChannel | int | `3` | Engine |
| CycleTrackingModeKey | concept CycleTrackingModeKey | hotkey | `PageUp, Ctrl+Shift+G` | |
| Mode | local [Camera] Mode | enum `ControlRotation`, `UpdateCamera` | `UpdateCamera` | comment `ControlRotation or UpdateCamera (decoupled).` |
| LeanDelayMs | local [Position] LeanDelayMs | int | `50` | comment `Milliseconds before a lean starts, and how far it reaches.`; Range(0, 1000) |
| LeanScale | local [Position] LeanScale | float | `1.0` | no comment; Range(0, 2) |
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

A case directory holds one of two pairs:

- `input.ini` and `expected.tsv`: the table applied to `input.ini` as the reader parses it,
  onto a config whose fields hold other values beforehand. `expected.tsv` has one `field` row
  per field in table order (name, and the field's value as its codec writes it) and one
  `diagnostic` row per diagnostic Apply returns, in its order (kind, lines comma-separated,
  and the sentence DescribeCanonicalDiagnostic / `Describe` gives). The reader's own
  diagnostics are not listed.
- `values.tsv` and `expected.ini`: `values.tsv` has a `field` row for each field that differs
  from its default, the value as its codec writes it; the config holding them renders exactly
  as `expected.ini`.

Both TSV files are ASCII with the note rule above, fields separated by one tab, and values and
sentences in the byte escape. For every case a runner also renders the config the case ends
with, parses and applies that, and requires no diagnostic from the reader or the table, the
same field values, and the same bytes when rendered again. Each runner also checks that a
member of the config no row binds keeps its value through Apply.
