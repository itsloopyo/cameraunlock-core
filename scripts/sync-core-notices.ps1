#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# cameraunlock-core/scripts/sync-core-notices.ps1
# ============================================================================
# Points each mod's THIRD-PARTY-NOTICES.md at the cameraunlock-core commit
# that mod actually pins.
#
# The notices file ships at the root of every release ZIP, so the commit it
# names is the attribution the user receives. Bumping the submodule pointer
# does not touch it, which is how the two drift apart without anyone seeing:
# a wrong hash reads exactly like a right one. Run this after any bump.
#
#   pwsh scripts/sync-core-notices.ps1              # every sibling mod repo
#   pwsh scripts/sync-core-notices.ps1 -Repo .      # one repo
#   pwsh scripts/sync-core-notices.ps1 -Check       # report only, exit 1 on drift
#
# A repo whose notices record no cameraunlock-core hash at all is reported
# and left alone: where the record belongs depends on that file's layout,
# and guessing produces attribution nobody wrote.
# ============================================================================

param(
    [string[]]$Repo,
    [switch]$Check
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$coreRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $coreRoot 'powershell/ReleaseWorkflow.psm1') -Force

if (-not $Repo) {
    # The canonical core checkout sits beside the mod repos it serves.
    $Repo = Get-ChildItem -Path (Split-Path -Parent $coreRoot) -Directory |
            Where-Object {
                (Test-Path (Join-Path $_.FullName '.gitmodules')) -and
                (Test-Path (Join-Path $_.FullName 'THIRD-PARTY-NOTICES.md'))
            } |
            Select-Object -ExpandProperty FullName
}

$updated = @()
$drifted = @()
$missing = @()

foreach ($path in $Repo) {
    $root = (Resolve-Path -LiteralPath $path).Path
    $name = Split-Path -Leaf $root

    if (-not (Get-PinnedCoreCommit -RepoRoot $root)) { continue }

    $state = Sync-CoreCommitInNotices -RepoRoot $root -ReadOnly:$Check

    if ($state.Recorded -eq 0) {
        $missing += $name
        Write-Host "no record  $name - notices name no cameraunlock-core commit (pins $($state.Pin))" -ForegroundColor Red
        continue
    }
    if ($state.Stale.Count -eq 0) {
        Write-Host "ok         $name" -ForegroundColor DarkGray
        continue
    }

    if ($Check) {
        $drifted += $name
        Write-Host "stale      $name - records $($state.Stale -join ', '), pins $($state.Pin)" -ForegroundColor Yellow
    } else {
        $updated += $name
        Write-Host "updated    $name - $($state.Stale -join ', ') -> $($state.Pin)" -ForegroundColor Green
    }
}

Write-Host ''
if ($Check) {
    Write-Host "$($drifted.Count) stale, $($missing.Count) with no record, of $($Repo.Count) repos."
} else {
    Write-Host "$($updated.Count) updated, $($missing.Count) with no record, of $($Repo.Count) repos."
    if ($updated.Count -gt 0) {
        Write-Host 'Commit the notices change in each updated repo.' -ForegroundColor Cyan
    }
}
if ($drifted.Count -gt 0 -or $missing.Count -gt 0) { exit 1 }
