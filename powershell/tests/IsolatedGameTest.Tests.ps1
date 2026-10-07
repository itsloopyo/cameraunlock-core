#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for IsolatedGameTest.psm1
# ============================================================================
# Run: pixi run test-powershell-isolated-game-test
#
# The parts that need no game: reading an import table, classifying it against
# data/isolated-input.json, the refusal that names what to build, the test port
# and writing it into a config, the saved files and folders a test restores, and
# the wait for a log line. The rig lock and the session wrapper are in
# GameRig.Tests.ps1. No Pester dependency, matching the other tests.
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

# --- import tables ----------------------------------------------------------
# user32.dll is on every Windows machine and imports from other system DLLs by name.
$user32 = Join-Path $env:SystemRoot 'System32\user32.dll'
$imports = Get-PeImports -Path $user32
Check 'a system DLL has imports' ($imports.Count -gt 0)
Check 'each import names its DLL and its functions' (@($imports | Where-Object { $_.Dll -and $_.Functions.Count -gt 0 }).Count -gt 0)
Check 'a file that is not a PE is refused' (Throws { Get-PeImports -Path $modulePath } 'not a PE file')

# --- coverage ---------------------------------------------------------------
$coverage = Get-IsolatedInputCoverage
$statuses = @($coverage.input_paths.PSObject.Properties | ForEach-Object { $_.Value.status }) +
            @($coverage.mod_hosts.PSObject.Properties | ForEach-Object { $_.Value.status })
Check 'every entry is supported, unsupported or not-needed' (@($statuses | Where-Object { $_ -notin 'supported', 'unsupported', 'not-needed' }).Count -eq 0)
$unbuilt = @($coverage.input_paths.PSObject.Properties + $coverage.mod_hosts.PSObject.Properties |
             Where-Object { $_.Value.status -eq 'unsupported' -and -not $_.Value.PSObject.Properties['build'] })
Check 'every unsupported entry says what to build' ($unbuilt.Count -eq 0) (($unbuilt | ForEach-Object Name) -join ', ')
$unproven = @($coverage.input_paths.PSObject.Properties |
              Where-Object { $_.Value.status -eq 'supported' -and -not $_.Value.PSObject.Properties['proven_in'] })
Check 'every supported input path names a game it was proven in' ($unproven.Count -eq 0) (($unproven | ForEach-Object Name) -join ', ')
$undetectable = @($coverage.input_paths.PSObject.Properties |
                  Where-Object { -not $_.Value.PSObject.Properties['imports'] -and -not $_.Value.PSObject.Properties['dlls'] })
Check 'every input path has something an import table shows' ($undetectable.Count -eq 0) (($undetectable | ForEach-Object Name) -join ', ')

# --- classifying a binary ---------------------------------------------------
# A synthetic classification: user32.dll imports none of the input APIs from itself, so the
# refusal for a binary with nothing recognisable is exercised on it.
$paths = @(Get-GameInputPaths -BinaryPath $user32)
Check 'a binary that imports no input API yields no paths' ($paths.Count -eq 0) (($paths | ForEach-Object Path) -join ', ')
Check 'a binary with no usable path is refused, and told not to use the real devices' `
    (Throws { Assert-IsolatedInputCovers -BinaryPath $user32 -ModHost native } 'Do not test it with the real keyboard and mouse')
# The script host, which nothing has been built for.
Check 'a mod host that is not supported is refused with what to build' `
    (Throws { Assert-IsolatedInputCovers -BinaryPath $user32 -ModHost script } 'cannot host isolated input yet\. Build:')
# A Java agent mod is a host of its own, and a supported one: only the binary is refused here.
$jvm = $(try { Assert-IsolatedInputCovers -BinaryPath $user32 -ModHost jvm | Out-Null; '' } catch { $_.Exception.Message })
Check 'a Java agent mod can host isolated input' ($jvm -match 'imports no input API' -and $jvm -notmatch 'cannot host isolated input yet') $jvm

# --- the test port ----------------------------------------------------------
# The two worked examples the fleet's prompts give.
$titanfall = Get-ModTestPort -RepoName 'titanfall-2-headtracking'
$starfield = Get-ModTestPort -RepoName 'starfield-headtracking'
Check 'titanfall-2 is 5183 or the next free port up' ($titanfall -ge 5183 -and $titanfall -lt 5193) "$titanfall"
Check 'starfield is 5198 or the next free port up' ($starfield -ge 5198 -and $starfield -lt 5208) "$starfield"
Check 'the suffix does not change the port' ((Get-ModTestPort -RepoName 'starfield') -eq $starfield)
Check 'the pose sender refuses port 4242' (Throws { Start-TestPoseSender -Port 4242 -PoseFile (Join-Path $env:TEMP 'p.txt') } '4242')

