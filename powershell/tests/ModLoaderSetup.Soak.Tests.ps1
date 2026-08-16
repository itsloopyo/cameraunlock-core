#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for the supply-chain soak in ModLoaderSetup.psm1
# ============================================================================
# Run: pixi run test-powershell
#
# The soak is the only thing standing between `pixi run update-deps` and a
# freshly-published upstream release, so it gets a test that a later
# simplification has to break loudly.
#
# GitHub is mocked inside the module's own session state: functions declared
# `script:` from a module scriptblock shadow the real cmdlets for module code
# and persist for the session. No Pester dependency.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$modulePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'ModLoaderSetup.psm1'
$mod = Import-Module $modulePath -Force -PassThru -WarningAction SilentlyContinue

& $mod {
    $script:MockReleases = @()
    $script:MockDownloadedUrl = $null

    function script:Invoke-RestMethod {
        param($Uri, $Headers, $TimeoutSec)
        if ($Uri -match '/releases') { return $script:MockReleases }
        return [pscustomobject]@{ object = [pscustomobject]@{ sha = 'deadbeef' } }
    }

    function script:Invoke-WebRequest {
        param($Uri, $OutFile, $Headers, $TimeoutSec, [switch]$UseBasicParsing)
        $script:MockDownloadedUrl = $Uri
        Set-Content -Path $OutFile -Value "payload for $Uri"
    }

    function script:Expand-Archive {
        param($Path, $DestinationPath, [switch]$Force)
    }
}

function Set-MockReleases {
    param($Releases)
    & $mod { param($r) $script:MockReleases = @($r); $script:MockDownloadedUrl = $null } $Releases
}

function Get-MockDownloadedUrl {
    & $mod { $script:MockDownloadedUrl }
}

$script:Now = (Get-Date).ToUniversalTime()
$script:Failures = 0

function Check {
    param([string]$Name, [bool]$Condition, [string]$Detail)
    if ($Condition) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name :: $Detail" -ForegroundColor Red
        $script:Failures++
    }
}

function New-MockRelease {
    param(
        [string]$Tag,
        [double]$AgeDays,
        [string]$AssetName = 'dinput8.zip',
        [switch]$PublishedAsDateTime,
        [switch]$NoPublishedAt,
        [switch]$NoAssets,
        [switch]$Prerelease
    )

    $published = if ($NoPublishedAt) {
        $null
    } elseif ($PublishedAsDateTime) {
        $script:Now.AddDays(-$AgeDays)
    } else {
        $script:Now.AddDays(-$AgeDays).ToString('yyyy-MM-ddTHH:mm:ssZ')
    }

    $assets = if ($NoAssets) { @() } else {
        @([pscustomobject]@{ name = $AssetName; browser_download_url = "https://example.invalid/$Tag/$AssetName" })
    }

    [pscustomobject]@{
        tag_name     = $Tag
        prerelease   = [bool]$Prerelease
        published_at = $published
        assets       = $assets
    }
}

$asiArgs = @{
    Owner = 'ThirteenAG'; Repo = 'Ultimate-ASI-Loader'
    VersionPrefix = 'v9.'; AssetPattern = '^dinput8\.zip$'
}
$outFile  = Join-Path $env:TEMP 'soak-tests-asset.zip'
$gameDir  = Join-Path $env:TEMP 'soak-tests-game'
$vendorDir = Join-Path $env:TEMP 'soak-tests-vendor'

function Reset-GameDir {
    if (Test-Path $gameDir) { Remove-Item $gameDir -Recurse -Force }
    New-Item -ItemType Directory -Path $gameDir -Force | Out-Null
}

# --- Invoke-FetchLatestLoader ---------------------------------------------

Set-MockReleases @((New-MockRelease -Tag 'v9.7.3' -AgeDays 2), (New-MockRelease -Tag 'v9.6.0' -AgeDays 40))
$meta = Invoke-FetchLatestLoader -OutputPath $outFile @asiArgs
Check 'takes the 40-day-old release over the 2-day-old one' ($meta.Tag -eq 'v9.6.0') "got $($meta.Tag)"
Check 'downloads the asset of the release it selected' ((Get-MockDownloadedUrl) -like '*v9.6.0*') "got $(Get-MockDownloadedUrl)"

Set-MockReleases @((New-MockRelease -Tag 'v9.7.3' -AgeDays 2), (New-MockRelease -Tag 'v9.7.2' -AgeDays 5))
$err = $null
try { Invoke-FetchLatestLoader -OutputPath $outFile @asiArgs } catch { $err = $_.Exception.Message }
$eligibleOn = $script:Now.AddDays(-2).AddDays(14).ToString('yyyy-MM-dd')
Check 'throws when every match is inside the window' ($null -ne $err) 'no throw'
Check 'the error names the newest match' ($err -like '*v9.7.3*') $err
Check 'the error names the date it becomes eligible' ($err -like "*$eligibleOn*") $err

$meta = Invoke-FetchLatestLoader -OutputPath $outFile @asiArgs -MinimumAgeDays 0
Check '-MinimumAgeDays 0 takes the newest' ($meta.Tag -eq 'v9.7.3') "got $($meta.Tag)"

