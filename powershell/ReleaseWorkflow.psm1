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

    # The pointer has moved, so the commit THIRD-PARTY-NOTICES.md names is now
    # the wrong one. Nothing else in a bump touches that file, which is how the
    # two drift apart, so they move together here and get committed together.
    $bumped = (& git -C $CoreRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $bumped) { throw "Could not read the bumped cameraunlock-core commit." }
    $synced = Sync-CoreCommitInNotices -RepoRoot $modRoot -Commit $bumped
    if ($synced.Updated) {
        Write-Host "THIRD-PARTY-NOTICES.md now records cameraunlock-core $bumped - commit it alongside the pointer." -ForegroundColor Yellow
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

    # THIRD-PARTY-NOTICES.md is copied verbatim into the release ZIPs, so the
    # cameraunlock-core commit it names is the attribution the user receives.
    # A submodule bump does not touch it, so it is checked here rather than
    # trusted - a wrong hash reads exactly like a right one.
    Assert-CoreCommitInNotices -RepoRoot ([System.IO.Path]::GetFullPath((Join-Path $CoreRoot '..')))

    # install-body-* and uninstall-body are the per-strategy script bodies
    # used by thin per-mod wrapper install.cmd / uninstall.cmd files. Every
    # mod ships every body in shared/; the wrapper picks the one matching
    # its FRAMEWORK_TYPE. Cheap (each body is ~10KB), and it means a mod
    # changing strategy across versions doesn't require a release-tooling
    # change. Enumerated rather than listed, so a body added to core reaches
    # the fleet's release ZIPs without a second edit here - a list is one more
    # place to forget, and the mod that forgot ships a wrapper whose body is
    # not in the ZIP.
    $sources = @(
        @{ Src = 'data\games.json';                   Dest = 'games.json' }
        @{ Src = 'powershell\GamePathDetection.psm1'; Dest = 'GamePathDetection.psm1' }
        @{ Src = 'scripts\find-game.ps1';             Dest = 'find-game.ps1' }
        @{ Src = 'scripts\check-loader-arch.ps1';     Dest = 'check-loader-arch.ps1' }
        @{ Src = 'scripts\cecil-marker-check.ps1';    Dest = 'cecil-marker-check.ps1' }
    )
    $bodyDir = Join-Path $CoreRoot 'scripts'
    $bodies = @(Get-ChildItem -Path $bodyDir -File -Filter 'install-body-*.cmd' | Select-Object -ExpandProperty Name)
    $bodies += 'uninstall-body.cmd'
    foreach ($body in $bodies) {
        $sources += @{ Src = "scripts\$body"; Dest = $body }
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
function Copy-LicenceNotices {
    <#
    .SYNOPSIS
        Stage the licence notices that must accompany a binary distribution.
    .DESCRIPTION
        MIT, BSD-2-Clause and BSD-3-Clause all require the copyright notice,
        the conditions and the disclaimer to travel with the binary, not just
        with the source. Every ZIP we publish is a binary distribution, so
        LICENSE and THIRD-PARTY-NOTICES.md belong at the root of each one -
        the Nexus ZIP as much as the installer ZIP.

        A missing notice is a licence violation, so this throws rather than
        skipping. A silent skip turns a compliance failure into a green build.
    #>
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$StagingDir,
        [Parameter(Mandatory=$true)][string]$ProjectRoot,
        # Extra repo-root documents to stage alongside the notices.
        [string[]]$Additional = @()
    )

    if (-not (Test-Path $StagingDir)) {
        throw "Copy-LicenceNotices: staging directory does not exist: $StagingDir"
    }

    foreach ($doc in (@('LICENSE', 'THIRD-PARTY-NOTICES.md') + $Additional)) {
        $src = Join-Path $ProjectRoot $doc
        if (-not (Test-Path $src)) {
            throw "Required notice file not found: $doc. Every published ZIP is a binary distribution and must carry it."
        }
        Copy-Item $src -Destination $StagingDir -Force
        Write-Host "  $doc" -ForegroundColor Green
    }

    # cameraunlock-core is MIT under a different copyright holder from the
    # mod's own LICENSE, and it is compiled into the shipped DLLs, so MIT wants
    # its notice in this distribution too. "It is our own code" is not an
    # exemption: the test is whose name is on the LICENSE file, not who wrote
    # it.
    $coreLicence = Join-Path $PSScriptRoot '..\LICENSE'
    if (-not (Test-Path $coreLicence)) {
        throw "cameraunlock-core/LICENSE not found at $coreLicence. It is compiled into the shipped DLLs and its notice must travel with them."
    }
    $licencesDir = Join-Path $StagingDir 'licenses'
    New-Item -ItemType Directory -Path $licencesDir -Force | Out-Null
    Copy-Item $coreLicence -Destination (Join-Path $licencesDir 'cameraunlock-core-LICENSE.txt') -Force
    Write-Host "  licenses/cameraunlock-core-LICENSE.txt" -ForegroundColor Green
}

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
    Reads the authoritative version out of whichever file a mod keeps it in.
.DESCRIPTION
    The fleet keeps its version in whichever file its toolchain already had:
    a .csproj <Version>, a CMakeLists project(... VERSION ...), Cargo.toml,
    pixi.toml, a JSON manifest, or - for the largest group, the ASI mods - a
    plain C++ header. Everything downstream of the tag-vs-file check is
    loader-agnostic, so the reusable release workflow parameterises just this
    one read rather than shipping a workflow per toolchain.

    'regex' is the deliberate escape hatch for the last of those. A version in
    src/version.h, gradle.properties or install.cmd's `set "MOD_VERSION=..."`
    has no schema to parse, only a line to match, and enumerating a named
    Source per spelling would mean editing this module every time a mod picks
    a new macro name. Multiple capture groups are joined with '.', which is
    what the VERSION_MAJOR / VERSION_MINOR / VERSION_PATCH headers need.

    This lives here, not in workflow YAML, for the same reason the build does:
    a developer running the release script and CI validating the tag have to
    read the version the same way or the check passes locally and fails on the
    tag push.
.PARAMETER Source
    csproj | cmake | cargo | pixi | manifest | regex
.PARAMETER Path
    Path to the file holding the version.
.PARAMETER Key
    Manifest only. Dotted path to the version property, for manifests that nest
    it (launcher-manifest.json keeps it at mod_info.version). Defaults to
    'version'.
.PARAMETER Pattern
    Regex only, and required there. Must capture the version; if it captures
    several groups they are joined with '.'.
.OUTPUTS
    String containing the version.
#>
function Get-ProjectVersion {
    param(
        [Parameter(Mandatory=$true)]
        [ValidateSet('csproj', 'cmake', 'cargo', 'pixi', 'manifest', 'regex')]
        [string]$Source,
        [Parameter(Mandatory=$true)]
        [string]$Path,
        [string]$Key = 'version',
        [string]$Pattern
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Version source not found: $Path (source type '$Source')"
    }

    switch ($Source) {
        'csproj' { return Get-CsprojVersion $Path }

        'cmake' {
            $content = Get-Content -LiteralPath $Path -Raw
            # project(Name VERSION x.y.z LANGUAGES ...). CMake allows up to four
            # components; the tag check wants exactly what is written here, so the
            # capture is left as-is rather than normalised to three.
            if ($content -match '(?is)\bproject\s*\([^)]*?\bVERSION\s+([0-9]+(?:\.[0-9]+)*)') {
                return $matches[1]
            }
            throw "No project(... VERSION ...) found in $Path"
        }

        'cargo' { return Get-TomlTableVersion -Path $Path -Tables @('package') }

        # 'project' is the deprecated spelling of pixi's workspace table. Both are
        # still on disk across the fleet, so both are accepted here.
        'pixi' { return Get-TomlTableVersion -Path $Path -Tables @('workspace', 'project') }

        'manifest' {
            $json = (Get-Content -LiteralPath $Path -Raw).TrimStart([char]0xFEFF) | ConvertFrom-Json
            $node = $json
            foreach ($segment in ($Key -split '\.')) {
                if ($null -eq $node -or -not $node.PSObject.Properties[$segment]) {
                    throw "No '$Key' property in $Path (stopped at '$segment')"
                }
                $node = $node.$segment
            }
            if ([string]::IsNullOrWhiteSpace([string]$node)) {
                throw "Property '$Key' in $Path is empty"
            }
            return [string]$node
        }

        'regex' {
            if ([string]::IsNullOrWhiteSpace($Pattern)) {
                throw "Source 'regex' needs -Pattern; there is nothing to match in $Path without one."
            }
            $content = Get-Content -LiteralPath $Path -Raw
            $match = [regex]::Match($content, $Pattern)
            if (-not $match.Success) {
                throw "Pattern '$Pattern' did not match anything in $Path"
            }
            $groups = @($match.Groups | Select-Object -Skip 1 | Where-Object { $_.Success })
            if ($groups.Count -eq 0) {
                throw "Pattern '$Pattern' matched in $Path but captured no group, so there is no version to read."
            }
            return (($groups | ForEach-Object { $_.Value.Trim() }) -join '.')
        }
    }
}

# Narrow TOML read: walk table headers and return `version` from the first of
# $Tables that has one. Deliberately not a TOML parser - a `version = ` under
# [dependencies] is the failure mode this exists to avoid, and scoping to a
# named table is all that takes.
function Get-TomlTableVersion {
    param(
        [Parameter(Mandatory=$true)][string]$Path,
        [Parameter(Mandatory=$true)][string[]]$Tables
    )

    $current = ''
    $found = @{}
    foreach ($line in [System.IO.File]::ReadAllLines($Path)) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[\[?([^\]]+)\]\]?') { $current = $matches[1].Trim(); continue }
        if ($current -notin $Tables) { continue }
        if ($trimmed -match '^version\s*=\s*"([^"]+)"') { $found[$current] = $matches[1] }
    }

    foreach ($table in $Tables) {
        if ($found.ContainsKey($table)) { return $found[$table] }
    }
    throw "No version under [$($Tables -join '] or [')] in $Path"
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

