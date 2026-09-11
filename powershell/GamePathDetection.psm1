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
    Executable, GogGameIds, EpicPaths, EaPaths, XboxPaths, DataFolder,
    UsesOWML).
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

    # -Encoding UTF8: games.json is UTF-8 and contains non-ASCII characters
    # (e.g. the trademark sign in the PixelJunk Monsters 2 steam_folder). Windows
    # PowerShell 5.1 defaults Get-Content to the ANSI codepage, which mojibakes
    # those bytes and breaks steam_folder matching for unicode-named games.
    $raw = Get-Content -Raw -Encoding UTF8 -LiteralPath $Script:GamesFilePath | ConvertFrom-Json
    # Strict mode turns a missing property into a terminating error, so the
    # malformed-file message below is only reachable behind an existence check.
    if ($null -eq $raw -or -not (Test-JsonProp $raw 'games') -or -not $raw.games) {
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
        if (Test-JsonProp $src 'ubisoft_app_ids') {
            if ($src.ubisoft_app_ids.Count -gt 0) { $cfg.UbisoftAppIds = @($src.ubisoft_app_ids | ForEach-Object { [string]$_ }) }
        }
        if (Test-JsonProp $src 'epic_search_paths') {
            if ($src.epic_search_paths.Count -gt 0) { $cfg.EpicPaths = @($src.epic_search_paths) }
        }
        if (Test-JsonProp $src 'ea_search_paths') {
            if ($src.ea_search_paths.Count -gt 0) { $cfg.EaPaths = @($src.ea_search_paths) }
        }
        if (Test-JsonProp $src 'xbox_paths') {
            if ($src.xbox_paths.Count -gt 0) { $cfg.XboxPaths = @($src.xbox_paths) }
        }
        if (Test-JsonProp $src 'msix_identity_name') {
            $cfg.MsixIdentityName = $src.msix_identity_name
        }
        # Optional override used by titles whose Xbox/GDK build ships under a
        # different executable name + relpath than their Steam build (e.g.
        # UE5 GDK games: Foo-WinGDK-Shipping.exe under Binaries\WinGDK\
        # vs the Steam Foo-Win64-Shipping.exe under Binaries\Win64\). If
        # absent, callers fall back to the platform-neutral Executable field.
        if (Test-JsonProp $src 'xbox_executable_relpath') {
            $cfg.XboxExecutable = $src.xbox_executable_relpath
        }
        # The GDK package identity, which is what tells two titles apart when
        # they ship the same executable name - see Find-XboxGamePaths.
        if (Test-JsonProp $src 'xbox_identity_name') {
            $cfg.XboxIdentityName = $src.xbox_identity_name
        }
        if (Test-JsonProp $src 'steam_app_id') {
            if ($null -ne $src.steam_app_id) { $cfg.SteamAppId = [int]$src.steam_app_id }
        }
        if (Test-JsonProp $src 'registry_paths') {
            if ($src.registry_paths.Count -gt 0) { $cfg.RegistryPaths = @($src.registry_paths) }
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

    # A key can exist with the value missing (a partial uninstall, a
    # policy-written stub). Strict mode makes the bare property read a
    # terminating error, so every registry read is guarded.
    foreach ($regPath in @('HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam')) {
        if (-not (Test-Path $regPath)) { continue }
        $props = Get-ItemProperty -Path $regPath -ErrorAction Stop
        if (-not $props.PSObject.Properties['InstallPath']) { continue }
        $steamPath = $props.InstallPath
        if ($steamPath) { break }
    }

    if (-not $steamPath -or -not (Test-Path -LiteralPath $steamPath)) {
        return @()
    }

    $libraries.Add($steamPath)

    # Parse libraryfolders.vdf to find all Steam library paths
    $vdfPath = Join-Path $steamPath 'steamapps\libraryfolders.vdf'

    if (Test-Path -LiteralPath $vdfPath) {
        $content = Get-Content -LiteralPath $vdfPath -Raw
        # Match path entries in VDF format
        $pathMatches = [regex]::Matches($content, '"path"\s+"([^"]+)"')
        foreach ($match in $pathMatches) {
            $path = $match.Groups[1].Value -replace '\\\\', '\'
            if ($path -and (Test-Path -LiteralPath $path) -and -not $libraries.Contains($path)) {
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
            if (-not (Test-Path $key)) { continue }
            $props = Get-ItemProperty -Path $key -ErrorAction Stop
            if (-not $props.PSObject.Properties['path']) { continue }
            $gamePath = $props.path
            if ($gamePath -and (Test-Path -LiteralPath (Join-Path $gamePath $Executable))) {
                return $gamePath
            }
        }
    }

    return $null
}

<#
.SYNOPSIS
    Finds a game in the Ubisoft Connect launcher registry.
.PARAMETER UbisoftAppIds
    Array of Ubisoft launcher install IDs to check (numeric, as strings or ints).
.PARAMETER Executable
    Executable name to verify the installation.
.OUTPUTS
    System.String or $null
#>
function Find-UbisoftGamePath {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory=$true)]
        [string[]]$UbisoftAppIds,

        [Parameter(Mandatory=$true)]
        [string]$Executable
    )

    foreach ($appId in $UbisoftAppIds) {
        $ubiKeys = @(
            "HKLM:\SOFTWARE\WOW6432Node\Ubisoft\Launcher\Installs\$appId",
            "HKLM:\SOFTWARE\Ubisoft\Launcher\Installs\$appId"
        )

        foreach ($key in $ubiKeys) {
            if (-not (Test-Path $key)) { continue }
            $props = Get-ItemProperty -Path $key -ErrorAction Stop
            if (-not $props.PSObject.Properties['InstallDir']) { continue }
            $installDir = $props.InstallDir
            if ($installDir) {
                $gamePath = $installDir.TrimEnd('/', '\')
                if (Test-GameInstallation -Path $gamePath -Executable $Executable) {
                    return $gamePath
                }
            }
        }
    }

    return $null
}

