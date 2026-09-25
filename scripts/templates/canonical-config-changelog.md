<!--
The fleet's standard CHANGELOG.md wording for the release that moves a mod onto the
canonical config format. Append the bullets to [Unreleased], under the headings given here,
and never repurpose an existing bullet.

A repo in data/config-format.json's `legacy` takes the Legacy bullets. A repo with no
published build (outside `legacy`) has no legacy file and takes none of them, only the
Removed bullets it needs, and where it is a BepInEx mod, the ConfigurationManager bullet
with "does not list" in place of "no longer lists".

Then:

- Replace <legacy> with the entry's `legacy_source`, the file the pre-canonical builds read
  (HeadTracking.ini, or the BepInEx <GUID>.cfg), and <path> with the installed path of
  CameraUnlock.ini (BepInEx\config\CameraUnlock.ini).
- Take the hotkey example from the repo's committed config, which differs from the fleet
  default where data/config-format.json records a hotkey_exceptions entry.
- Keep a bullet marked "Only where" only where that is true of the repo, and delete the
  marker line. Where a Removed bullet's condition is false, say instead what changes for
  which players.
- List the differences from the newest published build that the repo's differential test
  records (comparison 1, design 6.2), one bullet each with the commit that made it.
- Add each Removed bullet where the mod had that setting or key before the conversion,
  whether or not it published a build.

The migration bullets take their wording from the README config block that
scripts/generate-readme.mjs renders (legacyParagraphs, the ConfigurationManager line and
APPROVED_CHANGE_LINES), so a player reads the same thing in both. Change the two together.
-->

## Legacy

### Changed

- Settings move to `<path>`. Earlier versions of the mod kept these settings in `<legacy>`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `<legacy>` and writes them into `CameraUnlock.ini`. It never changes `<legacy>`, and does not read it again while `CameraUnlock.ini` exists.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
  - The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.
- An older version of the mod reads `<legacy>` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `<legacy>`.
- Deleting only `CameraUnlock.ini` makes the next start read `<legacy>` again. To go back to the defaults, delete both files.
- Only where the mod is a BepInEx mod:
  BepInEx's ConfigurationManager no longer lists these settings. Edit `<path>` with any text editor.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- Only where the mod did not already ignore a plain hotkey while Ctrl and Shift were held:
  A hotkey bound to a plain key no longer fires while Ctrl and Shift are both held, so Ctrl+Shift with that key reaches only a binding that names the chord.
- Only where End saved its state before:
  Turning head tracking on or off with End no longer changes the file. The mod starts with head tracking on or off as `EnableOnStartup` says.

## Removed, with or without the Legacy bullets

### Removed

- The key that toggled the reticle, and the reticle settings.
- The sensitivity, scale, deadzone, response curve and axis inversion settings. Set these in your tracker app instead.
- Only where every copy of the config the mod shipped (installer, Nexus ZIP and launcher seed) had the same defaults for these settings:
  With these settings at their shipped defaults the camera moves as it did before.