<#
.SYNOPSIS
    Read the cameraunlock-core commit a mod repo has committed as its
    submodule pointer.
.DESCRIPTION
    This is the commit whose source is compiled into the mod binary, which
    is what THIRD-PARTY-NOTICES.md has to name. It is read from the index
    entry, not from the submodule working tree, because a working tree that
    has been moved but not committed is not what a release ships.

    Returns $null for a repo that does not consume core as a submodule -
    a real layout in this fleet, not an error.
.PARAMETER RepoRoot
    Root of the mod repository.
#>
function Get-PinnedCoreCommit {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$RepoRoot)

    if (-not (Test-Path (Join-Path $RepoRoot '.gitmodules'))) { return $null }

    # --quiet --verify, not `2>$null`: under Windows PowerShell 5.1 a native
    # command's stderr becomes a NativeCommandError record, which an
    # $ErrorActionPreference = 'Stop' caller turns into a terminating error. A
    # repo that vendors no core is an ordinary answer here, not a failure.
    $pin = & git -C $RepoRoot rev-parse --quiet --verify 'HEAD:cameraunlock-core'
    if ($LASTEXITCODE -ne 0 -or -not $pin) { return $null }
    return $pin.Trim()
}

<#
.SYNOPSIS
    Rewrite the cameraunlock-core commit recorded in a mod's
    THIRD-PARTY-NOTICES.md so it names the commit the mod actually pins.
