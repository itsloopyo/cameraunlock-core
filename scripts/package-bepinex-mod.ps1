#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Shared BepInEx mod packaging script.
.DESCRIPTION
    Creates a release ZIP containing:
    - install.cmd and uninstall.cmd scripts
    - Mod DLLs (in plugins subfolder)
    - Documentation (README, LICENSE, CHANGELOG)
.PARAMETER ModName
    Name of the mod (used in ZIP filename).
.PARAMETER CsprojPath
    Path to the .csproj file (version source).
.PARAMETER BuildOutputDir
    Directory containing compiled DLLs.
.PARAMETER ModDlls
    Array of DLL filenames to include.
.PARAMETER ProjectRoot
    Root directory of the mod project (default: cwd).
.PARAMETER CreateNexusZip
    When set, also creates a NexusMods-compatible ZIP with BepInEx/plugins/ structure (DLLs only).
#>
param(
    [Parameter(Mandatory=$true)]
    [string]$ModName,

    [Parameter(Mandatory=$true)]
    [string]$CsprojPath,

    [Parameter(Mandatory=$true)]
    [string]$BuildOutputDir,

    [Parameter(Mandatory=$true)]
    [string[]]$ModDlls,

    [string]$ProjectRoot = $PWD,

    [switch]$CreateNexusZip
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Import-Module (Join-Path $scriptDir "..\powershell\ReleaseWorkflow.psm1") -Force

# Resolve paths
$CsprojPath = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $CsprojPath))
$BuildOutputDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectRoot $BuildOutputDir))
$releaseDir = Join-Path $ProjectRoot "release"
$scriptsDir = Join-Path $ProjectRoot "scripts"

Write-Host "=== $ModName - Package Release ===" -ForegroundColor Magenta
Write-Host ""

$version = Get-CsprojVersion $CsprojPath
Write-Host "Version: $version" -ForegroundColor Cyan
Write-Host ""

# Validate all DLLs exist
foreach ($dll in $ModDlls) {
    $dllPath = Join-Path $BuildOutputDir $dll
    if (-not (Test-Path $dllPath)) {
        throw "Required DLL not found: $dllPath"
    }
}

# Create release directory
if (-not (Test-Path $releaseDir)) {
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
}

# Create staging directory
$stagingDir = Join-Path $releaseDir "staging"
if (Test-Path $stagingDir) {
    Remove-Item -Recurse -Force $stagingDir
}
New-Item -ItemType Directory -Path $stagingDir -Force | Out-Null

Write-Host "Staging release files..." -ForegroundColor Cyan

# Copy install/uninstall scripts
foreach ($script in @("install.cmd", "uninstall.cmd")) {
    $scriptPath = Join-Path $scriptsDir $script
    # Fatal, not skipped. A repo that moves or renames install.cmd otherwise packaged
    # and published a release ZIP with no installer in it, and users on the legacy
    # (non-manifest) path had nothing to run. The manifest check further down already
    # throws for its missing input; this is the same contract.
    if (-not (Test-Path $scriptPath)) {
        throw "Required script not found: $scriptPath"
    }
    Copy-Item $scriptPath -Destination $stagingDir -Force
    Write-Host "  $script" -ForegroundColor Green
}

# install.cmd / uninstall.cmd resolve the game via shared/find-game.ps1.
# Bundle that shim alongside them so the release ZIP is self-contained.
Copy-SharedBundle -StagingDir $stagingDir

# Bundle vendored BepInEx (zip + LICENSE + README.md) so install.cmd has no
# GitHub dependency at install time. Mods opt out by not committing vendor/bepinex/.
$vendorSrc = Join-Path $ProjectRoot "vendor\bepinex"
if (Test-Path $vendorSrc) {
    $vendorDest = Join-Path $stagingDir "vendor\bepinex"
    New-Item -ItemType Directory -Path $vendorDest -Force | Out-Null
    foreach ($vendorFile in (Get-ChildItem -Path $vendorSrc -File)) {
        Copy-Item $vendorFile.FullName -Destination $vendorDest -Force
        Write-Host "  vendor/bepinex/$($vendorFile.Name)" -ForegroundColor Green
    }
}

# Copy mod DLLs to plugins subfolder
$pluginsDestDir = Join-Path $stagingDir "plugins"
New-Item -ItemType Directory -Path $pluginsDestDir -Force | Out-Null

foreach ($dll in $ModDlls) {
    $dllPath = Join-Path $BuildOutputDir $dll
    Copy-Item $dllPath -Destination $pluginsDestDir -Force
    Write-Host "  plugins/$dll" -ForegroundColor Green
}

# Copy documentation. LICENSE and THIRD-PARTY-NOTICES.md are not documentation
# in the optional sense - the licences of everything we bundle or link require
# them to accompany the binary - so Copy-LicenceNotices throws when one is
# absent instead of warning and shipping anyway.
Copy-LicenceNotices -StagingDir $stagingDir -ProjectRoot $ProjectRoot

