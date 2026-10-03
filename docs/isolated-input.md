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
- `cameraunlock/dev/directinput_state.h`: what a DirectInput keyboard and mouse
  report for the input a script plays. Pure, tested.
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
cursor <x> <y>        put the mouse cursor at a point of the game window's client area, in pixels
text <characters>     typed one character at a time, to the end of the line
wait <ms>
```

Key names are the ones hotkey lists use (`data/keys.json`). A key with no name is
its code: the backquote is `tap 0xC0`, the digit 2 is `tap 0x32`.

`move` is for a game that turns the camera by how the mouse moved. `cursor` is
for a menu that reads where the cursor is: after the first `cursor`, a `mouse`
button also arrives as the click a pointer at that point would make. The point is
in the client area, so take it from a capture and subtract the title bar and the
border.

Pass a script's commands through `powershell -Command "& script.ps1 -Commands 'tap E','wait 500'"`.
With `powershell -File` the list arrives as one string and the mod refuses line 2.

To a DirectInput keyboard, `text` presses the key that types each character on a
US keyboard, with Shift held for upper case and the shifted symbols. It types
printable ASCII and nothing else: no Tab, no Enter (`tap Return`), no accented
letters. Each key is held 40 ms.

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
| `GetCursorPos` | the point the last `cursor` command gave, once one has | a menu with a pointer follows the script's cursor, not the real one |
| the game window's procedure | never sees `WM_ACTIVATEAPP`, `WM_ACTIVATE` or `WM_KILLFOCUS` saying it lost the foreground | a game that stops, or stops following its mouse, when deactivated carries on |

The dev build also watches the real foreground itself. Whenever a window of the
game holds it, the game gives it back to the window that had it before, or to the
desktop if that one has gone. Only the foreground process may hand the foreground
on, so this is done from inside the game and not from the harness.

For a game that reads DirectInput 8 (`dinput8.dll` loaded in the process): the
methods of the keyboard and mouse devices are detoured in the device vtable,
found from throwaway devices, so a device the game makes later is covered too.
The detours go in from the command-file thread once `dinput8.dll` is loaded, and
`dinput8.dll` is never loaded into a game that has not loaded it. A DirectInput
device that is neither a keyboard nor a mouse is left as it was.

| Method | Answer | Why |
|---|---|---|
| `GetDeviceState` | keyboard: 256 bytes, `0x80` at the `DIK_` code of each key the script holds. Mouse: the buttons held and the movement since that device last asked, as `DIMOUSESTATE` or `DIMOUSESTATE2` | the real device is never read |
| `GetDeviceData` | the events played since that device last asked: `dwOfs` a `DIK_` code or `DIMOFS_X`, `DIMOFS_Y`, `DIMOFS_BUTTON0` to `2`. Honours the caller's capacity, `DIGDD_PEEK` and a null buffer, and answers `DI_BUFFEROVERFLOW` after more than 256 unread events | a game that reads buffered input is covered as well |
| `Acquire`, `Poll` | `DI_OK`, without reaching DirectInput | the real keyboard and mouse are never acquired, so a lost or refused acquire cannot reach the game |
| `SetCooperativeLevel` | passed on as `DISCL_BACKGROUND \| DISCL_NONEXCLUSIVE` | the device stays usable behind other windows |
| `Release` | passed on | forgets a device that is gone |

Only the standard data formats are answered (`c_dfDIKeyboard`, `c_dfDIMouse`,
`c_dfDIMouse2`): a `GetDeviceState` of any other size is refused and the log says
so. There is no mouse wheel command, so `lZ` is always 0. A game that waits on
`SetEventNotification` is not signalled. The log names, once for each device,
which of `GetDeviceState` and `GetDeviceData` the game reads it through.

## Limits

- A game window takes the real foreground when it appears, and some take it
  again later. The dev build hands it back within its 50 ms poll each time, so
  the window that had it loses it for that long.
- Someone who clicks into the game window gets the same treatment: the game
  gives the foreground straight back. A run in which `Invoke-GameInput` saw the
  game hold it is still flagged and repeated.
- A launcher can start a stub that hands over to the game's own process (Far Cry
  6 does). `Start-IsolatedGame` returns the first process it sees, so take the
  process id again once the game is up.
- The first window a game shows can be its splash screen. The dev build looks
  for the game window again once the one it had is gone or hidden.
- Games running at once share the GPU and the speakers. How many a machine
  carries has not been measured.
- `Test-IsolatedInputProof` compares two captures, so a picture that moves by
  itself (a weapon swaying at idle) passes it with no input taken. Prove such a
  title with an input whose effect the mod also logs, and read the log.

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

Fallout: New Vegas 1.4.0.525 (Steam, 32-bit, DirectInput 8), 2026-10-03, with the
real foreground sampled through every script and the game never holding it:

- The log named the paths: keyboard through `GetDeviceState` (256 bytes) and
  `GetDeviceData`, mouse through `GetDeviceState` (20 bytes).
- Keyboard: the console opened at the main menu and in play, and the game's own
  screenshot key saved its files.
- `text`: `load "Save 176   Courier  Primm  01 22 56"` typed with its quotes and
  spaces and loaded the save; `tgm`, `player.additem` and `player.equipitem` ran.
  No character came out twice.
- Mouse: the right button raised and lowered the sights, the left fired (the
  ammunition count dropped).
- The mod's hotkeys fired through the synthetic key state.
- A 60 ms `tap` was missed about one time in four by keys the game polls
  (console, screenshot). `down`, `wait 400`, `up` was not missed.
- `move` did not show a cursor on the main menu, and `down W` did not walk the
  player. Neither was followed up.
- `Save-GameCapture` returns a black frame for this renderer, so
  `Test-IsolatedInputProof` cannot pass here. The proof was the game's own
  screenshots before and after each script. Those are taken before a mod draws
  on `Present`, so a mod's overlay needs the presented frame saved by the dev
  build (`fallout-new-vegas-headtracking/src/isolated_input.cpp`).
- The game stops when its window is deactivated. The dev build drops the
  deactivation messages in a window procedure of its own.
- The game finds its data through the working directory: launch it from the game
  folder.

Far Cry 6 (Ubisoft Connect, engine fingerprint 6824D119, DirectInput 8), 2026-10-03,
windowed and behind other windows for the whole session:

- The log named the paths: keyboard and mouse through `GetDeviceData`, and the
  cursor's position through `GetCursorPos`.
- Keyboard: Escape opened and closed the game's menus, the number keys chose a
  weapon, and the mod's hotkeys (nav keys and a chord) fired.
- Mouse: `move` turned the camera, the right button raised the sights and the
  left fired a round (the ammunition count dropped).
- Menus: `move` does not move the menu pointer. `cursor` does, and a click then
  activates what is under it, but only with the deactivation messages kept from
  the window procedure: a build without that left the pointer still.
- `Test-IsolatedInputProof` passed on `move 700 0`.
- The game's windows took the real foreground five times in one launch, as they
  appeared and during the intro, and the game gave it back each time. A script
  that spanned those moments was flagged, and the proof run was not. Before the
  dev build did this, a game that took the foreground after
  `Start-IsolatedGame`'s settle time kept it, and Windows refused a hand-back
  from outside the game.
- The pose sender drove the mod through this module, rotation and position, on
  the mod's test port.
- `Save-GameCapture` shows the frame as presented, a mod's Direct3D 12 overlay
  included.
- The game's own Quit to Desktop, reached by `cursor` and a click, ended the
  process with exit code 0.
- The intro before the main menu does not skip on Space and runs about three
  minutes.

Ready or Not (Steam, Unreal Engine 5.3), 2026-10-03, game behind other windows:

- Unreal takes mouse buttons from window messages, not from raw input: with no
  cursor placed, the scripted right button left the sights down. After
  `cursor 960 540` the right button raised and lowered them (the mod's log read
  `aiming=1` and the field of view went from 90 to 67.5) and the left fired a
  round.
- The mod's hotkeys fired through the synthetic key state, `Insert` and the
  `Ctrl+Shift+U` chord.
- `Test-IsolatedInputProof` passed with the sights still down, on idle weapon
  sway alone. The log is what proved the input.
- The first window found was the launch splash.
- The pose sender drove the mod through whole sessions, rotation and position,
  on the mod's test port.
- `Save-GameCapture` worked with the window at the bottom of the z-order. With
  DLSS frame generation on, some captures showed a moving HUD element bent or
  doubled and others showed it sharp; with it off every capture was sharp.
- Not tried: `move`, `text`, and the game's own keyboard bindings.
