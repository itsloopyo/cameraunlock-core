#!/usr/bin/env pwsh
# ModLoaderSetup.psm1 - Shared module for BepInEx and MelonLoader installation
# Part of CameraUnlock-Core shared utilities

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

# Get-BepInExPluginsPath / Get-MelonLoaderModsPath live in GamePathDetection.
# They used to be defined here as well, with different path separators, so which
# implementation a consumer got depended on module import order. Imported and
# re-exported instead, so the name keeps working for callers that only import
# this module.
# NO -Force here. Remove-Module is not scoped to the importing module, so -Force
# (which is Remove-Module + re-import) unloads GamePathDetection from the CALLER's
# session too - and every mod's release.ps1 imports GamePathDetection first, then this
# module, then calls Find-GamePath. That is a CommandNotFoundException for the whole
# fleet, and an order-dependent one, so it presents as intermittent.
Import-Module (Join-Path $PSScriptRoot 'GamePathDetection.psm1')

$Script:StateFileName = ".headtracking-state.json"

# The state file is parsed by the Lopari launcher with a strict JSON parser
# that rejects a UTF-8 BOM (the mod then reads as "not installed"). Windows
# PowerShell 5.1's `Set-Content -Encoding UTF8` writes one, so all state-file
# writes go through this BOM-less helper.
function Write-StateFile {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Path,
        [Parameter(Mandatory=$true)]
        $State
    )

    $json = $State | ConvertTo-Json -Depth 10
    [System.IO.File]::WriteAllText($Path, $json, (New-Object System.Text.UTF8Encoding($false)))
}

# UE4SS parses mods.txt line by line with no BOM handling, so a BOM written by
# 5.1's `Set-Content -Encoding UTF8` becomes part of the first mod name and that
# mod silently never loads.
function Write-ModsTxt {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Path,
        [Parameter(Mandatory=$true)]
        [AllowEmptyCollection()]
        [string[]]$Lines
    )

    # .NET resolves a relative path against the process directory, not the
    # PowerShell location.
    $fullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    [System.IO.File]::WriteAllLines($fullPath, $Lines, (New-Object System.Text.UTF8Encoding($false)))
}

# GitHub timestamps arrive either as ISO-8601 strings or as [datetime] already
# converted by ConvertFrom-Json, depending on the PowerShell edition.
function ConvertTo-UtcDateTime {
    param($Value)

    if ($null -eq $Value) { return $null }
    if ($Value -is [datetime]) { return $Value.ToUniversalTime() }

    $text = [string]$Value
    if ([string]::IsNullOrWhiteSpace($text)) { return $null }

    $parsed = [datetime]::MinValue
    $styles = [System.Globalization.DateTimeStyles]::AdjustToUniversal -bor [System.Globalization.DateTimeStyles]::AssumeUniversal
    if ([datetime]::TryParse($text, [cultureinfo]::InvariantCulture, $styles, [ref]$parsed)) {
        return $parsed
    }
    return $null
}

# Soak period. A version-prefix pin alone still tracks upstream's newest build
# within minutes of it being published, which is exactly the window a
# compromised release is live before anyone yanks it. Every path here that
# resolves "newest release from GitHub" filters through this first; the default
# is 14 days and callers override it only deliberately.
function Select-SoakedReleases {
    param(
        [Parameter(Mandatory=$true)] [AllowEmptyCollection()] $Releases,
        [Parameter(Mandatory=$true)] [int]$MinimumAgeDays
    )

    if ($MinimumAgeDays -le 0) { return @($Releases) }

    $now = (Get-Date).ToUniversalTime()
    $cutoff = $now.AddDays(-$MinimumAgeDays)
    $eligible = [System.Collections.Generic.List[object]]::new()

    foreach ($candidate in $Releases) {
        $published = ConvertTo-UtcDateTime $candidate.published_at
        if (-not $published) {
            Write-Host "    skipping $($candidate.tag_name): no published_at timestamp, age cannot be verified" -ForegroundColor DarkYellow
            continue
        }
        if ($published -gt $cutoff) {
            $ageDays = [math]::Floor(($now - $published).TotalDays)
            Write-Host "    skipping $($candidate.tag_name): published $($published.ToString('yyyy-MM-dd')), ${ageDays}d old, minimum is ${MinimumAgeDays}d" -ForegroundColor DarkYellow
            continue
        }
        $eligible.Add($candidate)
    }

    return $eligible.ToArray()
}

# GitHub returns /releases in created_at order, which is not version order.
# Upstream shipping v5.4.24 and then back-porting v5.4.23-hotfix a week later
# puts the older version first, and "take element 0" silently downgrades the
# vendored loader. Sort by parsed version, falling back to publish date for tags
# that carry no version (nightly builds).
function Get-ReleaseVersionKey {
    param([string]$Tag)

    # Two stages, because the fleet's tags are two different shapes and a single pattern
    # cannot rank both.
    #
    # Dotted first, LONGEST wins. A plain "first digit run" search picks the architecture
    # out of a tag like 'BepInEx_x64_5.4.22.0' and scores it 64.0, outranking every real
    # 5.4.x. Taking the longest dotted run picks 5.4.22.0 there while still handling
    # 'BepInEx.5.4.22' and 'v10.0.0-rc.1', which an anchored pattern rejects because the
    # character before the version is a dot.
    $best = $null
    foreach ($m in [regex]::Matches($Tag, '\d+(?:\.\d+){1,3}')) {
        if ($null -eq $best -or $m.Value.Length -gt $best.Length) { $best = $m.Value }
    }

    # No dotted run: fall back to the FIRST bare digit run, which is the build number in
    # praydog's 'nightly-01394-<sha40>' tags. Seven RE-Engine mods vendor REFramework
    # nightlies with no -VersionPrefix and nothing else to rank by, and scoring those 0.0
    # collapses the sort onto published_at - exactly the ordering the comment above says
    # cannot be trusted. First, not longest: the trailing sha is hex and its digit runs
    # are routinely longer than the build number.
    if ($null -eq $best) {
        $bare = [regex]::Match($Tag, '\d+')
        if (-not $bare.Success) { return [version]'0.0' }
        $best = "$($bare.Value).0"
    }

    $parsed = [version]'0.0'
    if ([version]::TryParse($best, [ref]$parsed)) { return $parsed }
    return [version]'0.0'
}

