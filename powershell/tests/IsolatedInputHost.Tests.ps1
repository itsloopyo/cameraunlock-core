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
'@

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

$hostLog = Get-Content (Join-Path $mod 'CameraUnlockIsolatedInput.log') -Raw
Check 'the host log says the detours are in' ($hostLog -match 'takes its keyboard and mouse from the command file')
Check 'the host log names GetKeyState once it is read' ($hostLog -match 'through GetKeyState')
Check 'the host log names GetKeyboardState once it is read' ($hostLog -match 'through GetKeyboardState')

if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) check(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host 'IsolatedInputHost: all checks passed' -ForegroundColor Green
exit 0
