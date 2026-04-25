#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Template: <mod>/scripts/update-deps.ps1
# ============================================================================
# Bumps the vendored mod-loader copies under <mod>/vendor/<slug>/ to the
# latest upstream release within the pinned version range, and writes
# refreshed LICENSE + README.md sidecar metadata.
#
# Usage:    pixi run update-deps
# Frequency: manual. Vendored copies are the single source of truth at
# install time, so the dev runs this whenever they want a fresh upstream
# bump, then commits the updated vendor/ tree. CI does not refresh.
#
# Wiring required in pixi.toml:
#   update-deps = "powershell -ExecutionPolicy Bypass -File scripts/update-deps.ps1"
#
# Customise the CALL BLOCK below with one Refresh-VendoredLoader call per
# loader slug the mod ships. See ~/.claude/CLAUDE.md "Vendoring Third-Party
# Dependencies" for the canonical version-prefix table.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$moduleCandidates = @(
    (Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'),
    (Join-Path $projectDir '../cameraunlock-core/powershell/ModLoaderSetup.psm1')
)
$modulePath = $moduleCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $modulePath) {
    throw "ModLoaderSetup.psm1 not found. Run 'pixi run sync' to update the cameraunlock-core submodule."
}
Import-Module $modulePath -Force

# --- CALL BLOCK ----------------------------------------------------------
# Replicate one block per loader your mod uses. Reference table:
#   BepInEx x64:           Owner=BepInEx Repo=BepInEx VersionPrefix=v5.4.
#                          AssetPattern=^BepInEx_win_x64_.*\.zip$
#   BepInEx x86:           Owner=BepInEx Repo=BepInEx VersionPrefix=v5.4.
#                          AssetPattern=^BepInEx_win_x86_.*\.zip$
#   BepInExPack PEAK:      DirectUrl=https://thunderstore.io/package/download/BepInEx/BepInExPack_PEAK/<v>/
#                          (no version filter; pin a Thunderstore version manually)
#   MelonLoader 0.6.x x64: Owner=LavaGang Repo=MelonLoader VersionPrefix=v0.6.
#                          AssetPattern=^MelonLoader\.x64\.zip$
#   MelonLoader 0.5.x x64: Owner=LavaGang Repo=MelonLoader VersionPrefix=v0.5.
#                          AssetPattern=^MelonLoader\.x64\.zip$
#   REFramework nightly:   Owner=praydog Repo=REFramework-nightly  AllowPrerelease
#                          AssetPattern=^RE9\.zip$  (or RE2.zip / RE4.zip / ...)
#   Ultimate ASI Loader:   Owner=ThirteenAG Repo=Ultimate-ASI-Loader VersionPrefix=v9.
#                          AssetPattern=^dinput8\.zip$
#                          NOTE: extract the zip locally and commit only the
#                          dinput8.dll if the upstream asset is a wrapper zip.
#
# Refresh-VendoredLoader writes LICENSE + README.md alongside the zip.
# Use -OutputFileName to force a stable filename (install.cmd looks for it).

Refresh-VendoredLoader `
    -Name 'bepinex' `
    -OutputDir (Join-Path $projectDir 'vendor/bepinex') `
    -OutputFileName 'BepInEx_win_x64.zip' `
    -Owner 'BepInEx' -Repo 'BepInEx' `
    -VersionPrefix 'v5.4.' `
    -AssetPattern '^BepInEx_win_x64_.*\.zip$' `
    -LicenseUrl 'https://raw.githubusercontent.com/BepInEx/BepInEx/master/LICENSE' | Out-Null

# --- END CALL BLOCK ------------------------------------------------------

Write-Host ""
Write-Host "Vendored dependencies refreshed. Review and commit the changes under vendor/." -ForegroundColor Green
