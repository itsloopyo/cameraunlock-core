#!/usr/bin/env pwsh
# ModLoaderSetup.psm1 - Shared module for BepInEx and MelonLoader installation
# Part of CameraUnlock-Core shared utilities

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$Script:StateFileName = ".headtracking-state.json"

<#
.SYNOPSIS
    Tests if BepInEx is installed at the specified game path.
.PARAMETER GamePath
    Path to the game installation directory.
.OUTPUTS
    Boolean indicating if BepInEx is installed.
#>
function Test-BepInExInstalled {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath
    )

    $bepinexCore = Join-Path $GamePath "BepInEx/core/BepInEx.dll"
    return (Test-Path $bepinexCore)
}

<#
.SYNOPSIS
    Tests if MelonLoader is installed at the specified game path.
.PARAMETER GamePath
    Path to the game installation directory.
.OUTPUTS
    Boolean indicating if MelonLoader is installed.
#>
function Test-MelonLoaderInstalled {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath
    )

    $melonLoaderPath = Join-Path $GamePath "MelonLoader"
    return (Test-Path $melonLoaderPath)
}

<#
.SYNOPSIS
    Tests if MelonLoader has been initialized (game run once).
.PARAMETER GamePath
    Path to the game installation directory.
.PARAMETER NetFolder
    Target framework subfolder (default: net35).
.OUTPUTS
    Boolean indicating if MelonLoader is ready.
#>
function Test-MelonLoaderInitialized {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath,
        [string]$NetFolder = "net35"
    )

    $melonDll = Join-Path $GamePath "MelonLoader/$NetFolder/MelonLoader.dll"
    return (Test-Path $melonDll)
}

<#
.SYNOPSIS
    Gets the BepInEx core DLL path.
.PARAMETER GamePath
    Path to the game installation directory.
.OUTPUTS
    Full path to BepInEx core directory.
#>
function Get-BepInExCorePath {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath
    )

    return Join-Path $GamePath "BepInEx/core"
}

<#
.SYNOPSIS
    Gets the BepInEx plugins path.
.PARAMETER GamePath
    Path to the game installation directory.
.OUTPUTS
    Full path to BepInEx plugins directory.
#>
function Get-BepInExPluginsPath {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath
    )

    return Join-Path $GamePath "BepInEx/plugins"
}

<#
.SYNOPSIS
    Gets the MelonLoader Mods path.
.PARAMETER GamePath
    Path to the game installation directory.
.OUTPUTS
    Full path to MelonLoader Mods directory.
#>
function Get-MelonLoaderModsPath {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath
    )

    return Join-Path $GamePath "Mods"
}

<#
.SYNOPSIS
    Gets the MelonLoader library path.
.PARAMETER GamePath
    Path to the game installation directory.
.PARAMETER NetFolder
    Target framework subfolder (default: net35).
.OUTPUTS
    Full path to MelonLoader library directory.
#>
function Get-MelonLoaderLibPath {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath,
        [string]$NetFolder = "net35"
    )

    return Join-Path $GamePath "MelonLoader/$NetFolder"
}

<#
.SYNOPSIS
    Installs BepInEx to a game directory.
.PARAMETER GamePath
    Path to the game installation directory.
.PARAMETER Architecture
    Target architecture: x64 or x86 (default: x64).
.PARAMETER MajorVersion
    BepInEx major version to install: 5 or 6 (default: 5).
    Version 5 is stable and works with most games.
    Version 6 is newer but may have compatibility issues.
.PARAMETER EnableConsole
    Enable BepInEx console logging (default: true for development).
.PARAMETER Force
    Reinstall even if already present.
.OUTPUTS
    Hashtable with installation details including version.