.DESCRIPTION
    The notices file ships at the root of every release ZIP and is the
    attribution the user receives, so the commit it names has to be the one
    compiled into the binary. Nothing about bumping the submodule pointer
    touches that file, so the two drift apart the moment the pin moves -
    silently, because a wrong commit hash reads exactly like a right one.

    Rewritten hashes are those on a line that mentions cameraunlock-core, or
    on any line inside the file's `## cameraunlock-core` section. Length is
    preserved, so a table cell carrying a short hash keeps its short form.
    Everything else in the file, including the hashes of other dependencies,
    is left alone.

    A file that records no cameraunlock-core hash at all is reported through
    Recorded = 0 and left untouched; where to introduce the record is a
    judgement about that mod's notices layout, not something to guess at.
.PARAMETER RepoRoot
    Root of the mod repository.
.PARAMETER ReadOnly
    Report what would change without writing the file.
.OUTPUTS
    PSCustomObject with RepoRoot, Pin, Recorded (hashes found), Stale (the
    distinct wrong hashes found) and Updated (whether the file was written).
#>
function Sync-CoreCommitInNotices {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$RepoRoot,
        [string]$Commit,
        [switch]$ReadOnly
    )

    # $Commit exists for the bump path: immediately after `git submodule
    # update --remote` the working tree holds the new commit while the index
    # still holds the old one, so the pointer the notices must name is not yet
    # readable from HEAD. Both get committed together.
    $pin = if ($Commit) { $Commit } else { Get-PinnedCoreCommit -RepoRoot $RepoRoot }
    if (-not $pin) {
        throw "$RepoRoot does not pin cameraunlock-core as a submodule, so there is no commit to record."
    }

    $noticesPath = Join-Path $RepoRoot 'THIRD-PARTY-NOTICES.md'
    if (-not (Test-Path $noticesPath)) {
        throw "THIRD-PARTY-NOTICES.md not found at $noticesPath. cameraunlock-core is compiled into the mod binary and its notice must travel with it."
    }

    # Split on `n only, so each element keeps any trailing `r: joining them
    # back reproduces the file's own line endings rather than normalising a
    # whole file to CRLF for the sake of one hash.
    $raw   = Get-Content -LiteralPath $noticesPath -Raw
    $lines = $raw -split "`n"

    $inCoreSection = $false
    $recorded = 0
    $stale    = @()
    $changed  = $false

    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]

        # Headings and prose both spell it several ways across the fleet -
        # `## cameraunlock-core`, `### CameraUnlock Core`, `## CameraUnlock-Core
        # (shared)`. Matching one spelling leaves a stale hash sitting under
        # the others, which is the failure this function exists to prevent.
        if ($line -match '^#{1,6}\s') {
            $inCoreSection = $line -match '^#{1,6}\s+cameraunlock[- ]core\b'
        }
        if (-not ($inCoreSection -or $line -match 'cameraunlock[- ]core')) { continue }

        # A hash needs a digit to be a hash. Without that, ordinary words
        # spelled from a-f ("acceded", "defaced") match the character class
        # and get rewritten into the middle of a sentence.
        $hits = @([regex]::Matches($line, '(?<![0-9A-Za-z])[0-9a-f]{7,40}(?![0-9A-Za-z])') |
                  Where-Object { $_.Value -match '[0-9]' })
        if ($hits.Count -eq 0) { continue }

        $updated = $line
        for ($j = $hits.Count - 1; $j -ge 0; $j--) {
            $hit = $hits[$j]
            $recorded++
            $want = $pin.Substring(0, $hit.Length)
            if ($hit.Value -eq $want) { continue }
            $stale += $hit.Value
            $updated = $updated.Remove($hit.Index, $hit.Length).Insert($hit.Index, $want)
        }
        if ($updated -ne $line) {
            $lines[$i] = $updated
            $changed = $true
        }
    }

    if ($changed -and -not $ReadOnly) {
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($noticesPath, ($lines -join "`n"), $utf8NoBom)
    }

    return [PSCustomObject]@{
        RepoRoot = $RepoRoot
        Pin      = $pin
        Recorded = $recorded
        Stale    = @($stale | Select-Object -Unique)
        Updated  = ($changed -and -not $ReadOnly)
    }
}

