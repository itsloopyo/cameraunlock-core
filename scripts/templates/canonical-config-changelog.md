<!--
The fleet's standard CHANGELOG.md wording for the release that moves a mod onto the
canonical config format. Append the bullets to [Unreleased], under the headings given here,
and never repurpose an existing bullet.

Pick one variant:

- In place: a repo in data/config-format.json's `legacy` whose config is its own INI or
  CFG file. The file is converted where it is.
- BepInEx: a repo in `legacy` whose entry has a `legacy_source`. The settings move from the
  BepInEx .cfg to a new .ini beside it.
- A repo with no published build (outside `legacy`) has no migration and takes neither
  variant, only the Removed bullets it needs.

Then:

- Replace <file> with the installed file name (HeadTracking.ini), and <GUID> with the
  plugin GUID.
- Take the hotkey example from the repo's committed config, which differs from the fleet
  default where data/config-format.json records a hotkey_exceptions entry.
- Keep a bullet marked "Only where" only where that is true of the repo, and delete the
  marker line.
- List the differences from the newest published build that the repo's differential test
  records (comparison 1, design 6.2), one bullet each with the commit that made it.
- Add each Removed bullet where the mod had that setting or key before the conversion,
  whether or not it published a build.

The migration bullets take their wording from the README config block that
scripts/generate-readme.mjs renders (legacyParagraphs, bepinexParagraphs and
APPROVED_CHANGE_LINES), so a player reads the same thing in both. Change the two together.
-->

## In place

### Changed

- `<file>` has a new layout. The first time this version starts, it converts the file once into the new layout and keeps the file as it was beside it as `<file>.pre-canonical`. `<file>.pre-canonical.last`, when present, is the file as it was before the most recent conversion: the mod converts the file again when it finds the older layout later, for example after an older version of the mod rewrote it.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
  - The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- Only where the mod did not already ignore a plain hotkey while Ctrl and Shift were held:
  A hotkey bound to a plain key no longer fires while Ctrl and Shift are both held, so Ctrl+Shift with that key reaches only a binding that names the chord.
- An older version of the mod may not read the new layout correctly. It reads a key that moved as its own default, and it can misread a hotkey or another value that is now written as a name. To go back to an older version, first copy `<file>.pre-canonical` back over `<file>`, which restores the old file.
- Only where End saved its state before:
  Turning head tracking on or off with End no longer changes the file. The mod starts with head tracking on or off as `EnableOnStartup` says.

## BepInEx

### Changed

- Settings move from `BepInEx\config\<GUID>.cfg` to `BepInEx\config\<GUID>.ini`. The first time this version starts, it reads your settings from the `.cfg` and writes them into the `.ini`. The `.cfg` is left as it was, and an older version of the mod still reads it.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
  - The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.
- BepInEx's ConfigurationManager no longer lists these settings. Edit `BepInEx\config\<GUID>.ini` with any text editor.
- Deleting only the `.ini` makes the next start convert the `.cfg` again. To go back to the defaults, delete both files.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- Only where End saved its state before:
  Turning head tracking on or off with End no longer changes the file. The mod starts with head tracking on or off as `EnableOnStartup` says.

## Removed, either variant or none

### Removed

- The key that toggled the reticle, and the reticle settings.
- The sensitivity, deadzone, response curve and axis inversion settings. Set these in your tracker app instead. With these settings at their shipped defaults the camera moves as it did before.
