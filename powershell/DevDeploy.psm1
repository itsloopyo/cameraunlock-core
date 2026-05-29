#!/usr/bin/env pwsh
# DevDeploy.psm1 - Orchestrators for `pixi run install` (dev-deploy) flows
# Part of CameraUnlock-Core shared utilities.
#
# Each per-strategy `Invoke-DevDeploy*` runs the canonical dev-install
# pipeline for that loader (resolve game, copy DLLs from build output,
# run the patch / loader-setup, cleanup). Per-mod scripts/deploy.ps1
# files reduce to a thin wrapper that sets the mod-specific config and
# delegates here. Source of truth for the orchestration is here - bumping
# the cameraunlock-core submodule + re-running `pixi run install` ships
# any change without per-mod re-templating.
#
# Build output is heterogeneous across mods (cmake -> bin/<Config>/,
# cargo -> target/.../<config>/, dotnet -> src/<Project>/bin/<Config>/<TFM>/).
# Wrappers know their own build layout; they pass an absolute
# -BuildOutputPath into each orchestrator. The dev-deploy code does not
# guess.

$ErrorActionPreference = "Stop"

Import-Module (Join-Path $PSScriptRoot 'GamePathDetection.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'ModDeployment.psm1')     -Force
Import-Module (Join-Path $PSScriptRoot 'ModLoaderSetup.psm1')    -Force

# Shared resolver: prefer caller-supplied -GivenPath (the launcher always
# passes one), else fall back to Find-GamePath against games.json. Throws
# with a clear diagnostic on miss so every orchestrator's "not found"
# error matches the install.cmd template's wording.
function Resolve-DevGamePath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$GameId,
        [Parameter(Mandatory)][string]$GameDisplayName,
        [string]$GivenPath
    )
    if ($GivenPath) {
        Write-Host "Using launcher-provided game path: $GivenPath" -ForegroundColor Green
        return $GivenPath
    }
    $gamePath = Find-GamePath -GameId $GameId
    if (-not $gamePath) {
        $config = Get-GameConfig -GameId $GameId
        Write-GameNotFoundError `
            -GameName $GameDisplayName `
            -EnvVar $config.EnvVar `
            -SteamFolder $config.SteamFolder
        throw "Game not found: $GameDisplayName"
    }
    Write-Host "Found game installation at: $gamePath" -ForegroundColor Green
    return $gamePath
}

# Internal: validate the named file exists in the build output dir.
# Failure mode mirrors the pre-refactor wording so muscle memory holds.
function Assert-DevBuildArtifact {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$BuildOutputPath,
        [Parameter(Mandatory)][string]$FileName
    )
    if (-not (Test-Path $BuildOutputPath)) {
        throw "Build output directory not found: $BuildOutputPath. Run 'pixi run build' first."
    }
    $sourceFile = Join-Path $BuildOutputPath $FileName
    if (-not (Test-Path $sourceFile)) {
        throw "Built file not found at: $sourceFile. Run 'pixi run build' first."
    }
}

# Internal: derive game-exe directory from games.json relpath. Used by
# ASI/Shim where plugins land in the same dir as the game's main exe.
function Resolve-DevExeDir {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$GamePath,
        [Parameter(Mandatory)][string]$GameId
    )
    $gameExeRelpath = (Get-GameConfig -GameId $GameId).Executable
    $exeDir = Split-Path -Parent (Join-Path $GamePath $gameExeRelpath)
    if (-not (Test-Path $exeDir)) {
        throw "Exe directory not found: $exeDir (derived from $gameExeRelpath)"
    }
    return $exeDir
}

# Internal: refuse to deploy while the game is running. A loaded mod holds
# an exclusive handle on its DLL/.asi, so Copy-Item -Force fails with an
# opaque IOException mid-deploy. Fail fast with an actionable message
# before touching any files.
function Assert-DevGameNotRunning {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$GameId,
        [Parameter(Mandatory)][string]$GameDisplayName
    )
    $exeLeaf  = Split-Path -Leaf (Get-GameConfig -GameId $GameId).Executable
    $procName = [IO.Path]::GetFileNameWithoutExtension($exeLeaf)
    if (Get-Process -Name $procName -ErrorAction SilentlyContinue) {
        throw "$GameDisplayName is running ($exeLeaf). Close it before deploying - the loaded mod locks its files."
    }
}

<#
.SYNOPSIS
    Dev-deploy a Mono.Cecil-patched mod into the game's Managed folder.
.DESCRIPTION
    The Cecil flow: build output already produced by `pixi run build`,
    DLLs copied into Managed, then a per-mod patcher script-block is
    invoked to mutate Assembly-CSharp.dll. Backup/restore around the
    patch keeps re-runs clean (always patches a known-good baseline).
.PARAMETER BuildOutputPath
    Absolute path to the directory holding the freshly-built mod DLL +
    extras. Caller computes from its own build layout.
.PARAMETER ModDllName
    Final DLL filename produced by the build (e.g. HeadTracking.dll).
.PARAMETER ManagedSubfolder
    Relative path under GamePath that contains Assembly-CSharp.dll.
.PARAMETER ExtraDlls
    Additional DLLs from BuildOutputPath to copy into Managed.
.PARAMETER Patcher
    [scriptblock] called with one positional arg ($assemblyPath).
.OUTPUTS
    Hashtable: @{ GamePath; ManagedPath; DeployedDllPath }
#>
function Invoke-DevDeployCecil {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$GameId,
        [Parameter(Mandatory)][string]$GameDisplayName,
        [Parameter(Mandatory)][string]$BuildOutputPath,
        [Parameter(Mandatory)][string]$ModDllName,
        [Parameter(Mandatory)][string]$ManagedSubfolder,
        [string]$AssemblyDll = 'Assembly-CSharp.dll',
        [string[]]$ExtraDlls = @(),
        [string]$GivenPath,
        [Parameter(Mandatory)][scriptblock]$Patcher
    )

    Assert-DevBuildArtifact -BuildOutputPath $BuildOutputPath -FileName $ModDllName

    $gamePath = Resolve-DevGamePath -GameId $GameId -GameDisplayName $GameDisplayName -GivenPath $GivenPath
    $managedPath = Join-Path $gamePath $ManagedSubfolder
    if (-not (Test-Path $managedPath)) {
        throw "Managed folder not found at: $managedPath"
    }
    $assemblyPath = Join-Path $managedPath $AssemblyDll
    if (-not (Test-Path $assemblyPath)) {
        throw "$AssemblyDll not found at: $assemblyPath"
    }

    Write-Host ""
    Write-Host "Deploying mod files..." -ForegroundColor Yellow
    $copyResult = Copy-ModFiles `
        -SourceDir $BuildOutputPath `
        -TargetDir $managedPath `
        -ModDllName $ModDllName `
        -Dependencies $ExtraDlls
    if (-not $copyResult.Success) {
        Write-DeploymentError -Errors $copyResult.Errors
        throw "Copy-ModFiles failed"
    }

    Write-Host ""
    Write-Host "Patching $AssemblyDll..." -ForegroundColor Yellow
    $backupPath = New-FileBackup -FilePath $assemblyPath
    if ($backupPath -and (Test-Path $backupPath)) {
        Restore-FileFromBackup -FilePath $assemblyPath | Out-Null
    }

    & $Patcher $assemblyPath

    $removedFiles = @(Remove-OldDoorstopFiles -GamePath $gamePath)
    if ($removedFiles.Count -gt 0) {
        Write-Host "  Cleaned up $($removedFiles.Count) old doorstop file(s)" -ForegroundColor Gray
    }

    return @{
        GamePath        = $gamePath
        ManagedPath     = $managedPath
        DeployedDllPath = (Join-Path $managedPath $ModDllName)
    }
}

<#
.SYNOPSIS
    Dev-deploy a BepInEx mod to <game>/BepInEx/plugins/.
.DESCRIPTION
    Ensures BepInEx is installed (via Install-BepInEx) if -EnsureLoader
    is set. Copies the freshly-built mod DLL + extras from BuildOutputPath
    into BepInEx/plugins/.
#>
function Invoke-DevDeployBepInEx {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$GameId,
        [Parameter(Mandatory)][string]$GameDisplayName,
        [Parameter(Mandatory)][string]$BuildOutputPath,
        [Parameter(Mandatory)][string]$ModDllName,
        [string[]]$ExtraDlls = @(),
        [string]$GivenPath,
        [switch]$EnsureLoader,
        [ValidateSet(5,6)][int]$MajorVersion = 5,
        [ValidateSet('x64','x86')][string]$Architecture = 'x64',
        [string]$VendorZip,
        [string]$PluginSubfolder
    )

    Assert-DevBuildArtifact -BuildOutputPath $BuildOutputPath -FileName $ModDllName

    $gamePath = Resolve-DevGamePath -GameId $GameId -GameDisplayName $GameDisplayName -GivenPath $GivenPath

    if (-not (Test-BepInExInstalled -GamePath $gamePath)) {
        if ($EnsureLoader) {
            $installArgs = @{
                GamePath     = $gamePath
                MajorVersion = $MajorVersion
                Architecture = $Architecture
            }
            if ($VendorZip) { $installArgs.VendorZip = $VendorZip }
            Install-BepInEx @installArgs | Out-Null
        } else {
            throw "BepInEx not detected at $gamePath. Pass -EnsureLoader to auto-install, or install BepInEx by hand."
        }
    }

    $pluginsPath = Get-BepInExPluginsPath -GamePath $gamePath
    if ($PluginSubfolder) { $pluginsPath = Join-Path $pluginsPath $PluginSubfolder }
    if (-not (Test-Path $pluginsPath)) { New-Item -ItemType Directory -Path $pluginsPath -Force | Out-Null }

    Write-Host ""
    Write-Host "Deploying mod files..." -ForegroundColor Yellow
    $copyResult = Copy-ModFiles `
        -SourceDir $BuildOutputPath `
        -TargetDir $pluginsPath `
        -ModDllName $ModDllName `
        -Dependencies $ExtraDlls
    if (-not $copyResult.Success) {
        Write-DeploymentError -Errors $copyResult.Errors
        throw "Copy-ModFiles failed"
    }

    return @{
        GamePath        = $gamePath
        PluginsPath     = $pluginsPath
        DeployedDllPath = (Join-Path $pluginsPath $ModDllName)
    }
}

<#
.SYNOPSIS
    Dev-deploy a MelonLoader mod to <game>/Mods/.
.DESCRIPTION
    Ensures MelonLoader is installed if -EnsureLoader is set. Copies mod
    DLL + extras into Mods/. Some mods pin a specific MelonLoader version
    (Firewatch needs 0.5.7 to avoid the RegexOptions crash on Unity 2017
    Mono) - pass it via -Version.
#>
function Invoke-DevDeployMelonLoader {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$GameId,
        [Parameter(Mandatory)][string]$GameDisplayName,
        [Parameter(Mandatory)][string]$BuildOutputPath,
        [Parameter(Mandatory)][string]$ModDllName,
        [string[]]$ExtraDlls = @(),
        [string]$GivenPath,
        [switch]$EnsureLoader,
        [ValidateSet('x64','x86')][string]$Architecture = 'x64',
        [string]$Version
    )

    Assert-DevBuildArtifact -BuildOutputPath $BuildOutputPath -FileName $ModDllName

    $gamePath = Resolve-DevGamePath -GameId $GameId -GameDisplayName $GameDisplayName -GivenPath $GivenPath

    if (-not (Test-MelonLoaderInstalled -GamePath $gamePath)) {
        if ($EnsureLoader) {
            $installArgs = @{ GamePath = $gamePath; Architecture = $Architecture }
            if ($Version) { $installArgs['Version'] = $Version }
            Install-MelonLoader @installArgs | Out-Null
        } else {
            throw "MelonLoader not detected at $gamePath. Pass -EnsureLoader to auto-install, or install MelonLoader by hand."
        }
    }

    $modsPath = Get-MelonLoaderModsPath -GamePath $gamePath
    if (-not (Test-Path $modsPath)) { New-Item -ItemType Directory -Path $modsPath -Force | Out-Null }

    Write-Host ""
    Write-Host "Deploying mod files..." -ForegroundColor Yellow
    $copyResult = Copy-ModFiles `
        -SourceDir $BuildOutputPath `
        -TargetDir $modsPath `
        -ModDllName $ModDllName `
        -Dependencies $ExtraDlls
    if (-not $copyResult.Success) {
        Write-DeploymentError -Errors $copyResult.Errors
        throw "Copy-ModFiles failed"
    }

    return @{
        GamePath        = $gamePath
        ModsPath        = $modsPath
        DeployedDllPath = (Join-Path $modsPath $ModDllName)
    }
}

<#
.SYNOPSIS
    Dev-deploy an Ultimate ASI Loader mod to the game's exe directory.
.DESCRIPTION
    Copies the .asi (and any sibling INI / config file) from BuildOutputPath
    to the directory containing the game executable. The exe location is
    resolved from games.json by GameId - some games have nested .exe paths
    (e.g. ph/work/bin/x64/) and the ASI loader must land in the same dir
    as the .exe.

    If the ASI loader is not present and -VendorLoaderDll is supplied,
    the loader is auto-installed from that path. Otherwise the deploy
    fails loudly with a "run install.cmd first" hint.
.PARAMETER ConfigFile
    Optional absolute path to a sibling config file (typically
    HeadTracking.ini at the project root) to deploy alongside the .asi.
.PARAMETER VendorLoaderDll
    Optional absolute path to a vendored ASI loader DLL
    (e.g. <root>/vendor/ultimate-asi-loader/dinput8.dll). When provided
    and the loader is missing in the game, it is copied to ExeDir as
    AsiLoaderName.
.PARAMETER AsiLoaderName
    Filename the ASI DLL is renamed to (winmm.dll, dinput8.dll, etc.).
#>
function Invoke-DevDeployASILoader {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$GameId,
        [Parameter(Mandatory)][string]$GameDisplayName,
        [Parameter(Mandatory)][string]$BuildOutputPath,
        [Parameter(Mandatory)][string]$ModDllName,
        [string]$ConfigFile,
        [string]$VendorLoaderDll,
        [string]$AsiLoaderName = 'winmm.dll',
        [string[]]$ExtraDlls = @(),
        [string]$GivenPath
    )

    Assert-DevBuildArtifact -BuildOutputPath $BuildOutputPath -FileName $ModDllName
    if ($ConfigFile -and -not (Test-Path $ConfigFile)) {
        throw "ConfigFile not found at: $ConfigFile"
    }

    $gamePath = Resolve-DevGamePath -GameId $GameId -GameDisplayName $GameDisplayName -GivenPath $GivenPath
    Assert-DevGameNotRunning -GameId $GameId -GameDisplayName $GameDisplayName
    $exeDir   = Resolve-DevExeDir -GamePath $gamePath -GameId $GameId

    $loaderTarget = Join-Path $exeDir $AsiLoaderName
    if (-not (Test-Path $loaderTarget)) {
        if ($VendorLoaderDll) {
            if (-not (Test-Path $VendorLoaderDll)) {
                throw "VendorLoaderDll not found at: $VendorLoaderDll"
            }
            Copy-Item -Path $VendorLoaderDll -Destination $loaderTarget -Force
            Write-Host "Installed ASI loader as $AsiLoaderName" -ForegroundColor Green
        } else {
            throw "ASI loader $AsiLoaderName not present at $exeDir. Run install.cmd to install the loader first, or pass -VendorLoaderDll."
        }
    }

    Write-Host ""
    Write-Host "Deploying mod files to: $exeDir" -ForegroundColor Yellow
    $copyResult = Copy-ModFiles `
        -SourceDir $BuildOutputPath `
        -TargetDir $exeDir `
        -ModDllName $ModDllName `
        -Dependencies $ExtraDlls
    if (-not $copyResult.Success) {
        Write-DeploymentError -Errors $copyResult.Errors
        throw "Copy-ModFiles failed"
    }

    if ($ConfigFile) {
        $configLeaf = Split-Path -Leaf $ConfigFile
        Copy-Item -Path $ConfigFile -Destination (Join-Path $exeDir $configLeaf) -Force
        Write-Host "Deployed $configLeaf" -ForegroundColor Green
    }

    return @{
        GamePath        = $gamePath
        ExeDir          = $exeDir
        DeployedDllPath = (Join-Path $exeDir $ModDllName)
    }
}

<#
.SYNOPSIS
    Dev-deploy a REFramework mod to <game>/reframework/plugins/.
.DESCRIPTION
    Copies the mod DLL + extras + optional config file into
    reframework/plugins/. If -VendorReframeworkZip is supplied and the
    REFramework loader (dinput8.dll) is absent, extracts the bundled zip
    into the game directory. Otherwise fails loudly.
.PARAMETER VendorReframeworkZip
    Optional absolute path to a vendored REFramework zip
    (e.g. <root>/vendor/reframework/RE2.zip). Extracted into the game
    directory when the loader is missing.
.PARAMETER ConfigFile
    Optional absolute path to a sibling config file (typically
    HeadTracking.ini at the project root) to deploy alongside the DLL.
#>
function Invoke-DevDeployREFramework {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$GameId,
        [Parameter(Mandatory)][string]$GameDisplayName,
        [Parameter(Mandatory)][string]$BuildOutputPath,
        [Parameter(Mandatory)][string]$ModDllName,
        [string]$ConfigFile,
        [string]$VendorReframeworkZip,
        [string[]]$ExtraDlls = @(),
        [string]$GivenPath
    )

    Assert-DevBuildArtifact -BuildOutputPath $BuildOutputPath -FileName $ModDllName
    if ($ConfigFile -and -not (Test-Path $ConfigFile)) {
        throw "ConfigFile not found at: $ConfigFile"
    }

    $gamePath = Resolve-DevGamePath -GameId $GameId -GameDisplayName $GameDisplayName -GivenPath $GivenPath

    $loaderDll = Join-Path $gamePath 'dinput8.dll'
    if (-not (Test-Path $loaderDll)) {
        if ($VendorReframeworkZip) {
            if (-not (Test-Path $VendorReframeworkZip)) {
                throw "VendorReframeworkZip not found at: $VendorReframeworkZip"
            }
            Write-Host "REFramework not found. Extracting bundled copy..." -ForegroundColor Yellow
            Expand-Archive -Path $VendorReframeworkZip -DestinationPath $gamePath -Force
            if (-not (Test-Path $loaderDll)) {
                throw "REFramework install failed: dinput8.dll not found after extraction of $VendorReframeworkZip."
            }
            Write-Host "  Installed REFramework from $VendorReframeworkZip" -ForegroundColor Green
        } else {
            throw "REFramework loader (dinput8.dll) not present at $gamePath. Run install.cmd, or pass -VendorReframeworkZip."
        }
    }

    $pluginsPath = Join-Path $gamePath 'reframework\plugins'
    if (-not (Test-Path $pluginsPath)) { New-Item -ItemType Directory -Path $pluginsPath -Force | Out-Null }

    Write-Host ""
    Write-Host "Deploying mod files to: $pluginsPath" -ForegroundColor Yellow
    $copyResult = Copy-ModFiles `
        -SourceDir $BuildOutputPath `
        -TargetDir $pluginsPath `
        -ModDllName $ModDllName `
        -Dependencies $ExtraDlls
    if (-not $copyResult.Success) {
        Write-DeploymentError -Errors $copyResult.Errors
        throw "Copy-ModFiles failed"
    }

    if ($ConfigFile) {
        $configLeaf = Split-Path -Leaf $ConfigFile
        Copy-Item -Path $ConfigFile -Destination (Join-Path $pluginsPath $configLeaf) -Force
        Write-Host "Deployed $configLeaf" -ForegroundColor Green
    }

    return @{
        GamePath        = $gamePath
        PluginsPath     = $pluginsPath
        DeployedDllPath = (Join-Path $pluginsPath $ModDllName)
    }
}

<#
.SYNOPSIS
    Dev-deploy a shim-only mod (system-DLL replacement) to the game's
    exe directory, with first-install backup of any pre-existing file.
.DESCRIPTION
    Shim mods replace a system DLL the game loads at startup
    (xinput1_3.dll / dxgi.dll / winmm.dll / etc.). On first deploy, if
    the target name already exists in the exe dir, back it up to
    <name>.backup so uninstall can restore it.

    For builds that emit a different filename than the deployed name
    (Rust/cargo crates often produce `<crate_name>.dll`, not the system
    DLL the game expects), pass -SourceDllName for the build artifact's
    actual filename. The deployed file in the game uses ModDllName.
.PARAMETER SourceDllName
    Optional. Filename of the built artifact in BuildOutputPath. Defaults
    to ModDllName if the build produces a file with the deployed name
    directly.
#>
function Invoke-DevDeployShim {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$GameId,
        [Parameter(Mandatory)][string]$GameDisplayName,
        [Parameter(Mandatory)][string]$BuildOutputPath,
        [Parameter(Mandatory)][string]$ModDllName,
        [string]$SourceDllName,
        [string[]]$ExtraDlls = @(),
        [string]$GivenPath
    )

    if (-not $SourceDllName) { $SourceDllName = $ModDllName }
    Assert-DevBuildArtifact -BuildOutputPath $BuildOutputPath -FileName $SourceDllName

    $gamePath = Resolve-DevGamePath -GameId $GameId -GameDisplayName $GameDisplayName -GivenPath $GivenPath
    $exeDir   = Resolve-DevExeDir -GamePath $gamePath -GameId $GameId

    # Backup-on-first-deploy. .backup is the user's pre-mod state and
    # never gets clobbered.
    $allFiles = @($ModDllName) + $ExtraDlls
    foreach ($f in $allFiles) {
        $target = Join-Path $exeDir $f
        $backup = "$target.backup"
        if ((Test-Path $target) -and -not (Test-Path $backup)) {
            Copy-Item -Path $target -Destination $backup -Force
            Write-Host "  Backed up original $f to $f.backup" -ForegroundColor Gray
        }
    }

    Write-Host ""
    Write-Host "Deploying shim files to: $exeDir" -ForegroundColor Yellow

    # Source filename may differ from deployed filename - explicit copy
    # with rename; can't use Copy-ModFiles which assumes source==dest.
    $sourceFile = Join-Path $BuildOutputPath $SourceDllName
    $targetFile = Join-Path $exeDir $ModDllName
    Copy-Item -Path $sourceFile -Destination $targetFile -Force
    Write-Host "Deployed $ModDllName" -ForegroundColor Green

    foreach ($extra in $ExtraDlls) {
        $extraSrc = Join-Path $BuildOutputPath $extra
        if (-not (Test-Path $extraSrc)) {
            throw "Required extra file not found: $extraSrc"
        }
        Copy-Item -Path $extraSrc -Destination (Join-Path $exeDir $extra) -Force
        Write-Host "Deployed $extra" -ForegroundColor Green
    }

    return @{
        GamePath        = $gamePath
        ExeDir          = $exeDir
        DeployedDllPath = (Join-Path $exeDir $ModDllName)
    }
}

Export-ModuleMember -Function @(
    'Invoke-DevDeployCecil',
    'Invoke-DevDeployBepInEx',
    'Invoke-DevDeployMelonLoader',
    'Invoke-DevDeployASILoader',
    'Invoke-DevDeployREFramework',
    'Invoke-DevDeployShim',
    'Resolve-DevGamePath'
)
