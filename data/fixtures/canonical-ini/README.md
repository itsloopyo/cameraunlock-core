# Canonical INI fixtures

Byte fixtures for the canonical INI format: `reader/` for the reader and `editor/` for the
editor. Core's C++ suite runs them unchanged (`cpp/tests/canonical_ini_tests.cpp` and
`cpp/tests/ini_editor_tests.cpp`), and so does its C# suite (`CanonicalIniFixtures` and
`IniEditorFixtures`, under xunit on net8.0 and in `CameraUnlock.Core.FrameworkTests` on
.NET Framework 3.5 and 4.7.2). Lopari's Rust codec is to run the same files, so nothing
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
