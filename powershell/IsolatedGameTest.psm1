#!/usr/bin/env pwsh
# IsolatedGameTest.psm1 - run a game for a mod test in the background.
# Part of CameraUnlock-Core shared utilities.
#
# The person at the machine keeps their mouse, keyboard and foreground window
# while a test runs. The game takes its input from the mod's dev build
# (cameraunlock/dev/isolated_input.h), this module writes the command file that
# build plays, and captures come from PrintWindow, which reads a covered window.
#
# There is deliberately no function here that sends real keyboard or mouse
# input, and none that brings the game to the foreground. A game this cannot
# drive yet is a gap to close in core (data/isolated-input.json says how), not
# a reason to take the machine over.
#
# What stays in the mod's own .lab: how to get from the title screen into a
# save, the game's cheats, and what to look for in the captures.

$ErrorActionPreference = 'Stop'

$script:CoverageFile = Join-Path (Split-Path -Parent $PSScriptRoot) 'data\isolated-input.json'
$script:CommandFileName = 'CameraUnlockInput.txt'
$script:HostDllName = 'CameraUnlockIsolatedInput.dll'
$script:HostDllBuild = Join-Path (Split-Path -Parent $PSScriptRoot) "cpp\tools\isolated_input_host\build\Release\$script:HostDllName"

