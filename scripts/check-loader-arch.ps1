#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Verify a Windows PE file's machine architecture matches an expected
    value (x86 or x64).
.DESCRIPTION
    Used by install-body-bepinex.cmd to detect when an existing on-disk
    BepInEx loader (winhttp.dll) is the wrong architecture for the game.
    This catches the failure mode where another mod manager - typically
    Thunderstore Mod Manager / r2modman - has dropped the generic x64
    BepInExPack into an x86 Unity 2017 game directory, which silently
    prevents the loader from injecting (a 32-bit process cannot load a
    64-bit DLL). Without this check, install.cmd's "BepInEx already
    present, skipping loader install" branch trusts the dead loader and
    deploys plugins onto it.

    Reads IMAGE_FILE_HEADER.Machine (WORD at PE_offset+4, where
    PE_offset is the DWORD at file offset 0x3C) and maps it to x86 /
    x64 / arm64 / unknown.

.PARAMETER Path
    Absolute path to the PE file under test.
.PARAMETER ExpectedArch
    Either 'x86' or 'x64'.

    The exit code is the whole answer. stdout stays empty: install.cmd calls
    this mid-install and prints its own explanation afterwards, so anything
    written here lands ahead of that text and reads like the installer's own
    output. A failure reason goes to stderr; a match says nothing.

.EXITCODE
    0 - PE machine matches ExpectedArch
    1 - mismatch (PE is a different arch)
    2 - file does not exist
    3 - file is not a valid PE (bad MZ/PE signature, truncated header)
#>
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][ValidateSet('x86','x64')][string]$ExpectedArch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
    [Console]::Error.WriteLine("PE file not found: $Path")
    exit 2
}

$bytes = [System.IO.File]::ReadAllBytes($Path)
if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
    [Console]::Error.WriteLine("Not a PE file (no MZ signature): $Path")
    exit 3
}

$peOff = [BitConverter]::ToInt32($bytes, 0x3C)
if ($peOff -lt 0 -or ($peOff + 6) -gt $bytes.Length -or
    $bytes[$peOff] -ne 0x50 -or $bytes[$peOff + 1] -ne 0x45) {
    [Console]::Error.WriteLine("Invalid PE header in $Path")
    exit 3
}

$machine = [BitConverter]::ToUInt16($bytes, $peOff + 4)
$actual = switch ($machine) {
    0x014C  { 'x86' }
    0x8664  { 'x64' }
    0xAA64  { 'arm64' }
    default { 'unknown' }
}

if ($actual -eq $ExpectedArch) { exit 0 }

[Console]::Error.WriteLine(("PE machine 0x{0:X4} ({1}); expected {2}" -f $machine, $actual, $ExpectedArch))
exit 1
