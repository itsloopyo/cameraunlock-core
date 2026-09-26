<!--
The fleet's standard CHANGELOG.md wording for the release that moves a mod onto the
canonical config format. Append the bullets to [Unreleased], under the headings given here,
and never repurpose an existing bullet.

A repo in data/config-format.json's `legacy` takes the Legacy bullets. A repo with no
published build (outside `legacy`) has no legacy file and takes none of them, only the
Outside legacy bullet, the Removed bullets it needs, and where it is a BepInEx mod, the
ConfigurationManager bullet with "does not list" in place of "no longer lists". Every
converted repo takes the Defaults.ini bullets. A repo whose `dialect` is `unity` has core's C#
config owner, the one owner that runs natively on Linux and macOS, and takes the C# owner
variant of the creation bullet and the read-only bullet for its listing, under Changed in a
`legacy` repo, whose earlier versions saved settings there, and under Added outside it.

Then:

- Replace <legacy> with the entry's `legacy_source`, the file the pre-canonical builds read
  (HeadTracking.ini, or the BepInEx <GUID>.cfg), and <path> with the installed path of
  CameraUnlock.ini (BepInEx\config\CameraUnlock.ini).
- Take the hotkey example from the fleet default, or, where data/config-format.json per_game
  lists the repo's ToggleKey because the game binds a chord itself, from the value the repo's
  committed config holds on that row.
- Keep a bullet marked "Only where" only where that is true of the repo, and delete the
  marker line. Where a Removed bullet's condition is false, say instead what changes for
  which players.
- List the differences from the newest published build that the repo's differential test
  records (comparison 1, design 6.2), one bullet each with the commit that made it.
- Add each Removed bullet where the mod had that setting or key before the conversion,
  whether or not it published a build.
- Outside `legacy`, end the first Defaults.ini bullet's second sentence at "do not read it":
  there are no earlier versions.

The migration and Defaults.ini bullets take their wording from the README config block that
scripts/generate-readme.mjs renders (legacyParagraphs, defaultsParagraphs, the
ConfigurationManager line, APPROVED_CHANGE_LINES and NORMALISATION_LINES), so a player reads the
same thing in both.
Change the two together.

The reset bullet has the player replace what CameraUnlock.ini holds and never tells them to
delete the legacy file. A Lopari v0.9.0 receipt can record the legacy file as a seed:
resident-evil-requiem-headtracking v0.4.0 installs REFramework and seeds
reframework/plugins/HeadTracking.ini, and Lopari carries that record into the receipt of every
later install while it owns the loader. Once the file is gone the receipt is not intact, so
Lopari downloads the latest release and reinstalls it before every launch, and the reinstall
writes no seed because REFramework is already there.
-->

## Legacy

### Changed

- Settings move to `<path>`. Earlier versions of the mod kept these settings in `<legacy>`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `<legacy>` and writes them into `CameraUnlock.ini`. It never changes `<legacy>`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it.
- Only where the defaults set both `RotationEnabled` and `PositionEnabled` to `default`:
  `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
  - The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.
  - Only where the mod is not an REFramework mod:
    A hotkey set to Ctrl, Shift or Alt on its own. That key goes down before the key of any chord made with it, so the hotkey is left unbound, and it keeps its Ctrl+Shift chord where it has one.
- An older version of the mod reads `<legacy>` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `<legacy>`.
- Deleting only `CameraUnlock.ini` makes the next start read `<legacy>` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Only where the mod is a BepInEx mod:
  BepInEx's ConfigurationManager no longer lists these settings. Edit `<path>` with any text editor.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- Only where the mod did not already ignore a plain hotkey while Ctrl and Shift were held:
  A hotkey bound to a plain key no longer fires while Ctrl and Shift are both held, so Ctrl+Shift with that key reaches only a binding that names the chord.
- Only where End saved its state before:
  Turning head tracking on or off with End no longer changes the file. The mod starts with head tracking on or off as `EnableOnStartup` says.

## Outside legacy

### Added

- The mod keeps its settings in `<path>`, and creates the file when it starts and finds none. A new `CameraUnlock.ini` sets to `default` each setting the README lists with a built-in value, so that setting takes its value from `Defaults.ini`.

## Defaults.ini, in every converted repo

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- Only where the `dialect` is `native`:
  When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.
- Only where the `dialect` is `unity`:
  When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app, or the game runs on Linux or macOS without Wine or Proton. The mod never changes `Defaults.ini` after that.

## C# owner, where the `dialect` is `unity`

### Changed in a `legacy` repo, Added outside it

- Only where the repo is in `legacy`:
  On Linux and macOS without Wine or Proton, this version reads its settings and saves none: it creates no `CameraUnlock.ini`, reads your settings from `<legacy>` again at every start while there is no `CameraUnlock.ini`, and a change made in game lasts until the game closes.
- Only where the repo is outside `legacy`:
  On Linux and macOS without Wine or Proton, this version reads its settings and saves none: it creates no `CameraUnlock.ini` and a change made in game lasts until the game closes.

## Removed, with or without the Legacy bullets

### Removed

- The key that toggled the reticle, and the reticle settings.
- The sensitivity, scale, deadzone, response curve and axis inversion settings. Set these in your tracker app instead.
- Only where every copy of the config the mod shipped (installer, Nexus ZIP and launcher seed) had the same defaults for these settings:
  With these settings at their shipped defaults the camera moves as it did before.