#>
function Install-BepInEx {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath,
        [ValidateSet('x64', 'x86')]
        [string]$Architecture = 'x64',
        [ValidateSet(5, 6)]
        [int]$MajorVersion = 5,
        [bool]$EnableConsole = $true,
        [switch]$Force
    )

    # Check if already installed
    if ((Test-BepInExInstalled -GamePath $GamePath) -and -not $Force) {
        Write-Host "BepInEx already installed at: $GamePath" -ForegroundColor Green
        return @{
            AlreadyInstalled = $true
            Path = Get-BepInExCorePath -GamePath $GamePath
        }
    }

    Write-Host "Installing BepInEx to: $GamePath" -ForegroundColor Yellow

    # Fetch release info from GitHub
    Write-Host "  Fetching BepInEx release information..." -ForegroundColor Gray
    $apiUrl = "https://api.github.com/repos/BepInEx/BepInEx/releases"

    try {
        $releases = Invoke-RestMethod -Uri $apiUrl -Headers @{ "User-Agent" = "HeadTracking-ModLoader" }
    } catch {
        throw "Failed to fetch BepInEx releases from GitHub: $_"
    }

    # Find appropriate release
    if ($MajorVersion -eq 5) {
        $release = $releases | Where-Object { $_.tag_name -match '^v5\.' -and -not $_.prerelease } | Select-Object -First 1
    } else {
        $release = $releases | Where-Object { $_.tag_name -match '^v6\.' -and -not $_.prerelease } | Select-Object -First 1
        if (-not $release) {
            # Fall back to latest pre-release for v6
            $release = $releases | Where-Object { $_.tag_name -match '^v6\.' } | Select-Object -First 1
        }
    }

    if (-not $release) {
        throw "Could not find BepInEx $MajorVersion.x release"
    }

    $version = $release.tag_name -replace '^v', ''
    Write-Host "  Found BepInEx v$version" -ForegroundColor Cyan

    # Find download asset
    $assetPattern = "BepInEx_win_${Architecture}.*\.zip$"
    $asset = $release.assets | Where-Object { $_.name -match $assetPattern } | Select-Object -First 1

    if (-not $asset) {
        throw "Could not find BepInEx $Architecture asset in release"
    }

    # Download
    $tempZip = Join-Path $env:TEMP "BepInEx_install.zip"
    Write-Host "  Downloading: $($asset.name)..." -ForegroundColor Gray

    try {
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tempZip -UseBasicParsing
    } catch {
        throw "Failed to download BepInEx: $_"
    }

    # Extract
    Write-Host "  Extracting to game directory..." -ForegroundColor Gray
    try {
        Expand-Archive -Path $tempZip -DestinationPath $GamePath -Force
    } catch {
        throw "Failed to extract BepInEx: $_"
    }

    Remove-Item $tempZip -Force -ErrorAction SilentlyContinue

    # Create plugins directory
    $pluginsPath = Get-BepInExPluginsPath -GamePath $GamePath
    if (-not (Test-Path $pluginsPath)) {
        New-Item -ItemType Directory -Path $pluginsPath -Force | Out-Null
    }

    # Configure console logging
    if ($EnableConsole) {
        $configDir = Join-Path $GamePath "BepInEx/config"
        if (-not (Test-Path $configDir)) {
            New-Item -ItemType Directory -Path $configDir -Force | Out-Null
        }

        $configFile = Join-Path $configDir "BepInEx.cfg"
        $configContent = @"
[Logging.Console]
Enabled = true

[Logging.Disk]
Enabled = true
"@
        Set-Content -Path $configFile -Value $configContent -Encoding UTF8
        Write-Host "  Console logging enabled" -ForegroundColor Gray
    }

    # Update state file
    $stateFile = Join-Path $GamePath $Script:StateFileName
    $state = @{
        installed_at = (Get-Date).ToString("o")
        framework = @{
            type = "BepInEx"
            version = $version
            architecture = $Architecture
            installed_by_us = $true
        }
    }

    # Merge with existing state if present
    if (Test-Path $stateFile) {
        try {
            $existingState = Get-Content $stateFile -Raw | ConvertFrom-Json -AsHashtable
            foreach ($key in $existingState.Keys) {
                if ($key -ne 'framework') {
                    $state[$key] = $existingState[$key]
                }
            }
        } catch {
            throw "State file is corrupt: $stateFile - delete it manually and re-run. Parse error: $_"
        }
    }

    $state | ConvertTo-Json -Depth 10 | Set-Content $stateFile -Encoding UTF8

    Write-Host "  BepInEx v$version installed successfully!" -ForegroundColor Green

    return @{
        AlreadyInstalled = $false
        Version = $version
        Architecture = $Architecture
        Path = Get-BepInExCorePath -GamePath $GamePath
        PluginsPath = $pluginsPath
    }
}

<#
.SYNOPSIS
    Installs MelonLoader to a game directory.
.PARAMETER GamePath
    Path to the game installation directory.
.PARAMETER Architecture
    Target architecture: x64 or x86 (default: x64).
.PARAMETER Version
    MelonLoader version to install (default: 0.6.1).
.PARAMETER Force
    Reinstall even if already present.
.OUTPUTS
    Hashtable with installation details.
