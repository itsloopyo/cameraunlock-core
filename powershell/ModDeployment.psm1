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

# Same file ModLoaderSetup.psm1 writes. Duplicated as a literal rather than
# imported: pulling the loader-install module in here would drag its network
# paths into every deployment.
$Script:StateFileName = '.headtracking-state.json'

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
    if (-not (Test-Path -LiteralPath $SourceDir)) {
        $results.Success = $false
        $results.Errors += "Source directory not found: $SourceDir"
        return $results
    }

    # Create target directory if it doesn't exist
    if (-not (Test-Path -LiteralPath $TargetDir)) {
        New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
        Write-Host "Created target directory: $TargetDir" -ForegroundColor Gray
    }

    # Copy main mod DLL
    $modDllPath = Join-Path $SourceDir $ModDllName
    if (Test-Path -LiteralPath $modDllPath) {
        $destPath = Join-Path $TargetDir $ModDllName
        Copy-Item -LiteralPath $modDllPath -Destination $destPath -Force
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
        if (Test-Path -LiteralPath $depPath) {
            Copy-Item -LiteralPath $depPath -Destination $TargetDir -Force
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
        if (Test-Path -LiteralPath $depPath) {
            Copy-Item -LiteralPath $depPath -Destination $TargetDir -Force
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

    if (-not (Test-Path -LiteralPath $FilePath)) {
        Write-Warning "File not found, cannot create backup: $FilePath"
        return $null
    }

    $backupPath = $FilePath + $BackupSuffix

    if ((Test-Path -LiteralPath $backupPath) -and -not $Force) {
        Write-Host "Backup already exists: $backupPath" -ForegroundColor Gray
        return $backupPath
    }

    Copy-Item -LiteralPath $FilePath -Destination $backupPath -Force
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

    if (-not (Test-Path -LiteralPath $backupPath)) {
        Write-Warning "Backup not found: $backupPath"
        return $false
    }

    Copy-Item -LiteralPath $backupPath -Destination $FilePath -Force
    Write-Host "Restored from backup: $backupPath" -ForegroundColor Gray
    return $true
}

<#
.SYNOPSIS
    Reports whether the state file records the installed framework as ours.
.DESCRIPTION
    Ownership gate for destructive cleanup. Absent state file, absent
    framework block, or installed_by_us false all mean "not ours" - a
    hand-installed loader, or one another mod put there.
#>
function Test-FrameworkInstalledByUs {
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath
    )

    $stateFile = Join-Path $GamePath $Script:StateFileName
    if (-not (Test-Path -LiteralPath $stateFile)) {
        return $false
    }

    $state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
    if (-not $state.PSObject.Properties['framework']) {
        return $false
    }
    $framework = $state.framework
    if (-not $framework.PSObject.Properties['installed_by_us']) {
        return $false
    }
    return [bool]$framework.installed_by_us
}

<#
.SYNOPSIS
    Removes old Unity Doorstop files from a game directory.
.DESCRIPTION
    Cleans up doorstop files left over from a previous install OF OURS.
    winhttp.dll is BepInEx 5's own proxy and version.dll is a common
    Ultimate ASI Loader proxy, so deleting them blind takes out whatever
    other mod put them there. Nothing is removed unless
    .headtracking-state.json records the framework as installed by us.
.PARAMETER GamePath
    Path to the game installation directory.
.PARAMETER Force
    Delete without the ownership check. Only for callers that already know
    the doorstop is theirs.
.NOTES
    Deprecated, and unexercised. Nothing in the fleet reaches this: the only
    caller is Invoke-DevDeployCecil's -CleanDoorstop switch, which is off by
    default and which no mod repo passes. Kept because it is exported public
    API - removal is a major bump - but treat its behaviour as untested and do
    not build anything new on it.
.OUTPUTS
    Array of removed file names.
#>
function Remove-OldDoorstopFiles {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath,

        [switch]$Force
    )

    $doorstopFiles = @('winhttp.dll', 'version.dll', 'doorstop_config.ini', '.doorstop_version')
    $removedFiles = @()

    if (-not $Force -and -not (Test-FrameworkInstalledByUs -GamePath $GamePath)) {
        Write-Host "Leaving doorstop files in place: $Script:StateFileName does not record this framework as ours." -ForegroundColor Gray
        return $removedFiles
    }

    foreach ($file in $doorstopFiles) {
        $filePath = Join-Path $GamePath $file
        if (Test-Path -LiteralPath $filePath) {
            Remove-Item -LiteralPath $filePath -Force
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
    if (Test-Path -LiteralPath $modPath) {
        $results.FoundFiles += $ModDllName
    } else {
        $results.Success = $false
        $results.MissingFiles += $ModDllName
    }

    # Check dependencies
    foreach ($dep in $Dependencies) {
        $depPath = Join-Path $TargetDir $dep
        if (Test-Path -LiteralPath $depPath) {
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
    Key for recentering (default: "Home"). Used only when -Controls is not supplied.
.PARAMETER ToggleKey
    Key for toggling (default: "End"). Used only when -Controls is not supplied.
.PARAMETER Controls
    Full list of control lines to print under "Controls:", e.g.
    @("Home      - Recenter", "End       - Toggle tracking"). When supplied, this
    overrides RecenterKey/ToggleKey so mods with more than two hotkeys can show them
    all. Pre-align the descriptions yourself. Omit it for the legacy two-line output.
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
        [string]$ToggleKey = "End",

        [Parameter(Mandatory=$false)]
        [string[]]$Controls
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
    if ($Controls) {
        foreach ($line in $Controls) {
            Write-Host "  $line" -ForegroundColor Gray
        }
    } else {
        Write-Host "  $RecenterKey - Recenter head tracking" -ForegroundColor Gray
        Write-Host "  $ToggleKey  - Toggle head tracking on/off" -ForegroundColor Gray
    }
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
    foreach ($message in $Errors) {
        Write-Host "  - $message" -ForegroundColor Red
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
    'Test-FrameworkInstalledByUs',
    'Remove-OldDoorstopFiles',
    'Test-ModDeployment',
    'Write-DeploymentSuccess',
    'Write-DeploymentError',
    'Get-BuildOutputPath'
)
