param([string]$BodiesRoot = $PSScriptRoot)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Join-Path (Split-Path $PSScriptRoot) ('.lab/javaagent-tests-' + [guid]::NewGuid().ToString('N'))
$game = Join-Path $root 'Game ! Folder'
$shared = Join-Path $root 'shared'
$plugins = Join-Path $root 'plugins'
New-Item -ItemType Directory -Path $game, $shared, $plugins | Out-Null

function Assert-File([string]$Path, [string]$Expected) {
    if ([IO.File]::ReadAllText($Path) -ne $Expected) { throw "Unexpected contents: $Path" }
}

function Invoke-Installer([string]$Action, [int]$Expected, [string]$Flags = '/y') {
    $result = Join-Path $root ([guid]::NewGuid().ToString('N') + '.exit')
    $driver = $result + '.cmd'
    $lines = @('@echo off', ('call "{0}\{1}.cmd" "{2}" {3}' -f $root, $Action, $game, $Flags), ('> "{0}" echo %errorlevel%' -f $result))
    [IO.File]::WriteAllText($driver, ($lines -join "`r`n") + "`r`n")
    $process = Start-Process $env:ComSpec -ArgumentList @('/d', '/c', ('""{0}""' -f $driver)) -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(30000)) {
        Stop-Process -Id $process.Id -Force
        throw "Timed out: $Action $Flags in $root"
    }
    $actual = [int][IO.File]::ReadAllText($result).Trim()
    if ($actual -ne $Expected) { throw "$Action returned $actual, expected $Expected in $root" }
}

foreach ($name in @('install-body-javaagent.cmd', 'uninstall-body.cmd', 'jvm-site-config.ps1', 'cecil-marker-check.ps1', 'restore-kept-configs.ps1')) {
    Copy-Item -LiteralPath (Join-Path $BodiesRoot $name) -Destination $shared
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'find-game.ps1') -Destination $shared
Copy-Item -LiteralPath (Join-Path $PSScriptRoot '../powershell/GamePathDetection.psm1') -Destination $shared
[IO.File]::WriteAllText((Join-Path $shared 'games.json'), '{"schema_version":1,"games":{"fixture":{"display_name":"Fixture","env_var":"CUL_FIXTURE_PATH","executable_relpath":"Fixture64.exe"}}}')
[IO.File]::WriteAllText((Join-Path $game 'Fixture64.exe'), '')
foreach ($action in @('install', 'uninstall')) {
    $body = if ($action -eq 'install') { 'install-body-javaagent.cmd' } else { 'uninstall-body.cmd' }
    $wrapper = @"
@echo off
setlocal disabledelayedexpansion
set "WRAPPER_DIR=%~dp0"
set "GAME_ID=fixture"
set "MOD_DISPLAY_NAME=Fixture"
set "MOD_INTERNAL_NAME=Fixture"
set "MOD_VERSION=1.0.0"
set "STATE_FILE=.fixture-state.json"
set "FRAMEWORK_TYPE=JavaAgent"
set "MOD_DLLS=Fixture.jar Fixture.dll"
set "LEGACY_DLLS=OldFixture.jar OldFixture.dll"
set "MOD_SEED_FILES="
set "PRESERVE_FILES="
set "USER_FOLDER_EXTRAS="
call "%~dp0shared\$body" %*
exit /b %errorlevel%
"@
    [IO.File]::WriteAllText((Join-Path $root "$action.cmd"), $wrapper.Replace("`r`n", "`n").Replace("`n", "`r`n"))
}

$jar = Join-Path $game 'Fixture.jar'
$stock = Join-Path $game 'Fixture64.json'
$site = Join-Path $game 'Fixture64.site.json'
$state = Join-Path $game '.fixture-state.json'
$dll = Join-Path $game 'Fixture.dll'
[IO.File]::WriteAllText((Join-Path $plugins 'Fixture.jar'), 'jar v1')
[IO.File]::WriteAllText((Join-Path $plugins 'Fixture.dll'), 'dll v1')

Invoke-Installer install 1
if (@(Get-ChildItem -LiteralPath $game -Force).Count -ne 1) { throw 'An install with no stock JVM config changed the game folder' }

$stockJson = '{"mainClass":"a/Main","classpath":[".","game.jar"],"vmArgs":["-Xmx1g"],"windows":{"10.0":{"vmArgs":["-XX:+UseZGC"]}}}'
[IO.File]::WriteAllText($stock, $stockJson)
Invoke-Installer install 0
Assert-File $jar 'jar v1'
Assert-File $dll 'dll v1'
Assert-File $stock $stockJson
$written = [IO.File]::ReadAllText($site) | ConvertFrom-Json
if (($written.vmArgs -join '|') -ne '-Dcameraunlock.mainClass=a/Main|-XX:+EnableDynamicAgentLoading|--enable-native-access=ALL-UNNAMED|-Xmx1g') { throw "Unexpected vmArgs: $($written.vmArgs -join ' ')" }
if ($written.mainClass -ne 'com/cameraunlock/core/agent/Boot') { throw "The site config does not name core's boot class: $($written.mainClass)" }
if (($written.classpath -join '|') -ne '.|game.jar|Fixture.jar') { throw "Unexpected classpath: $($written.classpath -join ' ')" }
if (($written.windows.'10.0'.vmArgs -join '|') -ne '-XX:+UseZGC') { throw 'The site config lost the per-version vmArgs' }
if (-not ([IO.File]::ReadAllText($state)).Contains('"type": "JavaAgent"')) { throw 'The state file does not name JavaAgent' }

