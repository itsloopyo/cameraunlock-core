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

# Game detection data is loaded from ../data/games.json - the single
# source of truth for where each supported game lives on disk. That
# file is also (intended to be) consumed by install.cmd scripts and
# the launcher, so fixing a Steam folder name / env var here fixes it
# everywhere. The loader is lazy + memoised so modules that just call
# e.g. Get-BepInExPluginsPath don't pay the JSON parse cost.

# Two possible layouts (see find-game.ps1 for the same duality):
#   1. Dev tree: cameraunlock-core/powershell/*.psm1 with data next door
#      at cameraunlock-core/data/games.json.
#   2. Release ZIP: <zip>/shared/*.psm1 with games.json co-located.
# Try layout 2 first; it's the one end users hit.
$Script:GamesFilePath = Join-Path $PSScriptRoot 'games.json'
if (-not (Test-Path $Script:GamesFilePath)) {
    $Script:GamesFilePath = Join-Path $PSScriptRoot '..\data\games.json'
}
$Script:GameConfigsCache = $null

# Strict-mode-safe property existence check for PSCustomObjects
# returned by ConvertFrom-Json. Strict-mode throws on `$obj.foo` when
# the property is absent; this guards such accesses.
function Test-JsonProp {
    param([Parameter(Mandatory=$true)]$Object, [Parameter(Mandatory=$true)][string]$Name)
    return [bool]$Object.PSObject.Properties[$Name]
}

<#
.SYNOPSIS
    Load and cache the canonical games.json. Returns a hashtable
    keyed by game-id (hyphen-lowercase), each value normalised to the
    field names the rest of this module expects (EnvVar, SteamFolder,
    Executable, GogGameIds, EpicPaths, XboxPaths, DataFolder, UsesOWML).
.OUTPUTS
    System.Collections.Hashtable
#>
function Get-GameConfigs {
    [CmdletBinding()]
    [OutputType([hashtable])]
    param()

    if ($null -ne $Script:GameConfigsCache) {
        return $Script:GameConfigsCache
    }

    if (-not (Test-Path $Script:GamesFilePath)) {
        throw "canonical games.json not found at $($Script:GamesFilePath) - cameraunlock-core checkout is incomplete"
    }

    $raw = Get-Content -Raw -Path $Script:GamesFilePath | ConvertFrom-Json
    if (-not $raw.games) {
        throw "games.json at $($Script:GamesFilePath) is malformed: missing top-level .games object"
    }

    # `Set-StrictMode -Version Latest` at the top of the module makes
    # missing-property access throw, so every optional field has to
    # check PSObject.Properties first before reading. The nested-if
    # pattern (rather than `$has -and $obj.foo`) keeps strict-mode
    # happy because PowerShell short-circuits `-and` at the parameter
    # binding level, not the property access level.
    $out = @{}
    foreach ($prop in $raw.games.PSObject.Properties) {
        $id = $prop.Name
        $src = $prop.Value
        $cfg = @{
            Executable = $src.executable_relpath
        }
        if (Test-JsonProp $src 'display_name') { $cfg.DisplayName = $src.display_name }
        if (Test-JsonProp $src 'env_var')      { $cfg.EnvVar      = $src.env_var }
        if (Test-JsonProp $src 'steam_folder') { $cfg.SteamFolder = $src.steam_folder }
        if (Test-JsonProp $src 'data_folder')  { $cfg.DataFolder  = $src.data_folder }
        if (Test-JsonProp $src 'uses_owml') {
            if ($src.uses_owml) { $cfg.UsesOWML = [bool]$src.uses_owml }
        }
        if (Test-JsonProp $src 'gog_ids') {
            if ($src.gog_ids.Count -gt 0) { $cfg.GogGameIds = @($src.gog_ids) }
        }
        if (Test-JsonProp $src 'epic_search_paths') {
            if ($src.epic_search_paths.Count -gt 0) { $cfg.EpicPaths = @($src.epic_search_paths) }
        }
        if (Test-JsonProp $src 'xbox_paths') {
            if ($src.xbox_paths.Count -gt 0) { $cfg.XboxPaths = @($src.xbox_paths) }
        }
        if (Test-JsonProp $src 'steam_app_id') {
            if ($null -ne $src.steam_app_id) { $cfg.SteamAppId = [int]$src.steam_app_id }
        }
        $out[$id] = $cfg
    }

    $Script:GameConfigsCache = $out
    return $out
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
    Find a Steam game by its App ID, using Steam's own appmanifest as
    the source of truth for the install folder name.
.DESCRIPTION
    For each Steam library, looks for `steamapps/appmanifest_<AppId>.acf`
    and reads the `"installdir"` field out of it - that's the exact folder
    name Steam chose for this machine's install of this game. No need to
    know the folder name in advance: Steam records it, we read it.

    This is the preferred Steam detection path because it's fully
    dynamic. If a game's Steam folder is ever renamed (publisher
    change, DLC bundling, whatever), we pick up the new name
    automatically without a games.json edit.

    Falls back to $null if the manifest is missing (game not installed
    in this library) or the `installdir`-joined path doesn't contain
    the expected executable.