<#
.SYNOPSIS
    Fail packaging when THIRD-PARTY-NOTICES.md names a cameraunlock-core
    commit other than the one being compiled in.
.DESCRIPTION
    Checked rather than trusted: the notices file is copied verbatim into
    the release ZIPs, so a stale hash is a wrong attribution shipped to
    every user, and it is invisible on inspection.

    A repo that does not pin cameraunlock-core as a submodule is skipped.
.PARAMETER RepoRoot
    Root of the mod repository.
#>
function Assert-CoreCommitInNotices {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][string]$RepoRoot)

    if (-not (Get-PinnedCoreCommit -RepoRoot $RepoRoot)) { return }

    $state = Sync-CoreCommitInNotices -RepoRoot $RepoRoot -ReadOnly
    $fix   = "powershell -ExecutionPolicy Bypass -File cameraunlock-core\scripts\sync-core-notices.ps1 -Repo ."

    if ($state.Recorded -eq 0) {
        throw "THIRD-PARTY-NOTICES.md records no cameraunlock-core commit, but $($state.Pin) is compiled into this build. Add the pinned commit to the cameraunlock-core entry, then re-run."
    }
    if ($state.Stale.Count -gt 0) {
        throw "THIRD-PARTY-NOTICES.md records cameraunlock-core $($state.Stale -join ', ') but this build compiles $($state.Pin). Run: $fix - then commit the notices and re-run."
    }
}

<#
.SYNOPSIS
    Fails packaging when a launcher-manifest.json seed no longer matches the file it seeds.