# --- saved state ------------------------------------------------------------
$root = Join-Path $env:TEMP ("isolated-game-test-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $root | Out-Null
try {
    $existing = Join-Path $root 'config.ini'
    $absent = Join-Path $root 'custom.ini'
    $state = Join-Path $root 'state'
    Set-Content $existing 'UdpPort=default' -Encoding ASCII
    Save-GameTestState -Files $existing, $absent -Folder $state
    Check 'a second save over an unrestored state is refused' (Throws { Save-GameTestState -Files $existing -Folder $state } 'never restored')
    Set-Content $existing 'UdpPort=5198' -Encoding ASCII
    Set-Content $absent '[General]' -Encoding ASCII
    Restore-GameTestState -Folder $state
    Check 'a file that existed is put back as it was' ((Get-Content $existing -Raw).Trim() -eq 'UdpPort=default')
    Check 'a file the test created is removed' (-not (Test-Path $absent))
    Check 'the saved state is gone once restored' (-not (Test-Path $state))

    # --- saved folders ------------------------------------------------------
    # A save folder as a game leaves it: a file deleted, one rewritten, one added, a folder added.
    $saves = Join-Path $root 'Saves'
    $newFolder = Join-Path $root 'Made By The Run'
    New-Item -ItemType Directory -Force (Join-Path $saves 'world one') | Out-Null
    Set-Content (Join-Path $saves 'latest.ini') 'world one' -Encoding ASCII
    Set-Content (Join-Path $saves 'world one\player.bin') 'the player' -Encoding ASCII
    Set-Content (Join-Path $saves 'world one\chunk-1.bin') 'a chunk' -Encoding ASCII
    $listing = { (Get-ChildItem $saves -Recurse -File | Sort-Object FullName | ForEach-Object { "$($_.FullName.Substring($saves.Length + 1))=$((Get-Content $_.FullName -Raw).Trim())" }) -join ';' }
    $asSaved = & $listing
    Save-GameTestState -Files $existing -Folders "$saves\", $newFolder -Folder $state
    Remove-Item (Join-Path $saves 'world one\player.bin')
    Set-Content (Join-Path $saves 'world one\chunk-1.bin') 'rewritten' -Encoding ASCII
    Set-Content (Join-Path $saves 'world one\chunk-2.bin') 'added' -Encoding ASCII
    New-Item -ItemType Directory -Force (Join-Path $saves 'world two'), $newFolder | Out-Null
    Set-Content (Join-Path $saves 'world two\player.bin') 'another' -Encoding ASCII
    Set-Content (Join-Path $newFolder 'log.txt') 'x' -Encoding ASCII
    Set-Content $existing 'UdpPort=5198' -Encoding ASCII

    # The stopped game still holds one file: the restore fails, and fails whole.
    $held = [IO.File]::Open((Join-Path $saves 'world one\chunk-1.bin'), 'Open', 'ReadWrite', 'None')
    try {
        Check 'a folder restore that cannot write a file throws' (Throws { Restore-GameTestState -Folder $state } 'robocopy could not make')
    } finally { $held.Dispose() }
    Check 'and leaves the folder there, with the saved state still to restore from' `
        ((Test-Path (Join-Path $saves 'world one\chunk-1.bin')) -and (Test-Path (Join-Path $saves 'latest.ini')) -and (Test-Path (Join-Path $state 'state.json')))
    Restore-GameTestState -Folder $state
    Check 'a folder is put back as it was: deleted files back, rewritten ones as before, added ones gone' ((& $listing) -eq $asSaved) (& $listing)
    Check 'a folder the game added inside it is gone' (-not (Test-Path (Join-Path $saves 'world two')))
    Check 'a folder that did not exist before is removed' (-not (Test-Path $newFolder))
    Check 'a file saved beside the folders is put back too' ((Get-Content $existing -Raw).Trim() -eq 'UdpPort=default')
    Check 'the saved state is gone once the folders are restored' (-not (Test-Path $state))
    Check 'a save of nothing is refused' (Throws { Save-GameTestState -Folder $state } 'nothing to save')
    Check 'a file named as a folder is refused' (Throws { Save-GameTestState -Folders $existing -Folder $state } 'is a file')
    Check 'a robocopy success code does not become the exit code' ($LASTEXITCODE -eq 0) "$LASTEXITCODE"

    # A file the game still holds is waited for when the caller asks.
    Save-GameTestState -Files $existing -Folder $state
    Set-Content $existing 'UdpPort=5198' -Encoding ASCII
    $held = [IO.File]::Open($existing, 'Open', 'ReadWrite', 'None')
    try {
        Check 'a file that is held throws at once without -RetrySeconds' (Throws { Restore-GameTestState -Folder $state } 'being used by another process')
        $release = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList '-NoProfile', '-Command', 'Start-Sleep 2'
        $waited = [Diagnostics.Stopwatch]::StartNew()
        $job = [PowerShell]::Create().AddScript({ param($h, $p) $p.WaitForExit(); $h.Dispose() }).AddArgument($held).AddArgument($release)
        $pending = $job.BeginInvoke()
        Restore-GameTestState -Folder $state -RetrySeconds 20
        $job.EndInvoke($pending); $job.Dispose()
        Check 'with -RetrySeconds it is put back once the file is let go' ((Get-Content $existing -Raw).Trim() -eq 'UdpPort=default' -and $waited.Elapsed.TotalSeconds -ge 1.5) "$($waited.Elapsed.TotalSeconds)"
    } finally { $held.Dispose() }

    # --- the test port in a config file -------------------------------------
    $ini = Join-Path $root 'CameraUnlock.ini'
    $bytesOf = { param([string]$Text) [Text.Encoding]::GetEncoding(28591).GetBytes($Text) }
    $textOf = { [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($ini)) }
    # A byte order mark, a byte that is not UTF-8 (0xE9), odd spacing, a key given twice, CRLF.
    [IO.File]::WriteAllBytes($ini, [byte[]](0xEF, 0xBB, 0xBF) + (& $bytesOf "; caf$([char]0xE9)`r`n[Network]`r`nUdpPort=1`r`n  udpport = 4242   `r`n`r`n[General]`r`nEnableOnStartup=true`r`n"))
    Check 'the port written is returned' ((Set-ModTestPort -IniPath $ini -Port 5198) -eq 5198)
    Check 'the last UdpPort line is replaced, its spelling and spacing kept, every other byte as it was' `
        ((& $textOf) -eq ("$([char]0xEF)$([char]0xBB)$([char]0xBF)" + "; caf$([char]0xE9)`r`n[Network]`r`nUdpPort=1`r`n  udpport = 5198`r`n`r`n[General]`r`nEnableOnStartup=true`r`n")) (& $textOf)
    [IO.File]::WriteAllBytes($ini, (& $bytesOf "[Network]`nDataFreshnessMs=500`n`n[General]`nEnableOnStartup=true`n"))
    Set-ModTestPort -IniPath $ini -Port 5198 | Out-Null
    Check 'a missing key goes at the end of [Network], with the file''s line ending' ((& $textOf) -eq "[Network]`nDataFreshnessMs=500`nUdpPort=5198`n`n[General]`nEnableOnStartup=true`n") (& $textOf)
    [IO.File]::WriteAllBytes($ini, (& $bytesOf "[General]`r`nEnableOnStartup=true"))
    Set-ModTestPort -IniPath $ini -Port 5198 | Out-Null
    Check 'a missing section goes at the end' ((& $textOf) -eq "[General]`r`nEnableOnStartup=true`r`n[Network]`r`nUdpPort=5198`r`n") (& $textOf)
    Remove-Item $ini
    Check 'a config that does not exist is refused' (Throws { Set-ModTestPort -IniPath $ini -Port 5198 } 'does not exist')
    Set-ModTestPort -IniPath $ini -Port 5198 -Create | Out-Null
    Check 'unless the test asks for one holding the port alone' ((& $textOf) -eq "[Network]`r`nUdpPort=5198`r`n") (& $textOf)
    Check 'port 4242 is never written' (Throws { Set-ModTestPort -IniPath $ini -Port 4242 } '4242')
    [IO.File]::WriteAllBytes($ini, [Text.Encoding]::Unicode.GetBytes("[Network]`r`nUdpPort=1`r`n"))
    Check 'a UTF-16 file is refused' (Throws { Set-ModTestPort -IniPath $ini -Port 5198 } 'NUL byte')

    # --- waiting for a log line ---------------------------------------------
    $gameLog = Join-Path $root 'game.log'
    Check 'a log that never shows the line throws, and one that does not exist says so' (Throws { Wait-GameLogLine -Path $gameLog -Match 'loaded' -TimeoutSeconds 1 } 'did not show ''loaded'' within 1 seconds\. It does not exist')
    # Read while its writer holds it open for writing, as a game holds its log.
    $writer = New-Object IO.StreamWriter((New-Object IO.FileStream($gameLog, 'Create', 'Write', 'Read')))
    try {
        $writer.WriteLine('boot'); $writer.WriteLine('save loaded (1.5 s)'); $writer.Flush()
        $found = Wait-GameLogLine -Path $gameLog -Match 'loaded \((\d|\.)+ s\)' -TimeoutSeconds 5
        Check 'a line is found in a log its writer holds open' ($found.Line -eq 'save loaded (1.5 s)' -and $found.LineNumber -eq 2) "$($found.Line)"
        Check 'the text itself is matched with -SimpleMatch' ((Wait-GameLogLine -Path $gameLog -Match '(1.5 s)' -SimpleMatch -TimeoutSeconds 5).LineNumber -eq 2)
        Check 'a line above -After is not found again, and the error gives the log''s last line' `
            (Throws { Wait-GameLogLine -Path $gameLog -Match 'loaded' -After $found.LineNumber -TimeoutSeconds 1 } 'Its last line: save loaded')
        # A line written while the wait is on.
        $late = [PowerShell]::Create().AddScript({ param($w) Start-Sleep -Milliseconds 1500; $w.WriteLine('save loaded again'); $w.Flush() }).AddArgument($writer)
        $pending = $late.BeginInvoke()
        $found = Wait-GameLogLine -Path $gameLog -Match 'loaded' -After $found.LineNumber -TimeoutSeconds 10
        $late.EndInvoke($pending); $late.Dispose()
        Check 'a line written during the wait is found' ($found.Line -eq 'save loaded again' -and $found.LineNumber -eq 3) "$($found.Line)"
    } finally { $writer.Dispose() }
    $goneSession = [pscustomobject]@{ ProcessId = (Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList '-NoProfile', '-Command', 'exit').Id }
    Wait-Process -Id $goneSession.ProcessId -ErrorAction SilentlyContinue
    Check 'with a session, the wait ends when the game has gone' (Throws { Wait-GameLogLine -Path $gameLog -Match 'never' -Session $goneSession -TimeoutSeconds 60 } 'went before')

    # --- the host DLL of a managed mod ---------------------------------------
    $modFolder = Join-Path $root 'plugins'
    New-Item -ItemType Directory -Force $modFolder | Out-Null
    $builtHost = Join-Path $root 'built.dll'
    Check 'a host DLL that is not built is refused with how to build it' `
        (Throws { Copy-IsolatedInputHost -ModFolder $modFolder -HostDll $builtHost } 'not built: .*built\.dll.*build-isolated-input-host')
    Set-Content $builtHost 'host' -Encoding ASCII
    Check 'a mod folder that does not exist is refused' `
        (Throws { Copy-IsolatedInputHost -ModFolder (Join-Path $root 'absent') -HostDll $builtHost } 'mod folder does not exist')
    $copied = Copy-IsolatedInputHost -ModFolder $modFolder -HostDll $builtHost
    Check 'the host DLL is copied beside the mod under the name the C# entry point loads' `
        ($copied -eq (Join-Path $modFolder 'CameraUnlockIsolatedInput.dll') -and (Test-Path $copied)) "$copied"

    $pose = Join-Path $root 'pose.txt'
    Set-TestPose -PoseFile $pose -X 15 -Yaw -12.5
    Check 'a pose is written as six invariant numbers' ((Get-Content $pose -Raw).Trim() -eq '15 0 0 -12.5 0 0')

    # The sender, against a listener on a free port: the pose in the file is what arrives.
    $listener = New-Object System.Net.Sockets.UdpClient (New-Object System.Net.IPEndPoint ([System.Net.IPAddress]::Loopback, 0))
    $listener.Client.ReceiveTimeout = 5000
    $port = $listener.Client.LocalEndPoint.Port
    $sender = Start-TestPoseSender -Port $port -PoseFile $pose -Minutes 1
    try {
        $remote = New-Object System.Net.IPEndPoint ([System.Net.IPAddress]::Any, 0)
        $packet = $null
        $deadline = (Get-Date).AddSeconds(8)
        # The pose is eased, so it is read until it has arrived.
        do { $packet = $listener.Receive([ref]$remote) }
        while ((Get-Date) -lt $deadline -and [Math]::Abs([BitConverter]::ToDouble($packet, 24) + 12.5) -gt 0.1)
        Check 'the sender sends 48-byte OpenTrack packets' ($packet.Length -eq 48)
        Check 'the x it sends is the one in the pose file' ([Math]::Abs([BitConverter]::ToDouble($packet, 0) - 15) -lt 0.2) "$([BitConverter]::ToDouble($packet, 0))"
        Check 'the yaw it sends is the one in the pose file' ([Math]::Abs([BitConverter]::ToDouble($packet, 24) + 12.5) -lt 0.1) "$([BitConverter]::ToDouble($packet, 24))"
    } finally {
        Stop-Process -Id $sender -Force -ErrorAction SilentlyContinue
        $listener.Close()
    }
} finally {
    Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) check(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host 'IsolatedGameTest: all checks passed' -ForegroundColor Green
