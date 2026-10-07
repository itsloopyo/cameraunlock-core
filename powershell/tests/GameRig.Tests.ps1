#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for the rig lock and the session wrapper in IsolatedGameTest.psm1
# ============================================================================
# Run: pixi run test-powershell-game-rig
#
# Enter-GameRig / Exit-GameRig, Start- / Stop- / Invoke-IsolatedGameSession and the
# process sampler, with the takers as real separate processes and a stand-in for the
# game: a windowless exe compiled here that sleeps. The rig root is a temp folder
# (CAMERAUNLOCK_RIG_ROOT), so no real session's lock is met. No game is started, no
# window is shown and no input is sent. No Pester dependency, matching the other
# tests.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$modulePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'IsolatedGameTest.psm1'
Import-Module $modulePath -Force

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

function Throws {
    param([scriptblock]$Block, [string]$Pattern)
    try { & $Block | Out-Null; return $false } catch { return $_.Exception.Message -match $Pattern }
}

$root = Join-Path $env:TEMP ("game-rig-test-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $root | Out-Null
$env:CAMERAUNLOCK_RIG_ROOT = Join-Path $root 'rig'
$started = New-Object System.Collections.Generic.List[System.Diagnostics.Process]

# The stand-in game: its process name is this run's own, so two runs of these tests never meet.
$gameName = 'CuRigGame' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$gameExe = Join-Path $root "$gameName.exe"
Add-Type -TypeDefinition 'public static class StandInGame { public static void Main() { System.Threading.Thread.Sleep(600000); } }' `
    -OutputAssembly $gameExe -OutputType WindowsApplication
function Start-StandIn {
    $process = Start-Process $gameExe -PassThru
    $started.Add($process)
    $process
}
function Stop-StandIn {
    Get-Process -Name $gameName -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force; $_.WaitForExit() }
}

# A taker in a process of its own. When a gate file is named it says it is ready ("<gate>.<owner>")
# and waits for the gate, so several ask at the same moment. It logs "in <owner>" and "out <owner>" round its hold, and writes
# "<log>.<owner>" when it is refused.
$taker = Join-Path $root 'taker.ps1'
Set-Content $taker -Encoding ASCII -Value @'
param([string]$Module, [string]$Game, [string]$Owner, [string]$Log, [int]$HoldMs = 0, [string]$Gate = '', [int]$WaitSeconds = 3600,
      [switch]$Abandon, [string]$StateFolder = '', [string]$SaveFile = '', [string]$TokenFile = '', [switch]$WholeGpu)
$ErrorActionPreference = 'Stop'
Import-Module $Module
if ($Gate) { Set-Content "$Gate.$Owner" ''; while (-not (Test-Path $Gate)) { Start-Sleep -Milliseconds 10 } }
try { $rig = Enter-GameRig -Game $Game -Owner $Owner -WaitSeconds $WaitSeconds -StateFolder $StateFolder -WholeGpu:$WholeGpu 6>$null }
catch { Set-Content "$Log.$Owner" "$_"; exit 3 }
Add-Content $Log "in $Owner"
if ($TokenFile) { Set-Content $TokenFile $rig.Token }
if ($SaveFile) { Save-GameTestState -Files $SaveFile -Folder $StateFolder }
Start-Sleep -Milliseconds $HoldMs
Add-Content $Log "out $Owner"
if (-not $Abandon) { Exit-GameRig -Rig $rig }
'@
function Start-Taker {
    param([hashtable]$With)
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$taker`"", '-Module', "`"$modulePath`"")
    foreach ($name in $With.Keys) {
        if ($With[$name] -is [bool]) { if ($With[$name]) { $arguments += "-$name" } }
        else { $arguments += "-$name"; $arguments += "`"$($With[$name])`"" }
    }
    $process = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList $arguments
    $started.Add($process)
    $process
}
function Wait-All { param($Processes, [int]$Seconds = 90) foreach ($p in $Processes) { if (-not $p.WaitForExit($Seconds * 1000)) { throw "taker $($p.Id) did not end in $Seconds s" } } }
function Wait-Until { param([scriptblock]$Condition, [int]$Seconds = 30)
    $deadline = (Get-Date).AddSeconds($Seconds)
    while (-not (& $Condition)) { if ((Get-Date) -gt $deadline) { throw 'waited too long' }; Start-Sleep -Milliseconds 100 }
}
$absent = 'CuRigNoSuchGame'

