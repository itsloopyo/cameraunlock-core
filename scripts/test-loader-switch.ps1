param(
    [string]$BodiesRoot = $PSScriptRoot,
    # findstr cannot open a path past MAX_PATH, so a checkout nested deep enough
    # fails the state-file reads for a reason that has nothing to do with the
    # bodies. Point this somewhere short when that happens.
    [string]$WorkRoot = (Join-Path (Split-Path $PSScriptRoot) '.lab')
)

# A mod that moves from the Cecil patcher to BepInEx: the BepInEx install body
# runs the release's own uninstall.cmd first when the state file names another
# framework, and the uninstall body undoes the Cecil patch a wrapper still names
# while leaving alone a loader the state file does not say this mod installed.
# Each case runs the real bodies on a synthetic game folder whose path holds `!`.

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$root = Join-Path $WorkRoot ('loader-switch-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $root -Force | Out-Null

$marker = 'HeadTracking_Patched_Fixture_v1'
$pristine = 'pristine assembly bytes'
$managed = 'Fixture_Data\Managed'
# The loader checks read winhttp.dll's PE header, so the fixture's proxy is a
# real x64 image: this machine's own winhttp.dll, copied into the temp tree.
$proxy = Join-Path $env:WINDIR 'System32\winhttp.dll'

$failures = New-Object Collections.Generic.List[string]
function Check([string]$Case, [bool]$Ok, [string]$What) {
    if (-not $Ok) { $failures.Add("${Case}: $What") }
}

function New-Case([string]$Name, [switch]$CecilInstall, [switch]$PlayersBepInEx) {
    $caseRoot = Join-Path $root $Name
    $game = Join-Path $caseRoot 'Game ! Folder'
    $shared = Join-Path $caseRoot 'shared'
    $plugins = Join-Path $caseRoot 'plugins'
    $vendor = Join-Path $caseRoot 'vendor\bepinex'
    New-Item -ItemType Directory -Path $game, $shared, $plugins, $vendor | Out-Null

    foreach ($f in 'install-body-bepinex.cmd', 'uninstall-body.cmd', 'restore-kept-configs.ps1') {
        Copy-Item -LiteralPath (Join-Path $BodiesRoot $f) -Destination $shared
    }
    foreach ($f in 'find-game.ps1', 'cecil-marker-check.ps1', 'check-loader-arch.ps1') {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $f) -Destination $shared
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../powershell/GamePathDetection.psm1') -Destination $shared
    $gamesJson = @{ schema_version = 1; games = @{ fixture = @{ display_name = 'Fixture'; env_var = 'CUL_FIXTURE_PATH'; executable_relpath = 'Fixture.exe' } } }
    [IO.File]::WriteAllText((Join-Path $shared 'games.json'), ($gamesJson | ConvertTo-Json -Depth 5))

    $zip = [IO.Compression.ZipFile]::Open((Join-Path $vendor 'BepInEx_win_x64.zip'), 'Create')
    try {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $proxy, 'winhttp.dll') | Out-Null
        $entry = $zip.CreateEntry('BepInEx/core/BepInEx.dll')
        $w = New-Object IO.StreamWriter($entry.Open()); try { $w.Write('vendored loader') } finally { $w.Dispose() }
    } finally { $zip.Dispose() }

    foreach ($dll in 'Fixture.dll', 'CameraUnlock.Core.dll') {
        [IO.File]::WriteAllText((Join-Path $plugins $dll), "new $dll")
    }

    [IO.File]::WriteAllText((Join-Path $game 'Fixture.exe'), 'exe')
    New-Item -ItemType Directory -Path (Join-Path $game $managed) | Out-Null
    [IO.File]::WriteAllText((Join-Path $game "$managed\CameraUnlock.ini"), 'player settings')
    [IO.File]::WriteAllText((Join-Path $game "$managed\Fixture.cfg"), 'legacy settings')
    if ($CecilInstall) {
        [IO.File]::WriteAllText((Join-Path $game "$managed\Assembly-CSharp.dll"), "patched $marker bytes")
        [IO.File]::WriteAllText((Join-Path $game "$managed\Assembly-CSharp.dll.original"), $pristine)
        foreach ($dll in 'Fixture.dll', 'CameraUnlock.Core.dll', 'Mono.Cecil.dll', 'HeadTracking.log') {
            [IO.File]::WriteAllText((Join-Path $game "$managed\$dll"), "old $dll")
        }
        $state = "{`r`n  ""schema_version"": 1,`r`n  ""framework"": {`r`n    ""type"": ""MonoCecil"",`r`n    ""installed_by_us"": true`r`n  }`r`n}`r`n"
        [IO.File]::WriteAllText((Join-Path $game '.fixture-state.json'), $state)
    } else {
        [IO.File]::WriteAllText((Join-Path $game "$managed\Assembly-CSharp.dll"), $pristine)
    }
    if ($PlayersBepInEx) {
        New-Item -ItemType Directory -Path (Join-Path $game 'BepInEx\core'), (Join-Path $game 'BepInEx\plugins') | Out-Null
        [IO.File]::WriteAllText((Join-Path $game 'BepInEx\core\BepInEx.dll'), "player's loader")
        [IO.File]::WriteAllText((Join-Path $game 'BepInEx\plugins\Other.dll'), "player's other plugin")
        Copy-Item -LiteralPath $proxy -Destination (Join-Path $game 'winhttp.dll')
    }

    $common = @('@echo off', 'set "WRAPPER_DIR=%~dp0"', 'set "GAME_ID=fixture"', 'set "MOD_DISPLAY_NAME=Fixture"',
        'set "MOD_INTERNAL_NAME=Fixture"', 'set "STATE_FILE=.fixture-state.json"', 'set "FRAMEWORK_TYPE=BepInEx"',
        'set "MOD_DLLS=Fixture.dll CameraUnlock.Core.dll"', 'set "PLUGIN_SUBFOLDER="', 'set "MOD_SEED_FILES="')
    $install = $common + @('set "MOD_VERSION=1.0.0"', 'set "BEPINEX_ARCH=x64"', 'set "BEPINEX_VENDOR_ZIP_NAME="',
        'set "BEPINEX_SUBFOLDER="', 'set "MOD_CONTROLS=Controls:"', 'set "IL2CPP_VENDOR_DIR_NAME="',
        'set "IL2CPP_VENDOR_ZIP_NAME="', 'set "IL2CPP_PLUGIN_DIR_NAME="', 'set "IL2CPP_MOD_DLLS="',
        'call "%~dp0shared\install-body-bepinex.cmd" %*', 'exit /b %errorlevel%')
    $uninstall = $common + @('set "LEGACY_DLLS="',
        ('set "PRESERVE_FILES={0}\CameraUnlock.ini {0}\Fixture.cfg"' -f $managed),
        ('set "MANAGED_SUBFOLDER={0}"' -f $managed), 'set "ASSEMBLY_DLL=Assembly-CSharp.dll"',
        ('set "PATCH_MARKER={0}"' -f $marker), 'set "MANAGED_EXTRAS=Fixture.dll CameraUnlock.Core.dll Mono.Cecil.dll HeadTracking.log"',
        'set "ASI_LOADER_NAME=winmm.dll"', 'set "MOD_LEFTOVERS="', 'set "ROOT_EXTRAS="', 'set "USER_FOLDER_EXTRAS="',
        'set "SHIM_MARKER="', 'set "SHIM_MARKER_ALT="', 'set "ASI_SUBDIR="', 'set "UE4_BINARIES_RELDIR="',
        'call "%~dp0shared\uninstall-body.cmd" %*', 'exit /b %errorlevel%')
    [IO.File]::WriteAllText((Join-Path $caseRoot 'install.cmd'), ($install -join "`r`n") + "`r`n")
    [IO.File]::WriteAllText((Join-Path $caseRoot 'uninstall.cmd'), ($uninstall -join "`r`n") + "`r`n")
    [pscustomobject]@{ Name = $Name; Root = $caseRoot; Game = $game; Runs = 0 }
}

