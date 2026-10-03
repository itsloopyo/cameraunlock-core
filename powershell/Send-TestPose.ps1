#!/usr/bin/env pwsh
#Requires -Version 5.1
# The pose sender Start-TestPoseSender (IsolatedGameTest.psm1) runs: OpenTrack UDP packets to
# 127.0.0.1 at about 60 Hz, easing toward the pose in -PoseFile ("x y z yaw pitch roll",
# centimetres and degrees). One socket for the whole run: a receiver locks onto the first source
# it hears, and a fresh sender per test would be a second source it ignores.
param(
    [Parameter(Mandatory)][int]$Port,
    [Parameter(Mandatory)][string]$PoseFile,
    [int]$Minutes = 120
)

$udp = New-Object System.Net.Sockets.UdpClient
$udp.Connect('127.0.0.1', $Port)
$random = New-Object System.Random
$packet = New-Object byte[] 48
$current = New-Object 'double[]' 6
$goal = New-Object 'double[]' 6
$deadline = (Get-Date).AddMinutes($Minutes)
$lastRead = [datetime]::MinValue
$culture = [Globalization.CultureInfo]::InvariantCulture
# Noise on each packet, in the packet's own units: a receiver holds back the first large step
# until the next packet differs, which a stream of identical ones never does.
$noise = 0.002, 0.002, 0.002, 0.02, 0.02, 0.02

while ((Get-Date) -lt $deadline) {
    if (((Get-Date) - $lastRead).TotalMilliseconds -gt 100) {
        $lastRead = Get-Date
        # The harness rewrites the file while this reads it, so a read that fails or is cut
        # short keeps the pose it had and the next one, a tenth of a second on, picks it up.
        $stream = $null
        try {
            $stream = [IO.File]::Open($PoseFile, [IO.FileMode]::Open, [IO.FileAccess]::Read,
                                      ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
            $reader = New-Object IO.StreamReader $stream
            $parts = [regex]::Split($reader.ReadToEnd().Trim(), '\s+')
            if ($parts.Length -eq 6) {
                $parsed = New-Object 'double[]' 6
                $ok = $true
                for ($i = 0; $i -lt 6; $i++) {
                    # A [ref] to an array element does not write back, so each number lands in a
                    # variable first.
                    $number = 0.0
                    if ([double]::TryParse($parts[$i], [Globalization.NumberStyles]::Float, $culture, [ref]$number)) { $parsed[$i] = $number }
                    else { $ok = $false }
                }
                if ($ok) { $goal = $parsed }
            }
        } catch [IO.IOException] {
        } finally {
            if ($stream) { $stream.Dispose() }
        }
    }
    for ($i = 0; $i -lt 6; $i++) {
        # Eased, so each step stays under a receiver's large-jump threshold.
        $current[$i] += ($goal[$i] - $current[$i]) * 0.10
        $value = $current[$i] + ($random.NextDouble() - 0.5) * $noise[$i]
        [Array]::Copy([BitConverter]::GetBytes([double]$value), 0, $packet, $i * 8, 8)
    }
    [void]$udp.Send($packet, 48)
    Start-Sleep -Milliseconds 16
}
$udp.Close()
