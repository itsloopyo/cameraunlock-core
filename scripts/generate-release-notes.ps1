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
    # Re-written rather than copied. A byte-for-byte Copy-Item preserves whatever
    # encoding the hand-authored file has, and a UTF-8 BOM here publishes straight into
    # the release body - this is the likeliest path to carry one, because it is the only
    # file a human edits in an editor. Every Update-VendoredLoader README in the fleet
    # opens with a BOM, so the assumption that local files are BOM-less does not hold.
    Write-NotesFile -Path $OutputFile -Text (Get-Content -Raw -LiteralPath "RELEASE_NOTES.md")
    Get-Content $OutputFile
    exit 0
}

function Write-NotesFile {
    param([string]$Path, [string]$Text)

    # Written through .NET rather than Out-File/Set-Content because neither gives
    # BOM-less UTF-8 on Windows PowerShell 5.1: -Encoding utf8 there means UTF-8
    # WITH a BOM, and the notes file is handed straight to `gh release create
    # --notes-file`, so the BOM renders as a literal "" at the top of the
    # published release body. Set-Content with no -Encoding is worse - it writes
    # the system ANSI codepage, mangling any non-ASCII character in a commit
    # subject.
    #
    # The path is resolved through the provider: [IO.File] resolves a relative
    # path against [Environment]::CurrentDirectory, which does not follow
    # Set-Location.
    $full = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    [System.IO.File]::WriteAllText($full, $Text, (New-Object System.Text.UTF8Encoding $false))
}

# Check for previous tag
# Match version tags only. The nightly publisher moves the rolling `dev` tag to
# the tip on every build, so an unfiltered describe resolves to `dev` sitting
# just behind HEAD: a first release never reaches the branch below, and a later
# one diffs against last night's build instead of the previous version.
# Temporarily allow errors so git describe doesn't throw when there are no tags:
# under ErrorActionPreference=Stop, PowerShell 5.1 wraps the redirected stderr as
# a NativeCommandError and terminates before the first-release branch below runs.
$prevPref = $ErrorActionPreference
$ErrorActionPreference = "Continue"
$previousTag = git describe --tags --abbrev=0 --match 'v[0-9]*' HEAD^ 2>$null
$ErrorActionPreference = $prevPref
if ($LASTEXITCODE -ne 0) {
    # First release - use all commits
    Write-NotesFile -Path $OutputFile -Text "First release."
    Write-Host "First release - no previous tags found" -ForegroundColor Cyan
    Get-Content $OutputFile
    exit 0
}

# Get commits that touched artifact-affecting paths
Write-Host "Generating changelog from $previousTag to HEAD" -ForegroundColor Cyan
Write-Host "Artifact paths: $($ArtifactPaths -join ', ')" -ForegroundColor Gray

$commits = git log "$previousTag..HEAD" --pretty=format:"- %s" --no-merges -- $ArtifactPaths
if ($LASTEXITCODE -ne 0) {
    throw "git log $previousTag..HEAD failed with exit code $LASTEXITCODE. See the git error above."
}

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

Write-NotesFile -Path $OutputFile -Text $notes
Write-Host "`nRelease notes:" -ForegroundColor Green
Get-Content $OutputFile
