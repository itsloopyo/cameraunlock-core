#!/usr/bin/env pwsh
# FileHash.psm1 - SHA-256 of a file, for scripts that run under Windows PowerShell 5.1
# Part of CameraUnlock-Core shared utilities

# Not Get-FileHash: Windows PowerShell 5.1 autoloads it from a script module,
# and a powershell.exe started from pwsh (GitHub Actions' shell: pwsh)
# inherits pwsh's PSModulePath, resolves the Core-only
# Microsoft.PowerShell.Utility first and reports the cmdlet as not recognized.
function Get-FileSha256 {
    [OutputType([string])]
    param([Parameter(Mandatory)][string]$LiteralPath)

    $sha    = [System.Security.Cryptography.SHA256]::Create()
    $stream = [System.IO.File]::OpenRead((Convert-Path -LiteralPath $LiteralPath))
    try {
        return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $sha.Dispose()
    }
}

Export-ModuleMember -Function Get-FileSha256
