# NightlyRelease: shared publisher for public dev builds.
#
# Every mod's `scripts/release-nightly.ps1` is a thin shim that
# determines its own version (C++ mods read constants.h, C# mods read
# .csproj, etc), then calls Publish-NightlyBuild here.
#
# Dev builds ship as a rolling GitHub *pre-release* tagged `dev` on the
# mod's own repo:
#   1. Verify clean tree (caller can pass -AllowDirty through) and that
#      HEAD is on the remote (the release tags this commit).
#   2. Run the mod's build + package commands.
#   3. Stamp a dev version: <version>-nightly.<utc-date>.<sha>.
#   4. SHA-256 each ZIP (surfaced in the release notes).
#   5. Replace the `dev` pre-release (delete + recreate at HEAD) with the
#      fresh ZIPs attached as fixed-named assets.
#
# Both artifacts `pixi run package` produces go up: the installer ZIP and
# the Nexus ZIP (the same build laid out to extract straight into the game
# folder). A dev build that only shipped the installer left everyone who
# installs by hand or through a mod manager on the last tagged release.
#
# Why a rolling pre-release:
#   - Dev builds are free and open for everyone. A GitHub pre-release is
#     publicly browsable on the repo's Releases page, has a stable
#     download URL, and never expires (unlike 14-day Actions artifacts).
#   - The launcher's one-click install is a convenience for supporters,
#     not gated access - anyone can grab the same asset straight from
#     GitHub - so there's no separate storage or services to run.
#
# Auth: requires the GitHub CLI (`gh`) authenticated with permission to
# create releases on the repo. In CI that's the workflow's GITHUB_TOKEN
# (needs `contents: write`); locally it's your `gh auth login`.
# Also requires DISCORD_RELEASE_WEBHOOK in the environment - every dev
# build is announced, so publishing without it is refused up front.

# release/ keeps the previous run's zips. If the package step didn't actually
# produce one this time, that stale file is what gets hashed, uploaded as the
# new dev asset and announced as a fresh build.
function Assert-FreshArtifact {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Label,
        [Parameter(Mandatory)][datetime]$BuildStartedAt,
        [Parameter(Mandatory)][string]$PackageCommand
    )

    $writtenAt = (Get-Item -LiteralPath $Path).LastWriteTime
    if ($writtenAt -lt $BuildStartedAt) {
        throw "$Label ZIP $Path was last written $($writtenAt.ToString('o')), before this build started ($($BuildStartedAt.ToString('o'))). '$PackageCommand' did not produce it - refusing to publish a stale artifact."
    }
}