.OUTPUTS
    System.String or $null
#>
function Find-SteamGameByAppId {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory=$true)]
        [int]$AppId,

        [Parameter(Mandatory=$true)]
        [string]$Executable
    )

    $libraries = Find-SteamLibraries
    foreach ($library in $libraries) {
        $manifest = Join-Path $library "steamapps\appmanifest_$AppId.acf"
        if (-not (Test-Path $manifest)) {
            continue
        }
        # ACF is Valve's simple VDF: quoted key/value pairs. We only
        # need `installdir`. A one-line regex is safer than pulling in
        # a VDF parser; `installdir` is always a simple "key" "value"
        # on its own line.
        $content = Get-Content -Raw -Path $manifest
        if ($content -match '"installdir"\s+"([^"]+)"') {
            $installDir = $matches[1]
            $gamePath = Join-Path $library "steamapps\common\$installDir"
            if (Test-GameInstallation -Path $gamePath -Executable $Executable) {
                return $gamePath
            }
        }
    }
    return $null
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
        $configs = Get-GameConfigs
        $Config = $configs[$GameId]
        if (-not $Config) {
            throw "Unknown game: $GameId. Available games: $($configs.Keys -join ', ')"
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

    # Priority 2a: Steam via appmanifest (app_id-driven). This is the
    # preferred path because Steam's own manifest records the exact
    # install folder name, so we don't depend on a hand-maintained
    # `steam_folder` string in games.json.
    if ($Config.ContainsKey('SteamAppId') -and $Config.SteamAppId) {
        $appidPath = Find-SteamGameByAppId -AppId $Config.SteamAppId -Executable $executable
        if ($appidPath) {
            return $appidPath
        }
    }

    # Priority 2b: Steam via folder name. Fallback for games we haven't
    # recorded a steam_app_id for (non-Steam or pre-release titles
    # with a Steam entry but no published app_id in our catalog).
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

    # Priority 5: Xbox/Microsoft Store paths
    if ($Config.ContainsKey('XboxPaths') -and $Config.XboxPaths) {
        foreach ($path in $Config.XboxPaths) {
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

    $configs = Get-GameConfigs
    return $configs[$GameId]
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

    return @((Get-GameConfigs).Keys)
}

# Export functions
Export-ModuleMember -Function @(
    'Find-SteamLibraries',
    'Find-SteamGameByAppId',
    'Find-GogGamePath',
    'Find-GamePath',
    'Find-OWMLPath',
    'Test-GameInstallation',
    'Get-ManagedPath',
    'Get-BepInExPluginsPath',
    'Get-MelonLoaderModsPath',
    'Write-GameNotFoundError',
    'Get-GameConfig',
    'Get-GameConfigs',
    'Get-AvailableGames'
)
