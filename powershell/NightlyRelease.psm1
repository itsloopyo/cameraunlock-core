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
#   4. SHA-256 the installer ZIP (surfaced in the release notes).
#   5. Replace the `dev` pre-release (delete + recreate at HEAD) with the
#      fresh ZIP attached as a fixed-named asset.
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

    Push-Location $ProjectRoot
    try {
        Write-Host 'Building...' -ForegroundColor Cyan
        Invoke-Expression $BuildCommand
        if ($LASTEXITCODE -ne 0) { throw "$BuildCommand failed" }

        Write-Host 'Packaging...' -ForegroundColor Cyan
        Invoke-Expression $PackageCommand
        if ($LASTEXITCODE -ne 0) { throw "$PackageCommand failed" }
    } finally { Pop-Location }

    if (-not $InstallerZipPath) {
        $InstallerZipPath = Join-Path $ProjectRoot "release\$ModName-v$Version-installer.zip"
    }
    if (-not (Test-Path $InstallerZipPath)) {
        throw "Expected installer ZIP missing: $InstallerZipPath"
    }

    # Fixed asset name -> stable download URL:
    # github.com/<owner>/<repo>/releases/download/<DevTag>/<asset>
    $releaseDir = Join-Path $ProjectRoot 'release'
    $assetName = "$ModName-dev-installer.zip"
    $assetPath = Join-Path $releaseDir $assetName
    Copy-Item -Force $InstallerZipPath $assetPath

    $zipBytes = (Get-Item $assetPath).Length
    $zipHash = (Get-FileHash -Algorithm SHA256 $assetPath).Hash.ToLowerInvariant()

    Write-Host "Built : $assetName" -ForegroundColor Green
    Write-Host "  size   : $zipBytes bytes" -ForegroundColor DarkGray
    Write-Host "  sha256 : $zipHash" -ForegroundColor DarkGray
    Write-Host ''

    $title = "Development build $nightlyVersion"
    # Plain-text notes (no markdown backticks - backtick is PowerShell's
    # escape char inside double quotes, so `n is a newline here).
    $notes =
        "Development build - buggy but playable, not release-quality. Expect rough edges.`n`n" +
        "Automated build of in-progress work. The mod is open source and free; this prebuilt is a convenience. " +
        "To install manually: download the zip below, extract it, and run install.cmd.`n`n" +
        "Version: $nightlyVersion`nCommit: $fullSha`nBuilt (UTC): $builtAt`nSHA-256: $zipHash"

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
        # always the newest commit's build. The delete is best-effort: on
        # the first publish there's nothing to delete (gh prints "release
        # not found" to stderr, suppressed here) and that's fine.
        Write-Host "Replacing $DevTag pre-release on GitHub..." -ForegroundColor Cyan
        & gh release delete $DevTag --yes --cleanup-tag 2>$null

        & gh release create $DevTag $assetPath `
            --prerelease `
            --target $fullSha `
            --title $title `
            --notes $notes
        if ($LASTEXITCODE -ne 0) { throw "gh release create failed (exit $LASTEXITCODE)" }

        $repo = (& gh repo view --json nameWithOwner --jq .nameWithOwner 2>$null).Trim()
        if (-not $repo) { $repo = '<owner>/<repo>' }
    } finally {
        $ErrorActionPreference = $prevEAP
        Pop-Location
    }

    Write-Host ''
    Write-Host "Published dev build $nightlyVersion" -ForegroundColor Green
    Write-Host "  release  : https://github.com/$repo/releases/tag/$DevTag" -ForegroundColor DarkGray
    Write-Host "  download : https://github.com/$repo/releases/download/$DevTag/$assetName" -ForegroundColor DarkGray

    # Mirror the tagged-release Discord announce (scripts/templates/
    # discord-announce-step.yml), reusing the same webhook. The webhook is
    # validated as a precondition above, so this always runs.
    $name = (Get-Culture).TextInfo.ToTitleCase(((($repo -split '/')[-1]) -replace '-head-?tracking$','' -replace '-',' '))
    $releaseUrl = "https://github.com/$repo/releases/tag/$DevTag"
    $payload = @{
        username = 'Loopy Releases'
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