if (-not ('CameraUnlockIsolatedTest.Native' -as [type])) {
    Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;
namespace CameraUnlockIsolatedTest {
    public static class Native {
        [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
        [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hWnd);
        [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
        [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
        [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
        [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }

        public static uint ForegroundPid() { uint pid; GetWindowThreadProcessId(GetForegroundWindow(), out pid); return pid; }

        // PW_RENDERFULLCONTENT: the window's own content, whatever covers it.
        public static Bitmap Capture(IntPtr hWnd) {
            RECT r;
            if (!GetWindowRect(hWnd, out r)) return null;
            int w = r.Right - r.Left, h = r.Bottom - r.Top;
            if (w <= 0 || h <= 0) return null;
            Bitmap bmp = new Bitmap(w, h, System.Drawing.Imaging.PixelFormat.Format32bppArgb);
            using (Graphics g = Graphics.FromImage(bmp)) {
                IntPtr hdc = g.GetHdc();
                bool ok = PrintWindow(hWnd, hdc, 2);
                g.ReleaseHdc(hdc);
                if (!ok) { bmp.Dispose(); return null; }
            }
            return bmp;
        }
    }
}
'@
}

# ---------------------------------------------------------------------------
# What a game reads its input through
# ---------------------------------------------------------------------------

function Get-PeImports {
    <#
    .SYNOPSIS
    The DLLs a PE file imports and the functions it imports from each by name, delay imports
    included. A file that cannot be opened or is not a PE throws.
    #>
    param([Parameter(Mandatory)][string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40 -or [BitConverter]::ToUInt16($bytes, 0) -ne 0x5A4D) { throw "$Path is not a PE file" }
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    if ([BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550) { throw "$Path has no PE signature" }
    $sectionCount = [BitConverter]::ToUInt16($bytes, $pe + 6)
    $optionalSize = [BitConverter]::ToUInt16($bytes, $pe + 20)
    $optional = $pe + 24
    $is64 = [BitConverter]::ToUInt16($bytes, $optional) -eq 0x20B
    $directories = $optional + $(if ($is64) { 112 } else { 96 })
    $sections = for ($i = 0; $i -lt $sectionCount; $i++) {
        $o = $optional + $optionalSize + $i * 40
        [pscustomobject]@{
            Rva = [BitConverter]::ToUInt32($bytes, $o + 12); VirtualSize = [BitConverter]::ToUInt32($bytes, $o + 8)
            Raw = [BitConverter]::ToUInt32($bytes, $o + 20); RawSize = [BitConverter]::ToUInt32($bytes, $o + 16)
        }
    }
    $offsetOf = {
        param([uint32]$rva)
        foreach ($s in $sections) {
            $size = [Math]::Max($s.VirtualSize, $s.RawSize)
            if ($rva -ge $s.Rva -and $rva -lt $s.Rva + $size) { return [int]($s.Raw + $rva - $s.Rva) }
        }
        return -1
    }
    $stringAt = {
        param([int]$offset)
        $end = [Array]::IndexOf($bytes, [byte]0, $offset)
        [Text.Encoding]::ASCII.GetString($bytes, $offset, $end - $offset)
    }
    $thunkSize = $(if ($is64) { 8 } else { 4 })
    $namesAt = {
        param([uint32]$thunkRva)
        $names = New-Object System.Collections.Generic.List[string]
        $t = & $offsetOf $thunkRva
        while ($t -ge 0) {
            $value = $(if ($is64) { [BitConverter]::ToUInt64($bytes, $t) } else { [uint64][BitConverter]::ToUInt32($bytes, $t) })
            if ($value -eq 0) { break }
            $byOrdinal = $(if ($is64) { ($value -shr 63) -ne 0 } else { ($value -band 0x80000000) -ne 0 })
            if (-not $byOrdinal) {
                $hint = & $offsetOf ([uint32]($value -band 0x7FFFFFFF))
                if ($hint -ge 0) { $names.Add((& $stringAt ($hint + 2))) }
            }
            $t += $thunkSize
        }
        , $names.ToArray()
    }

    $imports = New-Object System.Collections.Generic.List[object]
    $importRva = [BitConverter]::ToUInt32($bytes, $directories + 8)
    $o = $(if ($importRva) { & $offsetOf $importRva } else { -1 })
    while ($o -ge 0) {
        $lookup = [BitConverter]::ToUInt32($bytes, $o)
        $nameRva = [BitConverter]::ToUInt32($bytes, $o + 12)
        $firstThunk = [BitConverter]::ToUInt32($bytes, $o + 16)
        if ($nameRva -eq 0) { break }
        $thunk = $(if ($lookup) { $lookup } else { $firstThunk })
        $imports.Add([pscustomobject]@{ Dll = (& $stringAt (& $offsetOf $nameRva)); Functions = (& $namesAt $thunk) })
        $o += 20
    }
    $delayRva = [BitConverter]::ToUInt32($bytes, $directories + 13 * 8)
    $o = $(if ($delayRva) { & $offsetOf $delayRva } else { -1 })
    while ($o -ge 0) {
        $nameRva = [BitConverter]::ToUInt32($bytes, $o + 4)
        $nameTable = [BitConverter]::ToUInt32($bytes, $o + 16)
        if ($nameRva -eq 0) { break }
        $imports.Add([pscustomobject]@{ Dll = (& $stringAt (& $offsetOf $nameRva)); Functions = (& $namesAt $nameTable) })
        $o += 32
    }
    , $imports.ToArray()
}

function Get-IsolatedInputCoverage {
    <#
    .SYNOPSIS
    data/isolated-input.json: the input paths and mod kinds isolated input can drive, and what to
    build for each one it cannot.
    #>
    Get-Content $script:CoverageFile -Raw | ConvertFrom-Json
}

function Get-GameInputPaths {
    <#
    .SYNOPSIS
    The input paths a game binary reads, from its import table, each with whether isolated input
    drives it yet.
    .DESCRIPTION
    Pass the binary that reads the input: the game exe for most native engines, UnityPlayer.dll for a
    Unity game. An import table says what a binary can call, not what it does call, so the answer is
    where to start and the watched proof run (Test-IsolatedInputProof) is what settles it.
    #>
    param([Parameter(Mandatory)][string]$BinaryPath)
    $imports = Get-PeImports -Path $BinaryPath
    $dlls = @($imports | ForEach-Object { $_.Dll.ToLowerInvariant() })
    $functions = @($imports | ForEach-Object { $_.Functions })
    $coverage = Get-IsolatedInputCoverage
    foreach ($entry in $coverage.input_paths.PSObject.Properties) {
        $path = $entry.Value
        $evidence = @()
        if ($path.PSObject.Properties['imports']) { $evidence += @($path.imports | Where-Object { $functions -contains $_ }) }
        if ($path.PSObject.Properties['dlls']) { $evidence += @($path.dlls | Where-Object { $dlls -contains $_ }) }
        if ($evidence.Count -eq 0) { continue }
        [pscustomobject]@{
            Path     = $entry.Name
            Status   = $path.status
            Evidence = $evidence -join ', '
            What     = $path.what
            Build    = $(if ($path.PSObject.Properties['build']) { $path.build } else { $null })
        }
    }
}

function Assert-IsolatedInputCovers {
    <#
    .SYNOPSIS
    Throws, naming what to build in core, unless isolated input can drive this game through this kind
    of mod. Returns the input paths found when it can.
    .DESCRIPTION
    Call it before the first in-game test of a title. The error is the work order: build what it
    names in cameraunlock-core, prove it in the game, change the entry in data/isolated-input.json to
    supported, and then test. It is never answered by sending real keyboard or mouse input.
    #>
    param(
        [Parameter(Mandatory)][string]$BinaryPath,
        [Parameter(Mandatory)][ValidateSet('native', 'managed', 'script')][string]$ModHost
    )
    $coverage = Get-IsolatedInputCoverage
    $modHostEntry = $coverage.mod_hosts.$ModHost
    $gaps = New-Object System.Collections.Generic.List[string]
    if ($modHostEntry.status -ne 'supported') {
        $gaps.Add("a $ModHost mod ($($modHostEntry.what)) cannot host isolated input yet. Build: $($modHostEntry.build)")
    }
    $paths = @(Get-GameInputPaths -BinaryPath $BinaryPath)
    $usable = @($paths | Where-Object { $_.Status -eq 'supported' })
    foreach ($p in $paths | Where-Object { $_.Status -eq 'unsupported' }) {
        $gaps.Add("$BinaryPath reads $($p.Path) ($($p.Evidence)): $($p.What). Build: $($p.Build)")
    }
    if ($paths.Count -eq 0) {
        $gaps.Add("$BinaryPath imports no input API this module knows. If the engine reads input in another binary (UnityPlayer.dll, an engine DLL), pass that one. Otherwise find what it reads, add the path to data/isolated-input.json and its detours to cameraunlock/dev/isolated_input.h.")
    }
    # An unsupported path beside a supported one may be the one the game really uses, so it is
    # reported and the proof run decides. Nothing usable at all is where this stops.
    if ($modHostEntry.status -ne 'supported' -or $usable.Count -eq 0) {
        throw ("Isolated input does not cover this game yet. Do not test it with the real keyboard and mouse: close the gap in cameraunlock-core first (docs/isolated-input.md, 'A game it does not cover yet').`n - " + ($gaps -join "`n - "))
    }
    foreach ($gap in $gaps) { Write-Warning $gap }
    $paths
}

# ---------------------------------------------------------------------------
# The test port
# ---------------------------------------------------------------------------

function Get-ModTestPort {
    <#
    .SYNOPSIS
    The UDP port a mod is tested on, so tests of different mods never share one and never use 4242:
    5000 plus the sum of the decimal digits in the SHA-256 hex digest of the repo folder name without
    its -headtracking suffix, then the next port up that nothing is bound to.
    #>
    param([Parameter(Mandatory)][string]$RepoName)
    $name = $RepoName -replace '-headtracking$', ''
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($name)) } finally { $sha.Dispose() }
    $hex = -join ($digest | ForEach-Object { $_.ToString('x2') })
    $port = 5000
    foreach ($c in $hex.ToCharArray()) { if ($c -ge '0' -and $c -le '9') { $port += [int][string]$c } }
    while (Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue) { $port++ }
    $port
}

# ---------------------------------------------------------------------------
# Running the game
# ---------------------------------------------------------------------------

function Copy-IsolatedInputHost {
    <#
    .SYNOPSIS
    Copies the isolated-input host DLL (cpp/tools/isolated_input_host) into a mod folder, for a mod
    with no native code of its own: a C# mod's dev build loads it from there through
    CameraUnlock.Core.Dev.IsolatedInput.StartIfAsked. Returns the path of the copy.
    .DESCRIPTION
    The default source is the x64 build `pixi run build-isolated-input-host` leaves in this core
    checkout. Pass -HostDll for a build made elsewhere, such as a 32-bit one for a 32-bit game.
    #>
    param([Parameter(Mandatory)][string]$ModFolder, [string]$HostDll = $script:HostDllBuild)
    if (-not (Test-Path $HostDll -PathType Leaf)) {
        throw "The isolated-input host DLL is not built: $HostDll. Build it in cameraunlock-core with: cmake -S cpp/tools/isolated_input_host -B cpp/tools/isolated_input_host/build; cmake --build cpp/tools/isolated_input_host/build --config Release (pixi run build-isolated-input-host)."
    }
    if (-not (Test-Path $ModFolder -PathType Container)) { throw "The mod folder does not exist: $ModFolder" }
    $target = Join-Path $ModFolder $script:HostDllName
    Copy-Item $HostDll $target -Force
    $target
}

function Start-IsolatedGame {
    <#
    .SYNOPSIS
    Starts a game for a background test and hands the foreground back to whatever had it.
    .DESCRIPTION
    Writes the command file beside the mod (its presence is what switches a dev build's isolated
    input on), launches, waits for the process, and for -SettleSeconds returns the foreground to the
    window that held it each time the new game window takes it. Returns the session the other
    functions take. Refuses to start while the game is already running: that one is someone else's.
    With -ModHost managed it also copies the host DLL beside the mod (Copy-IsolatedInputHost), and
    Stop-IsolatedGame removes that copy.
    #>
    param(
        [Parameter(Mandatory)][string]$ProcessName,
        # A URI (steam://rungameid/...), a shell:AppsFolder path or an executable.
        [Parameter(Mandatory)][string]$Launch,
        # The folder the mod's DLL is deployed to.
        [Parameter(Mandatory)][string]$ModFolder,
        [int]$StartTimeoutSeconds = 120,
        [int]$SettleSeconds = 60,
        # native: the mod's own dev build holds the detours. managed: a C# mod, which loads the host DLL.
        [ValidateSet('native', 'managed')][string]$ModHost = 'native',
        # The host DLL to copy for a managed mod, when it is not this checkout's x64 build.
        [string]$HostDll = $script:HostDllBuild
    )
    if (Get-Process -Name $ProcessName -ErrorAction SilentlyContinue) {
        throw "$ProcessName is already running and this session did not start it. Not launching over it."
    }
    $deployedHost = $(if ($ModHost -eq 'managed') { Copy-IsolatedInputHost -ModFolder $ModFolder -HostDll $HostDll } else { $null })
    $commandFile = Join-Path $ModFolder $script:CommandFileName
    Set-Content -Path $commandFile -Value '0' -Encoding ASCII
    Remove-Item "$commandFile.done" -ErrorAction SilentlyContinue

    $foreground = [CameraUnlockIsolatedTest.Native]::GetForegroundWindow()
    Start-Process $Launch
    $deadline = (Get-Date).AddSeconds($StartTimeoutSeconds)
    $process = $null
    while ((Get-Date) -lt $deadline -and -not $process) {
        $process = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $process) { Start-Sleep -Milliseconds 500 }
    }
    if (-not $process) { throw "$ProcessName did not start within $StartTimeoutSeconds seconds of launching $Launch" }

    $settled = (Get-Date).AddSeconds($SettleSeconds)
    while ((Get-Date) -lt $settled) {
        Start-Sleep -Milliseconds 500
        if ([CameraUnlockIsolatedTest.Native]::IsWindow($foreground) -and
            [CameraUnlockIsolatedTest.Native]::GetForegroundWindow() -ne $foreground) {
            [void][CameraUnlockIsolatedTest.Native]::SetForegroundWindow($foreground)
        }
    }
    [pscustomobject]@{ ProcessId = $process.Id; ProcessName = $ProcessName; CommandFile = $commandFile; Sequence = 0; HostDll = $deployedHost }
}

function Invoke-GameInput {
    <#
    .SYNOPSIS
    Plays input commands inside the game (cameraunlock/dev/input_script.h is the language) and waits
    for the mod to report them played.
    .DESCRIPTION
    Samples the real foreground for the whole of the script. The result's GameHeldForeground is true
    when the game ever had it: the real keyboard and mouse reach a foreground game, so that run
    proves nothing about what the script did and is repeated. Throws when the mod reports a line
    that did not parse or does not answer in time.
    #>
    param(
        [Parameter(Mandatory)]$Session,
        [Parameter(Mandatory)][string[]]$Commands,
        [int]$TimeoutSeconds = 60
    )
    if (-not (Get-Process -Id $Session.ProcessId -ErrorAction SilentlyContinue)) { throw "the game (pid $($Session.ProcessId)) is no longer running" }
    $Session.Sequence++
    $sequence = "$($Session.Sequence)"
    $temp = "$($Session.CommandFile).tmp"
    [IO.File]::WriteAllLines($temp, @($sequence) + $Commands)
    $heldForeground = [CameraUnlockIsolatedTest.Native]::ForegroundPid() -eq $Session.ProcessId
    Move-Item $temp $Session.CommandFile -Force
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $done = $null
    while ((Get-Date) -lt $deadline) {
        if ([CameraUnlockIsolatedTest.Native]::ForegroundPid() -eq $Session.ProcessId) { $heldForeground = $true }
        $done = Get-Content "$($Session.CommandFile).done" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($done -and $done.StartsWith($sequence)) { break }
        Start-Sleep -Milliseconds 50
    }
    if (-not ($done -and $done.StartsWith($sequence))) {
        throw "script $sequence was not played within $TimeoutSeconds seconds. Is the dev build deployed, and was the command file there when the game started?"
    }
    if ($done -ne $sequence) { throw "script $done" }
    [pscustomobject]@{ Sequence = $Session.Sequence; GameHeldForeground = $heldForeground }
}

function Save-GameCapture {
    <#
    .SYNOPSIS
    Saves the game window as a PNG, covered or not, at 1/Scale size.
    #>
    param([Parameter(Mandatory)]$Session, [Parameter(Mandatory)][string]$Path, [int]$Scale = 1)
    $process = Get-Process -Id $Session.ProcessId -ErrorAction Stop
    $bitmap = [CameraUnlockIsolatedTest.Native]::Capture($process.MainWindowHandle)
    if (-not $bitmap) { throw "the game window could not be captured" }
    try {
        if ($Scale -gt 1) {
            $small = New-Object System.Drawing.Bitmap ([int]($bitmap.Width / $Scale)), ([int]($bitmap.Height / $Scale))
            $graphics = [System.Drawing.Graphics]::FromImage($small)
            $graphics.InterpolationMode = 'HighQualityBicubic'
            $graphics.DrawImage($bitmap, 0, 0, $small.Width, $small.Height)
            $graphics.Dispose()
            $small.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
            $small.Dispose()
        } else {
            $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
        }
    } finally { $bitmap.Dispose() }
    $Path
}

function Test-CaptureDiffers {
    <#
    .SYNOPSIS
    True when two captures differ in more than Threshold of the pixels sampled.
    .DESCRIPTION
    A capture that never changes while the game should be moving is a broken instrument, not an
    answer: PrintWindow can return one frame over and over for some renderers. Check it once per
    title before trusting any capture.
    #>
    param([Parameter(Mandatory)][string]$First, [Parameter(Mandatory)][string]$Second, [double]$Threshold = 0.002)
    $a = [System.Drawing.Bitmap]::FromFile($First)
    $b = [System.Drawing.Bitmap]::FromFile($Second)
    try {
        if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) { return $true }
        $sampled = 0; $different = 0
        for ($y = 0; $y -lt $a.Height; $y += 16) {
            for ($x = 0; $x -lt $a.Width; $x += 16) {
                $sampled++
                $p = $a.GetPixel($x, $y); $q = $b.GetPixel($x, $y)
                if ([Math]::Abs($p.R - $q.R) + [Math]::Abs($p.G - $q.G) + [Math]::Abs($p.B - $q.B) -gt 24) { $different++ }
            }
        }
        return ($different / $sampled) -gt $Threshold
    } finally { $a.Dispose(); $b.Dispose() }
}

function Test-IsolatedInputProof {
    <#
    .SYNOPSIS
    The proof a title owes before its tests count: a scripted input changes what the game shows while
    the game never holds the real foreground.
    .DESCRIPTION
    Pass commands the game visibly reacts to from where it is (open a menu, turn the view). Captures
    before and after into -Folder. Returns Proven, and why not when it is not.
    #>
    param(
        [Parameter(Mandatory)]$Session,
        [Parameter(Mandatory)][string[]]$Commands,
        [Parameter(Mandatory)][string]$Folder
    )
    New-Item -ItemType Directory -Force $Folder | Out-Null
    $before = Save-GameCapture -Session $Session -Path (Join-Path $Folder 'proof-before.png')
    $played = Invoke-GameInput -Session $Session -Commands $Commands
    $after = Save-GameCapture -Session $Session -Path (Join-Path $Folder 'proof-after.png')
    $changed = Test-CaptureDiffers -First $before -Second $after
    $reason = $(if ($played.GameHeldForeground) { 'the game held the real foreground during the run, so real input could have done it: repeat' }
                elseif (-not $changed) { 'the game showed no change: it did not take the input, or the capture is not live' }
                else { $null })
    [pscustomobject]@{ Proven = (-not $reason); Reason = $reason; Before = $before; After = $after }
}

function Stop-IsolatedGame {
    <#
    .SYNOPSIS
    Stops the game this session started, by process id, and removes the command file so the next
    start of the same build answers to the real keyboard. Removes the host DLL too when the session
    copied one. The host's log (CameraUnlockIsolatedInput.log beside it) is left to be read.
    #>
    param([Parameter(Mandatory)]$Session)
    $process = Get-Process -Id $Session.ProcessId -ErrorAction SilentlyContinue
    if ($process -and $process.ProcessName -eq $Session.ProcessName) {
        Stop-Process -Id $Session.ProcessId -Force
        Wait-Process -Id $Session.ProcessId -ErrorAction SilentlyContinue
    }
    Remove-Item $Session.CommandFile, "$($Session.CommandFile).done" -ErrorAction SilentlyContinue
    if ($Session.HostDll) { Remove-Item $Session.HostDll -Force }
}

# ---------------------------------------------------------------------------
# The files a test changes
# ---------------------------------------------------------------------------

function Save-GameTestState {
    <#
    .SYNOPSIS
    Copies the files a test is about to change (the deployed mod, its config, the game's settings)
    into -Folder and records which of them did not exist, so Restore-GameTestState puts every one
    back exactly, absent ones included. Refuses a folder that already holds a saved state: that
    one has not been restored yet.
    #>
    param([Parameter(Mandatory)][string[]]$Files, [Parameter(Mandatory)][string]$Folder)
    $index = Join-Path $Folder 'state.json'
    if (Test-Path $index) { throw "$Folder already holds a saved state that was never restored. Restore it first." }
    New-Item -ItemType Directory -Force $Folder | Out-Null
    $entries = for ($i = 0; $i -lt $Files.Count; $i++) {
        $existed = Test-Path $Files[$i]
        if ($existed) { Copy-Item $Files[$i] (Join-Path $Folder "$i.bak") -Force }
        [pscustomobject]@{ Path = $Files[$i]; Existed = $existed; Copy = "$i.bak" }
    }
    ConvertTo-Json @($entries) | Set-Content $index -Encoding UTF8
}

function Restore-GameTestState {
    <#
    .SYNOPSIS
    Puts back every file Save-GameTestState recorded, deletes the ones that did not exist before,
    and removes the saved state.
    #>
    param([Parameter(Mandatory)][string]$Folder)
    $index = Join-Path $Folder 'state.json'
    # Windows PowerShell hands a JSON array back as one object, so it is unrolled here.
    $entries = @((Get-Content $index -Raw | ConvertFrom-Json) | ForEach-Object { $_ })
    foreach ($entry in $entries) {
        if ($entry.Existed) { Copy-Item (Join-Path $Folder $entry.Copy) $entry.Path -Force }
        else { Remove-Item $entry.Path -Force -ErrorAction SilentlyContinue }
    }
    Remove-Item $Folder -Recurse -Force
}

# ---------------------------------------------------------------------------
# Head poses
# ---------------------------------------------------------------------------

function Start-TestPoseSender {
    <#
    .SYNOPSIS
    Starts a hidden process that sends OpenTrack packets to the mod's test port at about 60 Hz,
    easing toward the pose in -PoseFile ("x y z yaw pitch roll": centimetres and degrees). Returns
    its process id, which is what to stop it by.
    .DESCRIPTION
    One long-lived sender per session: a receiver locks onto the first source it hears. The pose is
    eased and carries a little noise, because a stream of identical packets after a large step is
    what a frozen tracker looks like.
    #>
    param([Parameter(Mandatory)][int]$Port, [Parameter(Mandatory)][string]$PoseFile, [int]$Minutes = 120)
    if ($Port -eq 4242) { throw 'port 4242 is the one real trackers send to; test on Get-ModTestPort' }
    if (-not (Test-Path $PoseFile)) { Set-Content -Path $PoseFile -Value '0 0 0 0 0 0' -Encoding ASCII }
    $script = Join-Path $PSScriptRoot 'Send-TestPose.ps1'
    $process = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$script`"", '-Port', $Port, '-PoseFile', "`"$PoseFile`"", '-Minutes', $Minutes)
    $process.Id
}

function Set-TestPose {
    <#
    .SYNOPSIS
    Sets the pose the sender eases toward: centimetres and degrees.
    #>
    param([Parameter(Mandatory)][string]$PoseFile, [double]$X = 0, [double]$Y = 0, [double]$Z = 0,
          [double]$Yaw = 0, [double]$Pitch = 0, [double]$Roll = 0)
    $culture = [Globalization.CultureInfo]::InvariantCulture
    Set-Content -Path $PoseFile -Encoding ASCII -Value (($X, $Y, $Z, $Yaw, $Pitch, $Roll | ForEach-Object { $_.ToString($culture) }) -join ' ')
}

Export-ModuleMember -Function Get-PeImports, Get-IsolatedInputCoverage, Get-GameInputPaths, Assert-IsolatedInputCovers,
    Get-ModTestPort, Copy-IsolatedInputHost, Start-IsolatedGame, Invoke-GameInput, Save-GameCapture, Test-CaptureDiffers, Test-IsolatedInputProof,
    Stop-IsolatedGame, Save-GameTestState, Restore-GameTestState, Start-TestPoseSender, Set-TestPose
