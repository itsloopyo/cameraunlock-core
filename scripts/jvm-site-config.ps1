#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Writes the site JVM config that makes a game's own launcher start its JVM
    with this mod's Java agent. Called by install-body-javaagent.cmd and by
    Invoke-DevDeployJavaAgent.
.DESCRIPTION
    Some Java games ship a native launcher that reads its JVM arguments from a
    JSON file next to the exe (<Exe>.json: mainClass, classpath, vmArgs) and
    prefers a site file of the same shape (<Exe>.site.json) when one is there.
    The site file replaces the stock file, it is not merged with it, so it has
    to carry everything the stock file does. This writes one that is the stock
    file with three changes:

      mainClass  core's boot class (java/src/com/cameraunlock/core/agent/Boot.java,
                 compiled into the mod's jar), which loads the jar it is in
                 into the running JVM as a Java agent and then starts the
                 game's own main class
      classpath  the stock entries, then each agent jar
      vmArgs     -Dcameraunlock.mainClass=<the stock mainClass>,
                 -XX:+EnableDynamicAgentLoading and
                 --enable-native-access=ALL-UNNAMED, then the stock entries.
                 The last is for the jar's calls into CameraUnlockCore.dll
                 through java.lang.foreign: without it Java 25 prints a
                 warning at the first one and says a later release will
                 refuse them.

    It does not use -javaagent. Such a launcher loads jvm.dll by path, which
    leaves the runtime's bin folder off the DLL search path, and the JVM then
    cannot load instrument.dll at start-up: its imports, jli.dll and java.dll,
    are not found unless that folder happens to be on PATH. Measured on Project
    Zomboid 42.21, where the game exits within a second. A boot class runs
    after the runtime's DLLs are in the process and can load the agent then.

    The stock file is read on every run, so a reinstall after a game update
    picks up whatever the update changed in it.

    A site file that is already there and names none of these jars was written
    by the player or by another mod. It is never overwritten.

    Exit codes:
      0  written (or, with -CheckOnly, safe to write)
      1  the stock config is missing or is not the expected shape
      3  a site config that is not this mod's is already there
.PARAMETER ConfigPath
    The launcher's stock JVM config.
.PARAMETER SitePath
    The site config the launcher reads in preference to it.
.PARAMETER AgentJars
    Space-separated jar filenames, each relative to the launcher's folder.
.PARAMETER MainClass
    The boot class, in the form the stock config names its own. Core's, which
    every Java agent mod compiles into its jar, unless a mod names another.
.PARAMETER CheckOnly
    Run every check and write nothing.
#>
param(
    [Parameter(Mandatory=$true)][string]$ConfigPath,
    [Parameter(Mandatory=$true)][string]$SitePath,
    [Parameter(Mandatory=$true)][string]$AgentJars,
    [string]$MainClass = 'com/cameraunlock/core/agent/Boot',
    [switch]$CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# [Console]::Error rather than Write-Error: with ErrorActionPreference=Stop a
# Write-Error ends the script with exit 1 before the intended code is reached.
function Stop-With {
    param([int]$Code, [string]$Message)
    [Console]::Error.WriteLine($Message)
    exit $Code
}

$jars = @($AgentJars -split ' ' | Where-Object { $_ })
if ($jars.Count -eq 0) { Stop-With 1 'No agent jar was named.' }

if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    Stop-With 1 "The launcher's JVM config was not found: $ConfigPath"
}
$stock = [System.IO.File]::ReadAllText($ConfigPath) | ConvertFrom-Json
$names = if ($stock -is [System.Management.Automation.PSCustomObject]) { @($stock.PSObject.Properties.Name) } else { @() }
if ($names -notcontains 'mainClass' -or $stock.mainClass -isnot [string] -or
    $names -notcontains 'classpath' -or $stock.classpath -isnot [array] -or
    $names -notcontains 'vmArgs' -or $stock.vmArgs -isnot [array]) {
    Stop-With 1 "The launcher's JVM config does not have a mainClass, a classpath array and a vmArgs array: $ConfigPath"
}

if (Test-Path -LiteralPath $SitePath -PathType Leaf) {
    # Read as text: a site file that is not valid JSON is still somebody's, and
    # the question is only whether it names one of our jars.
    $siteText = [System.IO.File]::ReadAllText($SitePath)
    $ours = @($jars | Where-Object { $siteText.Contains($_) }).Count -gt 0
    if (-not $ours) { Stop-With 3 "A site config that does not load this mod is already there: $SitePath" }
}

if ($CheckOnly) { exit 0 }

$ownArgs = @("-Dcameraunlock.mainClass=$($stock.mainClass)", '-XX:+EnableDynamicAgentLoading', '--enable-native-access=ALL-UNNAMED')
$stock.vmArgs = $ownArgs + @($stock.vmArgs | Where-Object { $ownArgs -notcontains $_ })
$stock.classpath = @($stock.classpath | Where-Object { $jars -notcontains $_ }) + $jars
$stock.mainClass = $MainClass
$json = ConvertTo-Json -InputObject $stock -Depth 32
[System.IO.File]::WriteAllText($SitePath, $json + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
exit 0
