#!/usr/bin/env pwsh
#Requires -Version 5.1
Set-StrictMode -Version Latest

<#
.SYNOPSIS
    Shared game path detection utilities for CameraUnlock mods.
.DESCRIPTION
    Provides centralized game installation detection supporting:
    - Environment variables
    - Steam registry + libraryfolders.vdf parsing
    - GOG registry lookup
    - Epic Games paths
    - Executable verification
#>

# Game configurations - add new games here
$Script:GameConfigs = @{
    'DyingLight2' = @{
        EnvVar = 'DYING_LIGHT_2_PATH'
        SteamFolder = 'Dying Light 2'
        Executable = 'ph\work\bin\x64\DyingLightGame_x64_rwdi.exe'
    }
    'GoneHome' = @{
        EnvVar = 'GONEHOME_PATH'
        SteamFolder = 'Gone Home'
        Executable = 'GoneHome.exe'
        DataFolder = 'GoneHome_Data'
    }
    'PainscreekKillings' = @{
        EnvVar = 'PAINSCREEK_PATH'
        SteamFolder = 'The Painscreek Killings'
        Executable = 'Painscreek.exe'
        DataFolder = 'Painscreek_Data'
    }
    'FalloutNewVegas' = @{
        EnvVar = 'FalloutNVPath'
        SteamFolder = 'Fallout New Vegas'
        Executable = 'FalloutNV.exe'
        GogGameIds = @('1454587428')
        EpicPaths = @(
            'C:\Program Files\Epic Games\FalloutNewVegas\Fallout New Vegas English',
            'C:\Program Files\Epic Games\FalloutNewVegas',
            'D:\Epic Games\FalloutNewVegas\Fallout New Vegas English'
        )
    }
    'Subnautica' = @{
        EnvVar = 'SubnauticaDir'
        SteamFolder = 'Subnautica'
        Executable = 'Subnautica.exe'
        DataFolder = 'Subnautica_Data'
        EpicPaths = @(
            'C:\Program Files\Epic Games\Subnautica'
        )
    }
    'GreenHell' = @{
        EnvVar = 'GREEN_HELL_PATH'
        SteamFolder = 'Green Hell'
        Executable = 'GH.exe'
        DataFolder = 'GH_Data'
    }
    'ObraDinn' = @{
        EnvVar = 'OBRA_DINN_PATH'
        SteamFolder = 'ObraDinn'
        Executable = 'ObraDinn.exe'
        DataFolder = 'ObraDinn_Data'
    }
    'Peak' = @{
        EnvVar = 'PEAK_GAME_PATH'
        SteamFolder = 'Peak'
        Executable = 'Peak.exe'
    }
    'ShadowsOfDoubt' = @{
        EnvVar = 'SHADOWS_OF_DOUBT_PATH'
        SteamFolder = 'Shadows of Doubt'
        Executable = 'Shadows of Doubt.exe'
    }
    'Valheim' = @{
        EnvVar = 'VALHEIM_PATH'
        SteamFolder = 'Valheim'
        Executable = 'valheim.exe'
    }
    'OuterWilds' = @{
        # Outer Wilds uses OWML mod manager, not direct game detection
        EnvVar = 'OUTER_WILDS_PATH'
        SteamFolder = 'Outer Wilds'
        Executable = 'OuterWilds.exe'
        UsesOWML = $true
    }
    'Tacoma' = @{
        EnvVar = 'TACOMA_PATH'
        SteamFolder = 'Tacoma'
        Executable = 'Tacoma.exe'
        DataFolder = 'Tacoma_Data'
    }
    'WobblyLife' = @{
        EnvVar = 'WOBBLY_LIFE_PATH'
        SteamFolder = 'Wobbly Life'
        Executable = 'Wobbly Life.exe'
    }
    'Firewatch' = @{
        EnvVar = 'FIREWATCH_PATH'
        SteamFolder = 'Firewatch'
        Executable = 'Firewatch.exe'
    }
    'GreenLight' = @{
        EnvVar = 'GREENLIGHT_PATH'
        SteamFolder = 'The Green Light'
        Executable = 'TheGreenLight.exe'
        DataFolder = 'TheGreenLight_Data'
    }
}

<#
.SYNOPSIS
    Finds all Steam library folders from registry and libraryfolders.vdf.
.OUTPUTS
    System.String[] - Array of Steam library paths