function Publish-NightlyBuild {
    [CmdletBinding()]
    param(
        # Lowercase-hyphenated slug, matches the lopari catalog entry.
        # Display/logging only now - the GitHub repo is the storage key.
        [Parameter(Mandatory)][string]$ModId,

        # PascalCase assembly/project name. Used to find the installer ZIP
        # produced by `pixi run package` and to name the release asset.
        [Parameter(Mandatory)][string]$ModName,

        # SemVer base. The dev suffix is appended here; callers pass the
        # raw "0.1.0", not "0.1.0-nightly.foo".
        [Parameter(Mandatory)][string]$Version,

        # Absolute path to the mod repo root. Build/package + gh run here.
        [Parameter(Mandatory)][string]$ProjectRoot,

        [string]$BuildCommand = 'pixi run build-release',
        [string]$PackageCommand = 'pixi run package',

        # Override if the packager writes something other than
        # release/<ModName>-v<Version>-installer.zip.
        [string]$InstallerZipPath = $null,

        # Override if the packager writes something other than
        # release/<ModName>-v<Version>-nexus.zip.
        [string]$NexusZipPath = $null,

        # Opt out for a mod whose package step produces no Nexus ZIP. The
        # missing artifact is otherwise fatal: silently publishing a dev
        # release with one asset when the packager should have made two is
        # how the Nexus zip went missing from every dev build in the first
        # place.
        [switch]$NoNexusZip,

        # Git tag for the rolling dev pre-release. One release per repo,
        # replaced on every publish.
        [string]$DevTag = 'dev',

        [switch]$AllowDirty
    )

    $ErrorActionPreference = 'Stop'

    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        throw "GitHub CLI (gh) not found on PATH. Install via 'winget install GitHub.cli', run 'gh auth login', then re-run."
    }

    # Checked before any build/publish work: a dev build that ships without
    # its Discord announce reaches nobody tracking dev, so refuse up front
    # rather than publish silently.
    if (-not $env:DISCORD_RELEASE_WEBHOOK) {
        throw "DISCORD_RELEASE_WEBHOOK is not set. Every dev build must be announced - set the release-channel webhook URL in this shell, then re-run."
    }

    Push-Location $ProjectRoot
    try {
        if (-not $AllowDirty) {
            $dirty = & git status --porcelain
            if ($LASTEXITCODE -ne 0) { throw 'git status failed' }
            if ($dirty) {
                throw "Working tree is dirty. Commit or stash, or pass -AllowDirty.`n$dirty"
            }
        }

        $gitSha = (& git rev-parse --short HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $gitSha) { throw 'git rev-parse failed' }
        $fullSha = (& git rev-parse HEAD).Trim()

        # The release tags this exact commit, so it must exist on the
        # remote. Catch the "forgot to push" case with a clear message
        # instead of a cryptic gh failure.
        $onRemote = & git branch -r --contains HEAD
        if (-not $onRemote) {
            throw "HEAD ($gitSha) isn't on any remote branch yet. Push your commit first - the dev pre-release tags this commit on GitHub."
        }
    } finally { Pop-Location }

    $utcDate = (Get-Date).ToUniversalTime().ToString('yyyyMMdd')
    $builtAt = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    $nightlyVersion = "$Version-nightly.$utcDate.$gitSha"

    Write-Host "Publishing dev build $ModName $nightlyVersion" -ForegroundColor Cyan
    Write-Host "  pre-release tag : $DevTag (rolling)" -ForegroundColor DarkGray
    Write-Host ''

    $buildStartedAt = Get-Date

    Push-Location $ProjectRoot
    try {
        # $LASTEXITCODE is only meaningful after a NATIVE exe. A -BuildCommand
        # that resolves to a PowerShell script or function leaves it holding the
        # exit code of the last native call (the git above, i.e. 0), so a
        # silently-failing build would read as success. Reset before each run.
        Write-Host 'Building...' -ForegroundColor Cyan
        $global:LASTEXITCODE = 0
        Invoke-Expression $BuildCommand
        if ($LASTEXITCODE -ne 0) { throw "$BuildCommand failed" }

        Write-Host 'Packaging...' -ForegroundColor Cyan
        $global:LASTEXITCODE = 0
        Invoke-Expression $PackageCommand
        if ($LASTEXITCODE -ne 0) { throw "$PackageCommand failed" }
    } finally { Pop-Location }

    if (-not $InstallerZipPath) {
        $InstallerZipPath = Join-Path $ProjectRoot "release\$ModName-v$Version-installer.zip"
    }
    if (-not (Test-Path -LiteralPath $InstallerZipPath)) {
        throw "Expected installer ZIP missing: $InstallerZipPath"
    }
    Assert-FreshArtifact -Path $InstallerZipPath -Label 'Installer' -BuildStartedAt $buildStartedAt -PackageCommand $PackageCommand

    if (-not $NoNexusZip) {
        if (-not $NexusZipPath) {
            $NexusZipPath = Join-Path $ProjectRoot "release\$ModName-v$Version-nexus.zip"
        }
        if (-not (Test-Path -LiteralPath $NexusZipPath)) {
            throw "Expected Nexus ZIP missing: $NexusZipPath. '$PackageCommand' must produce it alongside the installer; pass -NexusZipPath if it is written elsewhere, or -NoNexusZip if this mod has no Nexus layout."
        }
        Assert-FreshArtifact -Path $NexusZipPath -Label 'Nexus' -BuildStartedAt $buildStartedAt -PackageCommand $PackageCommand
    }

    # Fixed asset names -> stable download URLs:
    # github.com/<owner>/<repo>/releases/download/<DevTag>/<asset>
    $releaseDir = Join-Path $ProjectRoot 'release'
    $sources = [ordered]@{ "$ModName-dev-installer.zip" = $InstallerZipPath }
    if (-not $NoNexusZip) {
        $sources["$ModName-dev-nexus.zip"] = $NexusZipPath
    }

    $assets = @()
    foreach ($assetName in $sources.Keys) {
        $assetPath = Join-Path $releaseDir $assetName
        Copy-Item -LiteralPath $sources[$assetName] -Destination $assetPath -Force
        $assets += [PSCustomObject]@{
            Name = $assetName
            Path = $assetPath
            Size = (Get-Item -LiteralPath $assetPath).Length
            Hash = (Get-FileHash -LiteralPath $assetPath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }

    foreach ($asset in $assets) {
        Write-Host "Built : $($asset.Name)" -ForegroundColor Green
        Write-Host "  size   : $($asset.Size) bytes" -ForegroundColor DarkGray
        Write-Host "  sha256 : $($asset.Hash)" -ForegroundColor DarkGray
    }
    Write-Host ''

    $title = "Development build $nightlyVersion"
    $installHint = "To install manually: download the installer zip below, extract it, and run install.cmd."
    if (-not $NoNexusZip) {
        $installHint += " The nexus zip is the same build laid out to extract straight into the game folder."
    }
    $hashLines = ($assets | ForEach-Object { "$($_.Name): $($_.Hash)" }) -join "`n"
    # Plain-text notes (no markdown backticks - backtick is PowerShell's
    # escape char inside double quotes, so `n is a newline here).
    $notes =
        "Development build - buggy but playable, not release-quality. Expect rough edges.`n`n" +
        "Automated build of in-progress work. The mod is open source and free; this prebuilt is a convenience. " +
        "$installHint`n`n" +
        "Version: $nightlyVersion`nCommit: $fullSha`nBuilt (UTC): $builtAt`n`nSHA-256:`n$hashLines"

    Push-Location $ProjectRoot
    # gh writes progress/info to stderr - and "release not found" when the
    # dev release doesn't exist yet - and under ErrorActionPreference=Stop
    # *any* native stderr line surfaces as a terminating NativeCommandError,
    # even on success. Drop to Continue for the gh calls and gate on
    # $LASTEXITCODE, which is the correct success signal for a native exe.
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        # Rolling tag: drop the existing dev pre-release + its tag, then
        # recreate at HEAD, so the single "Development build" release is
        # always the newest commit's build. "release not found" is the one
        # tolerated failure (first publish, nothing to delete). Anything
        # else (auth, rate limit, network) must throw here - swallowing it
        # leaves the old release in place and the create then fails with a
        # misleading "tag already exists" collision.
        Write-Host "Replacing $DevTag pre-release on GitHub..." -ForegroundColor Cyan
        $deleteOutput = (& gh release delete $DevTag --yes --cleanup-tag 2>&1 | ForEach-Object { "$_" }) -join "`n"
        if ($LASTEXITCODE -ne 0 -and $deleteOutput -notmatch 'release not found') {
            throw "gh release delete $DevTag failed (exit $LASTEXITCODE):`n$deleteOutput"
        }

        $assetPaths = @($assets | ForEach-Object { $_.Path })
        & gh release create $DevTag @assetPaths `
            --prerelease `
            --target $fullSha `
            --title $title `
            --notes $notes
        if ($LASTEXITCODE -ne 0) { throw "gh release create failed (exit $LASTEXITCODE)" }

        # The old form called .Trim() on the command output directly, so a failed
        # `gh repo view` threw on $null.Trim() and the '<owner>/<repo>' fallback below
        # was unreachable. Do NOT make that placeholder reachable: it flows straight
        # into the announce block and posts an embed linking to
        # https://github.com/<owner>/<repo>/releases/tag/dev - a 404 - in the public
        # release channel, which Invoke-RestMethod reports as success. A broken
        # announcement is worse than none. gh only fails here when it is missing or
        # unauthenticated, which is not recoverable at this point, so fail loudly.
        $repoRaw = & gh repo view --json nameWithOwner --jq .nameWithOwner 2>$null
        if (-not $repoRaw) {
            throw "gh repo view failed - cannot resolve owner/repo for the release announcement. Check that gh is installed and authenticated."
        }
        $repo = ([string]$repoRaw).Trim()
    } finally {
        $ErrorActionPreference = $prevEAP
        Pop-Location
    }

    Write-Host ''
    Write-Host "Published dev build $nightlyVersion" -ForegroundColor Green
    Write-Host "  release  : https://github.com/$repo/releases/tag/$DevTag" -ForegroundColor DarkGray
    foreach ($asset in $assets) {
        Write-Host "  download : https://github.com/$repo/releases/download/$DevTag/$($asset.Name)" -ForegroundColor DarkGray
    }

    # Mirror the tagged-release Discord announce (scripts/templates/
    # discord-announce-step.yml), reusing the same webhook. The webhook is
    # validated as a precondition above, so this always runs.
    $name = (Get-Culture).TextInfo.ToTitleCase(((($repo -split '/')[-1]) -replace '-head-?tracking$','' -replace '-',' '))
    $releaseUrl = "https://github.com/$repo/releases/tag/$DevTag"
    $payload = @{
        username = 'Mod Releases'
        embeds   = @(@{
            title       = "$name Head Tracking - dev build $nightlyVersion"
            url         = $releaseUrl
            description = "A new development build is up - buggy but playable, not release-quality.`n`n[Download the latest dev build]($releaseUrl)"
            color       = 15844367
        })
    } | ConvertTo-Json -Depth 5
    Invoke-RestMethod -Uri $env:DISCORD_RELEASE_WEBHOOK -Method Post -ContentType 'application/json' -Body $payload | Out-Null
    Write-Host "  discord  : announced to release channel" -ForegroundColor DarkGray
}

Export-ModuleMember -Function Publish-NightlyBuild
