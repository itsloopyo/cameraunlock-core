#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for the guarded launch of a background test's game
# ============================================================================
# Run: pixi run test-rig-guard            (60 seconds of misbehaviour)
#      pixi run test-rig-guard 180        (the few-minute proof)
#
# Starts no game. RigMisbehave.exe (cpp/tools/rig_guard) stands in for one that
# takes the machine over: it activates its windows by every call there is,
# asks for another display mode twice a second, goes exclusive fullscreen
# through Direct3D 9 and 11, clips and moves the cursor, and starts a 32-bit
# child that does the same. It also opens a plain window away from the corner
# of the desktop and moves it back out four times a second, with a tool window
# an owned window, a child window and a message-only window it moves too. It
# goes through Start-IsolatedGame, as a rig's game does, and refuses to run
# without the shim in it.
#
# A second process, CameraUnlockRig watch, writes down every change of the
# foreground window and of the display mode on the real desktop for the whole
# run. The test passes only if none of them was the stand-in's and the display
# never changed, while the stand-in kept drawing and took a scripted key.
#
# Then the guard's own end: a run held to a ten-second limit has to be ended
# whole, parent and child, with one line saying why. Ending a run because its
# window took the foreground is the same code path from the verdict on; this
# test does not take the foreground from the person at the machine to show it.
#
# Not run here: a window the game made before the shim was in (the stand-in
# waits for the shim), a maximised window, a window first shown by SetWindowPos
# or SetWindowPlacement, and a window placed from another thread.
# ============================================================================

param([int]$Seconds = 60)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:Failures = 0
$script:Checks = 0
function Check {
    param([string]$Name, [bool]$Condition, [string]$Detail = '')
    $script:Checks++
    if ($Condition) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name - $Detail" -ForegroundColor Red
        $script:Failures++
    }
}

function Read-Stats {
    param([string]$Path)
    $stats = @{}
    foreach ($line in Get-Content $Path) {
        $name, $value = $line -split '=', 2
        $stats[$name] = $value
    }
    $stats
}

