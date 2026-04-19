#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Template: vendor/<loader-slug>/fetch-latest.ps1
# ============================================================================
# Copy into each mod's vendor/<loader-slug>/ and customize the CONFIG BLOCK.
# Invoked by install.cmd at user install time to fetch the newest upstream
# release within the pinned version range. On any failure (network, 404,
# timeout, rate limit, missing asset) this script exits non-zero and
# install.cmd falls back to the bundled <loader>.zip next to it.
#
# Self-contained on purpose: the user's extracted installer ZIP does not
# contain cameraunlock-core/. The equivalent logic used at package time lives
# in Invoke-FetchLatestLoader (cameraunlock-core/powershell/ModLoaderSetup.psm1)
# and must stay in sync.
# ============================================================================

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# --- CONFIG BLOCK ---------------------------------------------------------
# GitHub mode: set $Owner + $Repo + $AssetPattern (+ optional $VersionPrefix,
#   $AllowPrerelease).
# Direct-URL mode: set $DirectUrl instead (leave $Owner / $Repo blank).
$Owner           = 'EXAMPLE-OWNER'
$Repo            = 'EXAMPLE-REPO'
$VersionPrefix   = 'v5.4.'                           # '' = no prefix filter
$AssetPattern    = 'EXAMPLE_.*\.zip'                 # regex matched to asset name
$AllowPrerelease = $false                            # true for nightlies
$DirectUrl       = ''                                # non-empty -> Direct-URL mode
$TimeoutSec      = 30
# --- END CONFIG BLOCK -----------------------------------------------------

$headers = @{ "User-Agent" = "CameraUnlock-HeadTracking" }
$outputDir = Split-Path -Parent $OutputPath
if ($outputDir -and -not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
}

try {
    if ($DirectUrl) {
        Invoke-WebRequest -Uri $DirectUrl -OutFile $OutputPath -UseBasicParsing -TimeoutSec $TimeoutSec -Headers $headers
        $sha = (Get-FileHash -Path $OutputPath -Algorithm SHA256).Hash.ToLower()
        Write-Host "fetch-latest: direct-url $(Split-Path -Leaf $DirectUrl) sha256=$($sha.Substring(0,12))..."
        exit 0
    }

    if (-not $Owner -or -not $Repo -or -not $AssetPattern) {
        throw "fetch-latest.ps1 CONFIG BLOCK incomplete: GitHub mode needs Owner, Repo, AssetPattern."
    }

    $apiUrl = "https://api.github.com/repos/$Owner/$Repo/releases?per_page=50"
    $releases = Invoke-RestMethod -Uri $apiUrl -Headers $headers -TimeoutSec $TimeoutSec

    $matching = $releases | Where-Object {
        ($VersionPrefix -eq '' -or $_.tag_name.StartsWith($VersionPrefix)) -and
        ($AllowPrerelease -or -not $_.prerelease)
    }

    if (-not $matching) {
        throw "No upstream release for $Owner/$Repo matches VersionPrefix='$VersionPrefix' AllowPrerelease=$AllowPrerelease."
    }

    $release = $matching | Select-Object -First 1
    $asset = $release.assets | Where-Object { $_.name -match $AssetPattern } | Select-Object -First 1
    if (-not $asset) {
        throw "Release $($release.tag_name) has no asset matching '$AssetPattern'."
    }

    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $OutputPath -UseBasicParsing -TimeoutSec $TimeoutSec -Headers $headers
    $sha = (Get-FileHash -Path $OutputPath -Algorithm SHA256).Hash.ToLower()

    Write-Host "fetch-latest: tag=$($release.tag_name) asset=$($asset.name) sha256=$($sha.Substring(0,12))..."
    exit 0
} catch {
    Write-Error "fetch-latest: $_"
    exit 1
}