.DESCRIPTION
    A manifest seed carries a base64 copy of a config file that the launcher writes
    into the game directory on install. The committed manifest is the authoritative
    copy: it is reviewable, diffable and in git, where the blob inside a release ZIP
    is a build product. So packaging ships the committed blob unchanged, and this is
    the gate that keeps the committed blob honest.

    Refreshing the blob from disk at package time instead would hide the drift: the
    ZIP would be right, the committed manifest would stay wrong, every review of it
    would read a stale config, and scripts/conformance.ps1's manifest-seed check
    would go on reporting a failure nobody could reproduce from a build.

    The rule for pairing a seed with a file is the same one conformance.ps1 uses -
    the single file under the repo whose leaf name matches the seed target - so the
    two cannot disagree about what a seed is meant to match. A seed with no
    counterpart is the loader's own config (BepInEx writes BepInEx.cfg; we ship no
    copy of it) and there is nothing to check it against.
.PARAMETER ManifestPath
    Path to the committed launcher-manifest.json.
.PARAMETER ProjectRoot
    Repo root the shipped config files are searched under.
#>
function Assert-ManifestSeedsMatchShipped {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$ManifestPath,
        [Parameter(Mandatory=$true)][string]$ProjectRoot
    )

    if (-not (Test-Path $ManifestPath)) { throw "Manifest file not found: $ManifestPath" }

    $manifest = [System.IO.File]::ReadAllText($ManifestPath).TrimStart([char]0xFEFF) | ConvertFrom-Json

    $seeds = New-Object System.Collections.Generic.List[object]
    if ($manifest.PSObject.Properties.Name -contains 'loader' -and $manifest.loader -and
        $manifest.loader.PSObject.Properties.Name -contains 'seed') {
        foreach ($s in @($manifest.loader.seed)) { if ($s) { $seeds.Add($s) } }
    }
    if ($manifest.PSObject.Properties.Name -contains 'seed') {
        foreach ($s in @($manifest.seed)) { if ($s) { $seeds.Add($s) } }
    }

    $root = (Resolve-Path $ProjectRoot).ProviderPath
    $normalise = { param($t) (($t -replace "^$([char]0xFEFF)", '') -replace "`r`n", "`n").TrimEnd() }

    foreach ($seed in $seeds) {
        if ($seed.PSObject.Properties.Name -notcontains 'content_b64') { continue }
        if ($seed.PSObject.Properties.Name -notcontains 'target') { continue }
        $leaf = ($seed.target -split '[\\/]')[-1]

        $shipped = @(Get-ChildItem -LiteralPath $root -File -Filter $leaf -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notmatch '(?i)\\(\.git|\.pixi|\.lab|\.vs|cameraunlock-core|vendor|node_modules|obj|bin|build|release|dist)\\' })

        if ($shipped.Count -eq 0) { continue }
        if ($shipped.Count -gt 1) {
            throw "launcher-manifest.json seeds $($seed.target) and this repo holds $($shipped.Count) files named $leaf, so nothing says which one the blob is meant to match. Rename or relocate the duplicates, then re-run."
        }

        try {
            $decoded = [System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($seed.content_b64))
        } catch {
            throw "launcher-manifest.json content_b64 for $($seed.target) is not valid base64: $($_.Exception.Message)"
        }

        $file = $shipped[0].FullName
        if ((& $normalise $decoded) -eq (& $normalise ([System.IO.File]::ReadAllText($file)))) { continue }

        $rel = $file.Substring($root.Length).TrimStart('\') -replace '\\', '/'
        throw "launcher-manifest.json seeds $($seed.target) from a base64 blob that no longer matches $rel, so installing would write the stale copy over the defaults this mod ships. The committed manifest is the authoritative copy: re-stamp its content_b64 from $rel and commit that, then re-run."
    }
}

# Export functions
Export-ModuleMember -Function @(
    'Update-CameraUnlockCoreToRemoteTip',
    'Get-PinnedCoreCommit',
    'Sync-CoreCommitInNotices',
    'Assert-CoreCommitInNotices',
    'Write-CoreBundleProvenance',
    'Copy-SharedBundle',
    'Copy-LicenceNotices',
    'Test-SemanticVersion',
    'Step-SemanticVersion',
    'Resolve-ReleaseVersion',
    'Test-CleanGitStatus',
    'Test-GitTagExists',
    'Test-NoiseCommit',
    'Update-ManifestVersion',
    'Assert-ManifestSeedsMatchShipped',
    'New-ChangelogFromCommits',
    'Get-ChangelogSection',
    'Invoke-VersionCommit',
    'New-ReleaseTag',
    'Get-CsprojVersion',
    'Set-CsprojVersion',
    'Get-ProjectVersion'
)