<#
.SYNOPSIS
    Finds a game via a direct registry value written by its installer.
.DESCRIPTION
    For retail / InstallShield-era titles that aren't on any launcher,
    the installer often records the install directory under a vendor
    key (e.g. Black & White's HKCU\Software\Lionhead Studios Ltd\Black
    & White\GameDir). Each entry names a hive ("HKCU"/"HKLM"), a key
    path under that hive, and the value holding the install directory.
.PARAMETER RegistryPaths
    Array of entries, each with .hive, .key, .value (as loaded from
    games.json registry_paths).
.PARAMETER Executable
    Executable name to verify the installation.
.OUTPUTS
    System.String or $null
#>
function Find-RegistryGamePath {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory=$true)]
        [array]$RegistryPaths,

        [Parameter(Mandatory=$true)]
        [string]$Executable
    )

    foreach ($entry in $RegistryPaths) {
        $regPath = "$($entry.hive):\$($entry.key)"
        if (-not (Test-Path $regPath)) {
            continue
        }
        $props = Get-ItemProperty -Path $regPath -ErrorAction Stop
        if (-not $props.PSObject.Properties[$entry.value]) {
            continue
        }
        $gamePath = $props.$($entry.value)
        if ($gamePath) {
            $gamePath = $gamePath.TrimEnd('/', '\')
            if (Test-GameInstallation -Path $gamePath -Executable $Executable) {
                return $gamePath
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

    # -LiteralPath throughout: a path like 'D:\Games\Prey [2017]' is a wildcard
    # character class to Test-Path, so the verification gate every detection
    # strategy runs through would report a game that plainly exists as missing.
    if (-not (Test-Path -LiteralPath $Path)) {
        return $false
    }

    $exePath = Join-Path $Path $Executable
    return (Test-Path -LiteralPath $exePath)
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
        if (-not (Test-Path -LiteralPath $manifest)) {
            continue
        }
        # ACF is Valve's simple VDF: quoted key/value pairs. We only
        # need `installdir`. A one-line regex is safer than pulling in
        # a VDF parser; `installdir` is always a simple "key" "value"
        # on its own line.
        $content = Get-Content -Raw -LiteralPath $manifest
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
    Every installed Game Pass title's Content directory on this machine.
.DESCRIPTION
    The Xbox app unpacks a title to `<root>\<Title>\Content`, so one level of
    listing per root surfaces every install without any game having to declare
    where it went.

    A directory only counts when it holds `gamelaunchhelper.exe`, the stub
    Windows registers and actually runs for a GDK title. The Xbox app stages
    downloads into sibling folders - named for a GUID rather than the game -
    that carry the game's exe, its appxmanifest and its MicrosoftGame.config,
    so every other signal says "installed game" for something the user cannot
    launch. Left in, one is indistinguishable from the real install, and
    deploying a mod into it puts the files somewhere the Xbox app is free to
    wipe. The launcher's Rust detector gates on the same file for the same
    reason (lopari/src-tauri/src/detect/xbox.rs).
.OUTPUTS
    System.String[]
#>
function Get-XboxContentDirs {
    [CmdletBinding()]
    [OutputType([string[]])]
    param()

    $dirs = [System.Collections.Generic.List[string]]::new()
    foreach ($root in Get-XboxGameRoots) {
        foreach ($child in (Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue)) {
            $content = Join-Path $child.FullName 'Content'
            if (-not (Test-Path -LiteralPath (Join-Path $content 'gamelaunchhelper.exe') -PathType Leaf)) { continue }
            if (-not $dirs.Contains($content)) { $dirs.Add($content) }
        }
    }
    return $dirs.ToArray()
}

<#
.SYNOPSIS
    Find a Game Pass install of a title by looking for its executable.
.DESCRIPTION
    No path, drive letter or folder name is configured anywhere for this: the
    install roots come off the disk and the title is identified by the
    executable it ships, which games.json already records for every game.

    That is the whole reason this exists. An `xbox_paths` entry can only name
    the drive whoever wrote it happened to install on, and the Xbox app asks
    every user which drive to use - so a hardcoded `C:\XboxGames\...` is a
    guess that is wrong for everyone who answered differently, and wrong
    silently, because a Game Pass copy legitimately produces nothing from
    every other detection source.
.PARAMETER Executable
    Executable relpath to look for, e.g. 'Fallout4.exe'. Pass the game's
    xbox_executable_relpath where it has one - a GDK build can ship under a
    different name and folder than the Steam build.
.OUTPUTS
    System.String[] - every matching Content directory, in root order.
#>
function Get-XboxPackageIdentity {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory = $true)]
        [string]$ContentDir
    )

    # Every GDK title ships this file beside its executable, and the Identity
    # element's Name is the package identity Windows registers it under - the
    # one value that is unique per title and survives every game update.
    $config = Get-ChildItem -LiteralPath $ContentDir -Filter 'MicrosoftGame.config' -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $config) { return '' }
    try {
        $xml = [xml](Get-Content -Raw -LiteralPath $config.FullName)
    } catch {
        # A config the publisher shipped malformed is not a title we can
        # identify; the executable check still decides on its own.
        return ''
    }
    $identity = $xml.Game.Identity
    if (-not $identity -or -not $identity.Name) { return '' }
    return [string]$identity.Name
}

