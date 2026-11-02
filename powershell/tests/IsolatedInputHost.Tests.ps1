#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for the isolated-input host of a managed mod
# ============================================================================
# Run: pixi run test-isolated-input-host
#
# The route a C# mod takes, in a process that is not a game: Windows PowerShell
# loads the net472 CameraUnlock.Core, IsolatedInput.StartIfAsked loads the host
# DLL (cpp/tools/isolated_input_host), and a command file holds a key down. The
# key state functions then have to report that key, and only that key, whatever
# is held on the real keyboard.
#
# The detours go into a child process this test starts and ends. That process
# has no window, so nothing is posted to one: what a game's window receives is
# not covered here.
# ============================================================================

param([switch]$Child, [string]$Folder, [string]$CoreDll, [string]$HostDll)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:Failures = 0
function Check {
    param([string]$Name, [bool]$Condition, [string]$Detail = '')
    if ($Condition) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name - $Detail" -ForegroundColor Red
        $script:Failures++
    }
}

if (-not $Child) {
    $core = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $builtCore = Join-Path $core 'csharp\src\CameraUnlock.Core\bin\Debug\net472\CameraUnlock.Core.dll'
    $builtHost = Join-Path $core 'cpp\tools\isolated_input_host\build\Release\CameraUnlockIsolatedInput.dll'
    foreach ($built in $builtCore, $builtHost) {
        if (-not (Test-Path $built)) { throw "$built is not built. Run this through: pixi run test-isolated-input-host" }
    }
    $scratch = Join-Path $env:TEMP ("isolated-input-host-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $scratch | Out-Null
    try {
        # Windows PowerShell, whatever runs this: the plugin route is .NET Framework and 64-bit.
        $powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Child -Folder $scratch -CoreDll $builtCore -HostDll $builtHost
        $exit = $LASTEXITCODE
        Write-Host '--- CameraUnlockIsolatedInput.log'
        Get-Content (Join-Path $scratch 'mod\CameraUnlockIsolatedInput.log') | ForEach-Object { Write-Host $_ }
    } finally {
        Remove-Item $scratch -Recurse -Force -ErrorAction SilentlyContinue
    }
    exit $exit
}

Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) 'IsolatedGameTest.psm1') -Force
Add-Type -Path $CoreDll
Add-Type -Namespace CameraUnlockHostTest -Name User32 -MemberDefinition @'
[DllImport("user32.dll")] public static extern short GetKeyState(int vk);
[DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vk);
[DllImport("user32.dll")] public static extern bool GetKeyboardState(byte[] state);
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr LoadLibraryW(string name);
[DllImport("xinput1_4.dll")] public static extern int XInputGetState(int index, byte[] state);
[DllImport("xinput1_4.dll")] public static extern int XInputGetCapabilities(int index, int flags, byte[] capabilities);
[DllImport("user32.dll")] public static extern int ShowCursor(bool show);
[DllImport("user32.dll")] public static extern IntPtr SetCursor(IntPtr cursor);
[DllImport("user32.dll")] public static extern IntPtr LoadCursorW(IntPtr instance, IntPtr name);
[DllImport("user32.dll")] public static extern bool GetCursorInfo(int[] info);
'@
# Only an XInput DLL the process has loaded when the host starts is detoured, as in a game.
if ([CameraUnlockHostTest.User32]::LoadLibraryW('xinput1_4.dll') -eq [IntPtr]::Zero) { throw 'xinput1_4.dll did not load' }

function Throws {
    param([scriptblock]$Block, [string]$Pattern)
    try { & $Block | Out-Null; return $false } catch { return $_.Exception.Message -match $Pattern }
}

function Play {
    param([int]$Sequence, [string[]]$Commands)
    $commandFile = Join-Path $mod 'CameraUnlockInput.txt'
    [IO.File]::WriteAllLines("$commandFile.tmp", @("$Sequence") + $Commands)
    Move-Item "$commandFile.tmp" $commandFile -Force
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        $done = Get-Content "$commandFile.done" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($done -and $done.StartsWith("$Sequence")) { return }
        Start-Sleep -Milliseconds 20
    }
    throw "script $Sequence was not played within 10 seconds"
}

function KeyboardState {
    $state = New-Object byte[] 256
    if (-not [CameraUnlockHostTest.User32]::GetKeyboardState($state)) { throw 'GetKeyboardState failed' }
    , $state
}

