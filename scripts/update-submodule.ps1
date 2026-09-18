#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# cameraunlock-core/scripts/update-submodule.ps1
# ============================================================================
# The second half of a mod repo's `pixi run update-submodule`. The task moves
# the submodule first (`git submodule update --remote cameraunlock-core`) and
# then runs this from the core it just checked out, so a repo pinned to a core
# older than this file still gets it on the first run.
#
# Restamps THIRD-PARTY-NOTICES.md with the commit now pinned and commits the
# pointer and the notices together. A bump used to commit the pointer alone,
# and the stale hash then failed the next `pixi run package` at
# Assert-CoreCommitInNotices, usually in CI, well after the bump.
#
# Refuses to run over uncommitted edits to THIRD-PARTY-NOTICES.md: committing
# that path would sweep them into a "bump" commit nobody reviewed as one.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$coreRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $coreRoot
Import-Module (Join-Path $coreRoot 'powershell/ReleaseWorkflow.psm1') -Force

$notices = 'THIRD-PARTY-NOTICES.md'
$paths = @('cameraunlock-core')
if (Test-Path (Join-Path $repoRoot $notices)) {
    & git -C $repoRoot diff --quiet HEAD -- $notices
    if ($LASTEXITCODE -ne 0) {
        throw "$notices has uncommitted changes. Commit or stash them first; this commits that file along with the submodule pointer."
    }
    $pin = (& git -C $coreRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Cannot read the cameraunlock-core checkout at $coreRoot." }
    $state = Sync-CoreCommitInNotices -RepoRoot $repoRoot -Commit $pin
    if ($state.Recorded -eq 0) {
        throw "$notices records no cameraunlock-core commit, so there is nothing to restamp to $pin. Add the pinned commit to its cameraunlock-core entry, then re-run."
    }
    $paths += $notices
}

& git -C $repoRoot add -- $paths
if ($LASTEXITCODE -ne 0) { throw "git add failed for $($paths -join ', ')." }

& git -C $repoRoot diff --cached --quiet -- $paths
if ($LASTEXITCODE -eq 0) {
    Write-Host 'cameraunlock-core is already up to date; nothing to commit.'
    exit 0
}

& git -C $repoRoot commit -m 'Update submodule to latest main' -- $paths
if ($LASTEXITCODE -ne 0) { throw 'git commit failed.' }