<#
.SYNOPSIS
    Find a Game Pass install of a title among the Content directories on disk.
.PARAMETER Executable
    Executable relpath to look for, e.g. 'Fallout4.exe'.
.PARAMETER IdentityName
    The title's GDK package identity, from games.json's xbox_identity_name.
    Optional, and only worth setting where the executable name is ambiguous.
.OUTPUTS
    System.String[] - every matching Content directory, in root order.
#>
function Find-XboxGamePaths {
    [CmdletBinding()]
    [OutputType([string[]])]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,

        [string]$IdentityName = ''
    )

    $found = [System.Collections.Generic.List[string]]::new()
    foreach ($content in Get-XboxContentDirs) {
        if (-not (Test-GameInstallation -Path $content -Executable $Executable)) { continue }
        # An executable name is not always unique across a publisher's
        # catalogue: Kingdom Come: Deliverance and its sequel both ship
        # `KingdomCome.exe` flat in Content, so the scan matches both for
        # either game and a mod pinned to one build deploys into the other.
        # Where the game declares its package identity, that decides.
        if ($IdentityName) {
            if (-not [string]::Equals((Get-XboxPackageIdentity -ContentDir $content),
                                      $IdentityName,
                                      [System.StringComparison]::OrdinalIgnoreCase)) {
                continue
            }
        }
        $found.Add($content)
    }
    return $found.ToArray()
}

<#
.SYNOPSIS
    Finds a Microsoft Store (MSIX/UWP) title by its package identity name.
.DESCRIPTION
    A Store-packaged game has no stable install path: the directory under
    WindowsApps carries the package version, so it changes with every game
    update. The package identity name (e.g. Microsoft.MinecraftUWP) is the
    stable handle, and the package manager resolves it to wherever the
    current version lives.
.PARAMETER IdentityName
    Package identity name, as recorded in games.json's msix_identity_name.
.PARAMETER Executable
    Executable name to verify the installation.
.OUTPUTS
    System.String or $null
