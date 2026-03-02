#!/usr/bin/env pwsh
# ReleaseWorkflow.psm1 - Shared module for mod release automation
# Part of CameraUnlock-Core shared utilities

$ErrorActionPreference = "Stop"

<#
.SYNOPSIS
    Tests if a version string is valid semantic versioning.
.PARAMETER Version
    Version string to validate (e.g., "1.0.1").
.OUTPUTS
    Boolean indicating if the version is valid.
#>
function Test-SemanticVersion {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version
    )

    return $Version -match '^\d+\.\d+\.\d+$'
}

<#
.SYNOPSIS
    Checks if the git working directory is clean.
.OUTPUTS
    Boolean indicating if the working directory is clean.
#>
function Test-CleanGitStatus {
    $gitStatus = git status --porcelain 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw "Not a git repository"
    }
    return -not $gitStatus
}

<#
.SYNOPSIS
    Checks if a git tag already exists.
.PARAMETER Tag
    Tag name to check for (e.g., "v1.0.1").
.OUTPUTS
    Boolean indicating if the tag exists.
#>
function Test-GitTagExists {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Tag
    )

    $tagExists = git tag -l $Tag 2>$null
    return ($LASTEXITCODE -eq 0 -and $tagExists)
}

<#
.SYNOPSIS
    Updates the version in a manifest.json file.
.PARAMETER ManifestPath
    Path to the manifest.json file.
.PARAMETER Version
    New version to set.
.PARAMETER VersionProperty
    Property name for version (defaults to trying "version" then "Version").
.OUTPUTS
    Hashtable with OldVersion and Updated status.
#>
function Update-ManifestVersion {
    param(
        [Parameter(Mandatory=$true)]
        [string]$ManifestPath,
        [Parameter(Mandatory=$true)]
        [string]$Version,
        [string]$VersionProperty = $null
    )

    if (-not (Test-Path $ManifestPath)) {
        throw "Manifest file not found: $ManifestPath"
    }

    $manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json

    # Determine version property name
    $propName = $VersionProperty
    if (-not $propName) {
        if ($manifest.PSObject.Properties['version']) {
            $propName = 'version'
        } elseif ($manifest.PSObject.Properties['Version']) {
            $propName = 'Version'
        } else {
            throw "Could not find version property in manifest"
        }
    }

    $oldVersion = $manifest.$propName

    if ($oldVersion -eq $Version) {
        return @{
            OldVersion = $oldVersion
            Updated = $false
        }
    }

    $manifest.$propName = $Version
    $manifest | ConvertTo-Json -Depth 10 | Set-Content $ManifestPath

    return @{
        OldVersion = $oldVersion
        Updated = $true
    }
}

<#
.SYNOPSIS
    Generates a changelog entry from git commit history.
.PARAMETER ChangelogPath
    Path to the CHANGELOG.md file.
.PARAMETER Version
    Version for the new changelog entry.
.OUTPUTS
    Hashtable with counts of features, fixes, and changes added.