# CURSORINFO as six ints: cbSize, flags, hCursor (two), ptScreenPos.
function CursorInfo {
    param([int]$Size = 24)
    $info = New-Object int[] 6
    $info[0] = $Size
    $read = [CameraUnlockHostTest.User32]::GetCursorInfo($info)
    [pscustomobject]@{ Read = $read; Showing = ($info[1] -band 1) -ne 0; Cursor = [long]$info[2] -bor ([long]$info[3] -shl 32) }
}

function KeysDown {
    $state = KeyboardState
    @($state | Where-Object { $_ -band 0x80 }).Count
}

$lines = New-Object System.Collections.Generic.List[string]
$log = [Action[string]] { param($line) $lines.Add($line) }
$mod = Join-Path $Folder 'mod'
New-Item -ItemType Directory -Force $mod | Out-Null
$keyA = 0x41
$keyB = 0x42

Check 'without the command file nothing starts' (-not [CameraUnlock.Core.Dev.IsolatedInput]::StartIfAsked($mod, $log))
Check 'and nothing is logged' ($lines.Count -eq 0)

Set-Content (Join-Path $mod 'CameraUnlockInput.txt') '0' -Encoding ASCII
Check 'with the command file and no host DLL it throws naming the DLL' `
    (Throws { [CameraUnlock.Core.Dev.IsolatedInput]::StartIfAsked($mod, $log) } 'host DLL is missing: .*CameraUnlockIsolatedInput\.dll')

Copy-IsolatedInputHost -ModFolder $mod -HostDll $HostDll | Out-Null
# The copy StartIfAsked loads, not the build: a second file is a second module with a state of its own.
Add-Type -Namespace CameraUnlockHostTest -Name Host -MemberDefinition @"
[DllImport(@"$(Join-Path $mod 'CameraUnlockIsolatedInput.dll')")] public static extern int CameraUnlockHoldIsolatedKey(int vk, int down);
"@
Check 'a key is not held before the host has started' ([CameraUnlockHostTest.Host]::CameraUnlockHoldIsolatedKey(0x43, 1) -eq 1)
Check 'with both it starts' ([CameraUnlock.Core.Dev.IsolatedInput]::StartIfAsked($mod, $log))
Check 'and logs where the input now comes from' ($lines.Count -eq 1 -and $lines[0] -match 'CameraUnlockInput\.txt') ($lines -join ' | ')
Check 'a second start in the same process throws' `
    (Throws { [CameraUnlock.Core.Dev.IsolatedInput]::StartIfAsked($mod, $log) } 'already started')

Check 'no key is down before a script holds one' ((KeysDown) -eq 0) "$(KeysDown)"

# The first read of the file only records its number, so one poll has to pass before a script.
Start-Sleep -Milliseconds 300
Play 1 'down A'
$state = [CameraUnlockHostTest.User32]::GetKeyState($keyA)
Check 'GetKeyState reports the held key down, toggled on' ($state -eq -32767) "$state"
Check 'GetKeyboardState reports it down, toggled on' ((KeyboardState)[$keyA] -eq 0x81) "$((KeyboardState)[$keyA])"
Check 'GetKeyboardState reports no other key down' ((KeysDown) -eq 1) "$(KeysDown)"
Check 'GetAsyncKeyState reports it down' (([CameraUnlockHostTest.User32]::GetAsyncKeyState($keyA) -band 0x8000) -ne 0)
Check 'GetKeyState reports a key the script does not hold as up' ([CameraUnlockHostTest.User32]::GetKeyState($keyB) -eq 0)

Play 2 'up A'
$state = [CameraUnlockHostTest.User32]::GetKeyState($keyA)
Check 'GetKeyState reports the released key up, still toggled on' ($state -eq 1) "$state"
Check 'GetKeyboardState reports it up, still toggled on' ((KeyboardState)[$keyA] -eq 0x01) "$((KeyboardState)[$keyA])"

Play 3 'tap A'
$state = [CameraUnlockHostTest.User32]::GetKeyState($keyA)
Check 'a second press turns the toggle off' ($state -eq 0) "$state"

$keyC = 0x43
Check 'a key held through the export is down to GetAsyncKeyState' `
    ([CameraUnlockHostTest.Host]::CameraUnlockHoldIsolatedKey($keyC, 1) -eq 0 -and ([CameraUnlockHostTest.User32]::GetAsyncKeyState($keyC) -band 0x8000) -ne 0)
Check 'and to GetKeyState, toggled on' ([CameraUnlockHostTest.User32]::GetKeyState($keyC) -eq -32767)
Check 'and let go through it, up again' `
    ([CameraUnlockHostTest.Host]::CameraUnlockHoldIsolatedKey($keyC, 0) -eq 0 -and ([CameraUnlockHostTest.User32]::GetAsyncKeyState($keyC) -band 0x8000) -eq 0)
