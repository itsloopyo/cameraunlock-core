# Isolated input

A lab build of a mod can feed its game keyboard and mouse input from inside the
process. The game then runs behind other windows, takes nothing from the real
devices, and the person at the machine keeps their mouse and keyboard. It is for
test sessions only and never ships: with it on, the game does not answer to the
real keyboard.

Headers: `cameraunlock/dev/input_script.h` (the command language, pure and
tested) and `cameraunlock/dev/isolated_input.h` (the Win32 half, MinHook).

## What it covers

Games that read raw input: `RegisterRawInputDevices`, `WM_INPUT` and
`GetRawInputData` in the game's import table. Each synthetic event is posted to
the game's window as a `WM_INPUT` whose handle is one of ours, and the
`GetRawInputData` detour answers for those handles.

Also detoured, all in user32:

| Function | Answer | Why |
|---|---|---|
| `GetForegroundWindow` | the game's own window | neither the game nor the mod stands down for being in the background |
| `GetAsyncKeyState` | the synthetic key state only | the mod's hotkeys fire for the script, not for what is typed elsewhere |
| `ClipCursor`, `SetCursorPos` | nothing | the game would trap and recentre the real cursor |
| `GetClipCursor` | what the game last asked for | a mod that reads the clip to tell gameplay from a menu still can |
| `SetForegroundWindow` | nothing | the game cannot take the foreground back |

A game that reads DirectInput, XInput or window key messages needs its own
detours here before this works for it. Check the import table first.

## Using it in a mod

In a dev-only source file, compiled only into the lab build:

```cpp
#define CAMERAUNLOCK_ISOLATED_INPUT_IMPLEMENTATION
#include <cameraunlock/dev/isolated_input.h>

// after MH_Initialize, before the mod's own hotkey thread starts
if (the command file exists) cameraunlock::dev::StartIsolatedInput(commandFile, &Log);
```

Gate it on the command file being present when the game starts, so the same lab
build answers to the real keyboard when the file is not there.

## The command file

The harness writes the whole file at once (write a temporary file and move it
over): a whole number on the first line, commands after it. The mod plays the
file each time the number changes and then writes `<file>.done` holding that
number, or the number and what went wrong. Every line is parsed before any is
played, so a script with a mistake in it sends the game nothing. Whatever the
file holds when the game starts is not played.

```
down <key>            press and hold: a key name, or Ctrl, Shift or Alt
up <key>              release it
tap <binding> [ms]    press and release, held ms (default 60); a chord works: Ctrl+Shift+U
mouse <left|right|middle> <down|up|click>
move <dx> <dy>        relative mouse movement, in counts
text <characters>     typed one character at a time, to the end of the line
wait <ms>
```

Key names are the ones hotkey lists use (`data/keys.json`).

## What a harness still has to do

- Start the game. A new game window takes the real foreground once, when it
  appears; hand the foreground back to the window that had it.
- Keep the game running without focus where the game pauses itself (Starfield:
  `bAlwaysActive=1`).
- Capture with `PrintWindow`, which reads a covered window.
- Send head poses over UDP on the mod's own test port, as before.

## Measured

Starfield 1.16.244.0 (Steam), 2026-10-03, one session. The detours installed, the
game window was found, and scripts played. `Insert` and `Ctrl+Shift+U` stepped the
mod's aim mode through the synthetic key state. Whether the GAME takes the
synthetic raw input is not established: a dialog was dismissed, the sights came
up and the view turned in that session, but a person brought the game to the
foreground and pressed keys during it, and the real devices reach a foreground
game. Repeat it with the foreground watched for the whole run before relying on
it. `text` and the game's console were not tried.
