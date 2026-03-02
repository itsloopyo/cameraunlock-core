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

# Manual override takes priority
if (Test-Path "RELEASE_NOTES.md") {
    Write-Host "Using RELEASE_NOTES.md override" -ForegroundColor Cyan
    Copy-Item "RELEASE_NOTES.md" $OutputFile
    Get-Content $OutputFile
    exit 0
}

# Check for previous tag
$previousTag = git describe --tags --abbrev=0 HEAD^ 2>$null
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
    throw "No artifact-affecting commits found between $previousTag and HEAD for paths: $($ArtifactPaths -join ', '). If this release has changes, widen ArtifactPaths or create a RELEASE_NOTES.md override."
}

# Filter out internal/noise commits
$filtered = $commits | Where-Object {
    $_ -notmatch "^- (chore|refactor|internal|clean ?up|wip|fixup|squash|ci|build|test|style|docs)(\(.*?\))?:" -and
    $_ -notmatch "^- (Update (cameraunlock|submodule)|Merge )" -and
    $_ -notmatch "^- (bump|release|version)" -and
    $_ -notmatch "^- Release v\d+"
}

if (-not $filtered) {
    throw "All commits between $previousTag and HEAD were filtered as noise. If this release has user-facing changes, use conventional commit prefixes (feat:, fix:, perf:) or create a RELEASE_NOTES.md override."
}

if ($filtered -is [array]) {
    $commitList = $filtered -join "`n"
} else {
    $commitList = $filtered
}
$notes = "## What's Changed in v$Version`n`n$commitList"

$notes | Out-File -FilePath $OutputFile -Encoding utf8
Write-Host "`nRelease notes:" -ForegroundColor Green
Get-Content $OutputFile