function Check-Stats {
    # What the stand-in saw of itself: everything it asked for was answered as done, and none of it happened.
    param([string]$Who, [hashtable]$Stats, [int]$Ran)
    Check "$Who`: its window was made non-activating" ($Stats.noActivateStyle -eq '1' -and $Stats.topmost -eq '0') "noActivate $($Stats.noActivateStyle), topmost $($Stats.topmost)"
    Check "$Who`: it never held the real foreground in $($Stats.grabRounds) rounds of taking it" ([int]$Stats.grabRounds -gt $Ran -and $Stats.realForegroundMine -eq '0') "held it in $($Stats.realForegroundMine) rounds"
    Check "$Who`: it believes it is active" ([int]$Stats.wmActivate -gt 0 -and [int]$Stats.wmActivateApp -gt 0 -and [int]$Stats.wmSetFocus -gt 0 -and $Stats.activeMine -eq $Stats.grabRounds -and $Stats.focusMine -eq $Stats.grabRounds) `
        "WM_ACTIVATE $($Stats.wmActivate), WM_ACTIVATEAPP $($Stats.wmActivateApp), WM_SETFOCUS $($Stats.wmSetFocus), active in $($Stats.activeMine) and focused in $($Stats.focusMine) of $($Stats.grabRounds) rounds"
    Check "$Who`: SetForegroundWindow, ClipCursor and SetCursorPos answered success" ($Stats.setForegroundAnswered -eq '1' -and $Stats.clipAnswered -eq '1' -and $Stats.cursorAnswered -eq '1')
    Check "$Who`: all $($Stats.modeAsked) display mode changes answered done" ([int]$Stats.modeAsked -gt $Ran -and $Stats.modeAsked -eq $Stats.modeAnsweredDone) "$($Stats.modeAnsweredDone) answered done"
    Check "$Who`: the exclusive fullscreen Direct3D 9 device was made, drew $($Stats.frames9) frames, and reset $($Stats.reset9Done) times" `
        ($Stats.create9 -eq '0x00000000' -and $Stats.answeredWindowed9 -eq '0' -and [int]$Stats.frames9 -gt $Ran * 5 -and [int]$Stats.reset9Asked -gt 0 -and $Stats.reset9Asked -eq $Stats.reset9Done) `
        "CreateDevice $($Stats.create9), frames $($Stats.frames9), resets $($Stats.reset9Done) of $($Stats.reset9Asked)"
    Check "$Who`: the exclusive fullscreen Direct3D 11 swap chain was made, says it is fullscreen, and drew $($Stats.frames11) frames" `
        ($Stats.create11 -eq '0x00000000' -and $Stats.setFullscreen -eq '0x00000000' -and $Stats.saysFullscreen -eq '1' -and $Stats.resizeTarget -eq '0x00000000' -and [int]$Stats.frames11 -gt $Ran * 5) `
        "create $($Stats.create11), SetFullscreenState $($Stats.setFullscreen), says $($Stats.saysFullscreen), frames $($Stats.frames11)"
    $w9, $h9 = $Stats.window9 -split 'x'; $w11, $h11 = $Stats.window11 -split 'x'
    Check "$Who`: both fullscreen windows ended small ($($Stats.window9) and $($Stats.window11))" ([int]$w9 -le 1320 -and [int]$h9 -le 780 -and [int]$w11 -le 1320 -and [int]$h11 -le 780)
    $work = [int[]]($Stats.work -split ',')
    $inCorner = {
        param([string]$Place, [int]$Width, [int]$Height)
        $l, $t, $r, $b = [int[]]($Place -split ',')
        $r -eq $work[2] -and $b -eq $work[3] -and $r - $l -eq $Width -and $b - $t -eq $Height
    }
    $askedW, $askedH = [int[]]($Stats.plainAsked -split 'x')
    Check "$Who`: both fullscreen windows ended in the corner ($($Stats.window9At) and $($Stats.window11At))" ((& $inCorner $Stats.window9At $w9 $h9) -and (& $inCorner $Stats.window11At $w11 $h11)) "work area $($Stats.work)"
    Check "$Who`: a window made visible was in the corner, at the size asked for, when CreateWindow returned ($($Stats.made9))" (& $inCorner $Stats.made9 480 300) "work area $($Stats.work)"
    Check "$Who`: a window made hidden at 200,200 was in the corner, at the size asked for, when ShowWindow returned ($($Stats.plainShown))" (& $inCorner $Stats.plainShown 320 200) "work area $($Stats.work)"
    Check "$Who`: that window was in the corner, at the size asked for, after each of the $($Stats.plainMoves) times it was moved to 200,200 or resized, and ended there ($($Stats.plain))" `
        ([int]$Stats.plainMoves -gt $Ran -and $Stats.plainAway -eq '0' -and (& $inCorner $Stats.plain $askedW $askedH)) "away or of another size after $($Stats.plainAway) moves, asked $($Stats.plainAsked), work area $($Stats.work)"
    Check "$Who`: minimised, moved to 200,200 and restored, it came back to the corner ($($Stats.plainRestored))" (& $inCorner $Stats.plainRestored 360 220) "work area $($Stats.work)"
    Check "$Who`: that window was never the foreground window" ($Stats.plainForeground -eq '0') "in $($Stats.plainForeground) rounds"
    Check "$Who`: its tool, owned, child and message-only windows went where it moved them ($($Stats.tool), $($Stats.owned), $($Stats.inner), $($Stats.messageOnly))" `
        ($Stats.tool -eq '16,16,176,76' -and $Stats.owned -eq '16,90,176,150' -and $Stats.inner -eq '20,20,120,60' -and $Stats.messageOnly -eq '5,5,15,15')
    Check "$Who`: raw input registered and the exclusive DirectInput mouse taken without error" ($Stats.rawRegistered -eq '1' -and $Stats.mouseLevel -eq '0x00000000' -and $Stats.mouseAcquire -eq '0x00000000') "raw $($Stats.rawRegistered), level $($Stats.mouseLevel), acquire $($Stats.mouseAcquire)"
}

$core = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$tools = Join-Path $core 'cpp\tools\rig_guard\build\out'
$hostDll = Join-Path $core 'cpp\tools\isolated_input_host\build\Release\CameraUnlockIsolatedInput.dll'
foreach ($built in (Join-Path $tools 'RigMisbehave.x64.exe'), (Join-Path $tools 'RigMisbehave.x86.exe'), (Join-Path $tools 'CameraUnlockRig.x64.exe'), $hostDll) {
    if (-not (Test-Path $built)) { throw "$built is not built. Run this through: pixi run test-rig-guard" }
}
if (Get-Process -Name 'RigMisbehave.x64', 'RigMisbehave.x86' -ErrorAction SilentlyContinue) { throw 'a RigMisbehave is already running: another run of this test has the stand-in' }

$scratch = Join-Path $env:TEMP ("rig-guard-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$mod = Join-Path $scratch 'mod'
$watch = Join-Path $scratch 'watch'
New-Item -ItemType Directory -Force $mod, $watch | Out-Null
$env:CAMERAUNLOCK_RIG_ROOT = Join-Path $scratch 'rig'
Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) 'IsolatedGameTest.psm1') -Force