#>
function Install-MelonLoader {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath,
        [ValidateSet('x64', 'x86')]
        [string]$Architecture = 'x64',
        [string]$Version = '0.6.1',
        [switch]$Force
    )

    # Check if already installed
    if ((Test-MelonLoaderInstalled -GamePath $GamePath) -and -not $Force) {
        Write-Host "MelonLoader already installed at: $GamePath" -ForegroundColor Green
        return @{
            AlreadyInstalled = $true
            Path = Join-Path $GamePath "MelonLoader"
            Initialized = Test-MelonLoaderInitialized -GamePath $GamePath
        }
    }

    Write-Host "Installing MelonLoader v$Version to: $GamePath" -ForegroundColor Yellow

    # Construct download URL
    $archSuffix = if ($Architecture -eq 'x64') { 'x64' } else { 'x86' }
    $zipUrl = "https://github.com/LavaGang/MelonLoader/releases/download/v$Version/MelonLoader.$archSuffix.zip"

    # Download
    $tempZip = Join-Path $env:TEMP "MelonLoader_install.zip"
    Write-Host "  Downloading MelonLoader v$Version ($archSuffix)..." -ForegroundColor Gray

    try {
        Invoke-WebRequest -Uri $zipUrl -OutFile $tempZip -UseBasicParsing
    } catch {
        throw "Failed to download MelonLoader: $_"
    }

    # Extract
    Write-Host "  Extracting to game directory..." -ForegroundColor Gray
    try {
        Expand-Archive -Path $tempZip -DestinationPath $GamePath -Force
    } catch {
        throw "Failed to extract MelonLoader: $_"
    }

    Remove-Item $tempZip -Force -ErrorAction SilentlyContinue

    # Create Mods directory
    $modsPath = Get-MelonLoaderModsPath -GamePath $GamePath
    if (-not (Test-Path $modsPath)) {
        New-Item -ItemType Directory -Path $modsPath -Force | Out-Null
    }

    # Update state file
    $stateFile = Join-Path $GamePath $Script:StateFileName
    $state = @{
        installed_at = (Get-Date).ToString("o")
        framework = @{
            type = "MelonLoader"
            version = $Version
            architecture = $Architecture
            installed_by_us = $true
        }
    }

    # Merge with existing state if present
    if (Test-Path $stateFile) {
        try {
            $existingState = Get-Content $stateFile -Raw | ConvertFrom-Json -AsHashtable
            foreach ($key in $existingState.Keys) {
                if ($key -ne 'framework') {
                    $state[$key] = $existingState[$key]
                }
            }
        } catch {
            throw "State file is corrupt: $stateFile - delete it manually and re-run. Parse error: $_"
        }
    }

    $state | ConvertTo-Json -Depth 10 | Set-Content $stateFile -Encoding UTF8

    Write-Host "  MelonLoader v$Version installed!" -ForegroundColor Green

    $initialized = Test-MelonLoaderInitialized -GamePath $GamePath
    if (-not $initialized) {
        Write-Host "" -ForegroundColor Yellow
        Write-Host "  IMPORTANT: Run the game ONCE to let MelonLoader initialize," -ForegroundColor Yellow
        Write-Host "  then deploy your mod." -ForegroundColor Yellow
    }

    return @{
        AlreadyInstalled = $false
        Version = $Version
        Architecture = $Architecture
        Path = Join-Path $GamePath "MelonLoader"
        ModsPath = $modsPath
        Initialized = $initialized
    }
}

<#
.SYNOPSIS
    Reads the mod loader state from a game installation.
.PARAMETER GamePath
    Path to the game installation directory.
.OUTPUTS
    Hashtable with state info or $null if no state file.
#>
function Get-ModLoaderState {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath
    )

    $stateFile = Join-Path $GamePath $Script:StateFileName
    if (-not (Test-Path $stateFile)) {
        return $null
    }

    try {
        return Get-Content $stateFile -Raw | ConvertFrom-Json -AsHashtable
    } catch {
        throw "State file is corrupt: $stateFile - delete it manually and re-run. Parse error: $_"
    }
}

<#
.SYNOPSIS
    Updates the mod loader state file.
.PARAMETER GamePath
    Path to the game installation directory.
.PARAMETER Updates
    Hashtable of values to update/merge into state.