#>
function Find-MsixGamePath {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory=$true)]
        [string]$IdentityName,

        [Parameter(Mandatory=$true)]
        [string]$Executable
    )

    # A package that is registered for another user, or staged but not
    # registered for this one, is not an install this machine can launch.
    $package = Get-AppxPackage -Name $IdentityName | Select-Object -First 1
    if (-not $package -or -not $package.InstallLocation) {
        return $null
    }
    if (Test-GameInstallation -Path $package.InstallLocation -Executable $Executable) {
        return $package.InstallLocation
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
    Every root the Xbox app installs games into on this machine.
.DESCRIPTION
    The Xbox app puts a `.GamingRoot` file at the root of each drive it has
    been asked to install into. It is eight bytes of header ("RGBX" plus a
    version dword) followed by the folder name as UTF-16LE - "XboxGames" in
    every case seen so far, but read rather than assumed, since the file
    exists precisely to record it.

    A game with two drives configured has one of these per drive, and only
    the drive the user picked holds any given title, so all of them are
    candidate roots.
.OUTPUTS
    System.String[] - e.g. @('C:\XboxGames', 'D:\XboxGames')
#>
function Get-XboxGameRoots {
    [CmdletBinding()]
    [OutputType([string[]])]
    param()

    $roots = [System.Collections.Generic.List[string]]::new()
    foreach ($drive in [System.IO.DriveInfo]::GetDrives()) {
        if (-not $drive.IsReady) { continue }
        $marker = Join-Path $drive.Name '.GamingRoot'
        if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) { continue }
        try {
            $bytes = [System.IO.File]::ReadAllBytes($marker)
        } catch {
            # A drive that disappears between the test and the read is not an
            # install location; nothing else can go wrong reading 32 bytes.
            continue
        }
        if ($bytes.Length -le 8) { continue }
        $folder = [System.Text.Encoding]::Unicode.GetString($bytes, 8, $bytes.Length - 8).TrimEnd([char]0)
        if (-not $folder) { continue }
        $root = Join-Path $drive.Name $folder
        if ((Test-Path -LiteralPath $root -PathType Container) -and -not $roots.Contains($root)) {
            $roots.Add($root)
        }
    }

    # The marker is the Xbox app's own record of the folder it chose, which is
    # why it is read first - it is the only thing that knows a non-default
    # name. It is not a guarantee, though, and the launcher's Rust detector
    # (lopari/src-tauri/src/detect/xbox.rs) scans for the folder itself and has
    # been right in the field, so take the union rather than letting the two
    # implementations disagree about what is installed.
    foreach ($drive in [System.IO.DriveInfo]::GetDrives()) {
        if (-not $drive.IsReady) { continue }
        $root = Join-Path $drive.Name 'XboxGames'
        if ((Test-Path -LiteralPath $root -PathType Container) -and -not $roots.Contains($root)) {
            $roots.Add($root)
        }
    }
    return $roots.ToArray()
}

<#
.SYNOPSIS
    Expand a game's configured xbox_paths across every Xbox install root on
    this machine.
.DESCRIPTION
    games.json records an Xbox path as a full path, which pins a drive letter
    the entry's author happened to have. A user who let the Xbox app install to
    their second drive has the game at the same place under a different root,
    and the configured path simply does not exist for them.

    So each configured path contributes two things: itself, and its tail below
    the "XboxGames" element re-anchored onto every root Get-XboxGameRoots
    finds. `C:\XboxGames\High on Life\Content` therefore also matches
    `D:\XboxGames\High on Life\Content` without games.json listing a drive it
    cannot know about.

    A configured path whose shape is not <root>\<title>\... is passed through
    unchanged rather than guessed at.
.OUTPUTS
    System.String[]
#>
function Expand-XboxPathCandidates {
    [CmdletBinding()]
    [OutputType([string[]])]
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyCollection()]
        [string[]]$XboxPaths
    )

    $out = [System.Collections.Generic.List[string]]::new()
    $roots = Get-XboxGameRoots

    foreach ($configured in $XboxPaths) {
        if (-not $configured) { continue }
        if (-not $out.Contains($configured)) { $out.Add($configured) }

        # Drop the drive and the gaming-root folder, keeping the part that is
        # the same on every machine: 'C:\XboxGames\High on Life\Content'
        # contributes 'High on Life\Content'.
        $segments = @(($configured -split '[\\/]+') | Where-Object { $_ -and $_ -notmatch '^[A-Za-z]:$' })
        if ($segments.Count -lt 2) { continue }
        $tail = $segments[1..($segments.Count - 1)] -join '\'

        foreach ($root in $roots) {
            $candidate = Join-Path $root $tail
            if (-not $out.Contains($candidate)) { $out.Add($candidate) }
        }
    }

    return $out.ToArray()
}

