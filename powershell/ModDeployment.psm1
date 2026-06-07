#!/usr/bin/env pwsh
#Requires -Version 5.1
Set-StrictMode -Version Latest

<#
.SYNOPSIS
    Shared deployment utilities for CameraUnlock mods.
.DESCRIPTION
    Provides common deployment operations:
    - DLL copying with dependency resolution
    - Backup management
    - Deployment verification
    - Cleanup of old mod loader files
#>

<#
.SYNOPSIS
    Copies a mod DLL and its dependencies to the target directory.
.PARAMETER SourceDir
    Directory containing the built DLL and dependencies.
.PARAMETER TargetDir
    Target directory (usually game's Managed folder or plugins folder).
.PARAMETER ModDllName
    Name of the main mod DLL (e.g., "PainscreekHeadTracking.dll").
.PARAMETER Dependencies
    Array of dependency DLL names to copy (default: CameraUnlock.Core.dll, CameraUnlock.Core.Unity.dll).
.PARAMETER OptionalDependencies
    Array of optional dependency DLL names (copied if present, no error if missing).
.OUTPUTS
    Hashtable with deployment results.
#>
function Copy-ModFiles {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$SourceDir,

        [Parameter(Mandatory=$true)]
        [string]$TargetDir,

        [Parameter(Mandatory=$true)]
        [string]$ModDllName,

        [Parameter(Mandatory=$false)]
        [string[]]$Dependencies = @('CameraUnlock.Core.dll', 'CameraUnlock.Core.Unity.dll'),

        [Parameter(Mandatory=$false)]
        [string[]]$OptionalDependencies = @('Mono.Cecil.dll')
    )

    $results = @{
        Success = $true
        CopiedFiles = @()
        Errors = @()
    }

    # Verify source directory exists
    if (-not (Test-Path $SourceDir)) {
        $results.Success = $false
        $results.Errors += "Source directory not found: $SourceDir"
        return $results
    }

    # Create target directory if it doesn't exist
    if (-not (Test-Path $TargetDir)) {
        New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
        Write-Host "Created target directory: $TargetDir" -ForegroundColor Gray
    }

    # Copy main mod DLL
    $modDllPath = Join-Path $SourceDir $ModDllName
    if (Test-Path $modDllPath) {
        $destPath = Join-Path $TargetDir $ModDllName
        Copy-Item -Path $modDllPath -Destination $destPath -Force
        $results.CopiedFiles += $ModDllName
        Write-Host "Deployed $ModDllName" -ForegroundColor Green
    } else {
        $results.Success = $false
        $results.Errors += "Main mod DLL not found: $modDllPath"
        return $results
    }

    # Copy required dependencies
    foreach ($dep in $Dependencies) {
        $depPath = Join-Path $SourceDir $dep
        if (Test-Path $depPath) {
            Copy-Item -Path $depPath -Destination $TargetDir -Force
            $results.CopiedFiles += $dep
            Write-Host "Deployed $dep" -ForegroundColor Green
        } else {
            $results.Success = $false
            $results.Errors += "Required dependency not found: $depPath"
        }
    }

    # Copy optional dependencies
    foreach ($dep in $OptionalDependencies) {
        $depPath = Join-Path $SourceDir $dep
        if (Test-Path $depPath) {
            Copy-Item -Path $depPath -Destination $TargetDir -Force
            $results.CopiedFiles += $dep
            Write-Host "Deployed $dep" -ForegroundColor Green
        }
    }

    return $results
}

<#
.SYNOPSIS
    Creates a backup of a file if one doesn't exist.
.PARAMETER FilePath
    Path to the file to backup.
.PARAMETER BackupSuffix
    Suffix for the backup file (default: ".original").
.PARAMETER Force
    Create a new backup even if one exists.
.OUTPUTS
    Path to the backup file, or $null if backup was skipped.
#>
function New-FileBackup {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$FilePath,

        [Parameter(Mandatory=$false)]
        [string]$BackupSuffix = ".original",

        [switch]$Force
    )

    if (-not (Test-Path $FilePath)) {
        Write-Warning "File not found, cannot create backup: $FilePath"
        return $null
    }

    $backupPath = $FilePath + $BackupSuffix

    if ((Test-Path $backupPath) -and -not $Force) {
        Write-Host "Backup already exists: $backupPath" -ForegroundColor Gray
        return $backupPath
    }

    Copy-Item -Path $FilePath -Destination $backupPath -Force
    Write-Host "Created backup: $backupPath" -ForegroundColor Gray
    return $backupPath
}

<#
.SYNOPSIS
    Tests whether a (binary) file contains a marker's ASCII byte sequence.
.DESCRIPTION
    Used to tell a Mono.Cecil-patched assembly (which carries an injected
    marker type name) from a pristine one, so backup logic never captures or
    trusts a patched file as the .original. Reads raw bytes rather than using
    findstr/Select-String, which are unreliable on multi-MB binaries.
.PARAMETER FilePath
    Path to the file to inspect.
.PARAMETER Marker
    The marker string to search for.
.OUTPUTS
    Boolean. $true if the marker bytes are present.
#>
function Test-FileContainsMarker {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$FilePath,
        [Parameter(Mandatory=$true)][string]$Marker
    )

    if (-not (Test-Path -LiteralPath $FilePath)) {
        throw "File not found, cannot check for marker: $FilePath"
    }

    $bytes = [System.IO.File]::ReadAllBytes($FilePath)
    $needle = [System.Text.Encoding]::ASCII.GetBytes($Marker)
    $limit = $bytes.Length - $needle.Length
    for ($i = 0; $i -le $limit; $i++) {
        $match = $true
        for ($j = 0; $j -lt $needle.Length; $j++) {
            if ($bytes[$i + $j] -ne $needle[$j]) { $match = $false; break }
        }
        if ($match) { return $true }
    }
    return $false
}