Check 'a key code that is none is refused' ([CameraUnlockHostTest.Host]::CameraUnlockHoldIsolatedKey(256, 1) -eq 2 -and [CameraUnlockHostTest.Host]::CameraUnlockHoldIsolatedKey(0, 1) -eq 2)

$notConnected = 1167
$capabilities = New-Object byte[] 20
Check 'XInputGetCapabilities answers no controller before a script plugs one in' `
    ([CameraUnlockHostTest.User32]::XInputGetCapabilities(0, 1, $capabilities) -eq $notConnected)
Play 4 'pad 0 a down'
$result = [CameraUnlockHostTest.User32]::XInputGetCapabilities(0, 1, $capabilities)
Check 'XInputGetCapabilities describes the script pad as a gamepad' `
    ($result -eq 0 -and $capabilities[0] -eq 1 -and $capabilities[1] -eq 1) "result $result, type $($capabilities[0]), subtype $($capabilities[1])"
Check 'with its buttons, triggers and sticks there' `
    ([BitConverter]::ToUInt16($capabilities, 4) -ne 0 -and $capabilities[6] -ne 0 -and $capabilities[7] -ne 0 -and [BitConverter]::ToInt16($capabilities, 8) -ne 0)
$state = New-Object byte[] 16
$result = [CameraUnlockHostTest.User32]::XInputGetState(0, $state)
Check 'XInputGetState reports its A button held' ($result -eq 0 -and ([BitConverter]::ToUInt16($state, 4) -band 0x1000) -ne 0) "result $result"
Check 'XInputGetCapabilities answers no controller at an index the script has not named' `
    ([CameraUnlockHostTest.User32]::XInputGetCapabilities(1, 1, $capabilities) -eq $notConnected)

# The child has no window and is not in front, so Windows would answer GetCursorInfo for the
# window that is, and nothing this process does would hide that cursor.
$arrow = [CameraUnlockHostTest.User32]::LoadCursorW([IntPtr]::Zero, [IntPtr]32512).ToInt64()
$info = CursorInfo
Check 'GetCursorInfo answers the arrow, shown, before the game sets a cursor' ($info.Read -and $info.Showing -and $info.Cursor -eq $arrow) "$info"
$count = [CameraUnlockHostTest.User32]::ShowCursor($false)
$info = CursorInfo
Check 'ShowCursor(FALSE) takes the display count below zero and GetCursorInfo answers it hidden' ($count -eq -1 -and $info.Read -and -not $info.Showing) "count $count, $info"
$count = [CameraUnlockHostTest.User32]::ShowCursor($true)
Check 'ShowCursor(TRUE) brings it back to zero, shown' ($count -eq 0 -and (CursorInfo).Showing) "count $count"
[CameraUnlockHostTest.User32]::SetCursor([IntPtr]::Zero) | Out-Null
$info = CursorInfo
Check 'SetCursor(NULL) hides it, with no cursor handle' ($info.Read -and -not $info.Showing -and $info.Cursor -eq 0) "$info"
$previous = [CameraUnlockHostTest.User32]::SetCursor([IntPtr]$arrow)
$info = CursorInfo
Check 'SetCursor with a cursor shows that cursor' ($info.Showing -and $info.Cursor -eq $arrow) "$info"
Check 'and SetCursor still answers the cursor it replaced' ($previous -eq [IntPtr]::Zero) "$previous"
[CameraUnlockHostTest.User32]::ShowCursor($false) | Out-Null
Check 'a cursor set while the count is below zero stays hidden' (-not (CursorInfo).Showing)
[CameraUnlockHostTest.User32]::ShowCursor($true) | Out-Null
Check 'GetCursorInfo with a wrong cbSize is refused, as Windows refuses it' (-not (CursorInfo -Size 20).Read)

$hostLog = Get-Content (Join-Path $mod 'CameraUnlockIsolatedInput.log') -Raw
Check 'the host log says the detours are in' ($hostLog -match 'takes its keyboard and mouse from the command file')
Check 'the host log names GetKeyState once it is read' ($hostLog -match 'through GetKeyState')
Check 'the host log names GetKeyboardState once it is read' ($hostLog -match 'through GetKeyboardState')
Check 'the host log names GetCursorInfo once it is read' ($hostLog -match 'GetCursorInfo is read')

if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) check(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host 'IsolatedInputHost: all checks passed' -ForegroundColor Green
exit 0