foreach ($doc in @("README.md", "CHANGELOG.md")) {
    $docPath = Join-Path $ProjectRoot $doc
    if (Test-Path $docPath) {
        Copy-Item $docPath -Destination $stagingDir -Force
        Write-Host "  $doc" -ForegroundColor Green
    }
}

# Stamp launcher-manifest.json with the real release version and stage it at
# the ZIP root. The launcher reads this file to deploy natively (manifest
# delivery mode), falling back to install.cmd for legacy packages - without
# it in the ZIP the launcher can't tell the two apart and always runs the
# script. Written through a no-BOM encoder: PowerShell 5.1's
# Set-Content -Encoding UTF8 emits a BOM that serde_json rejects.
$manifestSource = Join-Path $ProjectRoot "launcher-manifest.json"
if (-not (Test-Path $manifestSource)) {
    throw "launcher-manifest.json not found at project root ($manifestSource)"
}

# The seeded config the launcher writes on install comes out of the committed
# manifest unchanged - only mod_info.version is stamped below. Refreshing the
# blob from disk here would produce a correct ZIP over a stale committed file,
# leaving every review of it reading the wrong defaults, so drift fails the
# build instead.
Assert-ManifestSeedsMatchShipped -ManifestPath $manifestSource -ProjectRoot $ProjectRoot

$manifestJson = Get-Content $manifestSource -Raw | ConvertFrom-Json
$manifestJson.mod_info.version = $version
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllText(
    (Join-Path $stagingDir "launcher-manifest.json"),
    ($manifestJson | ConvertTo-Json -Depth 10),
    $utf8NoBom
)
Write-Host "  launcher-manifest.json (v$version)" -ForegroundColor Green

Write-Host ""

# Create ZIP archive
$zipName = "$ModName-v$version-installer.zip"
$zipPath = Join-Path $releaseDir $zipName

if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Write-Host "Creating ZIP archive..." -ForegroundColor Cyan

Push-Location $stagingDir
try {
    Compress-Archive -Path ".\*" -DestinationPath $zipPath -Force
} finally {
    Pop-Location
}

Remove-Item -Recurse -Force $stagingDir

$zipSize = (Get-Item $zipPath).Length / 1KB
Write-Host ""
Write-Host "=== GitHub Package Complete ===" -ForegroundColor Magenta
Write-Host ""
Write-Host "Release archive: $zipPath" -ForegroundColor Green
Write-Host ("Size: {0:N1} KB" -f $zipSize) -ForegroundColor White

$nexusZipPath = $null

if ($CreateNexusZip) {
    Write-Host ""
    Write-Host "=== Creating NexusMods ZIP ===" -ForegroundColor Magenta
    Write-Host ""

    $nexusStagingDir = Join-Path $releaseDir "staging-nexus"
    if (Test-Path $nexusStagingDir) {
        Remove-Item -Recurse -Force $nexusStagingDir
    }

    $nexusPluginsDir = Join-Path (Join-Path $nexusStagingDir "BepInEx") "plugins"
    New-Item -ItemType Directory -Path $nexusPluginsDir -Force | Out-Null

    foreach ($dll in $ModDlls) {
        $dllPath = Join-Path $BuildOutputDir $dll
        Copy-Item $dllPath -Destination $nexusPluginsDir -Force
        Write-Host "  BepInEx/plugins/$dll" -ForegroundColor Green
    }

    # The Nexus ZIP carries the same notice obligations as the installer ZIP:
    # it is a binary distribution too, and shipping the DLLs without the
    # licences they are distributed under is a violation, not a packaging nit.
    Copy-LicenceNotices -StagingDir $nexusStagingDir -ProjectRoot $ProjectRoot -Additional @("README.md")

    $nexusZipName = "$ModName-v$version-nexus.zip"
    $nexusZipPath = Join-Path $releaseDir $nexusZipName

    if (Test-Path $nexusZipPath) {
        Remove-Item $nexusZipPath -Force
    }

    Write-Host ""
    Write-Host "Creating NexusMods ZIP archive..." -ForegroundColor Cyan

    Push-Location $nexusStagingDir
    try {
        Compress-Archive -Path ".\*" -DestinationPath $nexusZipPath -Force
    } finally {
        Pop-Location
    }

    Remove-Item -Recurse -Force $nexusStagingDir

    $nexusZipSize = (Get-Item $nexusZipPath).Length / 1KB
    Write-Host ""
    Write-Host "=== NexusMods Package Complete ===" -ForegroundColor Magenta
    Write-Host ""
    Write-Host "NexusMods archive: $nexusZipPath" -ForegroundColor Green
    Write-Host ("Size: {0:N1} KB" -f $nexusZipSize) -ForegroundColor White
}

# One object on the success stream, read by property name. The caller used to
# take $output[0] and $output[1], so anything that ever leaked onto the stream
# would shift the indices and hand CI a garbage path with no error.
[PSCustomObject]@{
    GithubZip = $zipPath
    NexusZip  = $nexusZipPath
}
