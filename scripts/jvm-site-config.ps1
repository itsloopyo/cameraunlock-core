#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Writes the site JVM config that makes a game's own launcher start its JVM
    with this mod's Java agent. Called by install-body-javaagent.cmd.
.DESCRIPTION
    Some Java games ship a native launcher that reads its JVM arguments from a
    JSON file next to the exe (<Exe>.json: mainClass, classpath, vmArgs) and
    prefers a site file of the same shape (<Exe>.site.json) when one is there.
    The site file replaces the stock file, it is not merged with it, so it has
    to carry everything the stock file does. This writes one that is the stock
    file with a -javaagent argument for each agent jar put first in vmArgs.

    The stock file is read on every run, so a reinstall after a game update
    picks up whatever the update changed in it.

    A site file that is already there and does not load one of these jars was
    written by the player or by another mod. It is never overwritten.

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
.PARAMETER CheckOnly
    Run every check and write nothing.
#>
param(
    [Parameter(Mandatory=$true)][string]$ConfigPath,
    [Parameter(Mandatory=$true)][string]$SitePath,
    [Parameter(Mandatory=$true)][string]$AgentJars,
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

$agentArgs = @($AgentJars -split ' ' | Where-Object { $_ } | ForEach-Object { "-javaagent:$_" })
if ($agentArgs.Count -eq 0) { Stop-With 1 'No agent jar was named.' }

if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    Stop-With 1 "The launcher's JVM config was not found: $ConfigPath"
}
$stock = [System.IO.File]::ReadAllText($ConfigPath) | ConvertFrom-Json
if ($stock -isnot [System.Management.Automation.PSCustomObject] -or
    $stock.PSObject.Properties.Name -notcontains 'vmArgs' -or
    $stock.vmArgs -isnot [array]) {
    Stop-With 1 "The launcher's JVM config has no vmArgs array: $ConfigPath"
}

if (Test-Path -LiteralPath $SitePath -PathType Leaf) {
    # Read as text: a site file that is not valid JSON is still somebody's, and
    # the question is only whether it names one of our jars.
    $siteText = [System.IO.File]::ReadAllText($SitePath)
    $ours = @($agentArgs | Where-Object { $siteText.Contains("`"$_`"") }).Count -gt 0
    if (-not $ours) { Stop-With 3 "A site config that does not load this mod is already there: $SitePath" }
}

if ($CheckOnly) { exit 0 }

$stock.vmArgs = @($agentArgs) + @($stock.vmArgs | Where-Object { $agentArgs -notcontains $_ })
$json = ConvertTo-Json -InputObject $stock -Depth 32
[System.IO.File]::WriteAllText($SitePath, $json + "`r`n", (New-Object System.Text.UTF8Encoding($false)))
exit 0
