#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for IsolatedGameTest.psm1
# ============================================================================
# Run: pixi run test-powershell-isolated-game-test
#
# The parts that need no game: reading an import table, classifying it against
# data/isolated-input.json, the refusal that names what to build, the test port
# and the saved state a test restores. No Pester dependency, matching the other
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
Check 'a managed mod is refused with what to build' `
    (Throws { Assert-IsolatedInputCovers -BinaryPath $user32 -ModHost managed } 'cannot host isolated input yet\. Build:')

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