function Invoke-Script($Case, [string]$Script) {
    $Case.Runs++
    $stem = Join-Path $Case.Root ('run{0}' -f $Case.Runs)
    $lines = @('@echo off', ('call "{0}\{1}.cmd" "{2}" /y > "{3}.out" 2>&1' -f $Case.Root, $Script, $Case.Game, $stem),
        ('> "{0}.exit" echo %errorlevel%' -f $stem))
    [IO.File]::WriteAllText("$stem.cmd", ($lines -join "`r`n") + "`r`n")
    $p = Start-Process $env:ComSpec -ArgumentList @('/d', '/c', ('""{0}.cmd""' -f $stem)) -WindowStyle Hidden -PassThru
    if (-not $p.WaitForExit(120000)) { Stop-Process -Id $p.Id -Force; throw "$($Case.Name): $Script timed out" }
    [pscustomobject]@{ Exit = [int](Get-Content "$stem.exit" -Raw).Trim(); Output = (Get-Content "$stem.out" -Raw) }
}

function Text($Case, [string]$Rel) {
    $path = Join-Path $Case.Game $Rel
    if (Test-Path -LiteralPath $path) { (Get-Content -LiteralPath $path -Raw) } else { $null }
}

function Assert-CecilGone($Case) {
    Check $Case.Name ((Text $Case "$managed\Assembly-CSharp.dll") -eq $pristine) 'Assembly-CSharp.dll is not the pristine one'
    Check $Case.Name (-not (Test-Path -LiteralPath (Join-Path $Case.Game "$managed\Assembly-CSharp.dll.original"))) '.original was left behind'
    foreach ($f in 'Fixture.dll', 'CameraUnlock.Core.dll', 'Mono.Cecil.dll', 'HeadTracking.log') {
        Check $Case.Name (-not (Test-Path -LiteralPath (Join-Path $Case.Game "$managed\$f"))) "$managed\$f was left behind"
    }
    Check $Case.Name ((Text $Case "$managed\CameraUnlock.ini") -eq 'player settings') 'CameraUnlock.ini did not survive'
    Check $Case.Name ((Text $Case "$managed\Fixture.cfg") -eq 'legacy settings') 'the legacy config did not survive'
}

