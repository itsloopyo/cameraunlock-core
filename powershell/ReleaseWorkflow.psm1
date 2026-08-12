#!/usr/bin/env pwsh
# ReleaseWorkflow.psm1 - Shared module for mod release automation
# Part of CameraUnlock-Core shared utilities

$ErrorActionPreference = "Stop"

<#
.SYNOPSIS
    Fast-forward the cameraunlock-core submodule to its origin/main tip.
.DESCRIPTION
    Called by Copy-SharedBundle by default so the templates / bodies /
    games.json shipped in a release zip are always whatever's on the
    cameraunlock-core main branch when the release is cut - regardless
    of where the mod's submodule pointer was last bumped to. This is
    the "single source of truth" guarantee: a body bug fix in
    cameraunlock-core ships to a mod's users on that mod's next release
    with no per-mod template re-syncing or submodule-pointer bumping.

    Locates the parent mod repo by walking one level up from CoreRoot
    (the standard layout is <mod>/cameraunlock-core/) and runs
    `git submodule update --remote --merge -- cameraunlock-core` against
    it. `--merge` rebases any local cameraunlock-core commits on top of
    origin/main; conflicts surface as a hard failure rather than a
    silent stale-bundle release.
.PARAMETER CoreRoot
    Path to the cameraunlock-core checkout (i.e. the submodule path).
