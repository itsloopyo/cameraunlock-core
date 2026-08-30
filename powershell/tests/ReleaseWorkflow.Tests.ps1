#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for ReleaseWorkflow.psm1
# ============================================================================
# Run: pixi run test-powershell
#
# Every check here is a defect that shipped, not a shape that might break.
#
# The native-command checks run the module under Windows PowerShell 5.1's
# $ErrorActionPreference = 'Stop', where a native command's stderr becomes a
# terminating NativeCommandError. That turned `git` calls whose failure was an
# ordinary answer into a stack trace, and it is invisible on the machine of
# whoever wrote the call because their checkout has an origin/main.
#
# No Pester dependency, matching ModLoaderSetup.Soak.Tests.ps1.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$modulePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'ReleaseWorkflow.psm1'
Import-Module $modulePath -Force

$script:Failures = 0

function Check {
    param([string]$Name, [bool]$Condition, [string]$Detail)
    if ($Condition) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name - $Detail" -ForegroundColor Red
        $script:Failures++
    }
}

$sandbox = Join-Path ([System.IO.Path]::GetTempPath()) "cuc-releaseworkflow-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $sandbox -Force | Out-Null

function New-GitRepo {
    param([string]$Name)
    $path = Join-Path $sandbox $Name
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    & git -C $path init -q
    Set-Content -LiteralPath (Join-Path $path 'seed.txt') -Value 'seed'
    & git -C $path add -A
    & git -C $path -c user.email=t@t -c user.name=t commit -q -m seed
    return $path
}

# Returns '' when the action completed, or the terminating error's id when it
# did not - a string either way, so Check's detail never dereferences $null
# under Set-StrictMode.
function Get-ThrownId {
    param([scriptblock]$Action)
    try { & $Action | Out-Null; return '' } catch { return "$($_.FullyQualifiedErrorId): $($_.Exception.Message)" }
}

# --- native git calls must not terminate under 'Stop' -----------------------

$plainDir = Join-Path $sandbox 'not-a-work-tree'
New-Item -ItemType Directory -Path $plainDir -Force | Out-Null
$noOriginRepo = New-GitRepo 'no-origin'

$err = Get-ThrownId { Write-CoreBundleProvenance -CoreRoot $plainDir }
Check 'Write-CoreBundleProvenance survives a core path that is not a git work tree' ($err -eq '') "threw $err"

$err = Get-ThrownId { Write-CoreBundleProvenance -CoreRoot $noOriginRepo }
Check 'Write-CoreBundleProvenance survives a core checkout with no origin/main' ($err -eq '') "threw $err"

Push-Location $plainDir
try {
    $err = Get-ThrownId { Test-GitTagExists -Tag 'v1.0.0' }
    Check 'Test-GitTagExists returns false outside a git work tree' ($err -eq '') "threw $err"

    # This one is meant to throw, but with its own diagnostic rather than
    # NativeCommandError - the whole point of the message.
    try {
        Test-CleanGitStatus | Out-Null
        Check 'Test-CleanGitStatus throws outside a git work tree' $false 'did not throw'
    } catch {
        Check 'Test-CleanGitStatus throws its own diagnostic, not NativeCommandError' `
            ($_.Exception.Message -eq 'Not a git repository') "threw '$($_.Exception.Message)'"
    }
} finally { Pop-Location }

# --- Sync-CoreCommitInNotices rewrites only cameraunlock-core's hash --------

$pin = 'a1b2c3d4e5f60718293a4b5c6d7e8f9012345678'
$other = '0fedcba9876543210fedcba9876543210fedcba9'
$noticesRepo = New-GitRepo 'notices'
Set-Content -LiteralPath (Join-Path $noticesRepo '.gitmodules') -Value @'
[submodule "cameraunlock-core"]
	path = cameraunlock-core
	url = https://github.com/itsloopyo/cameraunlock-core.git
'@
& git -C $noticesRepo update-index --add --cacheinfo "160000,$pin,cameraunlock-core"
& git -C $noticesRepo add -- .gitmodules
& git -C $noticesRepo -c user.email=t@t -c user.name=t commit -q -m pin

$notices = Join-Path $noticesRepo 'THIRD-PARTY-NOTICES.md'
$body = @"
| Component | Commit | Licence | Notes |
| --- | --- | --- | --- |
| cameraunlock-core | 1111111111111111111111111111111111111111 | MIT | Compiled in |
| MinHook | $other | BSD-2-Clause | Vendored beside cameraunlock-core |

## MinHook

- Pinned commit: ``$other``

## cameraunlock-core

- Pinned commit: ``2222222222222222222222222222222222222222``
- MinHook commit: ``$other``
- Upstream: https://github.com/itsloopyo/cameraunlock-core
"@
[System.IO.File]::WriteAllText($notices, $body, (New-Object System.Text.UTF8Encoding($false)))

$state = Sync-CoreCommitInNotices -RepoRoot $noticesRepo
$written = [System.IO.File]::ReadAllText($notices)

Check 'the core table row is restamped' ($written -match "\| cameraunlock-core \| $pin \|") 'row not rewritten'
Check 'the core section commit bullet is restamped' ($written -match "^- Pinned commit: ``$pin``$" -or $written -match "Pinned commit: ``$pin``") 'bullet not rewritten'
Check "a MinHook hash on a row naming cameraunlock-core is left alone" ($written -match "\| MinHook \| $other \|") 'MinHook table row was rewritten'
Check "a MinHook hash in MinHook's own section is left alone" ($written -match "## MinHook\r?\n\r?\n- Pinned commit: ``$other``") "MinHook section was rewritten"
Check "a MinHook bullet inside the core section is left alone" ($written -match "- MinHook commit: ``$other``") 'a labelled foreign bullet was rewritten'
Check 'only the two core hashes were counted' ($state.Recorded -eq 2) "Recorded = $($state.Recorded)"

# --- Set-CsprojVersion preserves encoding ----------------------------------

foreach ($withBom in @($true, $false)) {
    $csproj = Join-Path $sandbox "encoding-$withBom.csproj"
    $xml = "<Project><PropertyGroup><Version>1.0.0</Version><Authors>Bj" + [char]0x00F6 + "rn</Authors></PropertyGroup></Project>"
    [System.IO.File]::WriteAllText($csproj, $xml, (New-Object System.Text.UTF8Encoding($withBom)))

    Set-CsprojVersion -CsprojPath $csproj -Version '2.3.4'

    $bytes = [System.IO.File]::ReadAllBytes($csproj)
    $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $roundTripped = [System.IO.File]::ReadAllText($csproj)
    Check "Set-CsprojVersion keeps the version (BOM=$withBom)" ((Get-CsprojVersion $csproj) -eq '2.3.4') "read back $(Get-CsprojVersion $csproj)"
    Check "Set-CsprojVersion keeps non-ASCII intact (BOM=$withBom)" ($roundTripped -match ([char]0x00F6)) 'the o-umlaut was re-encoded'
    Check "Set-CsprojVersion keeps the file's BOM state (BOM=$withBom)" ($hasBom -eq $withBom) "BOM is now $hasBom"
}

# --- cleanup ---------------------------------------------------------------

Remove-Item $sandbox -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ''
if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) check(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host 'all checks passed' -ForegroundColor Green
