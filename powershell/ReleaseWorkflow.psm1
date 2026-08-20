#!/usr/bin/env pwsh
# ReleaseWorkflow.psm1 - Shared module for mod release automation
# Part of CameraUnlock-Core shared utilities

$ErrorActionPreference = "Stop"

<#
.SYNOPSIS
    Fast-forward the cameraunlock-core submodule to its origin/main tip.
.DESCRIPTION
    OPT-IN. Call this deliberately, or pass -RefreshCore to Copy-SharedBundle.
    It used to run on every Copy-SharedBundle, which meant `pixi run package`
    silently moved the submodule working tree out from under the developer -
    the packaged artifact was then built against a core commit the mod's
    history does not record, and `git status` grew an unexplained
    ` M cameraunlock-core` that a later scripted commit could sweep up.
    Moving the pin is a decision with a commit attached, not a side effect of
    packaging.

    What that costs, stated plainly: the old default was a "single source of
    truth" guarantee - a body or games.json fix in cameraunlock-core reached a
    mod's users on that mod's next release with no pointer bump. That is gone.
    A mod now ships whatever core commit it pins, so a stale pin ships a stale
    bundle. Copy-SharedBundle therefore prints the core commit it bundled, and
    warns when that commit is behind origin/main, so a stale bundle is loud
    instead of silent.

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
# Reports which cameraunlock-core commit the shared bundle is being copied from,
# and whether it is behind origin/main. Advisory only - it never fetches and never
# fails the packaging run, because a release must not depend on network reachability.
function Write-CoreBundleProvenance {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$CoreRoot)

    $head = & git -C $CoreRoot rev-parse --short HEAD 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $head) {
        Write-Host "  Shared bundle: cameraunlock-core commit unknown (not a git checkout)." -ForegroundColor DarkYellow
        return
    }
    Write-Host "  Shared bundle from cameraunlock-core $head" -ForegroundColor Gray

    # No fetch: report against whatever origin/main this checkout already knows.
    $behind = & git -C $CoreRoot rev-list --count "HEAD..origin/main" 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $behind) { return }
    if ([int]$behind -gt 0) {
        Write-Warning "cameraunlock-core is $behind commit(s) behind the origin/main it last saw, so this release ships an older shared bundle (install/uninstall bodies, find-game.ps1, games.json). If that is not deliberate, bump the submodule and commit the pointer before releasing."
    }
}

function Copy-SharedBundle {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$StagingDir,
        [string]$CoreRoot,
        # Opt in to fast-forwarding cameraunlock-core to origin/main before
        # copying. OFF by default: packaging must not move the submodule the
        # build just used, because the artifact would then be built against a
        # commit the mod's history does not record. Bumping the pin is a
        # deliberate act with a commit attached - run
        # Update-CameraUnlockCoreToRemoteTip, or `git submodule update --remote`,
        # and commit the pointer.
        [switch]$RefreshCore,
        # Deprecated and now redundant: not refreshing IS the default. Kept
        # because most of the fleet's package-release.ps1 passes it, and
        # removing a parameter breaks those callers for no gain. Accepted and
        # ignored; it will be removed in a future major version.
        [switch]$NoRefresh
    )

    if (-not $CoreRoot) {
        # $PSScriptRoot is .../cameraunlock-core/powershell; the core root
        # is one up. Normalize with GetFullPath to collapse the `..`.
        $CoreRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    }

    if ($RefreshCore -and $NoRefresh) {
        throw "Copy-SharedBundle: -RefreshCore and -NoRefresh contradict each other. -NoRefresh is the default and deprecated; drop it and pass -RefreshCore only if you mean to move the submodule pointer."
    }
    if ($RefreshCore) {
        Update-CameraUnlockCoreToRemoteTip -CoreRoot $CoreRoot
    }

    # Provenance, always. The bundle below is copied out of whatever core commit
    # is checked out right now, and since that is no longer force-refreshed, the
    # packager is the only place a stale pin can be caught before users get it.
    Write-CoreBundleProvenance -CoreRoot $CoreRoot

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

    if (-not (Test-Path -LiteralPath $ChangelogPath)) {
        throw "CHANGELOG.md not found: $ChangelogPath"
    }

    # CHANGELOG.md is UTF-8. Windows PowerShell 5.1 reads with the ANSI codepage
    # by default, so an existing entry with an en dash or an accented name comes
    # back mojibaked and is rewritten that way.
    $changelog = Get-Content -LiteralPath $ChangelogPath -Raw -Encoding UTF8

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
    # Match version tags only. The nightly publisher moves the rolling `dev` tag
    # to the tip on every build, so an unfiltered describe resolves to `dev`
    # sitting at or just behind HEAD: a first release never reaches the
    # first-release branch below, and a later one diffs against last night's
    # build instead of the previous version. Both surface as the empty-range
    # throw further down. generate-release-notes.ps1 already filters this way.
    # Temporarily allow errors so git describe doesn't throw when there are no tags
    $prevPref = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $lastTag = git describe --tags --abbrev=0 --match 'v[0-9]*' 2>$null
    $ErrorActionPreference = $prevPref
    if ($LASTEXITCODE -ne 0) {
        # First release - use all commits
        $commitRange = "HEAD"
        $useAllCommits = $true
    } else {
        $commitRange = "$lastTag..HEAD"
        $useAllCommits = $false
    }

    # git emits UTF-8, but a native command's output is decoded with
    # [Console]::OutputEncoding - OEM 437/850 on a stock Windows console - so a
    # commit subject with an en dash or an accented name arrives mojibaked and
    # is committed that way, then served as the GitHub release body.
    $prevConsoleEncoding = [Console]::OutputEncoding
    [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
    try {
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
    } finally {
        [Console]::OutputEncoding = $prevConsoleEncoding
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

    # $newEntry is built from raw commit subjects and is about to be used as a -replace
    # REPLACEMENT string, where .NET expands $_, $&, $1, $` and $'. A commit subject like
    # "fix: use $_ instead of $PSItem" would otherwise splice the entire existing
    # changelog into the new entry, and that gets committed and shipped as the release
    # body. Doubling every $ makes the regex engine emit it literally.
    $safeEntry = $newEntry -replace '\$', '$$$$'

    # Insert new entry after header
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$safeEntry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$safeEntry"
    }

    $changelog = $changelog.TrimEnd() + "`n"
    # BOM-less UTF-8: Set-Content's default is the ANSI codepage on 5.1, which
    # writes the mojibake the read above now avoids. .NET resolves a relative
    # path against the process directory, not the PowerShell location, so the
    # path is made absolute first.
    $changelogFullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ChangelogPath)
    [System.IO.File]::WriteAllText($changelogFullPath, $changelog, (New-Object System.Text.UTF8Encoding($false)))

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

    if (-not (Test-Path -LiteralPath $ChangelogPath)) {
        return ""
    }

    # This section becomes the GitHub release body, so it reads with the same
    # explicit UTF-8 the writer uses.
    $changelog = Get-Content -LiteralPath $ChangelogPath -Raw -Encoding UTF8

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
        # Trimmed: a pretty-printed <Version> element puts newlines and indentation in
        # the capture, and the CI gate compares it to the git tag with -ne. That fails
        # with both sides printing an identical-looking version and no visible cause.
        return $matches[1].Trim()
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
    'Write-CoreBundleProvenance',
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