try {
    # --- two takers and one lock --------------------------------------------
    # Six processes ask in the same moment and none of them waits: one gets the rig.
    $log = Join-Path $root 'race.log'
    $gate = Join-Path $root 'race.gate'
    $takers = 1..6 | ForEach-Object { Start-Taker @{ Game = 'race'; Owner = "t$_"; Log = $log; HoldMs = 6000; Gate = $gate; WaitSeconds = 0 } }
    Wait-Until { @(Get-ChildItem $root -Filter 'race.gate.t*').Count -eq 6 }
    Set-Content $gate ''
    Wait-All $takers
    $in = @(Get-Content $log | Where-Object { $_ -like 'in *' })
    $refused = @(Get-ChildItem $root -Filter 'race.log.t*')
    Check 'of six takers at once, one gets the rig' ($in.Count -eq 1) "$($in.Count) got it"
    # Told who has it, or, for one whose turn came before the winner's, who was ahead of it.
    Check 'and the other five are refused, told who has it' ($refused.Count -eq 5 -and @($refused | Where-Object { (Get-Content $_.FullName -Raw) -match 'the rig is in use: t\d \(pid \d+, since |waiting behind t\d \(pid \d+\)' }).Count -eq 5) `
        (($refused | ForEach-Object { Get-Content $_.FullName -Raw }) -join ' | ')
    Check 'a released rig leaves nothing behind' ($null -eq (Get-GameRig -Game 'race'))

    # Five ask at once and wait: each has the rig alone, and all five get it.
    $log = Join-Path $root 'turns.log'
    $gate = Join-Path $root 'turns.gate'
    $takers = 1..5 | ForEach-Object { Start-Taker @{ Game = 'turns'; Owner = "t$_"; Log = $log; HoldMs = 300; Gate = $gate } }
    Wait-Until { @(Get-ChildItem $root -Filter 'turns.gate.t*').Count -eq 5 }
    Set-Content $gate ''
    Wait-All $takers
    $lines = @(Get-Content $log)
    $alone = $lines.Count -eq 10
    for ($i = 0; $alone -and $i -lt 10; $i += 2) { $alone = $lines[$i] -like 'in *' -and $lines[$i + 1] -eq ($lines[$i] -replace '^in', 'out') }
    Check 'five waiting takers each hold the rig alone, and all are served' $alone ($lines -join ', ')

    # --- queue order --------------------------------------------------------
    $log = Join-Path $root 'queue.log'
    $mine = Enter-GameRig -Game 'queue' -Owner 'first' -WaitSeconds 0
    $takers = foreach ($name in 'w1', 'w2', 'w3') {
        Start-Taker @{ Game = 'queue'; Owner = $name; Log = $log; HoldMs = 200 }
        Wait-Until { (Get-GameRig -Game 'queue').Waiting -contains $name }
    }
    $state = Get-GameRig -Game 'queue'
    Check 'the rig says who holds it and who waits, in the order they asked' `
        ($state.Holder.Owner -eq 'first' -and $state.Holder.ProcessId -eq $PID -and $state.Live -and ($state.Waiting -join ',') -eq 'w1,w2,w3') ($state.Waiting -join ',')
    Wait-All @(Start-Taker @{ Game = 'queue'; Owner = 'counted'; Log = $log; WaitSeconds = 0 })
    Check 'a session that asks is told how many asked before it' ((Get-Content "$log.counted" -Raw) -match 'the rig is in use: first \(.*\), and 3 asked before this session') (Get-Content "$log.counted" -Raw)
    Exit-GameRig -Rig $mine
    Wait-All $takers
    Check 'waiters are served oldest first' ((@(Get-Content $log | Where-Object { $_ -like 'in *' }) -join ',') -eq 'in w1,in w2,in w3') ((Get-Content $log) -join ', ')

    # A waiter that gives up leaves the queue.
    $mine = Enter-GameRig -Game 'queue' -Owner 'first' -WaitSeconds 0
    Wait-All @(Start-Taker @{ Game = 'queue'; Owner = 'late'; Log = $log; WaitSeconds = 1 })
    Check 'a wait that runs out throws, naming the holder' ((Get-Content "$log.late" -Raw) -match 'the rig is in use: first .*Waited 1 seconds')
    Check 'and its ticket is gone' (@((Get-GameRig -Game 'queue').Waiting).Count -eq 0)
    Check 'a process that asks again for a rig it holds is told so, and does not wait on itself' (Throws { Enter-GameRig -Game 'queue' -Owner 'first' } 'already holds the rig')
    Exit-GameRig -Rig $mine

    # The block a waiter passes is handed what it waits on.
    $tokenFile = Join-Path $root 'told.token'
    $holder = Start-Taker @{ Game = 'told'; Owner = 'holder'; Log = $log; HoldMs = 3000; TokenFile = $tokenFile }
    Wait-Until { Test-Path $tokenFile }
    $told = New-Object System.Collections.Generic.List[string]
    $mine = Enter-GameRig -Game 'told' -Owner 'waiter' -Waiting { param($line) $told.Add($line) } 6>$null
    Check 'the waiting block is handed each line the wait says, and the rig then comes' ($told.Count -ge 1 -and $told[0] -match '^the rig is in use: holder \(pid \d+' -and (Get-GameRig -Game 'told').Holder.Owner -eq 'waiter') ($told -join ' | ')
    Exit-GameRig -Rig $mine
    Wait-All @($holder)

    # --- a lock left with its game ------------------------------------------
    # The run that leaves the game up is a process of its own, and stays alive: the next run
    # still takes the lock back, because it was left with the game and not with that process.
    $keptFile = Join-Path $root 'kept.json'
    $keeper = Join-Path $root 'keeper.ps1'
    Set-Content $keeper -Encoding ASCII -Value @'
param([string]$Module, [string]$Game, [string]$Exe, [string]$KeptIn, [string]$Done)
$ErrorActionPreference = 'Stop'
Import-Module $Module
$rig = Enter-GameRig -Game $Game -Owner 'keeper' -KeptIn $KeptIn -WaitSeconds 0
Start-Process $Exe
while (-not (Get-Process -Name $Game -ErrorAction SilentlyContinue)) { Start-Sleep -Milliseconds 50 }
Exit-GameRig -Rig $rig -KeptIn $KeptIn
Set-Content $Done ''
Start-Sleep 600
'@
    $keeperDone = Join-Path $root 'keeper.done'
    $keeperProcess = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$keeper`"",
        '-Module', "`"$modulePath`"", '-Game', $gameName, '-Exe', "`"$gameExe`"", '-KeptIn', "`"$keptFile`"", '-Done', "`"$keeperDone`"")
    $started.Add($keeperProcess)
    Wait-Until { Test-Path $keeperDone }
    $state = Get-GameRig -Game $gameName
    Check 'a lock left with its running game is still held and live, with no process of its own' ($state.Live -and $state.Holder.Owner -eq 'keeper' -and $state.Holder.ProcessId -eq 0 -and (Test-Path $keptFile))
    Check 'another session waits for it, and is told it was left with the game' (Throws { Enter-GameRig -Game $gameName -Owner 'other' -WaitSeconds 0 } 'the rig is in use: keeper \(left with its running game')
    $back = Enter-GameRig -Game $gameName -Owner 'keeper' -KeptIn $keptFile -WaitSeconds 0
    Check 'the next run takes it back while the run that left it is still alive' ($back.Token -eq $state.Holder.Token -and (Get-GameRig -Game $gameName).Holder.ProcessId -eq $PID -and -not $keeperProcess.HasExited)
    Exit-GameRig -Rig $back -KeptIn $keptFile
    Check 'and leaves it with the game again' ((Get-GameRig -Game $gameName).Holder.ProcessId -eq 0 -and (Test-Path $keptFile))
    Stop-KeptGame -Game $gameName -KeptIn $keptFile
    Check 'Stop-KeptGame stops that game, releases the lock and removes the file' (-not (Get-Process -Name $gameName -ErrorAction SilentlyContinue) -and $null -eq (Get-GameRig -Game $gameName) -and -not (Test-Path $keptFile))
    $back = Enter-GameRig -Game $gameName -Owner 'keeper' -KeptIn $keptFile -WaitSeconds 0
    Exit-GameRig -Rig $back -KeptIn $keptFile
    Check 'a rig given up with no game running is released, and nothing is kept' ($null -eq (Get-GameRig -Game $gameName) -and -not (Test-Path $keptFile))
    Set-Content $keptFile '{"Key":"x","Token":"not the one","Owner":"keeper"}' -Encoding ASCII
    [void](Start-StandIn)
    Check 'a kept file whose lock has gone does not take a game someone else has up' (Throws { Enter-GameRig -Game $gameName -Owner 'keeper' -KeptIn $keptFile -WaitSeconds 0 } 'someone started it by hand')
    Check 'and the file is removed' (-not (Test-Path $keptFile))
    Set-Content $keptFile '{"Key":"x","Token":"not the one","Owner":"keeper"}' -Encoding ASCII
    Stop-KeptGame -Game $gameName -KeptIn $keptFile
    Check 'Stop-KeptGame leaves a game it has no lock on alone' ([bool](Get-Process -Name $gameName -ErrorAction SilentlyContinue) -and -not (Test-Path $keptFile))
    Stop-StandIn
    $keeperProcess.Kill()

    # --- a dead owner's lock ------------------------------------------------
    $log = Join-Path $root 'dead.log'
    Wait-All @(Start-Taker @{ Game = $absent; Owner = 'gone'; Log = $log; Abandon = $true })
    $state = Get-GameRig -Game $absent
    Check 'a lock whose taker has gone, with no game running, is not live' ($state.Holder.Owner -eq 'gone' -and -not $state.Live)
    $warnings = @()
    $mine = Enter-GameRig -Game $absent -Owner 'next' -WaitSeconds 0 -WarningVariable warnings -WarningAction SilentlyContinue
    Check 'it is taken over, and the taker is told whose it was' ($mine.TakenOverFrom.Owner -eq 'gone' -and "$warnings" -match 'still held by gone') "$warnings"
    Check 'the rig is then the new owner''s' ((Get-GameRig -Game $absent).Holder.Owner -eq 'next')
    Exit-GameRig -Rig $mine

    # --- a live owner's lock ------------------------------------------------
    $log = Join-Path $root 'live.log'
    $tokenFile = Join-Path $root 'live.token'
    $holder = Start-Taker @{ Game = $absent; Owner = 'holder'; Log = $log; HoldMs = 12000; TokenFile = $tokenFile }
    Wait-Until { Test-Path $tokenFile }
    $token = (Get-Content $tokenFile -Raw).Trim()
    Check 'a live owner''s lock is never taken' (Throws { Enter-GameRig -Game $absent -Owner 'thief' -WaitSeconds 0 } 'the rig is in use: holder')
    $forged = [pscustomobject]@{ Key = $absent.ToLowerInvariant(); Owner = 'thief'; Token = 'not-it'; Path = (Join-Path $env:CAMERAUNLOCK_RIG_ROOT "$($absent.ToLowerInvariant())\lock") }
    Check 'nor released by a session that does not hold it' (Throws { Exit-GameRig -Rig $forged } 'not by the session releasing it')
    $forged.Token = $token
    Check 'nor released from another process, token or no token' (Throws { Exit-GameRig -Rig $forged } "taken by pid $($holder.Id), not by this process")
    Check 'nor taken back by token while its taker is running' (Throws { Enter-GameRig -Game $absent -Owner 'thief' -Token $token } 'still running')
    Check 'and it is still the holder''s after all of that' ((Get-GameRig -Game $absent).Holder.Owner -eq 'holder')
    Wait-All @($holder)

    # --- the game holds the rig for a taker that has gone -------------------
    $log = Join-Path $root 'game.log'
    $tokenFile = Join-Path $root 'game.token'
    Start-StandIn | Out-Null
    Check 'a game running with no lock on it is someone''s, and is waited for' (Throws { Enter-GameRig -Game $gameName -Owner 'next' -WaitSeconds 0 } 'is running and no session holds its rig')
    Stop-StandIn
    # The taker's own process ends, and the game it would have started goes on running.
    $holder = Start-Taker @{ Game = $gameName; Owner = 'starter'; Log = $log; Abandon = $true; TokenFile = $tokenFile; HoldMs = 3000 }
    Wait-Until { Test-Path $tokenFile }
    Start-StandIn | Out-Null
    Wait-All @($holder)
    Check 'a lock whose taker has gone is live while its game runs' ((Get-GameRig -Game $gameName).Live)
    Check 'and is not taken over' (Throws { Enter-GameRig -Game $gameName -Owner 'next' -WaitSeconds 0 } 'the rig is in use: starter')
    $mine = Enter-GameRig -Game $gameName -Owner 'starter' -Token ((Get-Content $tokenFile -Raw).Trim())
    Check 'the session''s next process takes it back by token' ((Get-GameRig -Game $gameName).Holder.ProcessId -eq $PID)
    Stop-StandIn
    Exit-GameRig -Rig $mine
    Check 'and releases it' ($null -eq (Get-GameRig -Game $gameName))

    # --- a session that ended without restoring -----------------------------
    $log = Join-Path $root 'unrestored.log'
    $kept = Join-Path $root 'unrestored.ini'
    $stateFolder = Join-Path $root 'unrestored-state'
    Set-Content $kept 'as the player had it' -Encoding ASCII
    Wait-All @(Start-Taker @{ Game = $absent; Owner = 'crashed'; Log = $log; Abandon = $true; StateFolder = $stateFolder; SaveFile = $kept })
    Set-Content $kept 'as the test left it' -Encoding ASCII
    Check 'a dead owner''s lock is not taken over while what it saved is not back' `
        (Throws { Enter-GameRig -Game $absent -Owner 'next' -WaitSeconds 0 } 'ended without putting the game''s files back.*Restore-GameTestState -Folder')
    Check 'and the rig says where that state is' ((Get-GameRig -Game $absent).Unrestored -eq $stateFolder)
    Restore-GameTestState -Folder $stateFolder
    $mine = Enter-GameRig -Game $absent -Owner 'next' -WaitSeconds 0 -WarningAction SilentlyContinue
    Check 'once it is restored the rig is taken over' ($mine.TakenOverFrom.Owner -eq 'crashed' -and (Get-Content $kept -Raw).Trim() -eq 'as the player had it')
    Exit-GameRig -Rig $mine

    # --- the whole graphics card --------------------------------------------
    $game = Enter-GameRig -Game 'cardgame' -Owner 'player' -WaitSeconds 0
    Check 'work that needs the whole card waits for a held rig' (Throws { Enter-GameRig -WholeGpu -Owner 'pictures' -WaitSeconds 0 } 'the graphics card is in use: cardgame is held by player')
    $other = Enter-GameRig -Game 'othergame' -Owner 'second' -WaitSeconds 0
    Check 'two different games are held at once' ((Get-GameRig -Game 'othergame').Live -and (Get-GameRig -Game 'cardgame').Live)
    Exit-GameRig -Rig $other
    Exit-GameRig -Rig $game
    $card = Enter-GameRig -WholeGpu -Owner 'pictures' -What 'a batch of eight' -WaitSeconds 0
    Check 'with every rig free it takes the card, with no game named' ((Get-GameRig).Holder.WholeGpu -and (Get-GameRig).Holder.What -eq 'a batch of eight')
    Check 'and no game''s rig is given out while it holds' (Throws { Enter-GameRig -Game 'cardgame' -Owner 'player' -WaitSeconds 0 } 'the graphics card is in use: gpu is held by pictures \(.*a batch of eight, the whole graphics card\)')
    $log = Join-Path $root 'card.log'
    Wait-All @(Start-Taker @{ Owner = 'more'; Log = $log; WaitSeconds = 0; WholeGpu = $true })
    Check 'nor a second run that needs the card' ((Get-Content "$log.more" -Raw) -match 'the rig is in use: pictures')
    Exit-GameRig -Rig $card
    Check 'a name that is not a process name is refused' (Throws { Enter-GameRig -Game '..\elsewhere' -Owner 'x' -WaitSeconds 0 } 'cannot name a rig')

    # --- a whole session ----------------------------------------------------
    $mod = Join-Path $root 'mod'
    $saves = Join-Path $root 'saves'
    New-Item -ItemType Directory -Force $mod, (Join-Path $saves 'world') | Out-Null
    $ini = Join-Path $mod 'CameraUnlock.ini'
    $made = Join-Path $mod 'made-by-the-test.txt'
    $gameLog = Join-Path $mod 'game.log'
    $iniText = "[Network]`r`nUdpPort=4242`r`n[General]`r`nEnableOnStartup=true`r`n"
    [IO.File]::WriteAllText($ini, $iniText)
    Set-Content (Join-Path $saves 'player.bin') 'the player' -Encoding ASCII
    Set-Content (Join-Path $saves 'world\chunk-1.bin') 'a chunk' -Encoding ASCII
    $csv = Join-Path $root 'samples.csv'
    $seen = @{}
    $session = @{
        ProcessName = $gameName; Launch = $gameExe; ModFolder = $mod; Owner = 'session test'; SettleSeconds = 0
        Files = @($made); Folders = @($saves); StateFolder = (Join-Path $root 'state'); IniPath = $ini; Port = 5999
    }
    $thrown = ''
    try {
        Invoke-IsolatedGameSession @session -Prepare { $seen.PreparedWithRig = (Get-GameRig -Game $gameName).Holder.Owner } `
            -Launched { param($s) $seen.LaunchedWith = "$($s.ProcessId) $([bool](Get-Process -Id $s.ProcessId -ErrorAction SilentlyContinue))" } `
            -Enter { param($s) $seen.EnteredWith = "$((Get-GameRig -Game $gameName).Holder.Token -eq $s.RigToken) $(Test-Path (Join-Path $root 'state'))" } `
            -Leave { param($s) $seen.LeftWith = "$((Get-GameRig -Game $gameName).Holder.Token -eq $s.RigToken) $(Test-Path (Join-Path $root 'state')) $([IO.File]::ReadAllText($ini) -eq $iniText)" } `
            -Collect { param($s) $seen.CollectedIni = [IO.File]::ReadAllText($ini); $seen.CollectedAfterStop = -not (Get-Process -Id $s.ProcessId -ErrorAction SilentlyContinue) } `
            -Run {
                param($s)
                $seen.Pid = $s.ProcessId
                $seen.Running = [bool](Get-Process -Id $s.ProcessId -ErrorAction SilentlyContinue)
                $seen.IniInRun = [IO.File]::ReadAllText($ini)
                $seen.Sampler = Start-GameProcessSampler -ProcessId $s.ProcessId -Path $csv -IntervalSeconds 1
                # What a game does to its files while it runs.
                Remove-Item (Join-Path $saves 'player.bin')
                Set-Content (Join-Path $saves 'world\chunk-1.bin') 'a chunk the game rewrote' -Encoding ASCII
                Set-Content (Join-Path $saves 'world\chunk-2.bin') 'a chunk the game added' -Encoding ASCII
                Set-Content $made 'x' -Encoding ASCII
                Set-Content $gameLog "booting`r`nreached the save" -Encoding ASCII
                $seen.Line = Wait-GameLogLine -Path $gameLog -Match 'reached the (\w+)' -Session $s -TimeoutSeconds 5
                Start-Sleep -Seconds 4
                throw 'the test block failed'
            }
    } catch { $thrown = "$_" }
    Check 'the block''s own error is the one that comes out' ($thrown -eq 'the test block failed') $thrown
    Check 'the block ran with the game up and the rig held' ($seen.Running -and $seen.PreparedWithRig -eq 'session test')
    Check 'the test port was in the mod''s config during the run, and nothing else in it changed' ($seen.IniInRun -eq $iniText.Replace('4242', '5999')) $seen.IniInRun
    Check 'the log wait returned the line and where it was' ($seen.Line.Line -eq 'reached the save' -and $seen.Line.LineNumber -eq 2)
    Check 'the collect block ran after the game stopped and before the files went back' ($seen.CollectedAfterStop -and $seen.CollectedIni -eq $seen.IniInRun)
    Check 'after a throw the game is stopped' (-not (Get-Process -Id $seen.Pid -ErrorAction SilentlyContinue))
    Check 'the config is back byte for byte' ([IO.File]::ReadAllText($ini) -eq $iniText)
    Check 'a file the test made is gone' (-not (Test-Path $made))
    $tree = (Get-ChildItem $saves -Recurse -File | ForEach-Object { "$($_.FullName.Substring($saves.Length + 1))=$((Get-Content $_.FullName -Raw).Trim())" }) -join ';'
    Check 'the folder is as it was: the deleted file back, the changed one as before, the added one gone' ($tree -eq 'player.bin=the player;world\chunk-1.bin=a chunk') $tree
    Check 'the saved state and the command file are gone' (-not (Test-Path $session.StateFolder) -and -not (Test-Path (Join-Path $mod 'CameraUnlockInput.txt')))
    Check 'the rig is free' ($null -eq (Get-GameRig -Game $gameName))
    Check 'the launched block was handed the session with the game''s process in it' ($seen.LaunchedWith -eq "$($seen.Pid) True") $seen.LaunchedWith
    Check 'the enter block ran with the rig held and nothing saved yet' ($seen.EnteredWith -eq 'True False') $seen.EnteredWith
    Check 'the leave block ran with the files back and the rig still held' ($seen.LeftWith -eq 'True False True') $seen.LeftWith

    # The sampler was left running by the block: it ends with the process it watched.
    $samplerProcess = Get-Process -Id $seen.Sampler.SamplerId -ErrorAction SilentlyContinue
    Check 'a sampler ends by itself once its process has gone' ((-not $samplerProcess) -or $samplerProcess.WaitForExit(20000))
    $rows = @(Get-Content $csv)
    $cells = @($rows | Select-Object -Skip 1 | ForEach-Object { , $_.Split(',') })
    Check 'it wrote a header and a row a second' ($rows[0] -eq 'time,elapsedSeconds,privateMB,workingSetMB,dedicatedVideoMB,cpuSeconds' -and $cells.Count -ge 2) "$($rows.Count) lines"
    Check 'every row is whole, with the memory and processor time as numbers' `
        (@($cells | Where-Object { $_.Count -ne 6 -or $_[2] -notmatch '^\d+\.\d$' -or $_[3] -notmatch '^\d+\.\d$' -or $_[4] -notmatch '^(\d+\.\d)?$' -or $_[5] -notmatch '^\d+\.\d\d$' }).Count -eq 0) ($rows -join ' / ')
    Check 'stopping a sampler that has already ended returns its file' ((Stop-GameProcessSampler -Sampler $seen.Sampler) -eq $csv)

    # Stopped by hand, between two rows.
    $watched = Start-StandIn
    $sampler = Start-GameProcessSampler -ProcessId $watched.Id -Path $csv -IntervalSeconds 1
    Start-Sleep -Seconds 3
    Stop-GameProcessSampler -Sampler $sampler | Out-Null
    $rows = @(Get-Content $csv)
    Check 'a sampler stopped by hand leaves whole rows and no stop file' ($rows.Count -ge 2 -and $rows[-1].Split(',').Count -eq 6 -and -not (Test-Path "$csv.stop") -and -not (Get-Process -Id $sampler.SamplerId -ErrorAction SilentlyContinue)) ($rows -join ' / ')
    Stop-StandIn
    Check 'a process that is not there cannot be sampled' (Throws { Start-GameProcessSampler -ProcessId $watched.Id -Path $csv } 'no process')

    # The block's result comes back, and a clean run restores too.
    $result = Invoke-IsolatedGameSession @session -Run { param($s) Set-Content (Join-Path $saves 'player.bin') 'changed' -Encoding ASCII; "pid $($s.ProcessId)" }
    Check 'the block''s result is returned' ("$result" -match '^pid \d+$') "$result"
    Check 'and a run that did not throw is put back as well' ((Get-Content (Join-Path $saves 'player.bin') -Raw).Trim() -eq 'the player' -and $null -eq (Get-GameRig -Game $gameName))

    # The game does not start: nothing to launch, and a launch that never shows the process.
    [IO.File]::WriteAllText($ini, $iniText)
    $noGame = @{} + $session
    $noGame.Launch = Join-Path $root 'not-there.exe'
    Check 'a launch that fails throws' (Throws { Invoke-IsolatedGameSession @noGame -Prepare { Set-Content (Join-Path $saves 'player.bin') 'prepared' -Encoding ASCII } -Run { throw 'never run' } } 'not-there|cannot find')
    Check 'with the config, the folder and the made file put back' `
        ([IO.File]::ReadAllText($ini) -eq $iniText -and (Get-Content (Join-Path $saves 'player.bin') -Raw).Trim() -eq 'the player' -and -not (Test-Path (Join-Path $root 'state')))
    Check 'the command file gone and the rig free' (-not (Test-Path (Join-Path $mod 'CameraUnlockInput.txt')) -and $null -eq (Get-GameRig -Game $gameName))
    $noGame = @{} + $session
    $noGame.ProcessName = 'CuRigNeverShows'
    $noGame.StartTimeoutSeconds = 2
    Check 'a game whose process never shows throws' (Throws { Invoke-IsolatedGameSession @noGame -Run { throw 'never run' } } 'did not start within 2 seconds')
    Stop-StandIn
    Check 'and that is put back too' ([IO.File]::ReadAllText($ini) -eq $iniText -and -not (Test-Path (Join-Path $root 'state')) -and $null -eq (Get-GameRig -Game 'CuRigNeverShows'))

    # A prepare step that throws, before any launch.
    Check 'a prepare step that throws stops the session before the launch' `
        (Throws { Invoke-IsolatedGameSession @session -Prepare { Set-Content $made 'x'; throw 'the build failed' } -Run { throw 'never run' } } 'the build failed')
    Check 'with what it had changed put back and the rig free' (-not (Test-Path $made) -and -not (Get-Process -Name $gameName -ErrorAction SilentlyContinue) -and $null -eq (Get-GameRig -Game $gameName))

    # An enter step that throws: nothing has been saved, and its other half still runs.
    $seen.Left = 0
    Check 'an enter step that throws stops the session before anything is saved' `
        (Throws { Invoke-IsolatedGameSession @session -Enter { throw 'the other lock never came' } -Leave { $seen.Left++ } -Prepare { Set-Content $made 'x' } -Run { throw 'never run' } } 'the other lock never came')
    Check 'with the leave step run once, nothing changed and the rig free' ($seen.Left -eq 1 -and -not (Test-Path $made) -and -not (Test-Path (Join-Path $root 'state')) -and $null -eq (Get-GameRig -Game $gameName))

    Check 'files to keep with nowhere to keep them are refused before anything is taken' `
        (Throws { Start-IsolatedGameSession -ProcessName $gameName -Launch $gameExe -ModFolder $mod -Owner 'x' -Files $made } 'need -StateFolder')
    Check 'a port to write with no port is refused' `
        (Throws { Start-IsolatedGameSession -ProcessName $gameName -Launch $gameExe -ModFolder $mod -Owner 'x' -IniPath $ini -StateFolder (Join-Path $root 'state') } 'need -Port')

    # --- a session over several processes -----------------------------------
    # Started in one process, which ends; read back and stopped in this one.
    $sessionFile = Join-Path $root 'session.json'
    $starter = Join-Path $root 'starter.ps1'
    Set-Content $starter -Encoding ASCII -Value @'