#>
function New-ChangelogFromCommits {
    param(
        [Parameter(Mandatory=$true)]
        [string]$ChangelogPath,
        [Parameter(Mandatory=$true)]
        [string]$Version,
        [string[]]$ArtifactPaths
    )

    if (-not (Test-Path $ChangelogPath)) {
        throw "CHANGELOG.md not found: $ChangelogPath"
    }

    $changelog = Get-Content $ChangelogPath -Raw

    # Check if entry already exists
    if ($changelog -match "\[$Version\]") {
        return @{
            AlreadyExists = $true
            Features = 0
            Fixes = 0
            Changes = 0
        }
    }

    # Get commits since last tag
    $lastTag = git describe --tags --abbrev=0 2>$null
    if ($LASTEXITCODE -ne 0) {
        # First release - use all commits
        $commitRange = "HEAD"
        $useAllCommits = $true
    } else {
        $commitRange = "$lastTag..HEAD"
        $useAllCommits = $false
    }

    if ($useAllCommits) {
        if ($ArtifactPaths) {
            $commits = git log --pretty=format:"%s" --reverse --no-merges -- $ArtifactPaths
        } else {
            $commits = git log --pretty=format:"%s" --reverse --no-merges
        }
    } else {
        if ($ArtifactPaths) {
            $commits = git log $commitRange --pretty=format:"%s" --reverse --no-merges -- $ArtifactPaths
        } else {
            $commits = git log $commitRange --pretty=format:"%s" --reverse --no-merges
        }
    }
    if ($LASTEXITCODE -ne 0) {
        throw "git log failed (exit code $LASTEXITCODE) for range '$commitRange'. Check that the range is valid and the repository is not corrupt."
    }

    if (-not $commits) {
        throw "No commits found in range '$commitRange'. If this is the first release, create a RELEASE_NOTES.md override instead."
    }

    # Filter out noise commits before categorization
    $noisePattern = '^(chore|refactor|internal|clean ?up|wip|fixup|squash|ci|build|test|style|docs)(\(.*?\))?:'
    $commits = @($commits | Where-Object {
        $_ -notmatch $noisePattern -and
        $_ -notmatch '^Merge ' -and
        $_ -notmatch '^Release v\d+' -and
        $_ -notmatch '^(bump|version)'
    })

    if ($commits.Count -eq 0) {
        throw "All commits in range '$commitRange' were filtered as noise. If this release has user-facing changes, use conventional commit prefixes (feat:, fix:, perf:) or create a RELEASE_NOTES.md override."
    }

    # Categorize commits using conventional commit format
    $features = @()
    $fixes = @()
    $changes = @()
    $other = @()

    foreach ($commit in $commits) {
        if ($commit -match '^feat(\(.*?\))?:\s*(.+)$') {
            $features += "- $($matches[2])"
        } elseif ($commit -match '^fix(\(.*?\))?:\s*(.+)$') {
            $fixes += "- $($matches[2])"
        } elseif ($commit -match '^perf(\(.*?\))?:\s*(.+)$') {
            $changes += "- $($matches[2])"
        } else {
            $other += "- $commit"
        }
    }

    # Build new entry
    $date = Get-Date -Format 'yyyy-MM-dd'
    $newEntry = "`n## [$Version] - $date`n`n"

    if ($features.Count -gt 0) {
        $newEntry += "### Added`n`n"
        $newEntry += ($features -join "`n") + "`n`n"
    }

    if ($changes.Count -gt 0) {
        $newEntry += "### Changed`n`n"
        $newEntry += ($changes -join "`n") + "`n`n"
    }

    if ($fixes.Count -gt 0) {
        $newEntry += "### Fixed`n`n"
        $newEntry += ($fixes -join "`n") + "`n`n"
    }

    if ($other.Count -gt 0) {
        $newEntry += "### Other`n`n"
        $newEntry += ($other -join "`n") + "`n`n"
    }

    # Insert new entry after header
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$newEntry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$newEntry"
    }

    Set-Content $ChangelogPath $changelog

    return @{
        AlreadyExists = $false
        Features = $features.Count
        Fixes = $fixes.Count
        Changes = $changes.Count
    }
}

<#
.SYNOPSIS
    Gets the changelog section for a specific version.
.PARAMETER ChangelogPath
    Path to the CHANGELOG.md file.
.PARAMETER Version
    Version to get the section for.
.OUTPUTS
    String containing the changelog section or empty string if not found.
#>
function Get-ChangelogSection {
    param(
        [Parameter(Mandatory=$true)]
        [string]$ChangelogPath,
        [Parameter(Mandatory=$true)]
        [string]$Version
    )

    if (-not (Test-Path $ChangelogPath)) {
        return ""
    }

    $changelog = Get-Content $ChangelogPath -Raw

    if ($changelog -match "(?s)## \[$Version\].*?(?=(## \[|\z))") {
        return $matches[0].Trim()
    }

    return ""
}

<#
.SYNOPSIS
    Commits version bump changes.
.PARAMETER Version
    Version string for the commit message.
.PARAMETER Files
    Array of file paths to stage and commit.
.OUTPUTS
    Boolean indicating if a commit was made.
#>
function Invoke-VersionCommit {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,
        [Parameter(Mandatory=$true)]
        [string[]]$Files
    )

    foreach ($file in $Files) {
        if (Test-Path $file) {
            git add $file 2>$null
        }
    }

    $stagedChanges = git diff --cached --name-only 2>$null
    if (-not $stagedChanges) {
        return $false
    }

    git commit -m "chore: bump version to $Version"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to commit changes"
    }

    return $true
}