$watcher = Start-Process (Join-Path $tools 'CameraUnlockRig.x64.exe') -ArgumentList 'watch', "`"$watch`"", ($Seconds + 120) -PassThru
$failed = $null
try {
    while (-not (Test-Path (Join-Path $watch 'watch.ready'))) {
        if ($watcher.HasExited) { throw 'the watcher did not come up' }
        Start-Sleep -Milliseconds 50
    }

    # ------------------------------------------------------------------
    Write-Host "`n--- $Seconds seconds of a misbehaving game and its 32-bit child, through Start-IsolatedGame"
    $stats = Join-Path $scratch 'stats.txt'
    $commandFile = Join-Path $mod 'CameraUnlockInput.txt'
    $began = Get-Date
    $session = Start-IsolatedGame -ProcessName 'RigMisbehave.x64' -Processes 'RigMisbehave.x86' -ModFolder $mod -SettleSeconds 9 -GuardMinutes 10 `
        -Launch (Join-Path $tools 'RigMisbehave.x64.exe') `
        -Arguments "$Seconds", "`"$stats`"", 'child', "`"$(Join-Path $tools 'RigMisbehave.x86.exe')`"", 'host', "`"$hostDll`"", "`"$commandFile`""
    $guardLog = Join-Path $session.GuardFolder 'guard.log'
    $heldForeground = $false
    $scripts = 0
    try {
        # Isolated input plays what is written after it started, and the stand-in loads it six seconds in.
        $hostLog = Join-Path $mod 'CameraUnlockIsolatedInput.log'
        $up = (Get-Date).AddSeconds(45)
        while (-not ((Test-Path $hostLog) -and (Select-String -Path $hostLog -Pattern 'DirectInput keyboard and mouse devices answer' -Quiet))) {
            if ((Get-Date) -ge $up) { throw 'isolated input was not up in the stand-in 45 seconds after the settle' }
            Start-Sleep -Milliseconds 200
        }
        Start-Sleep -Seconds 1
        while (((Get-Date) - $began).TotalSeconds -lt $Seconds - 4) {
            $played = Invoke-GameInput -Session $session -Commands 'down W', 'wait 600', 'up W' -TimeoutSeconds 20
            $scripts++
            if ($played.GameHeldForeground) { $heldForeground = $true }
            Start-Sleep -Seconds 2
        }
        $game = Get-Process -Id $session.ProcessId -ErrorAction SilentlyContinue
        $exited = (-not $game) -or $game.WaitForExit(40000)
        Check 'the stand-in ran its time and exited by itself' ($exited -and (Test-Path $stats) -and (Test-Path "$stats.child")) "exited: $exited, stats: $(Test-Path $stats), child stats: $(Test-Path "$stats.child")"
        Check 'the guard ended nothing' (-not (Get-RigGuardVerdict -Folder $session.GuardFolder)) "$(Get-RigGuardVerdict -Folder $session.GuardFolder)"
    } finally {
        Stop-IsolatedGame -Session $session
    }
    Check "$scripts input scripts were played and none found the stand-in in the foreground" ($scripts -gt 0 -and -not $heldForeground)
    $log = @(Get-Content $guardLog)
    Check 'the guard started the stand-in suspended with the shim in' ([bool]($log -match 'started .*RigMisbehave\.x64\.exe .* suspended, with the shim in before its first instruction')) ($log -join ' | ')
    Check 'the guard knew the 32-bit child as part of the run' ([bool]($log -match 'RigMisbehave\.x86\.exe \(pid \d+\) was started by a process of the run')) ($log -join ' | ')
    if ((Test-Path $stats) -and (Test-Path "$stats.child")) {
        $parent = Read-Stats $stats
        $child = Read-Stats "$stats.child"
        Check 'the child had the shim before it ran' ($parent.childShimmed -eq '1' -and $parent.childExit -eq '0') "shimmed $($parent.childShimmed), exit code $($parent.childExit)"
        Check-Stats -Who '64-bit parent' -Stats $parent -Ran $Seconds
        Check-Stats -Who '32-bit child' -Stats $child -Ran ($Seconds - 8)
        Check "the stand-in took the scripted key from isolated input ($($parent.wHeldFrames) frames with W held, $($parent.keyDownW) WM_KEYDOWN)" `
            ($parent.hostResult -eq '0' -and [int]$parent.wHeldFrames -gt 0) "host start $($parent.hostResult), frames with W held $($parent.wHeldFrames)"
        Check 'with isolated input in, GetForegroundWindow answered the stand-in its own window' ([int]$parent.believedForeground -gt 0) "in $($parent.believedForeground) rounds"
        $hostLog = @(Get-Content (Join-Path $mod 'CameraUnlockIsolatedInput.log'))
        Check 'isolated input never found the stand-in holding the real foreground' (-not ($hostLog -match 'real foreground')) (($hostLog -match 'real foreground' | Select-Object -First 2) -join ' | ')
    }

    # ------------------------------------------------------------------
    Write-Host "`n--- a stand-in someone else starts, as Steam starts a game, is taken in by its name"
    $adoptFolder = Join-Path $scratch 'adopt'
    $adoptStats = Join-Path $scratch 'adopt-stats.txt'
    [void](Start-RigGuard -Folder $adoptFolder -Names 'RigMisbehave.x86' -LimitSeconds 120)
    try {
        $adopted = Start-Process (Join-Path $tools 'RigMisbehave.x86.exe') -ArgumentList '8', "`"$adoptStats`"" -PassThru
        $exited = $adopted.WaitForExit(40000)
        $log = @(Get-Content (Join-Path $adoptFolder 'guard.log'))
        Check 'the guard took it in and put the shim in' ([bool]($log -match 'took in RigMisbehave\.x86\.exe .* named for this run, seen \d+ ms after it started: shim in')) ($log -join ' | ')
        Check 'it ran with the shim and exited by itself, with nothing ended by the guard' ($exited -and (Test-Path $adoptStats) -and -not (Get-RigGuardVerdict -Folder $adoptFolder)) "exited: $exited, verdict: $(Get-RigGuardVerdict -Folder $adoptFolder)"
        if (Test-Path $adoptStats) { Check-Stats -Who 'taken in by name' -Stats (Read-Stats $adoptStats) -Ran 6 }
        Write-Host ($log -match 'took in')
    } finally {
        Stop-RigGuard -Folder $adoptFolder
    }

    # ------------------------------------------------------------------
    Write-Host "`n--- a run held to ten seconds is ended whole"
    $limitFolder = Join-Path $scratch 'limit'
    [void](Start-RigGuard -Folder $limitFolder -LimitSeconds 10 -Launch (Join-Path $tools 'RigMisbehave.x64.exe') `
        -Arguments '60', "`"$(Join-Path $scratch 'limit-stats.txt')`"", 'child', "`"$(Join-Path $tools 'RigMisbehave.x86.exe')`"")
    try {
        Start-Sleep -Seconds 4
        $before = @(Get-Process -Name 'RigMisbehave.x64', 'RigMisbehave.x86' -ErrorAction SilentlyContinue)
        $gone = (Get-Date).AddSeconds(30)
        while (-not (Get-RigGuardVerdict -Folder $limitFolder) -and (Get-Date) -lt $gone) { Start-Sleep -Milliseconds 20 }
        $ended = Get-Date
        do {
            $after = @(Get-Process -Name 'RigMisbehave.x64', 'RigMisbehave.x86' -ErrorAction SilentlyContinue)
            if ($after.Count -gt 0) { Start-Sleep -Milliseconds 20 }
        } while ($after.Count -gt 0 -and (Get-Date) -lt $gone)
        Write-Host "both were gone from the process list $([int]((Get-Date) - $ended).TotalMilliseconds) ms after the verdict was written"
        $verdict = Get-RigGuardVerdict -Folder $limitFolder
        Check 'parent and child were both up before the limit' ($before.Count -eq 2) "$($before.Count) up"
        Check 'the guard ended both at the limit, and said so in one line' ($after.Count -eq 0 -and $verdict -match 'LIMIT: the run.s 10 seconds are up\. Ended 2 process') "still up: $($after.Count); verdict: $verdict"
        Write-Host $verdict
    } finally {
        Stop-RigGuard -Folder $limitFolder
    }

    # ------------------------------------------------------------------
    Write-Host "`n--- what the witness saw on the real desktop"
    Set-Content (Join-Path $watch 'watch.stop') ''
    if (-not $watcher.WaitForExit(20000)) { throw 'the watcher did not stop' }
    $seen = @(Get-Content (Join-Path $watch 'watch.log'))
    $changes = @($seen -match ' foreground pid=')
    $ours = @($changes -match 'exe=(RigMisbehave|CameraUnlockRig)')
    $displays = @($seen -match ' display ')
    Check "none of the $($changes.Count - 1) foreground changes in the run went to the stand-in" ($ours.Count -eq 0) ($ours -join ' | ')
    Check 'the display mode never changed' ($displays.Count -eq 1) ($displays -join ' | ')
    Write-Host ($seen[-1])
    Write-Host ($displays[0])
} catch {
    $failed = $_
} finally {
    if (-not $watcher.HasExited) { Stop-Process -Id $watcher.Id -Force }
    Get-Process -Name 'RigMisbehave.x64', 'RigMisbehave.x86' -ErrorAction SilentlyContinue | Stop-Process -Force
    if ($failed -or $script:Failures) {
        Write-Host "`nThe run's files are kept in $scratch"
    } else {
        Remove-Item $scratch -Recurse -Force -ErrorAction SilentlyContinue
    }
}
if ($failed) {
    Write-Host "FAIL  the test stopped: $failed" -ForegroundColor Red
    $script:Failures++
}
Write-Host "`n$script:Checks checks, $script:Failures failed"
exit $(if ($script:Failures) { 1 } else { 0 })