param([string]$Module, [string]$Game, [string]$Exe, [string]$Mod, [string]$Saves, [string]$State, [string]$SessionFile, [int]$Settle = 0)
$ErrorActionPreference = 'Stop'
Import-Module $Module
Start-IsolatedGameSession -ProcessName $Game -Launch $Exe -ModFolder $Mod -Owner 'split test' -SettleSeconds $Settle -Folders $Saves -StateFolder $State -SessionFile $SessionFile | Out-Null
'@
    $process = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$starter`"",
        '-Module', "`"$modulePath`"", '-Game', $gameName, '-Exe', "`"$gameExe`"", '-Mod', "`"$mod`"", '-Saves', "`"$saves`"", '-State', "`"$(Join-Path $root 'state')`"", '-SessionFile', "`"$sessionFile`"")
    Wait-All @($process)
    $split = Get-IsolatedGameSession -SessionFile $sessionFile
    $state = Get-GameRig -Game $gameName
    Check 'a session started in a process that has ended is read back, its game running' ([bool](Get-Process -Id $split.ProcessId -ErrorAction SilentlyContinue) -and $split.Owner -eq 'split test')
    Check 'and still holds the rig, through the game' ($state.Live -and $state.Holder.Owner -eq 'split test' -and $state.Holder.ProcessId -eq $process.Id)
    Check 'the rig names the file the session is kept in' ($state.Holder.SessionFile -eq $sessionFile) "$($state.Holder.SessionFile)"
    Check 'a session that waits behind it is told its own process has gone' (Throws { Enter-GameRig -Game $gameName -Owner 'next' -WaitSeconds 0 } 'the rig is in use: split test \(.*\), whose own process has gone while the game runs on')
    Check 'a second start over it is refused' (Throws { Start-IsolatedGameSession -ProcessName $gameName -Launch $gameExe -ModFolder $mod -Owner 'x' -SessionFile $sessionFile } 'never stopped')
    Set-Content (Join-Path $saves 'world\chunk-9.bin') 'added while it ran' -Encoding ASCII
    $seen.SplitLeft = ''
    Stop-IsolatedGameSession -SessionFile $sessionFile -Leave { param($s) $seen.SplitLeft = "$($s.RigToken -eq (Get-GameRig -Game $gameName).Holder.Token) $(Test-Path (Join-Path $saves 'world\chunk-9.bin'))" }
    Check 'its leave block runs in the process that stops it, files back and rig held' ($seen.SplitLeft -eq 'True False') $seen.SplitLeft
    Check 'stopping it from another process stops the game, restores and releases' `
        (-not (Get-Process -Id $split.ProcessId -ErrorAction SilentlyContinue) -and -not (Test-Path (Join-Path $saves 'world\chunk-9.bin')) -and $null -eq (Get-GameRig -Game $gameName) -and -not (Test-Path $sessionFile))
    Check 'a session that is not there says so' (Throws { Stop-IsolatedGameSession -SessionFile $sessionFile } 'no session at')

    # The starting process is killed while the game settles: the session is in its file already, and another process ends it.
    $killed = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$starter`"",
        '-Module', "`"$modulePath`"", '-Game', $gameName, '-Exe', "`"$gameExe`"", '-Mod', "`"$mod`"", '-Saves', "`"$saves`"", '-State', "`"$(Join-Path $root 'state')`"", '-SessionFile', "`"$sessionFile`"", '-Settle', '60')
    $started.Add($killed)
    Wait-Until { Test-Path $sessionFile }
    Check 'the starter is still settling when the session is in its file' (-not $killed.HasExited)
    Check 'stopping a session its starter still has is refused' (Throws { Stop-IsolatedGameSession -SessionFile $sessionFile } 'still in the hands of pid')
    Check 'and leaves its file, its game and its rig' ((Test-Path $sessionFile) -and [bool](Get-Process -Name $gameName -ErrorAction SilentlyContinue) -and (Get-GameRig -Game $gameName).Holder.ProcessId -eq $killed.Id)
    $killed.Kill(); $killed.WaitForExit()
    $orphan = Get-IsolatedGameSession -SessionFile $sessionFile
    $game = Get-Process -Name $gameName
    Check 'a session killed while the game settled left the game''s process id in its file' ($orphan.ProcessId -eq $game.Id) "$($orphan.ProcessId)"
    Set-Content (Join-Path $saves 'world\chunk-9.bin') 'added while it ran' -Encoding ASCII
    Stop-IsolatedGameSession -SessionFile (Get-GameRig -Game $gameName).Holder.SessionFile
    Check 'and another process stops that game, restores and releases' `
        (-not (Get-Process -Name $gameName -ErrorAction SilentlyContinue) -and -not (Test-Path (Join-Path $saves 'world\chunk-9.bin')) -and $null -eq (Get-GameRig -Game $gameName) -and -not (Test-Path $sessionFile))

    # A session whose lock has gone: with nothing saved, a game that went leaves the lock to be taken over.
    $lost = Start-IsolatedGameSession -ProcessName $gameName -Launch $gameExe -ModFolder $mod -Owner 'lost' -SettleSeconds 0 -SessionFile $sessionFile
    Stop-StandIn
    Exit-GameRig -Rig (Enter-GameRig -Game $gameName -Owner 'lost' -Token $lost.RigToken)
    $taken = Enter-GameRig -Game $gameName -Owner 'another session' -WaitSeconds 0
    Check 'stopping a session that has lost the rig throws and touches nothing of the game''s' (Throws { Stop-IsolatedGameSession -SessionFile $sessionFile } 'no longer holds the rig')
    Check 'the rig is still the other session''s, and the lost session''s file is gone' ((Get-GameRig -Game $gameName).Holder.Owner -eq 'another session' -and -not (Test-Path $sessionFile))
    Exit-GameRig -Rig $taken

    # The sequence a session has reached is what its next process needs. A stand-in for the mod's
    # answer: the done file already says script 1 was played.
    $commandFile = Join-Path $mod 'CameraUnlockInput.txt'
    Set-Content "$commandFile.done" '1' -Encoding ASCII
    $kept = [pscustomobject]@{ ProcessId = $PID; ProcessName = 'powershell'; CommandFile = $commandFile; Sequence = 0; SessionFile = $sessionFile }
    Invoke-GameInput -Session $kept -Commands 'wait 1' | Out-Null
    Check 'a session with a file of its own is written there after each script' ((Get-IsolatedGameSession -SessionFile $sessionFile).Sequence -eq 1)
    Remove-Item "$commandFile.done"
    Check 'a script the mod never answers throws' (Throws { Invoke-GameInput -Session $kept -Commands 'wait 1' -TimeoutSeconds 1 } 'was not played')
    Check 'and the number it used is in the file, so the next process does not ask with it again' ((Get-IsolatedGameSession -SessionFile $sessionFile).Sequence -eq 2)
    Set-Content "$commandFile.done" '1' -Encoding ASCII
    Remove-Item $sessionFile
    $stopped = [pscustomobject]@{ ProcessId = 0; ProcessName = 'powershell'; CommandFile = $commandFile; Sequence = 0 }
    Check 'a session whose game was stopped says so at once' (Throws { Invoke-GameInput -Session $stopped -Commands 'wait 1' } 'no game running')
    # The session object every mod's script builds by hand today has no SessionFile.
    $plain = [pscustomobject]@{ ProcessId = $PID; ProcessName = 'powershell'; CommandFile = $commandFile; Sequence = 0; Sender = 0 }
    $played = Invoke-GameInput -Session $plain -Commands 'wait 1'
    Check 'a session without one plays as before and writes nothing' ($played.Sequence -eq 1 -and $plain.Sequence -eq 1 -and -not (Test-Path $sessionFile))
} finally {
    # By the process objects, which stay tied to what was started: an id can be handed on once its process ends.
    foreach ($process in $started) { if (-not $process.HasExited) { $process.Kill() } }
    Stop-StandIn
    $env:CAMERAUNLOCK_RIG_ROOT = $null
    Start-Sleep -Milliseconds 500
    Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) check(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host 'GameRig: all checks passed' -ForegroundColor Green
