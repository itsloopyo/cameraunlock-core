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
    if (Test-Path $scriptPath) {
        Copy-Item $scriptPath -Destination $stagingDir -Force
        Write-Host "  $script" -ForegroundColor Green
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

# Copy documentation
$docFiles = @("README.md", "LICENSE", "CHANGELOG.md", "THIRD-PARTY-NOTICES.md", "THIRD-PARTY-NOTICES.txt")
foreach ($doc in $docFiles) {
    $docPath = Join-Path $ProjectRoot $doc
    if (Test-Path $docPath) {
        Copy-Item $docPath -Destination $stagingDir -Force
        Write-Host "  $doc" -ForegroundColor Green
    } elseif ($doc -eq "LICENSE") {
        Write-Host "  WARNING: $doc not found" -ForegroundColor Yellow
    }
}

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

# Output zip path for CI capture
Write-Output $zipPath

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

    # Output nexus zip path for CI capture
    Write-Output $nexusZipPath
}
