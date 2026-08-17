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

# NO -Force on any of these. -Force is Remove-Module + re-import, and Remove-Module
# is not scoped to the importing module: it unloads the module from the CALLER's
# session too. A mod's deploy.ps1 that imports GamePathDetection or ModDeployment
# for itself and THEN imports this module lost Find-GamePath and
# Test-FileContainsMarker the moment this line ran - order-dependent, so it
# presents as intermittent. Reproduced in a child runspace: both went from
# available to CommandNotFoundException across this import. Same defect as the one
# already fixed in ModLoaderSetup.psm1.
Import-Module (Join-Path $PSScriptRoot 'GamePathDetection.psm1')
Import-Module (Join-Path $PSScriptRoot 'ModDeployment.psm1')
Import-Module (Join-Path $PSScriptRoot 'ModLoaderSetup.psm1')

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
    if (-not (Test-Path -LiteralPath $BuildOutputPath)) {
        throw "Build output directory not found: $BuildOutputPath. Run 'pixi run build' first."
    }
    $sourceFile = Join-Path $BuildOutputPath $FileName
    if (-not (Test-Path -LiteralPath $sourceFile)) {
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
    if (-not (Test-Path -LiteralPath $exeDir)) {
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
# True when a patcher's return value reports failure, whatever shape it arrived in.
#
# Three shapes are all in use and none of the obvious tests covers them: `-is [hashtable]`
# is FALSE for an [ordered] dictionary (that is an OrderedDictionary) and for a
# [pscustomobject], while PSObject.Properties does not see a Hashtable's KEYS at all - they
# come through the dictionary adapter. Getting this wrong fails OPEN: the failure check is
# skipped and a failed patch reports deployment success.
function Test-PatchResultFailure {
    param($Result)

    if ($null -eq $Result) { return $false }
    if ($Result -is [System.Collections.IDictionary]) {
        return $Result.Contains('Success') -and -not $Result['Success']
    }
    $property = $Result.PSObject.Properties['Success']
    return ($null -ne $property) -and (-not $property.Value)
}

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
        [Parameter(Mandatory)][scriptblock]$Patcher,
        # Optional: the additive patch marker. When set, the backup is
        # guaranteed pristine - a patched assembly is never captured as the
        # .original. With $Unpatcher we self-heal a patched source; without it
        # we fail fast rather than enshrine a corrupt backup.
        [string]$PatchMarker = '',
        # Optional: [scriptblock] called with one positional arg ($assemblyPath)
        # to reverse the patch in place. Enables self-heal of an already-patched
        # source when no pristine backup exists.
        [scriptblock]$Unpatcher = $null,
        # Delete leftover doorstop files (winhttp.dll, version.dll,
        # doorstop_config.ini, .doorstop_version) from the game root. Off by
        # default: those names belong to BepInEx 5 and Ultimate ASI Loader, so a
        # Cecil deploy that removes them silently stops another mod's loader.
        # Even when set, Remove-OldDoorstopFiles only deletes what the state
        # file records as ours.
        [switch]$CleanDoorstop
    )

    Assert-DevBuildArtifact -BuildOutputPath $BuildOutputPath -FileName $ModDllName

    $gamePath = Resolve-DevGamePath -GameId $GameId -GameDisplayName $GameDisplayName -GivenPath $GivenPath
    $managedPath = Join-Path $gamePath $ManagedSubfolder
    if (-not (Test-Path -LiteralPath $managedPath)) {
        throw "Managed folder not found at: $managedPath"
    }
    $assemblyPath = Join-Path $managedPath $AssemblyDll
    if (-not (Test-Path -LiteralPath $assemblyPath)) {
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

    # Guarantee the .original backup is pristine. The patch is additive, so a
    # patched file copied to .original would silently become a corrupt backup
    # that uninstall later restores as a broken assembly. With a marker we can
    # tell patched from clean; with an unpatcher we repair in place.
    if ($PatchMarker) {
        $backupFile = "$assemblyPath.original"
        if (Test-Path -LiteralPath $backupFile) {
            if (Test-FileContainsMarker -FilePath $backupFile -Marker $PatchMarker) {
                if ($Unpatcher) {
                    Write-Host "  Existing .original is patched (corrupt backup) - repairing via unpatch..." -ForegroundColor Yellow
                    & $Unpatcher $backupFile | Out-Null
                    if (Test-FileContainsMarker -FilePath $backupFile -Marker $PatchMarker) {
                        throw "Unpatch did not clean $AssemblyDll.original; refusing to keep a corrupt backup. Verify game files via Steam."
                    }
                } else {
                    throw "$AssemblyDll.original is patched (corrupt backup). Verify game files via Steam, delete the .original, and re-run."
                }
            }
        } elseif (Test-FileContainsMarker -FilePath $assemblyPath -Marker $PatchMarker) {
            if ($Unpatcher) {
                Write-Host "  $AssemblyDll is patched but has no .original - reconstructing a clean baseline via unpatch..." -ForegroundColor Yellow
                & $Unpatcher $assemblyPath | Out-Null
                if (Test-FileContainsMarker -FilePath $assemblyPath -Marker $PatchMarker) {
                    throw "Unpatch did not clean $AssemblyDll; cannot establish a pristine baseline. Verify game files via Steam."
                }
            } else {
                throw "$AssemblyDll is patched but no .original exists; cannot establish a clean baseline. Verify game files via Steam, then re-run."
            }
        }
    }

    $backupPath = New-FileBackup -FilePath $assemblyPath
    if ($backupPath -and (Test-Path -LiteralPath $backupPath)) {
        Restore-FileFromBackup -FilePath $assemblyPath | Out-Null
    }

    # Captured, not discarded and not left to leak. Leaking it joins this function's
    # output stream and the documented Hashtable return becomes a 2-element array, so
    # $result.Count and $result.Keys report on the array rather than the hashtable.
    #
    # Discarding it is just as wrong: Invoke-HeadTrackingPatch signals a patch failure by
    # RETURNING @{ Success = $false; Errors = @(...) }, not by throwing (it does throw for
    # a patcher compile failure - see CHANGELOG - but not for the patch itself). Piping to
    # Out-Null would swallow that and report deployment success after a failed patch.
    #
    # Matched on shape rather than on type. A patcher's result may arrive alongside stray
    # pipeline output (a Copy-Item -PassThru, a New-Item), and it may be a hashtable, a
    # [pscustomobject] or an [ordered] dictionary - and `-is [hashtable]` is FALSE for the
    # latter two, so a type test silently skipped the failure check for both. Anything
    # carrying a falsey Success is treated as a failure, which is the direction that must
    # not fail open.
    $patchOutput = @(& $Patcher $assemblyPath)
    $patchFailure = $patchOutput | Where-Object { Test-PatchResultFailure $_ } | Select-Object -First 1
    if ($patchFailure) {
        throw "Patcher reported failure for ${assemblyPath}: $($patchFailure.Errors -join '; ')"
    }

    if ($CleanDoorstop) {
        $removedFiles = @(Remove-OldDoorstopFiles -GamePath $gamePath)
        if ($removedFiles.Count -gt 0) {
            Write-Host "  Cleaned up $($removedFiles.Count) old doorstop file(s)" -ForegroundColor Gray
        }
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
    if (-not (Test-Path -LiteralPath $pluginsPath)) { New-Item -ItemType Directory -Path $pluginsPath -Force | Out-Null }

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
    if (-not (Test-Path -LiteralPath $modsPath)) { New-Item -ItemType Directory -Path $modsPath -Force | Out-Null }

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
.PARAMETER ExeSubDir
    Optional game-exe-relative subdirectory the loader and .asi deploy into
    instead of the exe directory itself. Source-engine games load tier0.dll
    from <game>\bin with an altered search path, so the proxy + .asi must
    live there (Portal 2 passes 'bin'). Created if absent.
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
        [string]$GivenPath,
        [string]$ExeSubDir
    )

    Assert-DevBuildArtifact -BuildOutputPath $BuildOutputPath -FileName $ModDllName
    if ($ConfigFile -and -not (Test-Path -LiteralPath $ConfigFile)) {
        throw "ConfigFile not found at: $ConfigFile"
    }

    $gamePath = Resolve-DevGamePath -GameId $GameId -GameDisplayName $GameDisplayName -GivenPath $GivenPath
    Assert-DevGameNotRunning -GameId $GameId -GameDisplayName $GameDisplayName
    $exeDir   = Resolve-DevExeDir -GamePath $gamePath -GameId $GameId
    if ($ExeSubDir) {
        $exeDir = Join-Path $exeDir $ExeSubDir
        if (-not (Test-Path -LiteralPath $exeDir)) { New-Item -ItemType Directory -Path $exeDir -Force | Out-Null }
    }

    $loaderTarget = Join-Path $exeDir $AsiLoaderName
    if (-not (Test-Path -LiteralPath $loaderTarget)) {
        if ($VendorLoaderDll) {
            if (-not (Test-Path -LiteralPath $VendorLoaderDll)) {
                throw "VendorLoaderDll not found at: $VendorLoaderDll"
            }
            Copy-Item -LiteralPath $VendorLoaderDll -Destination $loaderTarget -Force
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
        Copy-Item -LiteralPath $ConfigFile -Destination (Join-Path $exeDir $configLeaf) -Force
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
    if ($ConfigFile -and -not (Test-Path -LiteralPath $ConfigFile)) {
        throw "ConfigFile not found at: $ConfigFile"
    }

    $gamePath = Resolve-DevGamePath -GameId $GameId -GameDisplayName $GameDisplayName -GivenPath $GivenPath

    $loaderDll = Join-Path $gamePath 'dinput8.dll'
    if (-not (Test-Path -LiteralPath $loaderDll)) {
        if ($VendorReframeworkZip) {
            if (-not (Test-Path -LiteralPath $VendorReframeworkZip)) {
                throw "VendorReframeworkZip not found at: $VendorReframeworkZip"
            }
            Write-Host "REFramework not found. Extracting bundled copy..." -ForegroundColor Yellow
            Expand-Archive -LiteralPath $VendorReframeworkZip -DestinationPath $gamePath -Force
            if (-not (Test-Path -LiteralPath $loaderDll)) {
                throw "REFramework install failed: dinput8.dll not found after extraction of $VendorReframeworkZip."
            }
            Write-Host "  Installed REFramework from $VendorReframeworkZip" -ForegroundColor Green
        } else {
            throw "REFramework loader (dinput8.dll) not present at $gamePath. Run install.cmd, or pass -VendorReframeworkZip."
        }
    }

    $pluginsPath = Join-Path $gamePath 'reframework\plugins'
    if (-not (Test-Path -LiteralPath $pluginsPath)) { New-Item -ItemType Directory -Path $pluginsPath -Force | Out-Null }

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
        Copy-Item -LiteralPath $ConfigFile -Destination (Join-Path $pluginsPath $configLeaf) -Force
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
    (xinput1_3.dll / dxgi.dll / winmm.dll / etc.). If the target name
    already exists in the exe dir and its bytes DIFFER from the shim about
    to be written, it is the game's own file and is backed up to
    <name>.backup so uninstall can restore it. If the bytes match, it is
    this mod's shim from an earlier deploy and is left alone rather than
    enshrined as "the original".

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
        [string]$GivenPath,
        # A byte sequence present in EVERY build of this mod's shim, used to answer
        # "is the file already sitting there ours?" independently of build config.
        # Required, and deliberately not defaulted - see the backup block below for
        # what guessing costs.
        [Parameter(Mandatory)][string]$ShimMarker
    )

    if (-not $SourceDllName) { $SourceDllName = $ModDllName }
    Assert-DevBuildArtifact -BuildOutputPath $BuildOutputPath -FileName $SourceDllName

    $gamePath = Resolve-DevGamePath -GameId $GameId -GameDisplayName $GameDisplayName -GivenPath $GivenPath
    $exeDir   = Resolve-DevExeDir -GamePath $gamePath -GameId $GameId

    # Backup-on-first-deploy. .backup is the user's pre-mod state and
    # never gets clobbered.
    #
    # When the game ships no file of that name - the common case for a
    # dxgi/winmm/xinput proxy - the ABSENCE is the pre-mod state, and it has to
    # be recorded too. Without the sentinel, deploy 1 writes no backup (nothing
    # to back up), deploy 2 sees the target present (it is OUR shim) with no
    # backup and copies the shim to <name>.backup; uninstall then "restores the
    # original", reinstalling the mod permanently and reporting success.
    # Decided PER FILE by IDENTITY, not by comparing against the build about to be
    # written. That comparison was the bug: it answers "is this byte-identical to THIS
    # build", which is a different question. A Release shim already in place (from a
    # lopari install) does not match a Debug build a developer deploys, so our OWN shim
    # was captured as the user's pre-mod original. Uninstall then "restored the
    # original", silently reinstalling the mod and reporting a clean removal. Observed
    # on a real install, where the shim, its .backup AND lopari's .lopari-backup were
    # all head-tracking binaries and the game ships no file of that name at all.
    #
    # A marker present in every build answers the real question. This is the shim
    # equivalent of the Cecil path's PatchMarker, and it is Mandatory for the same
    # reason: with no way to establish identity there is no safe default. Backing up
    # unconditionally poisons the backup; skipping the backup unconditionally loses a
    # file that really was the user's. So the caller has to say.
    #
    # A sentinel file was tried and removed: nothing read or deleted it, so after a
    # deploy/uninstall cycle it survived with the shim gone. If the user then installed
    # something that legitimately owns that name (ReShade's dxgi.dll), the next deploy
    # saw the stale sentinel, skipped the backup, and overwrote it with no way back.
    $allFiles = @($ModDllName) + $ExtraDlls
    foreach ($f in $allFiles) {
        $target = Join-Path $exeDir $f
        $backup = "$target.backup"

        if (Test-Path -LiteralPath $backup) { continue }
        if (-not (Test-Path -LiteralPath $target)) { continue }

        if (Test-FileContainsMarker -FilePath $target -Marker $ShimMarker) {
            Write-Host "  $f is already this mod's shim - not capturing it as an original" -ForegroundColor Gray
        } else {
            Copy-Item -LiteralPath $target -Destination $backup -Force
            Write-Host "  Backed up original $f to $f.backup" -ForegroundColor Gray
        }
    }

    Write-Host ""
    Write-Host "Deploying shim files to: $exeDir" -ForegroundColor Yellow

    # Source filename may differ from deployed filename - explicit copy
    # with rename; can't use Copy-ModFiles which assumes source==dest.
    $sourceFile = Join-Path $BuildOutputPath $SourceDllName
    $targetFile = Join-Path $exeDir $ModDllName
    Copy-Item -LiteralPath $sourceFile -Destination $targetFile -Force
    Write-Host "Deployed $ModDllName" -ForegroundColor Green

    foreach ($extra in $ExtraDlls) {
        $extraSrc = Join-Path $BuildOutputPath $extra
        if (-not (Test-Path -LiteralPath $extraSrc)) {
            throw "Required extra file not found: $extraSrc"
        }
        Copy-Item -LiteralPath $extraSrc -Destination (Join-Path $exeDir $extra) -Force
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