<#
.SYNOPSIS
    Finds a game installation path.
.DESCRIPTION
    Searches for a game using multiple detection methods in priority order:
    1. Environment variable
    2. Steam libraries (via registry + libraryfolders.vdf)
    3. GOG registry
    4. Ubisoft Connect registry
    5. Epic Games paths
    6. EA App / Origin paths
    7. Xbox/Microsoft Store paths
    8. Microsoft Store MSIX package identity
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

    # Strict mode makes a missing hashtable key a terminating error, and both
    # env_var-only games (no steam_folder) and caller-supplied configs hit that,
    # so every optional key is read behind ContainsKey.
    if (-not $Config.ContainsKey('Executable') -or -not $Config.Executable) {
        throw "Config must include 'Executable'"
    }
    $executable = $Config.Executable

    # Priority 1: Environment variable
    if ($Config.ContainsKey('EnvVar') -and $Config.EnvVar) {
        $envPath = [Environment]::GetEnvironmentVariable($Config.EnvVar)
        if ($envPath -and (Test-GameInstallation -Path $envPath -Executable $executable)) {
            return $envPath
        }
    }

    # Priority 2: Registry value written by the game's own installer.
    # Authoritative for retail / InstallShield titles that predate any
    # launcher (e.g. Black & White), so it sits ahead of store lookups.
    if ($Config.ContainsKey('RegistryPaths') -and $Config.RegistryPaths) {
        $regPath = Find-RegistryGamePath -RegistryPaths $Config.RegistryPaths -Executable $executable
        if ($regPath) {
            return $regPath
        }
    }

    # Priority 3a: Steam via appmanifest (app_id-driven). This is the
    # preferred path because Steam's own manifest records the exact
    # install folder name, so we don't depend on a hand-maintained
    # `steam_folder` string in games.json.
    if ($Config.ContainsKey('SteamAppId') -and $Config.SteamAppId) {
        $appidPath = Find-SteamGameByAppId -AppId $Config.SteamAppId -Executable $executable
        if ($appidPath) {
            return $appidPath
        }
    }

    # Priority 3b: Steam via folder name. Fallback for games we haven't
    # recorded a steam_app_id for (non-Steam or pre-release titles
    # with a Steam entry but no published app_id in our catalog).
    if ($Config.ContainsKey('SteamFolder') -and $Config.SteamFolder) {
        $libraries = Find-SteamLibraries
        foreach ($library in $libraries) {
            $gamePath = Join-Path $library "steamapps\common\$($Config.SteamFolder)"
            if (Test-GameInstallation -Path $gamePath -Executable $executable) {
                return $gamePath
            }
        }
    }

    # Priority 4: GOG registry
    if ($Config.ContainsKey('GogGameIds') -and $Config.GogGameIds) {
        $gogPath = Find-GogGamePath -GogGameIds $Config.GogGameIds -Executable $executable
        if ($gogPath) {
            return $gogPath
        }
    }

    # Priority 5: Ubisoft Connect registry
    if ($Config.ContainsKey('UbisoftAppIds') -and $Config.UbisoftAppIds) {
        $ubiPath = Find-UbisoftGamePath -UbisoftAppIds $Config.UbisoftAppIds -Executable $executable
        if ($ubiPath) {
            return $ubiPath
        }
    }

    # Priority 6: Epic Games paths
    if ($Config.ContainsKey('EpicPaths') -and $Config.EpicPaths) {
        foreach ($path in $Config.EpicPaths) {
            if (Test-GameInstallation -Path $path -Executable $executable) {
                return $path
            }
        }
    }

    # Priority 7: EA App / Origin paths
    if ($Config.ContainsKey('EaPaths') -and $Config.EaPaths) {
        foreach ($path in $Config.EaPaths) {
            if (Test-GameInstallation -Path $path -Executable $executable) {
                return $path
            }
        }
    }

    # Priority 8: Xbox / Game Pass.
    #
    # GDK builds may live under a different exe name than the Steam exe
    # (Foo-WinGDK-Shipping.exe vs Foo-Win64-Shipping.exe), so prefer
    # XboxExecutable for the existence check when it's set.
    $xboxExecutable = if ($Config.ContainsKey('XboxExecutable') -and $Config.XboxExecutable) {
        $Config.XboxExecutable
    } else {
        $executable
    }
    $xboxIdentity = if ($Config.ContainsKey('XboxIdentityName') -and $Config.XboxIdentityName) {
        $Config.XboxIdentityName
    } else {
        ''
    }
    # An explicit xbox_paths entry is an override for a layout the scan below
    # cannot decompose. It is not how a Game Pass copy is normally found, and
    # no game needs one to be detected.
    if ($Config.ContainsKey('XboxPaths') -and $Config.XboxPaths) {
        foreach ($path in (Expand-XboxPathCandidates -XboxPaths $Config.XboxPaths)) {
            if (Test-GameInstallation -Path $path -Executable $xboxExecutable) {
                return $path
            }
        }
    }
    foreach ($path in (Find-XboxGamePaths -Executable $xboxExecutable -IdentityName $xboxIdentity)) {
        return $path
    }

    # Priority 9: Microsoft Store MSIX/UWP package identity. Distinct from
    # the Xbox app, which unpacks Game Pass titles into a writable folder
    # at a path we can list; a Store package lives under WindowsApps in a
    # directory named for the current version, so only the package manager
    # knows where it is right now. It is still the GDK build, so it is checked
    # with the Xbox executable like everything else in priority 8 - Pacific
    # Drive is the case that showed it, shipping
    # PenDriverPro\Binaries\WinGDK\PenDriverPro-WinGDK-Shipping.exe with no
    # Win64 directory at all, so the Steam relpath found nothing and the copy
    # read as not installed.
    if ($Config.ContainsKey('MsixIdentityName') -and $Config.MsixIdentityName) {
        $msixPath = Find-MsixGamePath -IdentityName $Config.MsixIdentityName -Executable $xboxExecutable
        if ($msixPath) {
            return $msixPath
        }
    }

    return $null
}

