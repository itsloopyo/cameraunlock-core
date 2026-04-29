#!/usr/bin/env pwsh
# ReleaseWorkflow.psm1 - Shared module for mod release automation
# Part of CameraUnlock-Core shared utilities

$ErrorActionPreference = "Stop"

<#
.SYNOPSIS
    Stage the shared detection bundle into a release staging directory.
.DESCRIPTION
    Each mod's install.cmd / uninstall.cmd calls shared/find-game.ps1
    at user-install time to resolve the game path via the canonical
    games.json. This function copies games.json + GamePathDetection.psm1
    + find-game.ps1 into <StagingDir>/shared/ so the release ZIP carries
    them alongside install.cmd. Call this from each mod's
    package-release.ps1 *after* creating the staging dir and *before*
    compressing the zip.
.PARAMETER StagingDir
    Release staging directory (the one that's about to be zipped).
.PARAMETER CoreRoot
    Optional path to the cameraunlock-core checkout. Defaults to the
    checkout this module lives in.
#>
function Copy-SharedBundle {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$StagingDir,
        [string]$CoreRoot
    )

    if (-not $CoreRoot) {
        # $PSScriptRoot is .../cameraunlock-core/powershell; the core root
        # is one up. Normalize with GetFullPath to collapse the `..`.
        $CoreRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    }

    $sources = @(
        @{ Src = 'data\games.json';                   Dest = 'games.json' }
        @{ Src = 'powershell\GamePathDetection.psm1'; Dest = 'GamePathDetection.psm1' }
        @{ Src = 'scripts\find-game.ps1';             Dest = 'find-game.ps1' }
    )

    $sharedDir = Join-Path $StagingDir 'shared'
    if (-not (Test-Path $sharedDir)) {
        New-Item -ItemType Directory -Path $sharedDir -Force | Out-Null
    }

    foreach ($s in $sources) {
        $src = Join-Path $CoreRoot $s.Src
        if (-not (Test-Path $src)) {
            throw "Shared bundle source missing: $src. Is cameraunlock-core checked out?"
        }
        Copy-Item -Path $src -Destination (Join-Path $sharedDir $s.Dest) -Force
        Write-Host "  shared/$($s.Dest)" -ForegroundColor Green
    }
}

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
    Bumps a semantic version by the given component.
.DESCRIPTION
    Returns a new X.Y.Z by incrementing major, minor, or patch. Pre-release
    and build suffixes on the input are dropped (a "patch" bump from
    "1.2.3-rc.1" returns "1.2.4", not "1.2.4-rc.2"). The intent is that
    callers feed in a release version and get a release version back.
.PARAMETER Version
    Current version (must be parseable as X.Y.Z, optionally with -prerelease).
.PARAMETER Bump
    One of major, minor, patch.
.OUTPUTS
    The bumped version string.
#>
function Step-SemanticVersion {
    param(
        [Parameter(Mandatory=$true)][string]$Version,
        [Parameter(Mandatory=$true)][ValidateSet('major','minor','patch')][string]$Bump
    )

    $core = ($Version -split '[-+]')[0]
    if ($core -notmatch '^(\d+)\.(\d+)\.(\d+)$') {
        throw "Cannot bump '$Version': not in X.Y.Z form"
    }
    $maj = [int]$matches[1]
    $min = [int]$matches[2]
    $pat = [int]$matches[3]

    switch ($Bump) {
        'major' { return "$($maj + 1).0.0" }
        'minor' { return "$maj.$($min + 1).0" }
        'patch' { return "$maj.$min.$($pat + 1)" }
    }
}

<#
.SYNOPSIS
    Resolves a release argument (literal version or major/minor/patch) into a concrete version.
.DESCRIPTION
    The mod release scripts accept either a literal X.Y.Z or one of the
    bump keywords 'major', 'minor', 'patch'. This helper centralizes that
    resolution so the call site stays a one-liner.
.PARAMETER Argument
    User-supplied argument: 'major', 'minor', 'patch', or a literal X.Y.Z[-prerelease].
.PARAMETER CurrentVersion
    Current version, used as the base when Argument is a bump keyword.
.OUTPUTS
    The resolved new version string. Throws on invalid input.