# A reinstall after a game update rereads the stock config and keeps one agent argument.
[IO.File]::WriteAllText($stock, $stockJson.Replace('-Xmx1g', '-Xmx2g'))
[IO.File]::WriteAllText((Join-Path $plugins 'Fixture.jar'), 'jar v2')
Invoke-Installer install 0
Assert-File $jar 'jar v2'
$written = [IO.File]::ReadAllText($site) | ConvertFrom-Json
if (($written.vmArgs -join '|') -ne '-Dcameraunlock.mainClass=a/Main|-XX:+EnableDynamicAgentLoading|--enable-native-access=ALL-UNNAMED|-Xmx2g') { throw "Unexpected vmArgs after reinstall: $($written.vmArgs -join ' ')" }
if (($written.classpath -join '|') -ne '.|game.jar|Fixture.jar') { throw "Unexpected classpath after reinstall: $($written.classpath -join ' ')" }

Invoke-Installer uninstall 0
if ((Test-Path -LiteralPath $jar) -or (Test-Path -LiteralPath $dll) -or (Test-Path -LiteralPath $site) -or (Test-Path -LiteralPath $state)) { throw 'Uninstall left the jar, the DLL, the site config or the state file' }
if (-not (Test-Path -LiteralPath $stock)) { throw 'Uninstall removed the stock JVM config' }

# An older version's layout: the jar, a DLL under a name this version does not use, and a site
# config naming a boot class of the mod's own. The new version installed over it leaves neither.
$oldDll = Join-Path $game 'OldFixture.dll'
[IO.File]::WriteAllText($jar, 'old jar')
[IO.File]::WriteAllText($oldDll, 'old dll')
[IO.File]::WriteAllText($site, '{"mainClass":"fixture/Boot","classpath":[".","game.jar","Fixture.jar"],"vmArgs":["-Dcameraunlock.mainClass=a/Main","-XX:+EnableDynamicAgentLoading","-Xmx2g"]}')
Invoke-Installer install 0
if (Test-Path -LiteralPath $oldDll) { throw 'An install over an older layout left its DLL beside the new one' }
Assert-File $jar 'jar v2'
Assert-File $dll 'dll v1'
$written = [IO.File]::ReadAllText($site) | ConvertFrom-Json
if ($written.mainClass -ne 'com/cameraunlock/core/agent/Boot') { throw "An install over an older layout kept its boot class: $($written.mainClass)" }
if (($written.classpath -join '|') -ne '.|game.jar|Fixture.jar') { throw "An install over an older layout kept its jar on the classpath: $($written.classpath -join ' ')" }
if (($written.vmArgs -join '|') -ne '-Dcameraunlock.mainClass=a/Main|-XX:+EnableDynamicAgentLoading|--enable-native-access=ALL-UNNAMED|-Xmx2g') { throw "Unexpected vmArgs over an older layout: $($written.vmArgs -join ' ')" }
# An update whose DLL cannot be copied leaves the older jar and its site config as they were:
# a new jar under a site config that names the older one's boot class would not start.
Invoke-Installer uninstall 0
[IO.File]::WriteAllText($jar, 'old jar')
$olderSite = '{"mainClass":"fixture/Boot","classpath":[".","game.jar","Fixture.jar"],"vmArgs":["-Xmx2g"]}'
[IO.File]::WriteAllText($site, $olderSite)
[IO.File]::WriteAllText($dll, 'held')
$held = [IO.File]::Open($dll, 'Open', 'Read', 'None')
try { Invoke-Installer install 1 } finally { $held.Dispose() }
Assert-File $jar 'old jar'
Assert-File $site $olderSite
Invoke-Installer install 0
Assert-File $jar 'jar v2'

Invoke-Installer uninstall 0
$left = @(Get-ChildItem -LiteralPath $game -Force | ForEach-Object Name | Sort-Object)
if (($left -join '|') -ne 'Fixture64.exe|Fixture64.json') { throw "Old layout, new install, uninstall left: $($left -join ', ')" }

# A site config that names only a jar from an older release is still this mod's.
[IO.File]::WriteAllText($site, '{"mainClass":"a/Main","classpath":[],"vmArgs":["-javaagent:OldFixture.jar"]}')
Invoke-Installer uninstall 0
if (Test-Path -LiteralPath $site) { throw 'Uninstall left a site config that names a legacy jar' }

$foreign = '{"mainClass":"a/Main","classpath":[],"vmArgs":["-Xmx8g"]}'
[IO.File]::WriteAllText($site, $foreign)
Invoke-Installer install 1
Assert-File $site $foreign
if ((Test-Path -LiteralPath $jar) -or (Test-Path -LiteralPath $state)) { throw 'A refused install deployed files' }
[IO.File]::WriteAllText($jar, 'left by hand')
Invoke-Installer uninstall 0
Assert-File $site $foreign
if (Test-Path -LiteralPath $jar) { throw 'Uninstall left the jar beside a site config that is not ours' }
Remove-Item -LiteralPath $site

Invoke-Installer install 0
$handle = [IO.File]::Open($site, 'Open', 'Read', 'None')
try { Invoke-Installer uninstall 1 } finally { $handle.Dispose() }
if (-not (Test-Path -LiteralPath $jar) -or -not (Test-Path -LiteralPath $state)) { throw 'An uninstall that could not read the site config removed the jar or the state file' }
Invoke-Installer uninstall 0
Invoke-Installer install 2 '--unknown /y'
Write-Host 'PASS javaagent: stock config required, site config written with the boot class of core and refreshed, native file beside the jar, older layout replaced, legacy and foreign site configs, locked site config, exit codes'
Write-Host "Fixtures retained at $root"