<#
.SYNOPSIS
    Restores a file from backup.
.PARAMETER FilePath
    Path to the file to restore.
.PARAMETER BackupSuffix
    Suffix of the backup file (default: ".original").
.OUTPUTS
    Boolean indicating success.
#>
function Restore-FileFromBackup {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$FilePath,

        [Parameter(Mandatory=$false)]
        [string]$BackupSuffix = ".original"
    )

    $backupPath = $FilePath + $BackupSuffix

    if (-not (Test-Path $backupPath)) {
        Write-Warning "Backup not found: $backupPath"
        return $false
    }

    Copy-Item -Path $backupPath -Destination $FilePath -Force
    Write-Host "Restored from backup: $backupPath" -ForegroundColor Gray
    return $true
}

<#
.SYNOPSIS
    Removes old Unity Doorstop files from a game directory.
.DESCRIPTION
    Cleans up doorstop files that may be left over from previous mod installations.
.PARAMETER GamePath
    Path to the game installation directory.
.OUTPUTS
    Array of removed file names.
#>
function Remove-OldDoorstopFiles {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath
    )

    $doorstopFiles = @('winhttp.dll', 'version.dll', 'doorstop_config.ini', '.doorstop_version')
    $removedFiles = @()

    foreach ($file in $doorstopFiles) {
        $filePath = Join-Path $GamePath $file
        if (Test-Path $filePath) {
            Remove-Item $filePath -Force
            $removedFiles += $file
            Write-Host "Removed old doorstop file: $file" -ForegroundColor Gray
        }
    }

    return $removedFiles
}

<#
.SYNOPSIS
    Verifies that a mod is properly deployed.
.PARAMETER TargetDir
    Directory where mod files should be deployed.
.PARAMETER ModDllName
    Name of the main mod DLL.
.PARAMETER Dependencies
    Array of dependency DLL names to verify.
.OUTPUTS
    Hashtable with verification results.
#>
function Test-ModDeployment {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$TargetDir,

        [Parameter(Mandatory=$true)]
        [string]$ModDllName,

        [Parameter(Mandatory=$false)]
        [string[]]$Dependencies = @('CameraUnlock.Core.dll', 'CameraUnlock.Core.Unity.dll')
    )

    $results = @{
        Success = $true
        MissingFiles = @()
        FoundFiles = @()
    }

    # Check main mod DLL
    $modPath = Join-Path $TargetDir $ModDllName
    if (Test-Path $modPath) {
        $results.FoundFiles += $ModDllName
    } else {
        $results.Success = $false
        $results.MissingFiles += $ModDllName
    }

    # Check dependencies
    foreach ($dep in $Dependencies) {
        $depPath = Join-Path $TargetDir $dep
        if (Test-Path $depPath) {
            $results.FoundFiles += $dep
        } else {
            $results.Success = $false
            $results.MissingFiles += $dep
        }
    }

    return $results
}

<#
.SYNOPSIS
    Displays a deployment success message with hotkey information.
.PARAMETER ModName
    Display name of the mod.
.PARAMETER DeployPath
    Path where the mod was deployed.
.PARAMETER RecenterKey
    Key for recentering (default: "Home").
.PARAMETER ToggleKey
    Key for toggling (default: "End").
#>
function Write-DeploymentSuccess {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$ModName,

        [Parameter(Mandatory=$true)]
        [string]$DeployPath,

        [Parameter(Mandatory=$false)]
        [string]$RecenterKey = "Home",

        [Parameter(Mandatory=$false)]
        [string]$ToggleKey = "End"
    )

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "  Deployment Complete!" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "$ModName has been deployed to:" -ForegroundColor White
    Write-Host "  $DeployPath" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Start the game to use head tracking!" -ForegroundColor White
    Write-Host ""
    Write-Host "Controls:" -ForegroundColor Yellow
    Write-Host "  $RecenterKey - Recenter head tracking" -ForegroundColor Gray
    Write-Host "  $ToggleKey  - Toggle head tracking on/off" -ForegroundColor Gray
    Write-Host ""
}

<#
.SYNOPSIS
    Displays a deployment error message.
.PARAMETER Errors
    Array of error messages.
#>
function Write-DeploymentError {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string[]]$Errors
    )

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "  Deployment Failed!" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    foreach ($error in $Errors) {
        Write-Host "  - $error" -ForegroundColor Red
    }
    Write-Host ""
}

<#
.SYNOPSIS
    Gets the standard build output path for a mod project.
.PARAMETER ProjectRoot
    Root directory of the mod project.
.PARAMETER ProjectName
    Name of the project (folder under src/).
.PARAMETER Configuration
    Build configuration (Debug or Release).
.PARAMETER TargetFramework
    Target framework (default: net48).
.OUTPUTS
    Path to the build output directory.
#>
function Get-BuildOutputPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$ProjectRoot,

        [Parameter(Mandatory=$true)]
        [string]$ProjectName,

        [Parameter(Mandatory=$false)]
        [ValidateSet('Debug', 'Release')]
        [string]$Configuration = 'Debug',

        [Parameter(Mandatory=$false)]
        [string]$TargetFramework = 'net48'
    )

    return Join-Path $ProjectRoot "src\$ProjectName\bin\$Configuration\$TargetFramework"
}

# Export functions
Export-ModuleMember -Function @(
    'Copy-ModFiles',
    'New-FileBackup',
    'Test-FileContainsMarker',
    'Restore-FileFromBackup',
    'Remove-OldDoorstopFiles',
    'Test-ModDeployment',
    'Write-DeploymentSuccess',
    'Write-DeploymentError',
    'Get-BuildOutputPath'
)