<#
.SYNOPSIS
    Creates and pushes a release tag.
.PARAMETER Version
    Version string (without 'v' prefix).
.PARAMETER Message
    Tag message/annotation.
.PARAMETER Branch
    Branch to push to (default: main).
.OUTPUTS
    None. Throws on failure.
#>
function New-ReleaseTag {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,
        [Parameter(Mandatory=$true)]
        [string]$Message,
        [string]$Branch = "main"
    )

    $tag = "v$Version"

    git tag -a $tag -m $Message
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to create git tag"
    }

    git push origin $Branch
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to push commits. Tag created locally. Run: git push origin $Branch --tags"
    }

    git push origin --tags
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to push tags. Run manually: git push origin --tags"
    }
}

<#
.SYNOPSIS
    Runs the full release workflow for a mod.
.PARAMETER Version
    Version to release (e.g., "1.0.1").
.PARAMETER ModName
    Display name of the mod (e.g., "Subnautica Head Tracking").
.PARAMETER ManifestPath
    Path to manifest.json (default: "manifest.json").
.PARAMETER ChangelogPath
    Path to CHANGELOG.md (default: "CHANGELOG.md").
.PARAMETER BuildCommand
    Build command to run (e.g., "dotnet build MyMod.csproj --configuration Release").
.PARAMETER ValidationScript
    Optional path to validation script to run before build.
.PARAMETER GitHubRepo
    GitHub repository for the release URL (e.g., "udkyo/subnautica-head-tracking").
.PARAMETER Branch
    Git branch to push to (default: main).
.OUTPUTS
    None. Writes progress to host and throws on failure.