try {
    # Standalone upgrade onto a game with no loader: the Cecil install comes off,
    # BepInEx goes on, and the state file says this mod installed it.
    $c = New-Case 'upgrade-no-loader' -CecilInstall
    $r = Invoke-Script $c 'install'
    Check $c.Name ($r.Exit -eq 0) "install exited $($r.Exit): $($r.Output)"
    Assert-CecilGone $c
    Check $c.Name ((Text $c 'BepInEx\plugins\Fixture.dll') -eq 'new Fixture.dll') 'the plugin was not deployed'
    Check $c.Name ((Text $c 'BepInEx\core\BepInEx.dll') -eq 'vendored loader') 'the vendored loader was not installed'
    $state = Text $c '.fixture-state.json'
    Check $c.Name ($state -match '"type": "BepInEx"' -and $state -match '"installed_by_us": true') "state file reads: $state"
    $r = Invoke-Script $c 'uninstall'
    Check $c.Name ($r.Exit -eq 0) "uninstall exited $($r.Exit): $($r.Output)"
    Check $c.Name (-not (Test-Path -LiteralPath (Join-Path $c.Game 'BepInEx\core'))) "the loader this mod installed was not removed; state before uninstall: $state"

    # Standalone upgrade onto a game where the player has their own BepInEx: the
    # old state's installed_by_us=true is about the Cecil patch, not about it.
    $c = New-Case 'upgrade-players-loader' -CecilInstall -PlayersBepInEx
    $r = Invoke-Script $c 'install'
    Check $c.Name ($r.Exit -eq 0) "install exited $($r.Exit): $($r.Output)"
    Assert-CecilGone $c
    Check $c.Name ((Text $c 'BepInEx\core\BepInEx.dll') -eq "player's loader") "the player's loader was replaced or removed"
    Check $c.Name ((Text $c 'BepInEx\plugins\Other.dll') -eq "player's other plugin") "the player's other plugin was touched"
    $state = Text $c '.fixture-state.json'
    Check $c.Name ($state -match '"installed_by_us": false') "state file reads: $state"
    $r = Invoke-Script $c 'uninstall'
    Check $c.Name ($r.Exit -eq 0) "uninstall exited $($r.Exit): $($r.Output)"
    Check $c.Name ((Text $c 'BepInEx\core\BepInEx.dll') -eq "player's loader") "uninstall removed the player's loader"

    # The launcher's route for an install with no receipt: the new release's
    # uninstall.cmd alone, on the old Cecil install beside the player's BepInEx.
    $c = New-Case 'uninstall-cecil-install' -CecilInstall -PlayersBepInEx
    $r = Invoke-Script $c 'uninstall'
    Check $c.Name ($r.Exit -eq 0) "uninstall exited $($r.Exit): $($r.Output)"
    Assert-CecilGone $c
    Check $c.Name ((Text $c 'BepInEx\core\BepInEx.dll') -eq "player's loader") "uninstall removed the player's loader"
    Check $c.Name (-not (Test-Path -LiteralPath (Join-Path $c.Game '.fixture-state.json'))) 'the state file was left behind'

    # A clean game: nothing from Cecil to undo, and the loader this mod installed
    # still comes off, so the new guard has not stopped the ordinary case.
    $c = New-Case 'fresh'
    $r = Invoke-Script $c 'install'
    Check $c.Name ($r.Exit -eq 0) "install exited $($r.Exit): $($r.Output)"
    Check $c.Name ($r.Output -notmatch 'another way') 'a fresh install ran the framework switch'
    $r = Invoke-Script $c 'uninstall'
    Check $c.Name ($r.Exit -eq 0) "uninstall exited $($r.Exit): $($r.Output)"
    Check $c.Name (-not (Test-Path -LiteralPath (Join-Path $c.Game 'BepInEx\core'))) 'the loader this mod installed was not removed'
    Check $c.Name ((Text $c "$managed\Assembly-CSharp.dll") -eq $pristine) 'the untouched assembly changed'
} finally {
    if ($failures.Count -eq 0) { Remove-Item -LiteralPath $root -Recurse -Force }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Host "FAIL $_" -ForegroundColor Red }
    Write-Host "Case folders kept under $root"
    exit 1
}
Write-Host 'loader switch: 4 cases passed' -ForegroundColor Green