#>
function Find-SteamLibraries {
    [CmdletBinding()]
    [OutputType([string[]])]
    param()

    $libraries = [System.Collections.Generic.List[string]]::new()

    # Find Steam installation from registry
    $steamPath = $null

    # Try registry (64-bit)
    $regPath64 = 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam'
    if (Test-Path $regPath64) {
        $steamPath = (Get-ItemProperty -Path $regPath64 -ErrorAction Stop).InstallPath
    }

    # Try registry (32-bit)
    if (-not $steamPath) {
        $regPath32 = 'HKLM:\SOFTWARE\Valve\Steam'
        if (Test-Path $regPath32) {
            $steamPath = (Get-ItemProperty -Path $regPath32 -ErrorAction Stop).InstallPath
        }
    }

    if (-not $steamPath -or -not (Test-Path $steamPath)) {
        return @()
    }

    $libraries.Add($steamPath)

    # Parse libraryfolders.vdf to find all Steam library paths
    $vdfPath = Join-Path $steamPath 'steamapps\libraryfolders.vdf'

    if (Test-Path $vdfPath) {
        $content = Get-Content $vdfPath -Raw
        # Match path entries in VDF format
        $pathMatches = [regex]::Matches($content, '"path"\s+"([^"]+)"')
        foreach ($match in $pathMatches) {
            $path = $match.Groups[1].Value -replace '\\\\', '\'
            if ($path -and (Test-Path $path) -and -not $libraries.Contains($path)) {
                $libraries.Add($path)
            }
        }
    }

    return $libraries.ToArray()
}

<#
.SYNOPSIS
    Finds a game in GOG registry.
.PARAMETER GogGameIds
    Array of GOG game IDs to check.
.PARAMETER Executable
    Executable name to verify the installation.
.OUTPUTS
    System.String or $null
#>
function Find-GogGamePath {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory=$true)]
        [string[]]$GogGameIds,

        [Parameter(Mandatory=$true)]
        [string]$Executable
    )

    foreach ($gogId in $GogGameIds) {
        $gogKeys = @(
            "HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games\$gogId",
            "HKLM:\SOFTWARE\GOG.com\Games\$gogId"
        )

        foreach ($key in $gogKeys) {
            if (Test-Path $key) {
                $gamePath = (Get-ItemProperty -Path $key -ErrorAction Stop).path
                if ($gamePath -and (Test-Path (Join-Path $gamePath $Executable))) {
                    return $gamePath
                }
            }
        }
    }

    return $null
}

<#
.SYNOPSIS
    Tests if a path contains a valid game installation.
.PARAMETER Path
    The path to test.
.PARAMETER Executable
    The executable name to check for.
.OUTPUTS
    System.Boolean
#>
function Test-GameInstallation {
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory=$true)]
        [string]$Path,

        [Parameter(Mandatory=$true)]
        [string]$Executable
    )

    if (-not (Test-Path $Path)) {
        return $false
    }

    $exePath = Join-Path $Path $Executable
    return (Test-Path $exePath)
}

<#
.SYNOPSIS
    Finds the OWML mods path for Outer Wilds.
.OUTPUTS
    System.String or $null
#>
function Find-OWMLPath {
    [CmdletBinding()]
    [OutputType([string])]
    param()

    $owmlPath = Join-Path $env:APPDATA 'OuterWildsModManager\OWML'
    if (Test-Path $owmlPath) {
        return $owmlPath
    }
    return $null
}

<#
.SYNOPSIS
    Finds a game installation path.
.DESCRIPTION
    Searches for a game using multiple detection methods in priority order:
    1. Environment variable
    2. Steam libraries (via registry + libraryfolders.vdf)
    3. GOG registry
    4. Epic Games paths
.PARAMETER GameId
    The game identifier (key in $GameConfigs).
.PARAMETER Config
    Optional custom configuration hashtable (overrides GameId lookup).
.OUTPUTS
    System.String or $null
