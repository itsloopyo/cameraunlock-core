#!/usr/bin/env pwsh
# Shared release notes generator for CameraUnlock mods
# Generates changelog from commits that touched source files
#
# Usage: generate-release-notes.ps1 -Version <version> -ArtifactPaths <paths> [-ProjectName <name>]

param(
    [Parameter(Mandatory=$true)]
    [string]$Version,

    [Parameter(Mandatory=$true)]
    [string[]]$ArtifactPaths,

    [string]$ProjectName = "CameraUnlock Mod",

    [string]$OutputFile = "release-notes.txt"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $scriptDir "..\powershell\ReleaseWorkflow.psm1") -Force

# Manual override takes priority
if (Test-Path "RELEASE_NOTES.md") {
    Write-Host "Using RELEASE_NOTES.md override" -ForegroundColor Cyan
    Copy-Item "RELEASE_NOTES.md" $OutputFile
    Get-Content $OutputFile
    exit 0
}

# Check for previous tag
# Temporarily allow errors so git describe doesn't throw when there are no tags:
# under ErrorActionPreference=Stop, PowerShell 5.1 wraps the redirected stderr as
# a NativeCommandError and terminates before the first-release branch below runs.
$prevPref = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$previousTag = git describe --tags --abbrev=0 HEAD^ 2>$null
$ErrorActionPreference = $prevPref
if ($LASTEXITCODE -ne 0) {
    # First release - use all commits
    "First release." | Set-Content $OutputFile
    Write-Host "First release - no previous tags found" -ForegroundColor Cyan
    Get-Content $OutputFile
    exit 0
}

# Get commits that touched artifact-affecting paths
Write-Host "Generating changelog from $previousTag to HEAD" -ForegroundColor Cyan
Write-Host "Artifact paths: $($ArtifactPaths -join ', ')" -ForegroundColor Gray

$commits = git log "$previousTag..HEAD" --pretty=format:"- %s" --no-merges -- $ArtifactPaths

if (-not $commits) {
    # An empty match means a bad/stale -ArtifactPaths, never a core-only
    # release (those still carry the version-bump and submodule pointer
    # commits), so the generic fallback below must not apply.
    throw "No artifact-affecting commits found between $previousTag and HEAD for paths: $($ArtifactPaths -join ', '). If this release has changes, widen ArtifactPaths or create a RELEASE_NOTES.md override."
}

# Filter out internal/noise commits (strip "- " prefix for Test-NoiseCommit)
$filtered = @($commits | Where-Object {
    $subject = $_ -replace '^- ', ''
    -not (Test-NoiseCommit $subject)
})

if ($filtered.Count -eq 0) {
    # Core-only release: everything since the last tag is a submodule pointer
    # bump or other noise-filtered commit. The release still ships a fresh
    # shared bundle, so publish generic notes instead of failing the run.
    Write-Host "No mod-local commits since $previousTag - using generic release notes." -ForegroundColor Yellow
    $filtered = @('- Performance and stability improvements')
}

$commitList = $filtered -join "`n"
$notes = "## What's Changed in v$Version`n`n$commitList"

$notes | Out-File -FilePath $OutputFile -Encoding utf8
Write-Host "`nRelease notes:" -ForegroundColor Green
Get-Content $OutputFile