#>
function Update-CameraUnlockCoreToRemoteTip {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$CoreRoot)

    $modRoot = [System.IO.Path]::GetFullPath((Join-Path $CoreRoot '..'))
    if (-not (Test-Path (Join-Path $modRoot '.gitmodules'))) {
        # Not running inside a mod repo with submodules - we're probably
        # in the cameraunlock-core dev tree itself. Nothing to refresh.
        return
    }

    if ($env:GITHUB_ACTIONS -eq 'true' -or $env:CI -eq 'true') {
        # actions/checkout clones submodules shallow (--depth=1). A subsequent
        # --remote --merge then fetches origin/main also shallow, and git sees
        # two grafted roots with no common ancestor: "refusing to merge
        # unrelated histories". CI must consume the committed submodule pointer
        # as-is; bumping it is a deliberate dev action with a commit attached.
        Write-Host "CI detected - skipping cameraunlock-core remote refresh (consuming committed pointer)." -ForegroundColor Gray
        return
    }

    Write-Host "Refreshing cameraunlock-core submodule from origin/main..." -ForegroundColor Cyan
    # Don't pipe stderr through 2>&1: in Windows PowerShell 5.1, native-command
    # stderr lines (git prints progress like "From https://...") get wrapped
    # as NativeCommandError records and turn a successful run (exit 0) into a
    # terminating exception. Let git stream both streams to the console
    # directly; we only care about $LASTEXITCODE for routing.
    & git -C $modRoot submodule update --remote --merge -- cameraunlock-core
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to fast-forward cameraunlock-core (git exit $LASTEXITCODE). Resolve local conflicts in cameraunlock-core/ and re-run, or pass -NoRefresh to Copy-SharedBundle to skip."
    }
}

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
        [string]$CoreRoot,
        # Skip pulling cameraunlock-core to its remote tip before copying.
        # Default behaviour fast-forwards the submodule so the bodies /
        # find-game.ps1 / games.json shipped in the release zip are
        # always whatever is on cameraunlock-core's main branch at
        # release time. Pass -NoRefresh in CI flows that have already
        # synced the submodule themselves, or during local iteration
        # when you're testing against a deliberately-pinned core.
        [switch]$NoRefresh
    )

    if (-not $CoreRoot) {
        # $PSScriptRoot is .../cameraunlock-core/powershell; the core root
        # is one up. Normalize with GetFullPath to collapse the `..`.
        $CoreRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    }

    if (-not $NoRefresh) {
        Update-CameraUnlockCoreToRemoteTip -CoreRoot $CoreRoot
    }

    # install-body-* and uninstall-body are the per-strategy script bodies
    # used by thin per-mod wrapper install.cmd / uninstall.cmd files. Every
    # mod ships every body in shared/; the wrapper picks the one matching
    # its FRAMEWORK_TYPE. Cheap (each body is ~10KB), and it means a mod
    # changing strategy across versions doesn't require a release-tooling
    # change. Bodies that haven't been ported from the templates/ shape
    # yet are skipped silently when missing - the mod is still on the old
    # in-tree copy and doesn't need them.
    $sources = @(
        @{ Src = 'data\games.json';                   Dest = 'games.json' }
        @{ Src = 'powershell\GamePathDetection.psm1'; Dest = 'GamePathDetection.psm1' }
        @{ Src = 'scripts\find-game.ps1';             Dest = 'find-game.ps1' }
        @{ Src = 'scripts\check-loader-arch.ps1';     Dest = 'check-loader-arch.ps1' }
        @{ Src = 'scripts\cecil-marker-check.ps1';    Dest = 'cecil-marker-check.ps1' }
    )
    $optionalBodies = @(
        'install-body-cecil.cmd'
        'install-body-asi.cmd'
        'install-body-melonloader.cmd'
        'install-body-reframework.cmd'
        'install-body-shim.cmd'
        'install-body-bepinex.cmd'
        'uninstall-body.cmd'
    )
    foreach ($body in $optionalBodies) {
        $srcPath = Join-Path $CoreRoot (Join-Path 'scripts' $body)
        if (Test-Path $srcPath) {
            $sources += @{ Src = "scripts\$body"; Dest = $body }
        }
    }

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
        # An empty range means a re-tag of an already-released commit or a bad
        # pathspec, never a core-only release (those still carry the submodule
        # pointer bump), so the generic fallback below must not apply.
        throw "No commits found in range '$commitRange'. If this is the first release, create a RELEASE_NOTES.md override instead."
    }

    # Filter out noise commits before categorization
    if (-not $IncludeAll) {
        $commits = @($commits | Where-Object { -not (Test-NoiseCommit $_) })
    }

    if ($commits.Count -eq 0 -and $useAllCommits) {
        throw "No user-facing commits found for the first release. Use conventional commit prefixes (feat:, fix:, perf:) or create a RELEASE_NOTES.md override."
    }

    # Categorize commits using conventional commit format
    $features = @()
    $fixes = @()
    $changes = @()
    $other = @()

    $generic = ($commits.Count -eq 0)
    if ($generic) {
        # Core-only release: everything since the last tag is a submodule
        # pointer bump or other noise-filtered commit, so there is nothing
        # mod-local to list. The shared bundle still picks up the new core at
        # package time, so release with a generic entry instead of blocking.
        $changes += '- Performance and stability improvements'
    }

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
        Generic = $generic
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
            git add -- $file
            if ($LASTEXITCODE -ne 0) { throw "git add failed for $file" }
        }
    }

    $stagedChanges = git diff --cached --name-only
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
        throw "Failed to push commits. Tag created locally. Run: git push origin $Branch; git push origin $tag"
    }

    # Push only the tag being released. `--tags` pushes every local tag, so a
    # remote missing older ones (a recreated repo, a fresh fork) gets them all
    # at once - release.yml fires per tag and republishes stale versions
    # alongside this one, each with its own Discord announcement.
    git push origin $tag
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to push tag $tag. Run manually: git push origin $tag"
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
    'Update-CameraUnlockCoreToRemoteTip',
    'Copy-SharedBundle',
    'Test-SemanticVersion',
    'Step-SemanticVersion',
    'Resolve-ReleaseVersion',
    'Test-CleanGitStatus',
    'Test-GitTagExists',
    'Test-NoiseCommit',
    'Update-ManifestVersion',
    'New-ChangelogFromCommits',
    'Get-ChangelogSection',
    'Invoke-VersionCommit',
    'New-ReleaseTag',
    'Get-CsprojVersion',
    'Set-CsprojVersion'
)