#>
function Update-ModLoaderState {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath,
        [Parameter(Mandatory=$true)]
        [hashtable]$Updates
    )

    $stateFile = Join-Path $GamePath $Script:StateFileName
    $state = Get-ModLoaderState -GamePath $GamePath

    if (-not $state) {
        $state = @{}
    }

    foreach ($key in $Updates.Keys) {
        $state[$key] = $Updates[$key]
    }

    $state | ConvertTo-Json -Depth 10 | Set-Content $stateFile -Encoding UTF8
}

<#
.SYNOPSIS
    Gets commonly needed BepInEx DLL names for referencing.
.OUTPUTS
    Array of DLL names typically needed for BepInEx mod development.
#>
function Get-BepInExReferenceDlls {
    return @(
        'BepInEx.dll',
        '0Harmony.dll'
    )
}

<#
.SYNOPSIS
    Gets commonly needed MelonLoader DLL names for referencing.
.OUTPUTS
    Array of DLL names typically needed for MelonLoader mod development.
#>
function Get-MelonLoaderReferenceDlls {
    return @(
        'MelonLoader.dll',
        '0Harmony.dll'
    )
}

<#
.SYNOPSIS
    Tests if UE4SS is installed at the specified game path.
.PARAMETER GamePath
    Path to the game installation directory.
.PARAMETER BinariesPath
    Relative path to the binaries folder (default: autodetect).
.OUTPUTS
    Boolean indicating if UE4SS is installed.
#>
function Test-UE4SSInstalled {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath,
        [string]$BinariesPath
    )

    if (-not $BinariesPath) {
        $BinariesPath = Find-UE4BinariesPath -GamePath $GamePath
    }

    if (-not $BinariesPath) {
        return $false
    }

    # UE4SS 3.x uses ue4ss subfolder
    $ue4ssDir = Join-Path $BinariesPath "ue4ss"
    $ue4ssDll = Join-Path $ue4ssDir "UE4SS.dll"

    if (Test-Path $ue4ssDll) {
        return $true
    }

    # Also check for older layout (files directly in binaries)
    $legacyDll = Join-Path $BinariesPath "UE4SS.dll"
    return (Test-Path $legacyDll)
}

<#
.SYNOPSIS
    Finds the Unreal Engine binaries path for a game.
.PARAMETER GamePath
    Path to the game installation directory.
.OUTPUTS
    Path to the Win64 binaries folder or $null.
#>
function Find-UE4BinariesPath {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath
    )

    # Standard UE layout: GameName/Binaries/Win64
    $gameName = Split-Path $GamePath -Leaf
    $standardPath = Join-Path $GamePath "$gameName\Binaries\Win64"
    if (Test-Path $standardPath) {
        return $standardPath
    }

    # Some games use Engine/Binaries/Win64
    $enginePath = Join-Path $GamePath "Engine\Binaries\Win64"
    if (Test-Path $enginePath) {
        return $enginePath
    }

    # Search for any Win64 folder with an exe
    $win64Folders = Get-ChildItem -Path $GamePath -Recurse -Directory -Filter "Win64" -ErrorAction SilentlyContinue |
        Where-Object { Get-ChildItem $_.FullName -Filter "*.exe" -ErrorAction SilentlyContinue }

    if ($win64Folders) {
        return $win64Folders[0].FullName
    }

    return $null
}

<#
.SYNOPSIS
    Gets the UE4SS mods path.
.PARAMETER GamePath
    Path to the game installation directory.
.PARAMETER BinariesPath
    Relative path to the binaries folder (default: autodetect).
.OUTPUTS
    Full path to UE4SS Mods directory.
#>
function Get-UE4SSModsPath {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath,
        [string]$BinariesPath
    )

    if (-not $BinariesPath) {
        $BinariesPath = Find-UE4BinariesPath -GamePath $GamePath
    }

    if (-not $BinariesPath) {
        return $null
    }

    # UE4SS 3.x layout
    return Join-Path $BinariesPath "ue4ss\Mods"
}

<#
.SYNOPSIS
    Installs UE4SS to an Unreal Engine game directory.
.PARAMETER GamePath
    Path to the game installation directory.
.PARAMETER BinariesPath
    Path to the binaries folder (default: autodetect).
.PARAMETER Version
    UE4SS version to install (default: latest stable).
.PARAMETER Force
    Reinstall even if already present.
.OUTPUTS
    Hashtable with installation details.