#>
function Invoke-ModRelease {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Version,
        [Parameter(Mandatory=$true)]
        [string]$ModName,
        [string]$ManifestPath = "manifest.json",
        [string]$ChangelogPath = "CHANGELOG.md",
        [Parameter(Mandatory=$true)]
        [string]$BuildCommand,
        [string]$ValidationScript = $null,
        [string]$GitHubRepo = $null,
        [string]$Branch = "main"
    )

    Write-Host "======================================" -ForegroundColor Cyan
    Write-Host "   $ModName Release" -ForegroundColor Cyan
    Write-Host "======================================" -ForegroundColor Cyan
    Write-Host ""

    # Validate version format
    if (-not (Test-SemanticVersion -Version $Version)) {
        Write-Host "ERROR: Version '$Version' is not valid semantic versioning" -ForegroundColor Red
        Write-Host "Use format: X.Y.Z (e.g., 1.0.1)" -ForegroundColor Yellow
        exit 1
    }

    Write-Host "Target version: $Version" -ForegroundColor Green
    Write-Host ""

    # Step 1: Check git status
    Write-Host "[1/8] Checking git repository status..." -ForegroundColor Cyan
    try {
        if (-not (Test-CleanGitStatus)) {
            Write-Host "ERROR: Working directory has uncommitted changes:" -ForegroundColor Red
            git status --short
            Write-Host ""
            Write-Host "Please commit or stash changes before releasing" -ForegroundColor Yellow
            exit 1
        }
    } catch {
        Write-Host "ERROR: $_" -ForegroundColor Red
        exit 1
    }
    Write-Host "Working directory is clean" -ForegroundColor Green
    Write-Host ""

    # Step 2: Check if tag already exists
    Write-Host "[2/8] Checking if tag v$Version already exists..." -ForegroundColor Cyan
    if (Test-GitTagExists -Tag "v$Version") {
        Write-Host "ERROR: Git tag v$Version already exists" -ForegroundColor Red
        Write-Host "Choose a different version number" -ForegroundColor Yellow
        exit 1
    }
    Write-Host "Tag v$Version is available" -ForegroundColor Green
    Write-Host ""

    # Step 3: Update manifest.json
    Write-Host "[3/8] Checking manifest.json..." -ForegroundColor Cyan
    try {
        $manifestResult = Update-ManifestVersion -ManifestPath $ManifestPath -Version $Version
        if ($manifestResult.Updated) {
            Write-Host "  Updated version: $($manifestResult.OldVersion) -> $Version" -ForegroundColor Yellow
        }
        Write-Host "manifest.json at version $Version" -ForegroundColor Green
    } catch {
        Write-Host "ERROR: $_" -ForegroundColor Red
        exit 1
    }
    Write-Host ""

    # Step 4: Update CHANGELOG.md
    Write-Host "[4/8] Generating CHANGELOG.md from git history..." -ForegroundColor Cyan
    try {
        $changelogResult = New-ChangelogFromCommits -ChangelogPath $ChangelogPath -Version $Version
        if ($changelogResult.AlreadyExists) {
            Write-Host "CHANGELOG.md already has entry for v$Version" -ForegroundColor Green
        } else {
            Write-Host "CHANGELOG.md generated from commits" -ForegroundColor Green
            Write-Host "   Found: $($changelogResult.Features) features, $($changelogResult.Fixes) fixes, $($changelogResult.Changes) changes" -ForegroundColor Gray
        }
    } catch {
        Write-Host "ERROR: $_" -ForegroundColor Red
        exit 1
    }
    Write-Host ""

    # Step 5: Run validation checks
    Write-Host "[5/8] Running pre-release validation..." -ForegroundColor Cyan
    if ($ValidationScript -and (Test-Path $ValidationScript)) {
        & $ValidationScript
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: Validation failed" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "Warning: No validation script specified, skipping validation" -ForegroundColor Yellow
    }
    Write-Host ""

    # Step 6: Build release version
    Write-Host "[6/8] Building release version..." -ForegroundColor Cyan
    try {
        $buildOutput = Invoke-Expression $BuildCommand 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: Build failed" -ForegroundColor Red
            Write-Host $buildOutput
            exit 1
        }
        Write-Host "Build succeeded" -ForegroundColor Green
    } catch {
        Write-Host "ERROR: Build failed with exception" -ForegroundColor Red
        Write-Host $_.Exception.Message -ForegroundColor Red
        exit 1
    }
    Write-Host ""

    # Step 7: Commit changes
    Write-Host "[7/8] Committing version bump..." -ForegroundColor Cyan
    try {
        $committed = Invoke-VersionCommit -Version $Version -Files @($ManifestPath, $ChangelogPath)
        if ($committed) {
            Write-Host "Changes committed" -ForegroundColor Green
        } else {
            Write-Host "No changes to commit (version and changelog already up to date)" -ForegroundColor Green
        }
    } catch {
        Write-Host "ERROR: $_" -ForegroundColor Red
        exit 1
    }
    Write-Host ""

    # Step 8: Create and push tag
    Write-Host "[8/8] Creating and pushing release tag..." -ForegroundColor Cyan
    try {
        $tagMessage = "Release v$Version"
        $changelogSection = Get-ChangelogSection -ChangelogPath $ChangelogPath -Version $Version
        if ($changelogSection) {
            $tagMessage = "Release v$Version`n`n$changelogSection"
        }
        New-ReleaseTag -Version $Version -Message $tagMessage -Branch $Branch
        Write-Host "Tag v$Version created and pushed" -ForegroundColor Green
    } catch {
        Write-Host "ERROR: $_" -ForegroundColor Red
        exit 1
    }
    Write-Host ""

    # Success!
    Write-Host "======================================" -ForegroundColor Green
    Write-Host "   Release Complete!" -ForegroundColor Green
    Write-Host "======================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "Version $Version has been released!" -ForegroundColor Cyan
    Write-Host ""

    if ($GitHubRepo) {
        Write-Host "Next steps:" -ForegroundColor Yellow
        Write-Host "  1. GitHub Actions will automatically build and create the release" -ForegroundColor White
        Write-Host "  2. Monitor the workflow at: https://github.com/$GitHubRepo/actions" -ForegroundColor White
        Write-Host "  3. Once complete, the release will be available at:" -ForegroundColor White
        Write-Host "     https://github.com/$GitHubRepo/releases/tag/v$Version" -ForegroundColor White
        Write-Host ""
    }
}

# Export functions
Export-ModuleMember -Function @(
    'Test-SemanticVersion',
    'Test-CleanGitStatus',
    'Test-GitTagExists',
    'Update-ManifestVersion',
    'New-ChangelogFromCommits',
    'Get-ChangelogSection',
    'Invoke-VersionCommit',
    'New-ReleaseTag',
    'Invoke-ModRelease'
)