Set-MockReleases @((New-MockRelease -Tag 'v9.7.0' -AgeDays 14.001))
$meta = Invoke-FetchLatestLoader -OutputPath $outFile @asiArgs
Check 'a release just past 14 days is eligible' ($meta.Tag -eq 'v9.7.0') "got $($meta.Tag)"

Set-MockReleases @((New-MockRelease -Tag 'v9.7.3' -AgeDays 1 -PublishedAsDateTime), (New-MockRelease -Tag 'v9.6.0' -AgeDays 40 -PublishedAsDateTime))
$meta = Invoke-FetchLatestLoader -OutputPath $outFile @asiArgs
Check 'published_at as a DateTime is handled' ($meta.Tag -eq 'v9.6.0') "got $($meta.Tag)"

Set-MockReleases @((New-MockRelease -Tag 'v9.9.9' -AgeDays 0 -NoPublishedAt))
$err = $null
try { Invoke-FetchLatestLoader -OutputPath $outFile @asiArgs } catch { $err = $_.Exception.Message }
Check 'a release with no published_at is not usable' ($null -ne $err) 'no throw'

Set-MockReleases @(
    (New-MockRelease -Tag 'v9.7.3' -AgeDays 2),
    (New-MockRelease -Tag 'v9.7.1' -AgeDays 30 -NoAssets),
    (New-MockRelease -Tag 'v9.7.0' -AgeDays 45)
)
$meta = Invoke-FetchLatestLoader -OutputPath $outFile @asiArgs
Check 'walks past an eligible release with no matching asset' ($meta.Tag -eq 'v9.7.0') "got $($meta.Tag)"

Set-MockReleases @()
$meta = Invoke-FetchLatestLoader -OutputPath $outFile -DirectUrl 'https://thunderstore.io/package/download/BepInEx/BepInExPack_PEAK/5.4.2100/'
Check 'DirectUrl mode is exempt from the soak' ($meta.Source -eq 'direct-url') "got $($meta.Source)"

# --- Update-VendoredLoader -------------------------------------------------

if (Test-Path $vendorDir) { Remove-Item $vendorDir -Recurse -Force }
Set-MockReleases @((New-MockRelease -Tag 'v9.7.3' -AgeDays 2))
$err = $null
try {
    Update-VendoredLoader -Name 'asiloader' -OutputDir $vendorDir -OutputFileName 'dinput8.zip' @asiArgs | Out-Null
} catch { $err = $_.Exception.Message }
Check 'Update-VendoredLoader inherits the soak' (($null -ne $err) -and ($err -like '*14 days*')) "err=$err"

# --- Install-BepInEx (legacy GitHub path) ---------------------------------

Reset-GameDir
Set-MockReleases @(
    (New-MockRelease -Tag 'v5.4.99' -AgeDays 3 -AssetName 'BepInEx_win_x64_5.4.99.zip'),
    (New-MockRelease -Tag 'v5.4.23' -AgeDays 200 -AssetName 'BepInEx_win_x64_5.4.23.zip')
)
$result = Install-BepInEx -GamePath $gameDir -EnableConsole $false
Check 'Install-BepInEx skips the 3-day-old release' ($result.Version -eq '5.4.23') "got $($result.Version)"

Reset-GameDir
Set-MockReleases @((New-MockRelease -Tag 'v5.4.99' -AgeDays 3 -AssetName 'BepInEx_win_x64_5.4.99.zip'))
$err = $null
try { Install-BepInEx -GamePath $gameDir -EnableConsole $false | Out-Null } catch { $err = $_.Exception.Message }
Check 'Install-BepInEx throws inside the window' (($null -ne $err) -and ($err -like '*14 days*')) "err=$err"
Check 'Install-BepInEx points at -VendorZip' ($err -like '*-VendorZip*') "err=$err"

Reset-GameDir
$err = $null
try { Install-BepInEx -GamePath $gameDir -VendorZip (Join-Path $env:TEMP 'no-such-vendor.zip') | Out-Null } catch { $err = $_.Exception.Message }
Check '-VendorZip fails on the missing zip, never on the soak' ($err -like '*VendorZip not found*') "err=$err"

Reset-GameDir
Set-MockReleases @(
    (New-MockRelease -Tag 'v6.0.0-pre.2' -AgeDays 300 -AssetName 'BepInEx_win_x64_6.0.0-pre.2.zip' -Prerelease),
    (New-MockRelease -Tag 'v5.4.23' -AgeDays 200 -AssetName 'BepInEx_win_x64_5.4.23.zip')
)
$result = Install-BepInEx -GamePath $gameDir -MajorVersion 6 -EnableConsole $false
Check 'MajorVersion 6 keeps its prerelease fallback' ($result.Version -eq '6.0.0-pre.2') "got $($result.Version)"

# --- cleanup ---------------------------------------------------------------

Remove-Item $outFile -Force -ErrorAction SilentlyContinue
Remove-Item $gameDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $vendorDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) check(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host "all checks passed" -ForegroundColor Green