function Sort-ReleasesByVersion {
    param([Parameter(Mandatory=$true)] [AllowEmptyCollection()] $Releases)

    return @($Releases | Sort-Object `
        @{ Expression = { Get-ReleaseVersionKey $_.tag_name }; Descending = $true }, `
        @{ Expression = {
                $published = ConvertTo-UtcDateTime $_.published_at
                if ($published) { $published } else { [datetime]::MinValue }
            }; Descending = $true })
}

# The failure message has to name the date the newest match becomes eligible,
# so it reports the newest candidate that actually carries a timestamp rather
# than element 0, which may be the one we skipped for having none.
function Get-SoakEligibilityDetail {
    param([AllowEmptyCollection()] $Releases, [int]$MinimumAgeDays)

    foreach ($candidate in @($Releases)) {
        $published = ConvertTo-UtcDateTime $candidate.published_at
        if ($published) {
            return " Newest match '$($candidate.tag_name)' was published $($published.ToString('yyyy-MM-dd')) and becomes usable on $($published.AddDays($MinimumAgeDays).ToString('yyyy-MM-dd'))."
        }
    }
    return " No matching release carries a usable publish timestamp, so none can be age-verified."
}

# ConvertFrom-Json -AsHashtable is PowerShell 6+. Every install-time entry point in
# this repo runs Windows PowerShell 5.1 (pixi.toml and all the install-body-*.cmd
# wrappers invoke `powershell`, not `pwsh`), where the parameter does not exist. The
# resulting parameter-binding error was caught by the surrounding handlers and
# re-reported as "State file is corrupt: delete it manually and re-run" - telling the
# user to destroy a perfectly valid file, and taking the installed_by_us bookkeeping
# that uninstall depends on with it.
function ConvertTo-HashtableRecursive {
    param($InputObject)

    if ($null -eq $InputObject) { return $null }

    if ($InputObject -is [System.Management.Automation.PSCustomObject]) {
        $hash = @{}
        foreach ($prop in $InputObject.PSObject.Properties) {
            $hash[$prop.Name] = ConvertTo-HashtableRecursive $prop.Value
        }
        return $hash
    }

    if ($InputObject -is [object[]]) {
        $list = @()
        foreach ($item in $InputObject) { $list += ,(ConvertTo-HashtableRecursive $item) }
        return ,$list
    }

    return $InputObject
}

function ConvertFrom-JsonToHashtable {
    param(
        [Parameter(Mandatory=$true)]
        [AllowEmptyString()]
        [string]$Json
    )

    if ([string]::IsNullOrWhiteSpace($Json)) { return @{} }
    return ConvertTo-HashtableRecursive ($Json | ConvertFrom-Json)
}

# Single reader for the state file. A parse failure is reported once, here.
function Read-ModLoaderStateFile {
    param(
        [Parameter(Mandatory=$true)]
        [string]$StateFile
    )

    if (-not (Test-Path -LiteralPath $StateFile)) { return @{} }

    try {
        return ConvertFrom-JsonToHashtable (Get-Content -LiteralPath $StateFile -Raw)
    } catch {
        throw "State file is corrupt: $StateFile - delete it manually and re-run. Parse error: $_"
    }
}

# Installer state write: the framework block belongs to whoever is installing,
# every other stored key (installed_at from the first install, per-mod
# bookkeeping the uninstaller reads) survives untouched.
function Merge-ModLoaderState {
    param(
        [Parameter(Mandatory=$true)]
        [string]$StateFile,
        [Parameter(Mandatory=$true)]
        [hashtable]$Framework
    )

    $state = @{
        installed_at = (Get-Date).ToString("o")
        framework    = $Framework
    }

    $existing = Read-ModLoaderStateFile -StateFile $StateFile
    foreach ($key in $existing.Keys) {
        if ($key -ne 'framework') {
            $state[$key] = $existing[$key]
        }
    }

    return $state
}

function New-DownloadRequestHeaders {
    <#
    .SYNOPSIS
        Headers for fetching a FILE, deliberately without the GitHub token.
    .DESCRIPTION
        Never attach the Authorization header to a file download. Two reasons:

        - DirectUrl mode is documented for non-GitHub sources (Thunderstore), so a dev
          with GH_TOKEN exported would send their PAT to an unrelated host.
        - A browser_download_url 302s to objects.githubusercontent.com, which is
          presigned. Windows PowerShell 5.1 re-sends caller headers across the redirect,
          so S3 sees two auth mechanisms and answers 400 - a failure that vanishes when
          the dev unsets the variable, which makes it very hard to diagnose.

        Use New-GitHubRequestHeaders for api.github.com calls only.
    #>
    param(
        [string]$UserAgent = "CameraUnlock-HeadTracking"
    )

    return @{ "User-Agent" = $UserAgent }
}