#>
function Install-UE4SS {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath,
        [string]$BinariesPath,
        [string]$Version,
        [switch]$Force
    )

    if (-not $BinariesPath) {
        $BinariesPath = Find-UE4BinariesPath -GamePath $GamePath
    }

    if (-not $BinariesPath) {
        throw "Could not find Unreal Engine binaries folder. Please specify BinariesPath."
    }

    # Check if already installed
    if ((Test-UE4SSInstalled -GamePath $GamePath -BinariesPath $BinariesPath) -and -not $Force) {
        Write-Host "UE4SS already installed at: $BinariesPath" -ForegroundColor Green
        return @{
            AlreadyInstalled = $true
            Path = Join-Path $BinariesPath "ue4ss"
            ModsPath = Get-UE4SSModsPath -GamePath $GamePath -BinariesPath $BinariesPath
        }
    }

    Write-Host "Installing UE4SS to: $BinariesPath" -ForegroundColor Yellow

    # Fetch release info from GitHub
    Write-Host "  Fetching UE4SS release information..." -ForegroundColor Gray
    $apiUrl = "https://api.github.com/repos/UE4SS-RE/RE-UE4SS/releases"

    try {
        $releases = Invoke-RestMethod -Uri $apiUrl -Headers @{ "User-Agent" = "HeadTracking-ModLoader" }
    } catch {
        throw "Failed to fetch UE4SS releases from GitHub: $_"
    }

    # Find appropriate release
    $release = $null
    if ($Version) {
        $release = $releases | Where-Object { $_.tag_name -eq "v$Version" -or $_.tag_name -eq $Version } | Select-Object -First 1
    }

    if (-not $release) {
        # Get latest stable (non-prerelease, non-experimental)
        $release = $releases | Where-Object {
            -not $_.prerelease -and
            $_.tag_name -notmatch 'experimental|beta|alpha'
        } | Select-Object -First 1
    }

    if (-not $release) {
        # Fall back to latest release
        $release = $releases | Select-Object -First 1
    }

    if (-not $release) {
        throw "Could not find UE4SS release"
    }

    $version = $release.tag_name -replace '^v', ''
    Write-Host "  Found UE4SS v$version" -ForegroundColor Cyan

    # Find download asset (non-dev version)
    $asset = $release.assets | Where-Object {
        $_.name -match 'UE4SS.*\.zip$' -and
        $_.name -notmatch 'zDEV|source|src'
    } | Select-Object -First 1

    if (-not $asset) {
        throw "Could not find UE4SS download asset in release"
    }

    # Download
    $tempZip = Join-Path $env:TEMP "UE4SS_install.zip"
    Write-Host "  Downloading: $($asset.name)..." -ForegroundColor Gray

    try {
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tempZip -UseBasicParsing
    } catch {
        throw "Failed to download UE4SS: $_"
    }

    # Extract to temp folder first to inspect structure
    $tempExtract = Join-Path $env:TEMP "UE4SS_extract"
    if (Test-Path $tempExtract) {
        Remove-Item $tempExtract -Recurse -Force
    }

    Write-Host "  Extracting..." -ForegroundColor Gray
    try {
        Expand-Archive -Path $tempZip -DestinationPath $tempExtract -Force
    } catch {
        throw "Failed to extract UE4SS: $_"
    }

    # UE4SS 3.x structure: put ue4ss folder and proxy DLL in binaries
    $ue4ssSourceDir = $null
    $proxyDll = $null

    # Find the ue4ss folder and proxy DLL in extracted content
    if (Test-Path (Join-Path $tempExtract "ue4ss")) {
        $ue4ssSourceDir = Join-Path $tempExtract "ue4ss"
        $proxyDll = Get-ChildItem $tempExtract -Filter "*.dll" | Where-Object { $_.Name -ne "UE4SS.dll" } | Select-Object -First 1
    } else {
        # Older structure - files directly in zip
        $ue4ssSourceDir = $tempExtract
    }

    # Copy UE4SS files
    $ue4ssDestDir = Join-Path $BinariesPath "ue4ss"
    if (-not (Test-Path $ue4ssDestDir)) {
        New-Item -ItemType Directory -Path $ue4ssDestDir -Force | Out-Null
    }

    # Copy ue4ss folder contents
    Copy-Item -Path "$ue4ssSourceDir\*" -Destination $ue4ssDestDir -Recurse -Force

    # Copy proxy DLL to binaries root
    if ($proxyDll) {
        Copy-Item -Path $proxyDll.FullName -Destination $BinariesPath -Force
        Write-Host "  Installed proxy DLL: $($proxyDll.Name)" -ForegroundColor Gray
    } else {
        # Try to find dwmapi.dll or other common proxy
        $commonProxies = @("dwmapi.dll", "xinput1_3.dll", "d3d11.dll")
        foreach ($proxy in $commonProxies) {
            $proxyPath = Join-Path $tempExtract $proxy
            if (Test-Path $proxyPath) {
                Copy-Item -Path $proxyPath -Destination $BinariesPath -Force
                Write-Host "  Installed proxy DLL: $proxy" -ForegroundColor Gray
                break
            }
        }
    }

    # Create Mods directory
    $modsPath = Join-Path $ue4ssDestDir "Mods"
    if (-not (Test-Path $modsPath)) {
        New-Item -ItemType Directory -Path $modsPath -Force | Out-Null
    }

    # Create default mods.txt if it doesn't exist
    $modsTxt = Join-Path $modsPath "mods.txt"
    if (-not (Test-Path $modsTxt)) {
        @"
; UE4SS Mods Configuration
; Format: ModName : 1 (enabled) or 0 (disabled)

"@ | Set-Content $modsTxt -Encoding UTF8
    }

    # Cleanup
    Remove-Item $tempZip -Force -ErrorAction SilentlyContinue
    Remove-Item $tempExtract -Recurse -Force -ErrorAction SilentlyContinue

    # Update state file
    $stateFile = Join-Path $GamePath $Script:StateFileName
    $state = @{
        installed_at = (Get-Date).ToString("o")
        framework = @{
            type = "UE4SS"
            version = $version
            installed_by_us = $true
            binaries_path = $BinariesPath
        }
    }

    if (Test-Path $stateFile) {
        try {
            $existingState = Get-Content $stateFile -Raw | ConvertFrom-Json -AsHashtable
            foreach ($key in $existingState.Keys) {
                if ($key -ne 'framework') {
                    $state[$key] = $existingState[$key]
                }
            }
        } catch {
            throw "State file is corrupt: $stateFile - delete it manually and re-run. Parse error: $_"
        }
    }

    $state | ConvertTo-Json -Depth 10 | Set-Content $stateFile -Encoding UTF8

    Write-Host "  UE4SS v$version installed successfully!" -ForegroundColor Green

    return @{
        AlreadyInstalled = $false
        Version = $version
        Path = $ue4ssDestDir
        ModsPath = $modsPath
        BinariesPath = $BinariesPath
    }
}