#>
function Resolve-ReleaseVersion {
    param(
        [Parameter(Mandatory=$true)][string]$Argument,
        [string]$CurrentVersion
    )

    $arg = $Argument.Trim().ToLowerInvariant()

    if ($arg -in @('major','minor','patch')) {
        if ([string]::IsNullOrWhiteSpace($CurrentVersion)) {
            throw "Cannot bump '$arg': no current version available."
        }
        return Step-SemanticVersion -Version $CurrentVersion -Bump $arg
    }

    if ($Argument -notmatch '^\d+\.\d+\.\d+(-[a-zA-Z0-9.]+)?$') {
        throw "Invalid version '$Argument'. Use 'major', 'minor', 'patch', or X.Y.Z[-prerelease]."
    }
    return $Argument
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
    Tests if a commit subject is noise that should be filtered from changelogs and release notes.
.PARAMETER Subject
    Raw commit subject line (without any formatting prefix like "- ").
.OUTPUTS
    Boolean. $true if the commit is noise, $false if it should be included.
#>
function Test-NoiseCommit {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Subject
    )

    # `^Update (cameraunlock|submodule)` covers parent-repo subjects from a
    # submodule pointer bump. The bump commit itself is opaque ("Update
    # cameraunlock-core to v1.2.3" tells users nothing); the underlying
    # changes are expanded in by New-ChangelogFromCommits, so the parent
    # subject can be safely filtered as noise. Do NOT remove this pattern
    # without also stripping the expansion logic.
    $noisePattern = '^(chore|refactor|internal|clean ?up|wip|fixup|squash|ci|build|test|style|docs)(\(.*?\))?:'
    return (
        $Subject -match $noisePattern -or
        $Subject -match '^Merge ' -or
        $Subject -match '^Release v\d+' -or
        $Subject -match '^(bump|release|version)' -or
        $Subject -match '^Update (cameraunlock|submodule)'
    )
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
    Collects commit subjects across a range, expanding submodule pointer
    changes into the underlying library commits.
.DESCRIPTION
    Submodule pointer bumps in a parent repo show up as a single opaque
    commit ("Update cameraunlock-core to v1.2.3") that tells consumers
    nothing about what actually changed. This helper walks a commit range
    and, whenever it hits a commit that updates a submodule pointer
    (160000-mode entry in `git diff-tree --raw`), it descends into the
    submodule's own history and inlines the commit subjects between the
    old and new SHA. Library-side fixes/features therefore appear as
    first-class entries in mod changelogs and CI release notes instead of
    being hidden behind one anonymous "Update submodule" line.

    The parent commit's own subject is always retained; the noise filter
    in Test-NoiseCommit drops pure pointer-bump subjects ("Update
    cameraunlock..." / "chore: bump...") so they don't duplicate the
    expansion, while meaningful subjects ("feat: bump cameraunlock-core
    for X") survive as feat entries.
.PARAMETER CommitRange
    Range expression understood by `git log` (e.g. "v1.0.0..HEAD" or
    "HEAD"). Required when -UseAllCommits is $false.
.PARAMETER UseAllCommits
    If $true, the range is ignored and `git log` walks all reachable
    history (used for first releases where no prior tag exists).
.PARAMETER ArtifactPaths
    Optional pathspec list passed to `git log -- <paths>` to constrain
    which parent-repo commits are considered. Submodule expansion still
    applies to whatever commits survive the filter.
.OUTPUTS
    Array of commit subject strings, in chronological order, with
    submodule pointer commits replaced (in addition to their own subject)
    by the expanded list of subjects from inside the submodule.
#>
function Get-CommitSubjectsWithSubmoduleExpansion {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$CommitRange,
        [switch]$UseAllCommits,
        [string[]]$ArtifactPaths
    )

    $logArgs = @('log', '--pretty=format:%H%x09%s', '--reverse', '--no-merges')
    if (-not $UseAllCommits) { $logArgs += $CommitRange }
    if ($ArtifactPaths) { $logArgs += @('--') + $ArtifactPaths }
    $rawLines = & git @logArgs
    if ($LASTEXITCODE -ne 0) {
        throw "git log failed (exit code $LASTEXITCODE) for range '$CommitRange'."
    }
    if (-not $rawLines) { return @() }

    $repoRoot = (& git rev-parse --show-toplevel).Trim()
    $commits = @()
    foreach ($line in $rawLines) {
        $tab = $line.IndexOf("`t")
        if ($tab -lt 0) { continue }
        $hash    = $line.Substring(0, $tab)
        $subject = $line.Substring($tab + 1)

        $commits += $subject

        # `git diff-tree -r --raw <hash>` emits one line per changed entry:
        #   :<old_mode> <new_mode> <old_sha> <new_sha> <status>\t<path>
        # Submodule pointer entries use mode 160000.
        $rawDiff = & git diff-tree -r --raw --no-commit-id $hash 2>$null
        if ($LASTEXITCODE -eq 0 -and $rawDiff) {
            foreach ($rawLine in $rawDiff) {
                if ($rawLine -match '^:160000 160000 ([0-9a-f]+) ([0-9a-f]+) [A-Z]\s+(.+)$') {
                    $oldSha  = $matches[1]
                    $newSha  = $matches[2]
                    $subPath = $matches[3]
                    $subFull = Join-Path $repoRoot $subPath
                    if (Test-Path (Join-Path $subFull '.git')) {
                        Push-Location $subFull
                        try {
                            $subCommits = & git log "$oldSha..$newSha" --pretty=format:"%s" --reverse --no-merges 2>$null
                            if ($LASTEXITCODE -eq 0 -and $subCommits) {
                                $commits += @($subCommits)
                            }
                        } finally {
                            Pop-Location
                        }
                    }
                }
            }
        }
    }
    return $commits
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
        [string[]]$ArtifactPaths,
        [switch]$IncludeAll
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
    # Temporarily allow errors so git describe doesn't throw when there are no tags
    $prevPref = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $lastTag = git describe --tags --abbrev=0 2>$null
    $ErrorActionPreference = $prevPref
    if ($LASTEXITCODE -ne 0) {
        # First release - use all commits
        $commitRange = "HEAD"
        $useAllCommits = $true
    } else {
        $commitRange = "$lastTag..HEAD"
        $useAllCommits = $false
    }

    $commits = Get-CommitSubjectsWithSubmoduleExpansion -CommitRange $commitRange -UseAllCommits:$useAllCommits -ArtifactPaths $ArtifactPaths

    # Filter out noise commits before categorization
    if (-not $IncludeAll) {
        $commits = @($commits | Where-Object { -not (Test-NoiseCommit $_) })

        if ($commits.Count -eq 0) {
            throw "All commits in range '$commitRange' were filtered as noise. If this release has user-facing changes, use conventional commit prefixes (feat:, fix:, perf:) or create a RELEASE_NOTES.md override."
        }
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
    $newEntry = "## [$Version] - $date`n`n"

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

    $changelog = $changelog.TrimEnd() + "`n"
    Set-Content $ChangelogPath $changelog -NoNewline

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
    Gets the version from a .csproj file.
.PARAMETER CsprojPath
    Path to the .csproj file.
.OUTPUTS
    String containing the version.
#>
function Get-CsprojVersion {
    param(
        [Parameter(Mandatory=$true)]
        [string]$CsprojPath
    )

    if (-not (Test-Path $CsprojPath)) {
        throw "csproj not found: $CsprojPath"
    }

    $content = Get-Content $CsprojPath -Raw
    if ($content -match '<Version>([^<]+)</Version>') {
        return $matches[1]
    }

    throw "No <Version> element found in $CsprojPath"
}

<#
.SYNOPSIS
    Sets the version in a .csproj file.
.PARAMETER CsprojPath
    Path to the .csproj file.
.PARAMETER Version
    New version to set.
#>
function Set-CsprojVersion {
    param(
        [Parameter(Mandatory=$true)]
        [string]$CsprojPath,
        [Parameter(Mandatory=$true)]
        [string]$Version
    )

    if (-not (Test-Path $CsprojPath)) {
        throw "csproj not found: $CsprojPath"
    }

    $content = Get-Content $CsprojPath -Raw
    if ($content -notmatch '<Version>[^<]+</Version>') {
        throw "No <Version> element found in $CsprojPath"
    }

    $content = $content -replace '<Version>[^<]+</Version>', "<Version>$Version</Version>"
    $content | Set-Content $CsprojPath -NoNewline
}

# Export functions
Export-ModuleMember -Function @(
    'Copy-SharedBundle',
    'Test-SemanticVersion',
    'Step-SemanticVersion',
    'Resolve-ReleaseVersion',
    'Test-CleanGitStatus',
    'Test-GitTagExists',
    'Test-NoiseCommit',
    'Update-ManifestVersion',
    'Get-CommitSubjectsWithSubmoduleExpansion',
    'New-ChangelogFromCommits',
    'Get-ChangelogSection',
    'Invoke-VersionCommit',
    'New-ReleaseTag',
    'Get-CsprojVersion',
    'Set-CsprojVersion'
)