#>
function Find-GamePath {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory=$false)]
        [string]$GameId,

        [Parameter(Mandatory=$false)]
        [hashtable]$Config
    )

    if (-not $Config) {
        if (-not $GameId) {
            throw "Either GameId or Config must be provided"
        }
        $Config = $Script:GameConfigs[$GameId]
        if (-not $Config) {
            throw "Unknown game: $GameId. Available games: $($Script:GameConfigs.Keys -join ', ')"
        }
    }

    $executable = $Config.Executable
    if (-not $executable) {
        throw "Config must include 'Executable'"
    }

    # Priority 1: Environment variable
    if ($Config.EnvVar) {
        $envPath = [Environment]::GetEnvironmentVariable($Config.EnvVar)
        if ($envPath -and (Test-GameInstallation -Path $envPath -Executable $executable)) {
            return $envPath
        }
    }

    # Priority 2: Steam libraries
    if ($Config.SteamFolder) {
        $libraries = Find-SteamLibraries
        foreach ($library in $libraries) {
            $gamePath = Join-Path $library "steamapps\common\$($Config.SteamFolder)"
            if (Test-GameInstallation -Path $gamePath -Executable $executable) {
                return $gamePath
            }
        }
    }

    # Priority 3: GOG registry
    if ($Config.ContainsKey('GogGameIds') -and $Config.GogGameIds) {
        $gogPath = Find-GogGamePath -GogGameIds $Config.GogGameIds -Executable $executable
        if ($gogPath) {
            return $gogPath
        }
    }

    # Priority 4: Epic Games paths
    if ($Config.ContainsKey('EpicPaths') -and $Config.EpicPaths) {
        foreach ($path in $Config.EpicPaths) {
            if (Test-GameInstallation -Path $path -Executable $executable) {
                return $path
            }
        }
    }

    return $null
}

<#
.SYNOPSIS
    Gets the path to a game's Managed folder containing DLLs.
.PARAMETER GamePath
    The root game installation path.
.PARAMETER DataFolder
    The name of the game's data folder (e.g., 'GoneHome_Data').
.OUTPUTS
    System.String
#>
function Get-ManagedPath {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath,

        [Parameter(Mandatory=$true)]
        [string]$DataFolder
    )

    return Join-Path $GamePath "$DataFolder\Managed"
}

<#
.SYNOPSIS
    Gets the path to the BepInEx plugins folder.
.PARAMETER GamePath
    The root game installation path.
.OUTPUTS
    System.String
#>
function Get-BepInExPluginsPath {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath
    )

    return Join-Path $GamePath 'BepInEx\plugins'
}

<#
.SYNOPSIS
    Gets the path to the MelonLoader mods folder.
.PARAMETER GamePath
    The root game installation path.
.OUTPUTS
    System.String
#>
function Get-MelonLoaderModsPath {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath
    )

    return Join-Path $GamePath 'Mods'
}

<#
.SYNOPSIS
    Displays an error message indicating the game was not found.
.PARAMETER GameName
    Display name of the game.
.PARAMETER EnvVar
    Environment variable name for the game.
.PARAMETER SteamFolder
    Steam folder name where the game would be found.
#>
function Write-GameNotFoundError {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]
        [string]$GameName,

        [Parameter(Mandatory=$false)]
        [string]$EnvVar,

        [Parameter(Mandatory=$false)]
        [string]$SteamFolder
    )

    Write-Host "ERROR: $GameName installation not found!" -ForegroundColor Red
    Write-Host ""

    $libraries = @(Find-SteamLibraries)
    if ($libraries.Count -gt 0) {
        Write-Host "Searched Steam libraries:" -ForegroundColor Yellow
        foreach ($lib in $libraries) {
            if ($SteamFolder) {
                Write-Host "  - $lib\steamapps\common\$SteamFolder" -ForegroundColor Gray
            } else {
                Write-Host "  - $lib" -ForegroundColor Gray
            }
        }
        Write-Host ""
    }

    if ($EnvVar) {
        Write-Host "Set the $EnvVar environment variable to the game's installation folder." -ForegroundColor Yellow
        Write-Host "  Example: `$env:$EnvVar = 'C:\Games\$GameName'" -ForegroundColor Cyan
    }
}

<#
.SYNOPSIS
    Gets the game configuration for a known game.
.PARAMETER GameId
    The game identifier.
.OUTPUTS
    Hashtable or $null
#>
function Get-GameConfig {
    [CmdletBinding()]
    [OutputType([hashtable])]
    param(
        [Parameter(Mandatory=$true)]
        [string]$GameId
    )

    return $Script:GameConfigs[$GameId]
}

<#
.SYNOPSIS
    Gets all available game IDs.
.OUTPUTS
    System.String[]
#>
function Get-AvailableGames {
    [CmdletBinding()]
    [OutputType([string[]])]
    param()

    return @($Script:GameConfigs.Keys)
}

# Export functions
Export-ModuleMember -Function @(
    'Find-SteamLibraries',
    'Find-GogGamePath',
    'Find-GamePath',
    'Find-OWMLPath',
    'Test-GameInstallation',
    'Get-ManagedPath',
    'Get-BepInExPluginsPath',
    'Get-MelonLoaderModsPath',
    'Write-GameNotFoundError',
    'Get-GameConfig',
    'Get-AvailableGames'
)
