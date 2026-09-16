#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Reports whether a file carries a marker byte sequence. Named for its first
    caller; the shim bodies ask the same question of a DLL.
.DESCRIPTION
    Two callers, two questions of the same bytes. The cecil install/uninstall
    bodies must never capture or trust a backup taken from an already-patched
    Assembly-CSharp.dll - that is how a patched file ends up masquerading as the
    pristine .original and a later uninstall restores a broken assembly. The
    shim bodies ask whether the DLL already sitting at a system DLL's name is
    one of this mod's own builds, which is what decides whether that file is the
    user's original and has to be preserved as <name>.backup.

    findstr is unreliable on multi-MB binaries (line-length limits), so this
    searches for the marker's ASCII or UTF-16LE byte sequence. Native launchers
    can contain only wide strings, unlike their companion DLLs.

    Exit codes:
      0  marker present
      1  marker absent
      2  error (file missing or unreadable)
.PARAMETER AssemblyPath
    Path to the file to inspect.
.PARAMETER Marker
    The marker string (e.g. HeadTracking_Patched_GoneHome_v4).
.PARAMETER AlternateMarker
    Optional identity for a companion payload with different embedded strings.
#>
param(
    [Parameter(Mandatory=$true)][string]$AssemblyPath,
    [Parameter(Mandatory=$true)][string]$Marker,
    [string]$AlternateMarker = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Use [Console]::Error rather than Write-Error: with ErrorActionPreference=Stop
# a Write-Error terminates the script (exit 1) before our `exit 2` runs, which
# the callers would then misread as "clean" (exit 1). Code 2 must be reachable.
if (-not (Test-Path -LiteralPath $AssemblyPath)) {
    [Console]::Error.WriteLine("Assembly not found: $AssemblyPath")
    exit 2
}

try {
    $bytes = [System.IO.File]::ReadAllBytes($AssemblyPath)
} catch {
    [Console]::Error.WriteLine("Failed to read assembly: $($_.Exception.Message)")
    exit 2
}

# Latin-1 maps each byte to one character, so ordinal search also finds wide
# strings at odd offsets without decoding arbitrary binary data as UTF-16.
$binaryEncoding = [System.Text.Encoding]::GetEncoding(28591)
$haystack = $binaryEncoding.GetString($bytes)
foreach ($value in @($Marker, $AlternateMarker)) {
    if (-not $value) { continue }
    foreach ($encoding in @([System.Text.Encoding]::ASCII, [System.Text.Encoding]::Unicode)) {
        $needle = $binaryEncoding.GetString($encoding.GetBytes($value))
        if ($haystack.IndexOf($needle, [StringComparison]::Ordinal) -ge 0) { exit 0 }
    }
}
exit 1
