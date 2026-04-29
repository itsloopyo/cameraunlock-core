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
$previousTag = git describe --tags --abbrev=0 HEAD^ 2>$null
if ($LASTEXITCODE -ne 0) {
    # First release - use all commits
    "First release." | Set-Content $OutputFile
    Write-Host "First release - no previous tags found" -ForegroundColor Cyan
    Get-Content $OutputFile
    exit 0
}

# Get commit subjects (with submodule pointer commits expanded into their
# underlying library commits — see Get-CommitSubjectsWithSubmoduleExpansion).
Write-Host "Generating changelog from $previousTag to HEAD" -ForegroundColor Cyan
Write-Host "Artifact paths: $($ArtifactPaths -join ', ')" -ForegroundColor Gray

$subjects = Get-CommitSubjectsWithSubmoduleExpansion `
    -CommitRange "$previousTag..HEAD" `
    -ArtifactPaths $ArtifactPaths

if (-not $subjects) {
    throw "No artifact-affecting commits found between $previousTag and HEAD for paths: $($ArtifactPaths -join ', '). If this release has changes, widen ArtifactPaths or create a RELEASE_NOTES.md override."
}

$filtered = @($subjects | Where-Object { -not (Test-NoiseCommit $_) } | ForEach-Object { "- $_" })

if ($filtered.Count -eq 0) {
    throw "All commits between $previousTag and HEAD were filtered as noise. If this release has user-facing changes, use conventional commit prefixes (feat:, fix:, perf:) or create a RELEASE_NOTES.md override."
}

$commitList = $filtered -join "`n"
$notes = "## What's Changed in v$Version`n`n$commitList"

$notes | Out-File -FilePath $OutputFile -Encoding utf8
Write-Host "`nRelease notes:" -ForegroundColor Green
Get-Content $OutputFile