function New-GitHubRequestHeaders {
    param(
        [string]$UserAgent = "CameraUnlock-HeadTracking",
        [hashtable]$AdditionalHeaders = @{}
    )

    $headers = @{ "User-Agent" = $UserAgent }
    foreach ($key in $AdditionalHeaders.Keys) {
        $headers[$key] = $AdditionalHeaders[$key]
    }

    foreach ($name in @("GITHUB_TOKEN", "GH_TOKEN", "LOPARI_GITHUB_TOKEN", "LOPARI_GH_TOKEN")) {
        $token = [Environment]::GetEnvironmentVariable($name)
        if (-not [string]::IsNullOrWhiteSpace($token)) {
            $headers["Authorization"] = "Bearer $token"
            $headers["X-GitHub-Api-Version"] = "2022-11-28"
            break
        }
    }

    return $headers
}

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

    $v5Marker = Join-Path $GamePath "BepInEx/core/BepInEx.dll"
    $v6Marker = Join-Path $GamePath "BepInEx/core/BepInEx.Core.dll"
    return ((Test-Path -LiteralPath $v5Marker) -or (Test-Path -LiteralPath $v6Marker))
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
    return (Test-Path -LiteralPath $melonLoaderPath)
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
    return (Test-Path -LiteralPath $melonDll)
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
.PARAMETER VendorZip
    Optional path to a vendored BepInEx zip (e.g. <mod>/vendor/bepinex/
    BepInEx_win_x64.zip). When supplied, the loader is extracted from this
    file and the GitHub-fetch path is bypassed entirely. Required for
    doctrine-clean dev-deploy: install.cmd uses the same vendored copy.
    When the path is supplied but does not exist, this throws - we never
    silently fall back to the network when vendor was requested.