<#
.SYNOPSIS
    Find EVERY installation of a game on this machine, not just the first.
.DESCRIPTION
    Find-GamePath returns the highest-priority hit and stops, which is what a
    player-facing install wants: one copy, one deployment. Dev tooling wants the
    opposite. Owning the game on two stores is normal - a Steam copy and a GOG
    copy of Fallout 4, say - and with `pixi run install` deploying to whichever
    store happens to sort first, the other copy silently keeps whatever build was
    last dropped into it. The failure is quiet in the worst way: you test a fix,
    it does not appear, and the reason is that you launched the copy that was
    never updated.

    Same sources and same order as Find-GamePath, but every source is collected
    rather than returned from, each candidate is validated with
    Test-GameInstallation, and the result is de-duplicated on the full path (the
    Steam app-manifest and steam_folder lookups routinely find the same install
    twice).
.PARAMETER GameId
    The game identifier (key in games.json).
.PARAMETER Config
    Optional custom configuration hashtable (overrides GameId lookup).
.OUTPUTS
    System.String[] - zero or more install paths, highest priority first.
#>
function Find-AllGamePaths {
    [CmdletBinding()]
    [OutputType([string[]])]
    param(
        [Parameter(Mandatory = $false)]
        [string]$GameId,

        [Parameter(Mandatory = $false)]
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

    if (-not $Config.ContainsKey('Executable') -or -not $Config.Executable) {
        throw "Config must include 'Executable'"
    }
    $executable = $Config.Executable

    # Every key is read behind ContainsKey: strict mode makes a missing hashtable
    # key a terminating error, and most games define only a few of these.
    $candidates = [System.Collections.Generic.List[object]]::new()

    if ($Config.ContainsKey('EnvVar') -and $Config.EnvVar) {
        $candidates.Add([Environment]::GetEnvironmentVariable($Config.EnvVar))
    }
    if ($Config.ContainsKey('RegistryPaths') -and $Config.RegistryPaths) {
        $candidates.Add((Find-RegistryGamePath -RegistryPaths $Config.RegistryPaths -Executable $executable))
    }
    if ($Config.ContainsKey('SteamAppId') -and $Config.SteamAppId) {
        $candidates.Add((Find-SteamGameByAppId -AppId $Config.SteamAppId -Executable $executable))
    }
    if ($Config.ContainsKey('SteamFolder') -and $Config.SteamFolder) {
        foreach ($library in Find-SteamLibraries) {
            $candidates.Add((Join-Path $library "steamapps\common\$($Config.SteamFolder)"))
        }
    }
    if ($Config.ContainsKey('GogGameIds') -and $Config.GogGameIds) {
        $candidates.Add((Find-GogGamePath -GogGameIds $Config.GogGameIds -Executable $executable))
    }
    if ($Config.ContainsKey('UbisoftAppIds') -and $Config.UbisoftAppIds) {
        $candidates.Add((Find-UbisoftGamePath -UbisoftAppIds $Config.UbisoftAppIds -Executable $executable))
    }
    foreach ($key in @('EpicPaths', 'EaPaths')) {
        if ($Config.ContainsKey($key) -and $Config.$key) {
            foreach ($path in $Config.$key) { $candidates.Add($path) }
        }
    }
    # Two keys per hit: the path the caller gets, and the canonical one the
    # de-duplication compares, which is the only one that can tell a junction
    # from a second install.
    $found = [System.Collections.Generic.List[string]]::new()
    $seen = [System.Collections.Generic.List[string]]::new()
    foreach ($candidate in $candidates) {
        if (-not $candidate) { continue }
        if (-not (Test-GameInstallation -Path $candidate -Executable $executable)) { continue }
        $full = ([System.IO.Path]::GetFullPath($candidate)).TrimEnd('/', '\')
        $key = Get-CanonicalPath -Path $full
        if ($seen -contains $key) { continue }
        $seen.Add($key)
        $found.Add($full)
    }

    # Xbox last and checked separately: a GDK build can ship the exe under a
    # different name, so it needs its own existence check rather than the one
    # every other source shares.
    $xboxExecutable = if ($Config.ContainsKey('XboxExecutable') -and $Config.XboxExecutable) {
        $Config.XboxExecutable
    } else {
        $executable
    }
    $xboxIdentity = if ($Config.ContainsKey('XboxIdentityName') -and $Config.XboxIdentityName) {
        $Config.XboxIdentityName
    } else {
        ''
    }
    $xboxCandidates = [System.Collections.Generic.List[string]]::new()
    if ($Config.ContainsKey('XboxPaths') -and $Config.XboxPaths) {
        foreach ($path in (Expand-XboxPathCandidates -XboxPaths $Config.XboxPaths)) {
            $xboxCandidates.Add($path)
        }
    }
    foreach ($path in (Find-XboxGamePaths -Executable $xboxExecutable -IdentityName $xboxIdentity)) {
        $xboxCandidates.Add($path)
    }
    # A Store package is the same GDK build and belongs in this group rather
    # than the generic candidate list above, which checks for the Steam exe.
    if ($Config.ContainsKey('MsixIdentityName') -and $Config.MsixIdentityName) {
        $msixPath = Find-MsixGamePath -IdentityName $Config.MsixIdentityName -Executable $xboxExecutable
        if ($msixPath) { $xboxCandidates.Add($msixPath) }
    }
    foreach ($path in $xboxCandidates) {
        if (-not (Test-GameInstallation -Path $path -Executable $xboxExecutable)) { continue }
        $full = ([System.IO.Path]::GetFullPath($path)).TrimEnd('/', '\')
        $key = Get-CanonicalPath -Path $full
        if ($seen -contains $key) { continue }
        $seen.Add($key)
        $found.Add($full)
    }

    return $found.ToArray()
}

<#
.SYNOPSIS
    Test whether a resolved game path is one of the configured Xbox paths
    for that game. Used by callers that need to know which platform a
    given install came from (e.g. to pick the correct executable name
    when the Xbox build differs from the Steam build).
#>
# Resolve-Path returns nothing at all for a path that does not exist, and most
# expanded Xbox candidates are exactly that - the drives the game is not on. So
# the caller gets the canonical form where there is one and the input otherwise.
function Get-ResolvedPathOrSelf {
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory = $true)][string]$Path)

    $resolved = @(Resolve-Path -LiteralPath $Path -ErrorAction SilentlyContinue)
    if ($resolved.Count -gt 0) { return $resolved[0].Path }
    return $Path
}