<#
.SYNOPSIS
    Enables or disables a UE4SS mod in mods.txt.
.PARAMETER ModsPath
    Path to the UE4SS Mods directory.
.PARAMETER ModName
    Name of the mod folder.
.PARAMETER Enabled
    Whether to enable (true) or disable (false) the mod.
#>
function Set-UE4SSModEnabled {
    param(
        [Parameter(Mandatory=$true)]
        [string]$ModsPath,
        [Parameter(Mandatory=$true)]
        [string]$ModName,
        [bool]$Enabled = $true
    )

    $modsTxt = Join-Path $ModsPath "mods.txt"

    if (-not (Test-Path $modsTxt)) {
        # Create new mods.txt
        $content = "$ModName : $(if ($Enabled) { '1' } else { '0' })"
        Set-Content $modsTxt -Value $content -Encoding UTF8
        return
    }

    $lines = Get-Content $modsTxt
    $found = $false
    $newLines = @()

    foreach ($line in $lines) {
        if ($line -match "^\s*$ModName\s*:") {
            $newLines += "$ModName : $(if ($Enabled) { '1' } else { '0' })"
            $found = $true
        } else {
            $newLines += $line
        }
    }

    if (-not $found) {
        $newLines += "$ModName : $(if ($Enabled) { '1' } else { '0' })"
    }

    Set-Content $modsTxt -Value $newLines -Encoding UTF8
}

# Export functions
Export-ModuleMember -Function @(
    'Test-BepInExInstalled',
    'Test-MelonLoaderInstalled',
    'Test-MelonLoaderInitialized',
    'Get-BepInExCorePath',
    'Get-BepInExPluginsPath',
    'Get-MelonLoaderModsPath',
    'Get-MelonLoaderLibPath',
    'Install-BepInEx',
    'Install-MelonLoader',
    'Get-ModLoaderState',
    'Update-ModLoaderState',
    'Get-BepInExReferenceDlls',
    'Get-MelonLoaderReferenceDlls',
    'Test-UE4SSInstalled',
    'Find-UE4BinariesPath',
    'Get-UE4SSModsPath',
    'Install-UE4SS',
    'Set-UE4SSModEnabled'
)
