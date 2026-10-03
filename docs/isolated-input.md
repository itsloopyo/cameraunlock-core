# Isolated input: testing a mod in a game, in the background

Every in-game test of a mod runs with the game behind other windows, taking its
keyboard and mouse from the mod's dev build instead of from the real devices.
The person at the machine keeps their mouse, keyboard and foreground window, and
several tests can run at once.

**No test sends real keyboard or mouse input, and none brings the game to the
foreground.** A game this cannot drive yet is a gap to close here first. That is
how coverage grows: the first mod to meet a new kind of game adds the missing
piece to core, and every later one gets it.

- `cameraunlock/dev/input_script.h`: the command language. Pure, tested.
- `cameraunlock/dev/isolated_input.h`: the Win32 half, compiled into a dev build.
- `powershell/IsolatedGameTest.psm1`: launch, play, capture, proof, restore.
- `data/isolated-input.json`: what is covered, and what to build for what is not.

## Testing a title

1. **Ask whether it is covered.** Before the first in-game test:

   ```powershell
   Import-Module cameraunlock-core/powershell/IsolatedGameTest.psm1
   Assert-IsolatedInputCovers -BinaryPath <the binary that reads input> -ModHost native
   ```

   The binary is the game exe for most native engines and `UnityPlayer.dll` for a
   Unity game. `-ModHost` is `native` for a C++ mod, `managed` for a C# one,
   `script` for a mod with no native code of its own. If it throws, go to
   [A game it does not cover yet](#a-game-it-does-not-cover-yet).

2. **Wire the dev build.** In a source file compiled only into the dev build:

   ```cpp
   #define CAMERAUNLOCK_ISOLATED_INPUT_IMPLEMENTATION
   #include <cameraunlock/dev/isolated_input.h>

   // after MH_Initialize, before the mod's own hotkey thread starts
   if (the command file exists) cameraunlock::dev::StartIsolatedInput(commandFile, &Log);
   ```

   The command file is `CameraUnlockInput.txt` beside the mod's DLL. Gating on it
   keeps the same dev build answering to the real keyboard when the file is not
   there. It never goes into a release build: with it on, the game does not answer
   to the real keyboard.

3. **Run the session.**

   ```powershell
   Save-GameTestState -Files <deployed mod, its config, the game's settings> -Folder <scratch>\state
   # deploy the dev build; set the mod's port to Get-ModTestPort; make the game windowed
   # and keep it running unfocused (per game: Starfield needs bAlwaysActive=1)
   $sender  = Start-TestPoseSender -Port $port -PoseFile $pose
   $session = Start-IsolatedGame -ProcessName <name> -Launch <uri or exe> -ModFolder <mod folder>
   Invoke-GameInput -Session $session -Commands 'tap Space', 'wait 5000'      # title, menus, load
   Test-IsolatedInputProof -Session $session -Commands <something visible> -Folder <scratch>
   Set-TestPose -PoseFile $pose -Yaw 15; Invoke-GameInput ...; Save-GameCapture ...
   Stop-IsolatedGame -Session $session; Stop-Process -Id $sender; Restore-GameTestState -Folder <scratch>\state
   ```

   What is the game's own stays in the mod's `.lab`: the way from the title screen
   into a save, its cheats, and what to look for in a capture.
   `starfield-headtracking/.lab/isolated.ps1` is a worked session.

4. **Prove it once per title, then trust it.** `Test-IsolatedInputProof` sends
   something the game visibly reacts to and passes only if the picture changed
   and the game never held the real foreground. Until it passes for a title,
   nothing a script did in that title counts.

Rules a run has to meet:

- **A run in which the game held the foreground does not count.**
  `Invoke-GameInput` samples the real foreground for the whole script and says
  so. The real devices reach a foreground game, so that run is repeated.
- **Only stop what the session started**, by process id. `Start-IsolatedGame`
  refuses to launch while the game is already running: that one is someone's.
- **Restore everything**, whether the run passed or not.
- **A capture that never changes is a broken instrument.** Check
  `Test-CaptureDiffers` on two captures a second apart, once per title.

## A game it does not cover yet

`Assert-IsolatedInputCovers` throws with what to build, taken from
`data/isolated-input.json`. Then:

1. Build what it names, in core: detours go in `isolated_input.h`, anything that
   is not Win32 gets a tested pure half beside `input_script.h`.
2. Prove it in the game with `Test-IsolatedInputProof`.
3. Change the entry to `supported` and name the game under `proven_in`, add what
   was measured to [Measured](#measured), and commit and push core.
4. Bump the mod and carry on with the test.

A game whose input path is in no entry at all gets a new entry, with what an
import table shows for it, in the same change.

Stop and say so, instead of building, only where the detours would be a risk to
the person's account: a game with anti-cheat. Name it in the mod's notes and
test that title by hand with its owner.

## The command file

The harness writes the whole file at once: a whole number on the first line,
commands after it. The mod plays it each time the number changes and writes
`<file>.done` holding that number, or the number and what went wrong. Every line
is parsed before any is played, so a script with a mistake in it sends the game
nothing. Whatever the file holds when the game starts is not played.

```
down <key>            press and hold: a key name, or Ctrl, Shift or Alt
up <key>              release it
tap <binding> [ms]    press and release, held ms (default 60); a chord works: Ctrl+Shift+U
mouse <left|right|middle> <down|up|click>
move <dx> <dy>        relative mouse movement, in counts
text <characters>     typed one character at a time, to the end of the line
wait <ms>
```

Key names are the ones hotkey lists use (`data/keys.json`). A key with no name is
its code: the backquote is `tap 0xC0`.

## What the dev build detours

For a game that reads raw input: each synthetic event is posted to the game's
window as a `WM_INPUT` whose handle is one of ours, and the `GetRawInputData`
detour answers for those handles. The real devices send a background game
nothing, which is what keeps the two apart.

| Function | Answer | Why |
|---|---|---|
| `GetForegroundWindow` | the game's own window | neither the game nor the mod stands down for being in the background |
| `GetAsyncKeyState` | the synthetic key state only | the mod's hotkeys fire for the script, not for what is typed elsewhere |
| `ClipCursor`, `SetCursorPos` | nothing | the game would trap and recentre the real cursor |
| `GetClipCursor` | what the game last asked for | a mod that reads the clip to tell gameplay from a menu still can |
| `SetForegroundWindow` | nothing | the game cannot take the foreground back |

## Limits

- A new game window takes the real foreground once, when it appears.
  `Start-IsolatedGame` hands it back to the window that had it.
- Someone who clicks into the game window gives it the real devices. The run is
  flagged and repeated.
- Games running at once share the GPU and the speakers. How many a machine
  carries has not been measured.

## Measured

Starfield 1.16.244.0 (Steam), 2026-10-03, with the real foreground sampled
through every script and the game never holding it:

- Keyboard: the title screen, the main menu and its confirm prompt were driven to
  a loaded save, and letter keys opened the game's own screens.
- Mouse: the right button raised and lowered the sights.
- `text`: console commands typed and ran (`tgm`, `help grenade 4 weap`,
  `player.additem ...`). The quote character did not type; leave quotes out.
- The mod's hotkeys fired through the synthetic key state, nav keys and chords.
- A whole session ran through `IsolatedGameTest.psm1`: launch, load, proof
  passed, sights, capture, stop, restore.
- The pose sender was checked against a local listener, not yet in a game
  through this module.
