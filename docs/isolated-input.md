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
- `cpp/tools/isolated_input_host`: that header as a DLL, for a mod with no native
  code. `CameraUnlock.Core.Dev.IsolatedInput` is what a C# mod loads it with.
- `powershell/IsolatedGameTest.psm1`: the lock on the game, the whole session
  round a script block, launch, play, capture, proof, restore, a log wait and a
  process sampler.
- `data/isolated-input.json`: what is covered, and what to build for what is not.

## Testing a title

1. **Ask whether it is covered.** Before the first in-game test:

   ```text
   Import-Module cameraunlock-core/powershell/IsolatedGameTest.psm1
   Assert-IsolatedInputCovers -BinaryPath <the binary that reads input> -ModHost native
   ```

   The binary is the game exe for most native engines and `UnityPlayer.dll` for a
   Unity game. `-ModHost` is `native` for a C++ mod, `managed` for a C# one,
   `script` for a mod with no native code of its own. If it throws, go to
   [A game it does not cover yet](#a-game-it-does-not-cover-yet).

2. **Wire the dev build.** In a source file compiled only into the dev build, define
   `CAMERAUNLOCK_ISOLATED_INPUT_IMPLEMENTATION`, include `<cameraunlock/dev/isolated_input.h>`,
   and call `cameraunlock::dev::StartIsolatedInput(commandFile, &Log)` when the command file
   exists: after `MH_Initialize`, before the mod's own hotkey thread starts.
   far-cry-6-headtracking's `src/dev/isolated_input.cpp` is that file in a mod.
   A C# mod has no such file: see [A C# mod](#a-c-mod).

   The command file is `CameraUnlockInput.txt` beside the mod's DLL. Gating on it
   keeps the same dev build answering to the real keyboard when the file is not
   there. It never goes into a release build: with it on, the game does not answer
   to the real keyboard.

3. **Run the session** with `Invoke-IsolatedGameSession`, which takes the game's
   rig, saves what the test changes, launches, runs your block, and puts
   everything back on every way out. [A whole session](#a-whole-session) is the
   call and a worked script.

   Inside the block:

   ```text
   Invoke-GameInput -Session $session -Commands 'tap Space', 'wait 5000'      # title, menus, load
   Test-IsolatedInputProof -Session $session -Commands <something visible> -Folder <scratch>
   Set-TestPose -PoseFile $session.PoseFile -Yaw 15; Invoke-GameInput ...; Save-GameCapture ...
   ```

   Per game, the prepare step makes the game windowed and keeps it running
   unfocused (Starfield needs `bAlwaysActive=1`). What is the game's own stays in
   the mod's `.lab`: the way from the title screen into a save, its cheats, and
   what to look for in a capture.

4. **Prove it once per title, then trust it.** `Test-IsolatedInputProof` sends
   something the game visibly reacts to and passes only if the picture changed
   and the game never held the real foreground. Until it passes for a title,
   nothing a script did in that title counts.

Rules a run has to meet:

- **A run in which the game held the foreground does not count.**
  `Invoke-GameInput` samples the real foreground for the whole script and says
  so. The real devices reach a foreground game, so that run is repeated.
- **One session has a game at a time.** Take its rig before touching the game's
  files, through `Invoke-IsolatedGameSession` or `Enter-GameRig`. Never delete a
  lock, and never start the game round one. See
  [One session at a time](#one-session-at-a-time).
- **Only stop what the session started**, by process id. `Start-IsolatedGame`
  refuses to launch while the game is already running: that one is someone's.
- **Restore everything**, whether the run passed or not.
- **A capture that never changes is a broken instrument.** Check
  `Test-CaptureDiffers` on two captures a second apart, once per title.

## One session at a time

An installed game is one thing on the machine: one process, one set of saves,
one settings file, and a share of one graphics card. Two sessions that both save
the game's files, launch and restore do not fail loudly. Each puts back a save
the other has played in. `Enter-GameRig` is the lock that stops that, and every
session that touches a game takes it first, whichever repo or worktree it is
working in.

```text
$rig = Enter-GameRig -Game <process name> -Owner <who you are> -What 'the lean check, run 3'
try { ... } finally { Exit-GameRig -Rig $rig }
```

`Invoke-IsolatedGameSession` does this itself. Call `Enter-GameRig` directly for
work on a game that is not a session: a deploy into the game folder, a build
whose output a running game would load.

- **The lock is keyed by the game and lives outside every repo**, in
  `%LOCALAPPDATA%\CameraUnlock\rig\<process name>\lock`. Two worktrees of one
  mod, and two mods for one game, meet the same lock.
- **It says who has it.** `owner.json` in the lock holds the owner's name, the
  process id, when it was taken, `-What`, and where the session saved the
  game's files. `Get-GameRig -Game <name>` shows it, with whether the lock is
  live and who is waiting.
- **It waits, oldest first.** A session that finds the rig taken queues and is
  served in the order it asked, for an hour by default (`-WaitSeconds`; 0 takes
  the rig now or throws). There is nothing to poll and no race to win. While it
  waits it prints who holds the rig and how many asked before it, each time
  that changes. A `-Waiting` block is handed that line in place of the print.
- **Only the session that took a lock releases it.** `Exit-GameRig` throws, and
  removes nothing, for a lock that is someone else's.
- **A lock is taken over only when its taker's process has gone and the game is
  not running.** The game alone keeps a lock live, so a session that starts the
  game in one process and stops it in another holds the rig in between.
- **A lock left by a session that never put the game's files back is not taken
  over.** `Enter-GameRig` throws and names the folder that session saved into.
  Run `Restore-GameTestState -Folder` on it, then ask again.
- **A game running with no lock on it was started by hand**, by the person at
  the machine or by a script that does not take the rig. `Enter-GameRig` waits
  for it to end.
- **A session whose own process was killed with its game up still holds the
  rig**, and nothing will ever stop that game for it. Whoever waits is told the
  holder's process has gone, and `Get-GameRig` names the file the session is
  kept in when it was started with `-SessionFile`.
  `Stop-IsolatedGameSession -SessionFile <that file>` then stops the game, puts
  its files back and releases the rig, from any process. It cannot tell a
  killed runner from a session that is between two of its own processes, so
  only do it for a runner known to do the whole run in one.

A script that leaves the game running when it ends, to be run again to restart
it, leaves its lock with the game instead of releasing it:

```powershell
$kept = Join-Path $PSScriptRoot 'rig-token.json'
$rig = Enter-GameRig -Game <process name> -Owner <who you are> -KeptIn $kept
try { <stop the game the last run left up, deploy, launch> } finally { Exit-GameRig -Rig $rig -KeptIn $kept }
```

While the game runs, `Exit-GameRig -KeptIn` writes the lock's token to that file
and leaves the lock held by the game alone. The next `Enter-GameRig -KeptIn`
takes it back, from any process, and the game left up is that run's to stop.
With the game gone the rig is released and the file removed. One file serves
every such script of a repo. A script of the same repo that wants the rig from
the start (a session, above all) calls `Stop-KeptGame -Game <name> -KeptIn $kept`
first: it stops the game left up and releases its lock, and does nothing when
none is kept.

The lock is left with the processes that were running, by process id, and with
no others. Once they have ended the lock is not live and the file is stale: a
game of the same name that someone starts afterwards is theirs, `Stop-KeptGame`
does not touch it, and `Enter-GameRig` waits for it. A script that leaves only
a launcher running, with the game's own process still to come, therefore keeps
nothing: wait for the game's process before the script ends.

Work that needs the whole graphics card takes the rig too, with `-WholeGpu`: an
image generation run, a benchmark. It waits until no rig is held, and no game's
rig is given out while it holds. It needs no game:

```text
$rig = Enter-GameRig -WholeGpu -Owner <who you are> -What 'eight pictures'
try { & python generate.py --count 8 } finally { Exit-GameRig -Rig $rig }
```

Run the work from the PowerShell that took the lock: with no game to keep it
live, the lock of a process that has ended is anyone's to take over. Hold it for
a batch, not for an evening, because every game session on the machine waits
behind it.

The lock is a convention, not a fence. A session that never asks is not seen,
and a script that still calls `Start-IsolatedGame` without the rig runs beside
whoever holds it.

Taking and releasing happen inside one named mutex, so no two sessions ever
decide on the same state, and the lock folder is renamed into place with its
owner record already in it. A folder made with `New-Item -ItemType Directory`
is not a lock: PowerShell looks and then makes, and in Project Zomboid on
2026-10-05 three runs started in the same second got through one.

## A whole session

`Invoke-IsolatedGameSession` is the run a mod's `.lab/isolated.ps1` otherwise
writes out by hand. In order:

1. Takes the game's rig, waiting its turn, then runs `-Enter` with the session.
   Nothing has been saved yet, so this is where to take a second lock that
   other users of the game still go by, and to wait out one of them that is
   still putting the game's files back.
2. Saves `-Files` and `-Folders` into `-StateFolder` (`Save-GameTestState`).
3. Runs `-Prepare`: build and deploy the dev build, change the game's settings.
   A build made here cannot land under another session's running game.
4. Writes `-Port` into `-IniPath` (`Set-ModTestPort`), and starts the pose
   sender on it when `-PoseFile` is given.
5. Launches (`Start-IsolatedGame`), from `-WorkingDirectory` and with
   `-Environment` set for the launch alone when those are given. `-Launched`
   runs as the game's process appears, before the settling: a sampler started
   there has the process from its first seconds.
6. Runs `-Run` with the session.
7. Stops the game and the pose sender, runs `-Collect`, puts every file and
   folder back, runs `-Leave`, releases the rig. `-Leave` is `-Enter`'s other
   half, and runs even when `-Enter` threw: it must do nothing where `-Enter`
   did nothing.

Step 7 runs however the call ends: the block throwing, the game not starting,
the prepare step failing. Each part of it runs whatever the part before it did.
The rig is released only once the files are back, so a restore that fails
leaves the game refused to everyone, with the folder to restore named, until
someone has run `Restore-GameTestState` on it.

A mod's `.lab/isolated.ps1` on it:

```text
param([Parameter(Mandatory)][string]$Name, [string]$Owner = $env:RIG_OWNER)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $repo 'cameraunlock-core\powershell\IsolatedGameTest.psm1')

$game  = '<the game folder>'
$saves = '<the folder the game keeps its saves in>'
$prefs = '<the settings file of the game>'
$asi   = Join-Path $game '<Mod>.asi'
$ini   = Join-Path $game 'CameraUnlock.ini'
$log   = Join-Path $game '<Mod>.log'
$out   = Join-Path $PSScriptRoot "runs\$Name"
New-Item -ItemType Directory -Force $out | Out-Null

Invoke-IsolatedGameSession -ProcessName '<Game>' -Launch 'steam://rungameid/<id>' -ModFolder $game `
    -Owner $Owner -What "isolated.ps1 $Name" `
    -Files $asi, $prefs, $log -Folders $saves -StateFolder (Join-Path $PSScriptRoot 'state') `
    -IniPath $ini -Port (Get-ModTestPort -RepoName (Split-Path -Leaf $repo)) -PoseFile (Join-Path $out 'pose.txt') `
    -Prepare {
        # build the dev build and copy it to $asi; make the game windowed and running unfocused in $prefs
    } `
    -Run {
        param($session)
        Wait-GameLogLine -Path $log -Match '<the line the mod logs once it is in>' -Session $session | Out-Null
        Invoke-GameInput -Session $session -Commands 'tap Space', 'wait 5000' | Out-Null    # the way into the save
        $sampler = Start-GameProcessSampler -ProcessId $session.ProcessId -Path (Join-Path $out 'process.csv')
        Set-TestPose -PoseFile $session.PoseFile -Yaw 15
        $played = Invoke-GameInput -Session $session -Commands 'tap Insert', 'wait 500'
        if ($played.GameHeldForeground) { throw 'the game held the foreground: this run does not count' }
        Save-GameCapture -Session $session -Path (Join-Path $out 'yaw15.png') | Out-Null
        Stop-GameProcessSampler -Sampler $sampler | Out-Null
    } `
    -Collect { Copy-Item $log, $ini $out -Force }
```

What is left in the script is the game's: its paths, the build, the way into a
save, the inputs and what to capture. A C# mod adds `-ModHost managed`.

The parts, for a script that needs one alone:

- **`Save-GameTestState -Files ... -Folders ...`** takes whole folders as well as
  files. Name a folder for anything the game writes while it runs: a save folder
  gains files as the game plays, and only putting the folder back removes them.
  `Restore-GameTestState` mirrors a folder back with robocopy. It never empties
  the folder first, it tries a file the stopped game still holds for 20 seconds,
  and when it gives up the folder has everything else in place, the saved copy is
  still there and it can be run again. Remove-then-copy left Project Zomboid's
  save folder half deleted once, on one locked file. `-RetrySeconds` gives a
  single file the same patience, for a DLL the game holds after it has gone.
- **`Set-ModTestPort -IniPath <config> -Port <port>`** replaces the last
  `UdpPort` line and keeps every other byte. A config that does not exist
  throws: a mod on the canonical config imports its older settings only while
  `CameraUnlock.ini` is absent, so a test that made the file would not test what
  a player gets. `-Create` is for the test that means to.
- **`Wait-GameLogLine -Path <log> -Match <regex> -TimeoutSeconds 120`** returns
  the line and its number, or throws with the log's last line. With `-Session`
  it stops waiting when the game has gone. A log left by an earlier run can hold
  the line already: delete it in `-Prepare`, or pass `-After` a line number.
- **`Start-GameProcessSampler -ProcessId <pid> -Path <csv>`** writes a row every
  two seconds (`-IntervalSeconds`): `time`, `elapsedSeconds`, `privateMB`,
  `workingSetMB`, `dedicatedVideoMB`, `cpuSeconds`. `dedicatedVideoMB` is the sum
  of Windows' `GPU Process Memory` counters for the process, and is empty in a
  row where Windows had none. `cpuSeconds` counts from the start of the process,
  so the load between two rows is the difference over the time between them. It
  ends by itself when the process goes, and `Stop-GameProcessSampler` ends it
  between two rows.

### A session over several processes

A session driven a step at a time (start, look at a capture, decide what to
play, stop) runs over several PowerShell processes. `Start-IsolatedGameSession`
and `Stop-IsolatedGameSession` are the two halves of `Invoke-IsolatedGameSession`,
with the session kept in a file between them:

```text
# start
Start-IsolatedGameSession ... -SessionFile $sessionFile | Out-Null
# play, any number of times
$session = Get-IsolatedGameSession -SessionFile $sessionFile
Invoke-GameInput -Session $session -Commands $Commands
# stop
Stop-IsolatedGameSession -SessionFile $sessionFile -Collect { ... }
```

`Invoke-GameInput` writes the session back to its file with each script, so the
next process has the sequence it reached. Call `Save-IsolatedGameSession` after
changing the session by hand: a new `ProcessId` once a launcher has handed over,
a property of the mod's own.

The rig stays held between the processes, by the running game. If the game goes
before the stop, the session's saved state is what keeps the rig from being
taken over, and `Stop-IsolatedGameSession` still restores and releases.

`Invoke-IsolatedGameSession` takes `-SessionFile` too. The session is written
there as soon as the game's process appears, before the settling, so a run
whose PowerShell is killed at any point after that can be ended by another
process with `Stop-IsolatedGameSession -SessionFile`. Give each run a file of
its own (one named for `$PID`): a start refuses a file that already holds a
session, before it has asked for the rig.

## A C# mod

A C# mod cannot compile `isolated_input.h`, so the detours come in a DLL of
core's, `CameraUnlockIsolatedInput.dll`, and the mod's dev build loads it.

1. **Build the host DLL once per core checkout**, and again when
   `isolated_input.h` changes:

   ```text
   pixi run build-isolated-input-host
   ```

   or, where pixi is not set up (a mod's submodule checkout), the two commands
   that task runs, from the core folder:

   ```text
   cmake -S cpp/tools/isolated_input_host -B cpp/tools/isolated_input_host/build
   cmake --build cpp/tools/isolated_input_host/build --config Release
   ```

   It lands at `cpp/tools/isolated_input_host/build/Release/CameraUnlockIsolatedInput.dll`:
   x64, MinHook and the C++ runtime linked in, one export. It is never committed
   and never goes into a mod's package.

2. **Call the entry point from the dev build**, in code a release build does not
   compile, as the plugin starts (`Awake` in a BepInEx plugin):
   `CameraUnlock.Core.Dev.IsolatedInput.StartIfAsked(modFolder, log)`, where
   `modFolder` is the folder the plugin's DLL is in and `log` takes one line of
   text. In a BepInEx 5 plugin that is
   `IsolatedInput.StartIfAsked(Path.GetDirectoryName(Info.Location), line => Logger.LogInfo(line))`.

   With no `CameraUnlockInput.txt` in that folder it does nothing and returns
   false, so the same dev build answers to the real keyboard. With the file
   there it loads `CameraUnlockIsolatedInput.dll` from the same folder, starts
   it and returns true. It throws when the file is there and the DLL is missing,
   does not load, or does not start, and the message says which. Let it throw:
   a test session that believes the detours are in when they are not sends the
   game nothing.

3. **Start the session with `-ModHost managed`.** `Start-IsolatedGame` then
   copies the host DLL into `-ModFolder` beside the command file
   (`Copy-IsolatedInputHost` is that step alone), and `Stop-IsolatedGame`
   removes the copy. `-HostDll <path>` names a build that is not this checkout's
   x64 one, such as a 32-bit build (`-A Win32` and a build folder of its own) for
   a 32-bit game.

The host logs to `CameraUnlockIsolatedInput.log` in the mod folder, keeping the
run before it as `CameraUnlockIsolatedInput.prev.log`. What the native route
writes to the mod's own log is in there: which paths the game reads, each script
played, each time the foreground was handed back. `Stop-IsolatedGame` leaves both
logs to be read, so name them in `Save-GameTestState -Files` to have the restore
remove them.

The DLL's one export is `int CameraUnlockStartIsolatedInput(const wchar_t* commandFile)`:
0 when it started, 1 when MinHook did not initialise, 2 when a detour could not
be installed (none is left in), 3 when it was already started in this process.
Loading the DLL starts nothing.

For a Unity game the binary that reads input is `UnityPlayer.dll`, and a native
plugin of the game can read input too: Untitled Goose Game ships Rewired's
`Rewired_DirectInput.dll`, which imports `dinput8.dll`. Run `Get-GameInputPaths`
on each.

Checked outside a game, 2026-10-04 (`pixi run test-isolated-input-host`): Windows
PowerShell loaded the net472 `CameraUnlock.Core`, `StartIfAsked` started the host,
every detour went in, and with a script holding `A` down `GetKeyState`,
`GetKeyboardState` and `GetAsyncKeyState` each reported `A` and no other key.
That process has no window, so it says nothing about what a game's window
receives. Untitled Goose Game is the first game driven this way: see
[Measured](#measured).

## A Java agent mod

A mod that is a Java agent in a game on a JVM has no native code that could
compile `isolated_input.h`, and loads the same host DLL a C# mod does.

1. Build the host DLL as for [a C# mod](#a-c-mod).
2. In the dev agent, when `CameraUnlockIsolatedInput.dll` and
   `CameraUnlockInput.txt` are both in the folder it is given, load the DLL
   through `java.lang.foreign` (`SymbolLookup.libraryLookup`) and call
   `CameraUnlockStartIsolatedInput` with the command file's path as a
   NUL-terminated UTF-16 string. Throw unless it answers 0.
3. Start the session with `-ModHost managed`, which copies the DLL into
   `-ModFolder`. `Assert-IsolatedInputCovers` takes `-ModHost jvm`.

Where the JVM is told what to load through the launch's environment
(`JAVA_TOOL_OPTIONS`), `Invoke-IsolatedGameSession -Environment` sets it for the
launch alone.

An LWJGL game reads its keyboard and mouse through GLFW, and no import table of
the game shows it: Project Zomboid's exe imports no input API, `lwjgl.dll`
imports only `KERNEL32.dll`, and `glfw.dll` is packed inside the game's jar. A
copy with the same bytes is unpacked in an `lwjgl_<user>` folder under the temp
folder. `Get-GameInputPaths` on that file shows what GLFW itself reads. See
[Measured](#measured) for where that stands.

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

```text
down <key>            press and hold: a key name, or Ctrl, Shift or Alt
up <key>              release it
tap <binding> [ms]    press and release, held ms (default 60); a chord works: Ctrl+Shift+U
mouse <left|right|middle> <down|up|click>
move <dx> <dy>        relative mouse movement, in counts
cursor <x> <y>        put the mouse cursor at a point of the game window's client area, in pixels
text <characters>     typed one character at a time, to the end of the line
wait <ms>
pad <n> <button> <down|up|tap>        a controller button: a b x y lb rb ls rs start back up down left right
pad <n> stick <left|right> <x> <y>    a stick held at -1 to 1 each way, up positive
pad <n> trigger <left|right> <pull>   a trigger held at 0 to 1
```

`pad` is an XInput controller, `n` from 0 to 3. A controller is plugged in from
its first command on and holds what it was last told, so a stick is let go with
`pad 1 stick left 0 0`. A `tap` holds the button 150 ms: a pad is polled once a
frame. The real controllers are not read while isolated input is on.

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
detour answers for those handles. Windows sends a background window no raw input
unless it registered with `RIDEV_INPUTSINK`. Where one did, a real keyboard or
mouse event is emptied on its way through `GetRawInputData` and
`GetRawInputBuffer`: it keeps its header and moves and presses nothing.

A key is also posted as `WM_KEYDOWN` and `WM_KEYUP`, as Windows sends it beside
raw input. Unreal Engine 4 registers raw input for the mouse and takes its keys
from the window procedure, so without these it turns its view for `move` and
answers to no key. They are not posted where the game registered its keyboard
with `RIDEV_NOLEGACY`, because Windows sends none there. A mouse button reaches
such a game as a window message too, which is posted once a `cursor` command has
placed the cursor: start the script with `cursor <x> <y>`.

| Function | Answer | Why |
|---|---|---|
| `GetForegroundWindow` | the game's own window | neither the game nor the mod stands down for being in the background |
| `GetAsyncKeyState` | the synthetic key state only | the mod's hotkeys fire for the script, not for what is typed elsewhere |
| `GetKeyState`, `GetKeyboardState` | the synthetic key state only: the high bit while the script holds the key, the low bit flipped by each press. A held `Ctrl`, `Shift` or `Alt` is also held as its left-hand key | an engine that polls keys or modifiers through them sees the script's keyboard. The log names each one the first time the game calls it |
| the same three, for `VK_LBUTTON`, `VK_RBUTTON` and `VK_MBUTTON` | held while a `mouse` command holds that button | Windows reports mouse buttons through the key state too, and a game can take its aim or fire button from there |
| `ClipCursor`, `SetCursorPos` | nothing | the game would trap and recentre the real cursor |
| `SetCursorPos`, after the dev build called `LetGameMoveScriptedCursor(true)` | moves the script's cursor to that point, once a `cursor` command has placed it; the real cursor stays put | a mod that places the cursor itself (The Ascent rests it on the point the shot lands, because the game draws its crosshair as the cursor) can be seen to land it |
| `NtUserSetCursorPos` in `win32u.dll` | nothing | only where `SetCursorPos` could not be detoured because its first bytes were already written over: it is what `SetCursorPos` jumps to |
| `GetClipCursor` | what the game last asked for | a mod that reads the clip to tell gameplay from a menu still can |
| `SetForegroundWindow` | nothing | the game cannot take the foreground back |
| `GetRawInputData`, `GetRawInputBuffer`, for an event of the real keyboard or mouse | the event with no movement, no buttons and no key | a window registered with `RIDEV_INPUTSINK` is sent the real devices in the background. The log says so the first time one arrives |
| `GetCursorPos` | the point the last `cursor` command gave, and the middle of the game window until one has | a menu with a pointer follows the script's cursor, and a game that reads the cursor never follows the real one |
| the game window's procedure | never sees `WM_ACTIVATEAPP`, `WM_ACTIVATE`, `WM_KILLFOCUS` or `WM_NCACTIVATE` saying it lost the foreground, and is posted all four saying it has it when the window is found | a game that stops, or stops following its mouse, when deactivated carries on |

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

For a game that has an XInput DLL loaded (`xinput1_4.dll`, `xinput1_3.dll`,
`xinput9_1_0.dll`, `xinput1_2.dll`, `xinput1_1.dll`; none is ever loaded for it):

| Function | Answer | Why |
|---|---|---|
| `XInputGetState` | the script's controller at that index, or `ERROR_DEVICE_NOT_CONNECTED` for one the script has not named | the real controllers never reach the game, and a second player can be played from a script |
| `XInputSetState` | `ERROR_SUCCESS` for a plugged-in script controller, without reaching XInput | rumble goes nowhere |

A controller's first command also posts `WM_DEVICECHANGE` to the game window:
Unreal Engine 4 looks for new controllers only when Windows says the devices
changed. The log says once that the game reads XInput.

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
  carries has not been measured. The rig keeps two sessions off one game, and
  off the card while `-WholeGpu` work holds it. It does not stop two different
  games running side by side.
- The rig sees only sessions that take it.
- `Test-IsolatedInputProof` compares two captures, so a picture that moves by
  itself (a weapon swaying at idle) passes it with no input taken. Prove such a
  title with an input whose effect the mod also logs, and read the log.

## Measured

Project Zomboid (Steam, a Java agent on the game's own Java 25 runtime), from the mod's notes of
2026-10-05 and the host's log of one run, not measured again for this entry:

- The host DLL, loaded from the dev agent through `java.lang.foreign`, started and its detours
  went in. The log read `DirectInput keyboard and mouse devices answer from the command file (10
  detours over 4 device vtables)`, `the game reads key state through GetKeyState`, the same for
  `GetKeyboardState`, and `the game reads the cursor's position (GetCursorPos)`.
- The mod's hotkeys, which core's poller reads with `GetAsyncKeyState`, fired once each for
  `tap PageUp`, `tap Ctrl+Shift+H`, `tap Insert` and `tap End`, and the game never held the
  foreground.
- The game's own keyboard and mouse were not driven through isolated input. The mod's dev probe
  answers the game's `org.lwjglx` key and button polls from Java. Whether the raw-input path
  drives the game's GLFW window has not been tried, which is why `glfw` is `unsupported` in
  `data/isolated-input.json`. The game's `glfw.dll` imports `GetRawInputData`,
  `RegisterRawInputDevices`, `GetKeyState` and `GetCursorPos`.

Red Eclipse 2.0.9 (Steam, Cube 2 / Tesseract on SDL2, ASI under Ultimate ASI Loader as
`winmm.dll`), 2026-10-04, windowed and behind other windows for the whole session, the game
never holding the foreground:

- The exe imports `SDL2.dll` and reads no input of its own beyond `GetKeyState` and
  `GetCursorPos`. `Get-GameInputPaths` on `SDL2.dll` shows raw input, and that path drove the
  game: nothing was pushed through SDL's event queue.
- Keyboard: `E` joined the match from the spectator camera, `Return` opened the chat prompt,
  `[` and `]` stepped the scope's zoom level, and the mod's hotkeys fired, `Insert` and the
  `Ctrl+Shift` and `Shift+Alt` chords.
- `text` typed into the chat prompt, slash, lower case, space and digits.
- Mouse: `move` turned the view (300 counts, about 50 degrees), the right button held the
  scope up (the mod's log read `Sights up`) and the left fired a round (the ammunition count
  dropped). No `cursor` was needed first.
- In the launches where the log read `the game had the real foreground and gave it back` as the
  window appeared, `move` and the mouse buttons did nothing until a `cursor 640 360` had been
  played. After it the right button raised the scope and `move` turned the view, and the left
  button fired only when held (`mouse left down`, `wait 200`, `mouse left up`): a `click` did not.
- `Test-IsolatedInputProof` passed on `move 300 0`.
- The pose sender drove the mod through whole sessions, rotation and position, on the mod's
  test port.
- `Save-GameCapture` showed live frames, the game's own HUD, crosshair and scope overlay
  included, quickly enough to catch a 150 ms transition in a burst of captures.
- The game runs on a home folder given with `-h<folder>`, so a test never reads or writes the
  player's own settings, and `-x<commands>` runs console commands at start (a player name, the
  map), which skips the first-run menu.
- Not tried: `cursor`, `pad`.

S.T.A.L.K.E.R.: Call of Prypiat - Enhanced Edition (Steam, X-Ray 1.10.3.653, ASI under Ultimate
ASI Loader as `dinput8.dll`), 2026-10-04, windowed and behind other windows for the whole session:

- The log named the paths: keyboard and mouse through DirectInput `GetDeviceData`, key state
  through `GetKeyState` and `GetKeyboardState`, and the cursor's position through `GetCursorPos`.
  The exe also imports SDL2, and nothing needed pushing through it.
- The loader being the `dinput8.dll` proxy in the game folder made no difference: the device
  detours went in (10 detours over 4 device vtables).
- Keyboard: the backquote opened the console at the main menu and in play, the arrow keys and
  Return chose a main menu row and loaded the save, `W` walked, the number keys chose weapons,
  and the mod's hotkeys fired, nav keys and the `Ctrl+Shift+U` chord.
- Mouse: `move` turned the view (about 8 counts a degree), the right button held the sights up
  (the mod's log read `sights=1` and the field of view went from 67.5 to 37.5) and the left
  fired a round (the ammunition count dropped).
- `text` typed console commands, lower case and digits. A shifted character came out unshifted:
  `(` typed `9` and `_` typed `-`. Holding Shift as its own step typed it:
  `down Shift`, `wait 80`, `tap 0xBD 80`, `wait 80`, `up Shift` gave `_`, and
  `mtb_fsr_frame_generation on` ran that way. Whether `text` needs the wait or the synthetic key
  state for Shift was not followed up.
- The console closes the sights when it opens, so a script raises them again after it.
- The game went on running unfocused, where it otherwise opens its menu and pauses.
- `Save-GameCapture` showed live frames, the game's own HUD and crosshair included.
- The pose sender drove the mod through whole sessions, rotation and position, on the mod's
  test port.
- Not tried: `cursor`, `pad`.

Deus Ex: Mankind Divided 1.19 build 801.0 (Steam), 2026-10-04, windowed and behind
other windows for the whole session:

- No scripted key or click did anything until the game was kept from seeing
  `WM_NCACTIVATE` with a false `wParam`, and was posted one with a true `wParam` when its
  window was found. The game's window procedure reads a `WM_INPUT` through `GetRawInputData`
  only while the application object reports itself active, and that state follows
  `WM_NCACTIVATE`, not `WM_ACTIVATE` or `WM_ACTIVATEAPP`. Before the change every `WM_INPUT`
  and `WM_KEYDOWN` reached the window procedure and `GetRawInputData` was never called.
- The launcher is a window of the same process and takes `Return` as a window message. The
  script that presses it is reported as `1 of N steps could not be sent`, because the
  launcher's window is gone before the key comes up.
- Keyboard: `Return` went through the intro screens and the main menu into the save, and the
  mod's hotkeys fired, nav keys and chords.
- Mouse: `move` turned the view, and after a `cursor` the right button raised and lowered the
  sights (the mod's log read `ADS: sights up`).
- `Test-IsolatedInputProof` passed on `move 500 0`.
- The pose sender drove the mod through this module, rotation and position.
- `Save-GameCapture` shows live frames.
- With five other games running on the machine the game drew three to four frames a second,
  and about fifteen with its process at `AboveNormal`.

The Ascent (Steam, Unreal Engine 4, build `++depot+release-CL-72946`), 2026-10-04,
windowed and behind other windows for the whole session, the game never holding
the foreground:

- Keyboard and `cursor`: the title screen, the menus, a new game, the pause menu,
  the journal and a dialogue's choices were driven, and `W`, `D`, `F`, `Ctrl`
  and a held `Space` walked, interacted, crouched and closed tutorial cards.
- Controller: in the couch co-op lobby `pad 1 a tap` joined a second player,
  `pad 1 down tap` moved through its profile list and `pad 1 a tap` chose one.
  In play `pad 1 stick left 0 1` for a second walked the second player 4.4 m,
  and the right stick turned and pitched that player's view. The log read
  `the game reads controllers through XInput`.
- `SetCursorPos` is swallowed, so a mod that recentres the cursor every frame
  has to measure mouse movement from where the cursor came to rest, read back
  with `GetCursorPos`, and not from the point it asked for.
- `Save-GameCapture` showed live frames, both halves of a split screen included.

Untitled Goose Game 1.1.4 (Steam, Unity 2018.4.1f1, BepInEx 5 plugin), 2026-10-04,
through the host DLL and `Start-IsolatedGame -ModHost managed`:

- Keyboard: the title menu, the player count and the save slot were driven into
  the world, and the arrow keys walked and turned the goose. The proof passed in
  the world, and the game never held the foreground.
- The game reads its keyboard through Rewired, whose native keyboard support
  registers raw input to a window of its own. Unity saw every scripted key and
  Rewired saw none until the script's raw input was also posted to the window
  `GetRegisteredRawInputDevices` names. Turning Rewired's native support off at
  run time is not a way round it: Rewired resets and the game's cached players
  go invalid.
- A chord (`tap Ctrl+Shift+Y`) did not reach a plugin that checks
  `Input.GetKey(KeyCode.LeftControl)` and `LeftShift`, though the letter did:
  the script held `VK_CONTROL` and `VK_SHIFT` only, and the game reads key state
  through `GetKeyState`. With the left-hand keys (`VK_LCONTROL`, `VK_LSHIFT`,
  `VK_LMENU`) held in the synthetic state as well, the mod's `Ctrl+Shift+Y` and
  `Ctrl+Shift+G` fired. `down Ctrl` and `down Shift` on their own left the goose
  where it stood.
- BepInEx's log console is a visible window of the game's process. It was taken
  for the game window by the host (every script went to it) and by
  `Save-GameCapture` (every capture was of the log) until both skipped
  `ConsoleWindowClass`.
- BepInEx 5 logs the host DLL in `plugins` as not a .NET assembly and skips it.
- The host DLL could not be deleted for a moment after the game was stopped;
  `Stop-IsolatedGame` waits for it.
- Not driven: the mouse.

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

Deep Rock Galactic 1.40.154534.0 (Steam, Unreal Engine 4.27), 2026-10-03, with
the real foreground sampled through every script and the game never holding it:

- `move` turned the view (200 counts, about 16 degrees) before this change and
  after it.
- Keyboard: before keys were posted as window messages, `down W` did not walk
  the player and `Escape` opened nothing. After, `down W`, `wait 1500`, `up W`
  walked the player 4.2 m and `Escape` opened and closed the pause menu.
- Mouse: `mouse left click` fired nothing until a `cursor 640 360` had been
  played, and fired the equipped weapon after it.
- Modifiers held (2026-10-04): `down Ctrl`, `down Shift`, `tap G` threw a grenade and the same
  with `H` ran the game's HUD toggle, and `down Ctrl` alone showed the laser pointer until
  `up Ctrl`. The game's `GetKeyState` import was not followed up: these did not need it.

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

Resident Evil Requiem 1.3.1.0 (Steam, REFramework 1.5.9.1), 2026-10-03, windowed and
behind other windows for the whole session:

- `SetCursorPos` could not be detoured when the plugin loaded: MinHook answered
  `MH_ERROR_UNSUPPORTED_FUNCTION` and the function started `C3 46 D3 4F FD 00`
  where this machine's `user32.dll` has `FF 25 3A 39 04 00`, a jump to
  `NtUserSetCursorPos`. The dev build detours that in `win32u.dll` instead.
- The log named the keyboard path: DirectInput through `GetDeviceState` (256
  bytes). It named none for the mouse.
- Keyboard: `F` held 200 ms chose Main Story and then Continue, and the save
  loaded.
- Mouse: `move 700 0` turned the camera, and the right button raised and lowered
  the sights (the mod's log read `sights up`).
- The mod's hotkeys fired through the synthetic key state, `Insert` and the
  `Ctrl+Shift+U` chord.
- `Test-IsolatedInputProof` passed on `move 700 0`, and the picture also moves by
  itself here: two captures a second apart with no input differed. The turn was
  read off the captures.
- The pose sender drove the mod through this module, rotation and position.
- The game took the real foreground once as it started and gave it back.
- `Save-GameCapture` shows the frame with the game's own reticle and HUD.
- Not tried: `cursor`, `text`.

A Plague Tale: Innocence (Steam, build 4336652), 2026-10-04, windowed and behind other
windows for the whole session:

- Keyboard: the title screen, the save slot and Continue were driven with `Return`, `W`
  walked and `E` picked up.
- Mouse: `move` turned the camera. The right button raised the sling only once the script
  also held `VK_RBUTTON` in the key state, and the left button primed and threw only with
  `VK_LBUTTON` held there. The `mouse` command now holds both.
- `cursor` and a click chose a pause menu row.
- The scripted pad's sticks and triggers moved nothing in the game. Not followed up.
- `Save-GameCapture` showed live frames.

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