.PARAMETER MinimumAgeDays
    Soak period an upstream release must have been public before the GitHub-fetch path will install it (default 14).
    Ignored when -VendorZip is used. 0 disables the check.
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
        [switch]$Force,
        [string]$VendorZip,
        [ValidateRange(0, 3650)]
        [int]$MinimumAgeDays = 14
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

    if ($VendorZip) {
        if (-not (Test-Path -LiteralPath $VendorZip)) {
            throw "VendorZip not found at: $VendorZip. Run 'pixi run update-deps' to refresh the vendored loader."
        }
        Write-Host "  Extracting vendored: $VendorZip" -ForegroundColor Gray
        try {
            Expand-Archive -LiteralPath $VendorZip -DestinationPath $GamePath -Force
        } catch {
            throw "Failed to extract vendored BepInEx from $VendorZip : $_"
        }
        $coreDllName = if ($MajorVersion -eq 6) { 'BepInEx.Core.dll' } else { 'BepInEx.dll' }
        $coreDll = Join-Path $GamePath "BepInEx/core/$coreDllName"
        if (Test-Path -LiteralPath $coreDll) {
            $version = (Get-Item -LiteralPath $coreDll).VersionInfo.FileVersion
        } else {
            throw "$coreDllName missing after extracting $VendorZip - vendored zip is corrupt."
        }
        Write-Host "  Installed BepInEx v$version" -ForegroundColor Cyan
    } else {
        # Fetch release info from GitHub (legacy path; prefer -VendorZip).
        Write-Host "  Fetching BepInEx release information..." -ForegroundColor Gray
        $apiUrl = "https://api.github.com/repos/BepInEx/BepInEx/releases"

        try {
            $releases = Invoke-RestMethod -Uri $apiUrl -Headers (New-GitHubRequestHeaders -UserAgent "HeadTracking-ModLoader")
        } catch {
            throw "Failed to fetch BepInEx releases from GitHub: $_"
        }

        $candidates = $releases | Where-Object { $_.tag_name -match "^v$MajorVersion\." -and -not $_.prerelease }
        if (-not $candidates -and $MajorVersion -eq 6) {
            $candidates = $releases | Where-Object { $_.tag_name -match '^v6\.' }
        }

        if (-not $candidates) {
            throw "Could not find BepInEx $MajorVersion.x release"
        }
        $candidates = Sort-ReleasesByVersion -Releases $candidates

        $soaked = Select-SoakedReleases -Releases $candidates -MinimumAgeDays $MinimumAgeDays
        if (-not $soaked) {
            $detail = Get-SoakEligibilityDetail -Releases $candidates -MinimumAgeDays $MinimumAgeDays
            throw "No BepInEx $MajorVersion.x release has been public for $MinimumAgeDays days.$detail Install from the committed vendored copy with -VendorZip, or pass -MinimumAgeDays 0 deliberately."
        }
        $release = $soaked | Select-Object -First 1

        $version = $release.tag_name -replace '^v', ''
        Write-Host "  Found BepInEx v$version" -ForegroundColor Cyan

        $assetPattern = "BepInEx_win_${Architecture}.*\.zip$"
        $asset = $release.assets | Where-Object { $_.name -match $assetPattern } | Select-Object -First 1

        if (-not $asset) {
            throw "Could not find BepInEx $Architecture asset in release"
        }

        $tempZip = Join-Path $env:TEMP "BepInEx_install.zip"
        Write-Host "  Downloading: $($asset.name)..." -ForegroundColor Gray

        try {
            Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tempZip -UseBasicParsing
        } catch {
            throw "Failed to download BepInEx: $_"
        }

        Write-Host "  Extracting to game directory..." -ForegroundColor Gray
        try {
            Expand-Archive -LiteralPath $tempZip -DestinationPath $GamePath -Force
        } catch {
            throw "Failed to extract BepInEx: $_"
        }

        Remove-Item -LiteralPath $tempZip -Force -ErrorAction SilentlyContinue
    }

    # Create plugins directory
    $pluginsPath = Get-BepInExPluginsPath -GamePath $GamePath
    if (-not (Test-Path -LiteralPath $pluginsPath)) {
        New-Item -ItemType Directory -Path $pluginsPath -Force | Out-Null
    }

    # Configure console logging
    if ($EnableConsole) {
        $configDir = Join-Path $GamePath "BepInEx/config"
        if (-not (Test-Path -LiteralPath $configDir)) {
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
    $state = Merge-ModLoaderState -StateFile $stateFile -Framework @{
        type = "BepInEx"
        version = $version
        architecture = $Architecture
        installed_by_us = $true
    }

    Write-StateFile -Path $stateFile -State $state

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
        Expand-Archive -LiteralPath $tempZip -DestinationPath $GamePath -Force
    } catch {
        throw "Failed to extract MelonLoader: $_"
    }

    Remove-Item -LiteralPath $tempZip -Force -ErrorAction SilentlyContinue

    # Create Mods directory
    $modsPath = Get-MelonLoaderModsPath -GamePath $GamePath
    if (-not (Test-Path -LiteralPath $modsPath)) {
        New-Item -ItemType Directory -Path $modsPath -Force | Out-Null
    }

    # Update state file
    $stateFile = Join-Path $GamePath $Script:StateFileName
    $state = Merge-ModLoaderState -StateFile $stateFile -Framework @{
        type = "MelonLoader"
        version = $Version
        architecture = $Architecture
        installed_by_us = $true
    }

    Write-StateFile -Path $stateFile -State $state

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
    if (-not (Test-Path -LiteralPath $stateFile)) {
        return $null
    }

    return Read-ModLoaderStateFile -StateFile $stateFile
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

    Write-StateFile -Path $stateFile -State $state
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

    if (Test-Path -LiteralPath $ue4ssDll) {
        return $true
    }

    # Also check for older layout (files directly in binaries)
    $legacyDll = Join-Path $BinariesPath "UE4SS.dll"
    return (Test-Path -LiteralPath $legacyDll)
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

    # Every candidate must actually hold an executable. The standard-layout branch used
    # to short-circuit on a bare Test-Path, so an install whose <GameName>\Binaries\Win64
    # exists but is empty returned that folder and never reached the scan below - UE4SS
    # then landed next to nothing.
    function Test-HasExecutable([string]$Path) {
        if (-not (Test-Path -LiteralPath $Path)) { return $false }
        return [bool](Get-ChildItem -LiteralPath $Path -Filter '*.exe' -ErrorAction SilentlyContinue)
    }

    # Standard UE layout: GameName/Binaries/Win64
    $gameName = Split-Path $GamePath -Leaf
    $standardPath = Join-Path $GamePath "$gameName\Binaries\Win64"
    if (Test-HasExecutable $standardPath) {
        return $standardPath
    }

    # <project>\Binaries\Win64 where the project folder is not named after the
    # install folder - Palworld ships "Pal", Hogwarts Legacy ships "Phoenix".
    # Bounded to that one shape: a -Recurse walk of an installed UE game is tens
    # of GB and minutes of wall time before it can return $null.
    #
    # This runs BEFORE the Engine fallback, and Engine is excluded from it. Every
    # UE install ships Engine\Binaries\Win64, so checking it first meant every
    # game with a non-matching project name resolved to the engine's tool folder
    # (CrashReportClient and friends) instead of the one holding the game exe -
    # and UE4SS only loads from the latter, because that is where dwmapi.dll has
    # to sit.
    # A shipping UE build names its executable <Project>-Win64-Shipping.exe, so preferring
    # that is a principled choice rather than "whichever directory sorted first". The old
    # any-exe scan returned AALauncher over ZZGame purely on name order, and Get-ChildItem
    # ordering is NTFS index order - deterministic on local NTFS, not guaranteed by the API
    # and different on ReFS or a network share.
    $fallback = $null
    foreach ($candidate in @(Get-ChildItem -LiteralPath $GamePath -Directory -ErrorAction SilentlyContinue)) {
        if ($candidate.Name -eq 'Engine') { continue }
        $win64 = Join-Path $candidate.FullName 'Binaries\Win64'
        if (-not (Test-Path -LiteralPath $win64)) { continue }

        if (Get-ChildItem -LiteralPath $win64 -Filter '*-Win64-Shipping.exe' -ErrorAction SilentlyContinue) {
            return $win64
        }
        if ($null -eq $fallback -and (Test-HasExecutable $win64)) {
            $fallback = $win64
        }
    }
    if ($fallback) { return $fallback }

    # Last resort only, for the rare layout that really does run out of Engine.
    $enginePath = Join-Path $GamePath "Engine\Binaries\Win64"
    if (Test-Path -LiteralPath $enginePath) {
        return $enginePath
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
.PARAMETER MinimumAgeDays
    Soak period a release must have been public before it is installable (default 14). Only applies when resolving
    "latest"; an explicit -Version is the caller naming an exact build. 0 disables the check.
.OUTPUTS
    Hashtable with installation details.
#>
function Install-UE4SS {
    param(
        [Parameter(Mandatory=$true)]
        [string]$GamePath,
        [string]$BinariesPath,
        [string]$Version,
        [switch]$Force,
        [ValidateRange(0, 3650)]
        [int]$MinimumAgeDays = 14
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
        $releases = Invoke-RestMethod -Uri $apiUrl -Headers (New-GitHubRequestHeaders -UserAgent "HeadTracking-ModLoader")
    } catch {
        throw "Failed to fetch UE4SS releases from GitHub: $_"
    }

    # An explicitly requested -Version is the caller naming an exact build, so
    # the soak does not apply to it - it applies to "whatever upstream published
    # last", which is what the unpinned path below resolves.
    $release = $null
    if ($Version) {
        $release = $releases | Where-Object { $_.tag_name -eq "v$Version" -or $_.tag_name -eq $Version } | Select-Object -First 1
    }

    if (-not $release) {
        # Latest stable (non-prerelease, non-experimental), else latest of anything.
        $candidates = $releases | Where-Object {
            -not $_.prerelease -and
            $_.tag_name -notmatch 'experimental|beta|alpha'
        }
        if (-not $candidates) {
            $candidates = $releases
        }

        if (-not $candidates) {
            throw "Could not find UE4SS release"
        }
        $candidates = Sort-ReleasesByVersion -Releases $candidates

        $soaked = Select-SoakedReleases -Releases $candidates -MinimumAgeDays $MinimumAgeDays
        if (-not $soaked) {
            $detail = Get-SoakEligibilityDetail -Releases $candidates -MinimumAgeDays $MinimumAgeDays
            throw "No UE4SS release has been public for $MinimumAgeDays days.$detail Pin an older build with -Version, or pass -MinimumAgeDays 0 deliberately."
        }
        $release = $soaked | Select-Object -First 1
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
    if (Test-Path -LiteralPath $tempExtract) {
        Remove-Item -LiteralPath $tempExtract -Recurse -Force
    }

    Write-Host "  Extracting..." -ForegroundColor Gray
    try {
        Expand-Archive -LiteralPath $tempZip -DestinationPath $tempExtract -Force
    } catch {
        throw "Failed to extract UE4SS: $_"
    }

    # UE4SS 3.x structure: put ue4ss folder and proxy DLL in binaries
    $ue4ssSourceDir = $null
    $proxyDll = $null

    # Find the ue4ss folder and proxy DLL in extracted content
    if (Test-Path -LiteralPath (Join-Path $tempExtract "ue4ss")) {
        $ue4ssSourceDir = Join-Path $tempExtract "ue4ss"
        $proxyDll = Get-ChildItem $tempExtract -Filter "*.dll" | Where-Object { $_.Name -ne "UE4SS.dll" } | Select-Object -First 1
    } else {
        # Older structure - files directly in zip
        $ue4ssSourceDir = $tempExtract
    }

    # Copy UE4SS files
    $ue4ssDestDir = Join-Path $BinariesPath "ue4ss"
    if (-not (Test-Path -LiteralPath $ue4ssDestDir)) {
        New-Item -ItemType Directory -Path $ue4ssDestDir -Force | Out-Null
    }

    # Copy ue4ss folder contents
    Copy-Item -Path "$ue4ssSourceDir\*" -Destination $ue4ssDestDir -Recurse -Force

    # Copy proxy DLL to binaries root
    if ($proxyDll) {
        Copy-Item -LiteralPath $proxyDll.FullName -Destination $BinariesPath -Force
        Write-Host "  Installed proxy DLL: $($proxyDll.Name)" -ForegroundColor Gray
    } else {
        # Try to find dwmapi.dll or other common proxy
        $commonProxies = @("dwmapi.dll", "xinput1_3.dll", "d3d11.dll")
        foreach ($proxy in $commonProxies) {
            $proxyPath = Join-Path $tempExtract $proxy
            if (Test-Path -LiteralPath $proxyPath) {
                Copy-Item -LiteralPath $proxyPath -Destination $BinariesPath -Force
                Write-Host "  Installed proxy DLL: $proxy" -ForegroundColor Gray
                break
            }
        }
    }

    # Create Mods directory
    $modsPath = Join-Path $ue4ssDestDir "Mods"
    if (-not (Test-Path -LiteralPath $modsPath)) {
        New-Item -ItemType Directory -Path $modsPath -Force | Out-Null
    }

    # Create default mods.txt if it doesn't exist
    $modsTxt = Join-Path $modsPath "mods.txt"
    if (-not (Test-Path -LiteralPath $modsTxt)) {
        Write-ModsTxt -Path $modsTxt -Lines @(
            '; UE4SS Mods Configuration'
            '; Format: ModName : 1 (enabled) or 0 (disabled)'
            ''
        )
    }

    # Cleanup
    Remove-Item -LiteralPath $tempZip -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $tempExtract -Recurse -Force -ErrorAction SilentlyContinue

    # Update state file
    $stateFile = Join-Path $GamePath $Script:StateFileName
    $state = Merge-ModLoaderState -StateFile $stateFile -Framework @{
        type = "UE4SS"
        version = $version
        installed_by_us = $true
        binaries_path = $BinariesPath
    }

    Write-StateFile -Path $stateFile -State $state

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

    if (-not (Test-Path -LiteralPath $modsTxt)) {
        # Create new mods.txt
        Write-ModsTxt -Path $modsTxt -Lines @("$ModName : $(if ($Enabled) { '1' } else { '0' })")
        return
    }

    $lines = @(Get-Content -LiteralPath $modsTxt)
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

    Write-ModsTxt -Path $modsTxt -Lines $newLines
}

# Export functions
<#
.SYNOPSIS
    Resolves the latest upstream release of a mod loader within a pinned version range, then downloads the matching asset to OutputPath.
.DESCRIPTION
    Two modes:
      - GitHub mode (Owner + Repo): queries GitHub API /repos/:owner/:repo/releases, filters by VersionPrefix +
        AllowPrerelease + MinimumAgeDays, picks the highest-versioned matching release, then downloads the asset whose
        filename matches AssetPattern.
      - Direct-URL mode (DirectUrl): fetches a single pinned URL (for non-GitHub sources like Thunderstore). The age
        soak does not apply here - the URL names an exact version the dev chose, not whatever upstream published last.
    On any failure (network, 404, timeout, rate limit, missing asset, corrupt zip) this function throws. Called from each mod's
    update-deps.ps1 (the manual `pixi run update-deps` flow). install.cmd does not call this; the vendored zip is the install-time
    source of truth.
.PARAMETER OutputPath
    Where to write the downloaded file.
.PARAMETER Owner
    GitHub repository owner (GitHub mode).
.PARAMETER Repo
    GitHub repository name (GitHub mode).
.PARAMETER VersionPrefix
    Tag prefix to filter by (e.g. "v5.4." rejects v5.5, v6). Empty string = no prefix filter.
.PARAMETER AssetPattern
    Regex matched against asset name (e.g. "BepInEx_win_x64_.*\.zip").
.PARAMETER AllowPrerelease
    Include prereleases/nightlies when selecting the latest match.
.PARAMETER DirectUrl
    Single pinned URL (Direct-URL mode). Overrides GitHub mode when provided.
.PARAMETER MinimumAgeDays
    Soak period a GitHub release must have been public before it can be vendored (default 14). Releases younger than
    this are skipped with a note; if that leaves nothing, this throws with the date the newest match becomes eligible.
    0 disables the check - use it only with a deliberate look at what upstream just shipped.
.PARAMETER TimeoutSec
    Per-request timeout (default 30s).
.OUTPUTS
    Hashtable: @{ Tag; CommitSha; AssetUrl; AssetName; Sha256; FetchedAt; Source }
#>
function Invoke-FetchLatestLoader {
    param(
        [Parameter(Mandatory=$true)] [string]$OutputPath,
        [string]$Owner,
        [string]$Repo,
        [string]$VersionPrefix = '',
        [string]$AssetPattern,
        [switch]$AllowPrerelease,
        [string]$DirectUrl,
        [ValidateRange(0, 3650)]
        [int]$MinimumAgeDays = 14,
        [int]$TimeoutSec = 30
    )

    $headers = New-GitHubRequestHeaders
    $outputDir = Split-Path -Parent $OutputPath
    if ($outputDir -and -not (Test-Path -LiteralPath $outputDir)) {
        New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
    }

    if ($DirectUrl) {
        Invoke-WebRequest -Uri $DirectUrl -OutFile $OutputPath -UseBasicParsing -TimeoutSec $TimeoutSec -Headers (New-DownloadRequestHeaders)
        $sha = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash.ToLower()
        return @{
            Tag = ''
            CommitSha = ''
            AssetUrl = $DirectUrl
            AssetName = (Split-Path -Leaf $DirectUrl)
            Sha256 = $sha
            FetchedAt = (Get-Date).ToString('o')
            Source = 'direct-url'
        }
    }

    if (-not $Owner -or -not $Repo -or -not $AssetPattern) {
        throw "Invoke-FetchLatestLoader: GitHub mode requires -Owner, -Repo, -AssetPattern."
    }

    $apiUrl = "https://api.github.com/repos/$Owner/$Repo/releases?per_page=50"
    $releases = Invoke-RestMethod -Uri $apiUrl -Headers $headers -TimeoutSec $TimeoutSec

    $matching = $releases | Where-Object {
        ($VersionPrefix -eq '' -or $_.tag_name.StartsWith($VersionPrefix)) -and
        ($AllowPrerelease.IsPresent -or -not $_.prerelease)
    }

    if (-not $matching) {
        throw "No upstream release matches Owner=$Owner Repo=$Repo VersionPrefix='$VersionPrefix' AllowPrerelease=$($AllowPrerelease.IsPresent)."
    }
    $matching = Sort-ReleasesByVersion -Releases $matching

    $soaked = Select-SoakedReleases -Releases $matching -MinimumAgeDays $MinimumAgeDays
    if (-not $soaked) {
        $detail = Get-SoakEligibilityDetail -Releases $matching -MinimumAgeDays $MinimumAgeDays
        throw "No upstream release for $Owner/$Repo matching VersionPrefix='$VersionPrefix' has been public for $MinimumAgeDays days.$detail Re-run then, or pass -MinimumAgeDays 0 to take the fresh release deliberately."
    }
    $matching = $soaked

    # REFramework-nightly: each nightly publishes a per-game subset (RE2.zip,
    # RE4.zip, ...) and may skip ours. Walk newest-to-oldest until we find one
    # whose asset list matches AssetPattern.
    $release = $null
    $asset = $null
    foreach ($candidate in $matching) {
        $candidateAsset = $candidate.assets | Where-Object { $_.name -match $AssetPattern } | Select-Object -First 1
        if ($candidateAsset) {
            $release = $candidate
            $asset = $candidateAsset
            break
        }
    }
    if (-not $asset) {
        throw "No release in the matching set has an asset matching regex '$AssetPattern' (scanned $($matching.Count) releases)."
    }

    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $OutputPath -UseBasicParsing -TimeoutSec $TimeoutSec -Headers (New-DownloadRequestHeaders)

    $sha = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash.ToLower()

    $commitSha = ''
    try {
        $tagInfo = Invoke-RestMethod -Uri "https://api.github.com/repos/$Owner/$Repo/git/refs/tags/$($release.tag_name)" -Headers $headers -TimeoutSec $TimeoutSec
        $commitSha = $tagInfo.object.sha
    } catch {
        # Tag lookup is best-effort; fallback to empty.
    }

    return @{
        Tag = $release.tag_name
        CommitSha = $commitSha
        AssetUrl = $asset.browser_download_url
        AssetName = $asset.name
        Sha256 = $sha
        FetchedAt = (Get-Date).ToString('o')
        Source = 'github'
    }
}

<#
.SYNOPSIS
    Dev-time helper. Updates vendor/<Name>/ to the latest upstream release within range and writes LICENSE + README.md.
.DESCRIPTION
    Called by each mod's scripts/update-deps.ps1 (the manual `pixi run update-deps` flow). Delegates the download to
    Invoke-FetchLatestLoader, then writes sibling metadata so the committed vendor tree is self-describing:
      vendor/<Name>/
        <OutputFileName>    (the downloaded zip)
        LICENSE             (fetched from the zip if present, else from the GitHub API)
        README.md           (tag, commit SHA, asset URL, SHA-256, fetched_at)
    The dev reviews the diff and commits. CI does not call this; install.cmd does not call this.
.PARAMETER Name
    Loader slug (e.g. "bepinex", "melonloader", "reframework"). Determines vendor subdir name only.
.PARAMETER OutputDir
    Full path to vendor/<name>/. Created if missing.
.PARAMETER OutputFileName
    Filename of the zip inside OutputDir (default: asset's own name).
.PARAMETER LicenseName
    License file name in upstream repo (default 'LICENSE'). Used if the zip does not contain a LICENSE at its root.
.PARAMETER Owner, Repo, VersionPrefix, AssetPattern, AllowPrerelease, DirectUrl, MinimumAgeDays, TimeoutSec
    Passed through to Invoke-FetchLatestLoader. MinimumAgeDays defaults to 14: a fresh upstream release is not
    vendorable until it has been public that long.
.OUTPUTS
    Hashtable with the same fields as Invoke-FetchLatestLoader plus LocalPath.
#>
function Update-VendoredLoader {
    param(
        [Parameter(Mandatory=$true)] [string]$Name,
        [Parameter(Mandatory=$true)] [string]$OutputDir,
        [string]$OutputFileName,
        [string]$Owner,
        [string]$Repo,
        [string]$VersionPrefix = '',
        [string]$AssetPattern,
        [switch]$AllowPrerelease,
        [string]$DirectUrl,
        [string]$LicenseName = 'LICENSE',
        [string]$LicenseUrl,
        [ValidateRange(0, 3650)]
        [int]$MinimumAgeDays = 14,
        [int]$TimeoutSec = 30
    )

    if (-not (Test-Path -LiteralPath $OutputDir)) {
        New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    }

    # Stage to a temp file first, then resolve final filename from the response.
    $tempFile = Join-Path $env:TEMP ("vendor-refresh-$Name-" + [IO.Path]::GetRandomFileName())

    Write-Host "  Updating vendor/$Name from upstream..." -ForegroundColor Cyan
    $meta = Invoke-FetchLatestLoader `
        -OutputPath $tempFile `
        -Owner $Owner -Repo $Repo `
        -VersionPrefix $VersionPrefix -AssetPattern $AssetPattern `
        -AllowPrerelease:$AllowPrerelease `
        -DirectUrl $DirectUrl -MinimumAgeDays $MinimumAgeDays -TimeoutSec $TimeoutSec

    if (-not $OutputFileName) {
        # A Thunderstore-style DirectUrl ends in the version (".../5.4.2100/"),
        # so the derived asset name is "5.4.2100": the vendored file lands with
        # no extension, the zip LICENSE extraction below is skipped, and the
        # name install.cmd hardcodes is missing from the tree.
        # NOTE the version-like test: [IO.Path]::GetExtension('5.4.2100') returns
        # '.2100', so an extension check ALONE never fires for exactly the Thunderstore
        # URL described above - which is the case this guard exists for. The message
        # below says "usable filename" rather than "no extension" for the same reason.
        if ((-not [IO.Path]::GetExtension($meta.AssetName)) -or ($meta.AssetName -match '^[0-9]+(\.[0-9]+)*$')) {
            throw "Cannot derive a vendored filename from '$($meta.AssetUrl)' - the URL's last segment ('$($meta.AssetName)') has no extension. Pass -OutputFileName with the name install.cmd expects."
        }
        $OutputFileName = $meta.AssetName
    }
    $targetPath = Join-Path $OutputDir $OutputFileName
    $readmePath = Join-Path $OutputDir 'README.md'
    $licensePath = Join-Path $OutputDir 'LICENSE'

    # Idempotency: if the on-disk vendor copy already matches the freshly-downloaded
    # SHA-256, leave the tree alone. Otherwise every run dirties README.md with a new
    # FetchedAt timestamp even when upstream is unchanged.
    if ((Test-Path -LiteralPath $targetPath) -and (Test-Path -LiteralPath $readmePath) -and (Test-Path -LiteralPath $licensePath)) {
        $existingSha = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash.ToLower()
        if ($existingSha -eq $meta.Sha256) {
            Remove-Item -LiteralPath $tempFile -Force -ErrorAction SilentlyContinue
            Write-Host "    no change (sha256=$($meta.Sha256.Substring(0,12))... matches on-disk vendor copy)" -ForegroundColor DarkGray
            $meta.LocalPath = $targetPath
            return $meta
        }
    }

    Move-Item -LiteralPath $tempFile -Destination $targetPath -Force

    # LICENSE resolution order:
    #   1. Explicit $LicenseUrl (e.g. LGPL mods or Thunderstore repacks that don't ship LICENSE).
    #   2. LICENSE extracted from the downloaded zip.
    #   3. GitHub API /repos/:owner/:repo/license as last resort.
    $extractedLicense = $false

    if ($LicenseUrl) {
        try {
            $licHeaders = @{ "User-Agent" = "CameraUnlock-HeadTracking" }
            Invoke-WebRequest -Uri $LicenseUrl -OutFile $licensePath -UseBasicParsing -TimeoutSec $TimeoutSec -Headers $licHeaders
            $extractedLicense = $true
        } catch {
            Write-Warning "LicenseUrl fetch failed ($_); will try other sources."
        }
    }

    if (-not $extractedLicense -and $targetPath -match '\.zip$') {
        try {
            Add-Type -AssemblyName System.IO.Compression.FileSystem -ErrorAction SilentlyContinue
            $zip = [System.IO.Compression.ZipFile]::OpenRead($targetPath)
            try {
                $entry = $zip.Entries | Where-Object {
                    $_.Name -match '^LICENSE(\.md|\.txt)?$' -and $_.FullName -notmatch '/.+/'
                } | Select-Object -First 1
                if ($entry) {
                    $outStream = [System.IO.File]::Create($licensePath)
                    try {
                        $in = $entry.Open()
                        try { $in.CopyTo($outStream) } finally { $in.Dispose() }
                    } finally { $outStream.Dispose() }
                    $extractedLicense = $true
                }
            } finally { $zip.Dispose() }
        } catch {
            # Fall through to API fetch.
        }
    }

    if (-not $extractedLicense -and $Owner -and $Repo) {
        try {
            $headers = New-GitHubRequestHeaders -AdditionalHeaders @{ "Accept" = "application/vnd.github.raw" }
            $licenseUrl = "https://raw.githubusercontent.com/$Owner/$Repo/$($meta.Tag)/$LicenseName"
            Invoke-WebRequest -Uri $licenseUrl -OutFile $licensePath -UseBasicParsing -TimeoutSec $TimeoutSec -Headers (New-DownloadRequestHeaders)
            $extractedLicense = $true
        } catch {
            # Try API fallback
            try {
                $headers = New-GitHubRequestHeaders
                $licenseInfo = Invoke-RestMethod -Uri "https://api.github.com/repos/$Owner/$Repo/license" -Headers $headers -TimeoutSec $TimeoutSec
                [System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($licenseInfo.content)) | Set-Content $licensePath -Encoding UTF8
                $extractedLicense = $true
            } catch {
                Write-Warning "Could not resolve LICENSE for $Name - you must add it manually to $licensePath."
            }
        }
    }

    # README.md with metadata.
    $readme = @()
    $readme += "# $Name (vendored)"
    $readme += ''
    $readme += 'This directory contains a bundled copy of the upstream mod loader. It is the install-time'
    $readme += 'source of truth: install.cmd extracts directly from here and never reaches out to the network.'
    $readme += 'Refresh manually with `pixi run update-deps`, then commit.'
    $readme += ''
    $readme += '## Snapshot'
    $readme += ''
    $readme += "- Asset: ``$($meta.AssetName)``"
    if ($meta.Tag) { $readme += "- Tag: ``$($meta.Tag)``" }
    if ($meta.CommitSha) { $readme += "- Commit: ``$($meta.CommitSha)``" }
    $readme += "- Upstream URL: $($meta.AssetUrl)"
    $readme += "- SHA-256: ``$($meta.Sha256)``"
    $readme += "- Fetched at: $($meta.FetchedAt)"
    $readme += "- Source: $($meta.Source)"
    $readme += ''
    $readme += 'Do not edit this directory by hand. Run ``pixi run package`` (or CI release) to refresh.'
    $readme -join "`n" | Set-Content $readmePath -Encoding UTF8

    Write-Host "    tag=$($meta.Tag) asset=$($meta.AssetName) sha256=$($meta.Sha256.Substring(0,12))..." -ForegroundColor DarkGray

    $meta.LocalPath = $targetPath
    return $meta
}

<#
.SYNOPSIS
    Deprecated alias for Update-VendoredLoader. Kept for consumer mods whose update-deps.ps1 still calls the
    old name; will be removed in a future major version. Migrate callers to Update-VendoredLoader.
#>
function Refresh-VendoredLoader {
    Write-Warning "Refresh-VendoredLoader is deprecated; use Update-VendoredLoader. This alias will be removed in a future major version."
    Update-VendoredLoader @args
}

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
    'Set-UE4SSModEnabled',
    'Invoke-FetchLatestLoader',
    'Update-VendoredLoader',
    'Refresh-VendoredLoader'
)