<#
.SYNOPSIS
    Resolve a path through junctions and symlinks to the directory it really is.
.DESCRIPTION
    Resolve-Path does not follow reparse points, so one install reached by two
    names compares as two installs. A Game Pass title is exactly that: the Xbox
    app puts it in `<drive>\XboxGames\<Title>\Content` and the package manager
    reports it under `C:\Program Files\WindowsApps\<package>`, which is a
    junction to a junction to that same directory. Find-AllGamePaths promises a
    de-duplicated list, so the key it compares has to see through them.

    The link can sit at any ancestor rather than on the leaf, so each hop walks
    up looking for the first reparse point and re-attaches the remainder. The
    hop limit is there because a junction is allowed to point at its own parent.
.OUTPUTS
    System.String
#>
function Get-CanonicalPath {
    [CmdletBinding()]
    [OutputType([string])]
    param([Parameter(Mandatory = $true)][string]$Path)

    $current = (Get-ResolvedPathOrSelf -Path $Path).TrimEnd('\')
    for ($hop = 0; $hop -lt 16; $hop++) {
        $probe = $current
        $suffix = ''
        $followed = $false
        while ($probe) {
            $item = Get-Item -Force -LiteralPath $probe -ErrorAction SilentlyContinue
            if ($item -and $item.Target) {
                $target = @($item.Target)[0].TrimEnd('\')
                $current = if ($suffix) { Join-Path $target $suffix } else { $target }
                $followed = $true
                break
            }
            $parent = Split-Path $probe -Parent
            if (-not $parent -or $parent -eq $probe) { break }
            $leaf = Split-Path $probe -Leaf
            $suffix = if ($suffix) { Join-Path $leaf $suffix } else { $leaf }
            $probe = $parent
        }
        if (-not $followed) { break }
    }
    return $current.TrimEnd('\')
}

function Test-IsXboxPath {
    [CmdletBinding()]
    [OutputType([bool])]
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$Config,
        [Parameter(Mandatory = $true)]
        [string]$Path
    )
    $normalised = (Get-ResolvedPathOrSelf -Path $Path).TrimEnd('\')

    # An install under one of this machine's Xbox roots is an Xbox install
    # whatever games.json says. Checking only the configured paths meant a
    # Game Pass copy found by the scan - which is every one of them, since no
    # game declares a path - was handed the Steam executable name.
    foreach ($root in Get-XboxGameRoots) {
        $rn = (Get-ResolvedPathOrSelf -Path $root).TrimEnd('\')
        if ($normalised.StartsWith($rn + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    # A Store package is a Game Pass copy that lives under WindowsApps rather
    # than an Xbox root, in a directory named for the installed package version,
    # so it matches neither the root scan above nor any configured path. Without
    # this it reads as a Steam install and install.cmd derives the exe directory
    # from a Win64 relpath the GDK build does not have.
    if ($Config.ContainsKey('MsixIdentityName') -and $Config.MsixIdentityName) {
        $package = Get-AppxPackage -Name $Config.MsixIdentityName -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($package -and $package.InstallLocation) {
            $pn = (Get-ResolvedPathOrSelf -Path $package.InstallLocation).TrimEnd('\')
            if ([string]::Equals($normalised, $pn, [System.StringComparison]::OrdinalIgnoreCase)) {
                return $true
            }
        }
    }

    if (-not ($Config.ContainsKey('XboxPaths') -and $Config.XboxPaths)) {
        return $false
    }
    foreach ($candidate in (Expand-XboxPathCandidates -XboxPaths $Config.XboxPaths)) {
        $cn = (Get-ResolvedPathOrSelf -Path $candidate).TrimEnd('\')
        if ([string]::Equals($normalised, $cn, [System.StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

<#
.SYNOPSIS
    The executable relpath that applies to ONE resolved install of a game.
.DESCRIPTION
    A GDK / Game Pass build can ship its exe under a different name and a
    different subtree than the Steam build - Prey's Steam exe is under
    Binaries\Danielle\x64\Release and its GDK exe under
    Binaries\Danielle\Gaming.Desktop.x64\Release - so the relpath is a
    property of the install, not of the game. Every caller that joins a
    relpath onto a path returned by Find-AllGamePaths goes through here;
    reading .Executable directly silently resolves a directory that does not
    exist on the Xbox copy.
.OUTPUTS
    System.String
#>
function Get-GameExecutableRelPath {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory = $true)]
        [hashtable]$Config,
        [Parameter(Mandatory = $true)]
        [string]$Path
    )
    if ($Config.ContainsKey('XboxExecutable') -and $Config.XboxExecutable) {
        if (Test-IsXboxPath -Config $Config -Path $Path) {
            return $Config.XboxExecutable
        }
    }
    return $Config.Executable
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
    'Find-UbisoftGamePath',
    'Find-RegistryGamePath',
    'Find-MsixGamePath',
    'Get-XboxGameRoots',
    'Get-XboxContentDirs',
    'Find-XboxGamePaths',
    'Get-XboxPackageIdentity',
    'Expand-XboxPathCandidates',
    'Find-GamePath',
    'Find-AllGamePaths',
    'Test-IsXboxPath',
    'Get-GameExecutableRelPath',
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
