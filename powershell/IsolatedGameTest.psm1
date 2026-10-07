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
# One session has a game at a time. Enter-GameRig is the lock, kept outside
# every repo so two worktrees and two mods' sessions meet the same one, and
# Invoke-IsolatedGameSession is the whole run round a script block: lock, save
# what the test changes, launch, the block, stop, put everything back.
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

        [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc callback, IntPtr lParam);
        [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hWnd);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr hWnd, System.Text.StringBuilder name, int max);
        delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);

        // A process can own a console as well (BepInEx's log console), and that is what
        // Process.MainWindowHandle returns for some games. The game is the largest visible
        // window of the process that is not a console.
        public static IntPtr GameWindow(uint pid) {
            IntPtr best = IntPtr.Zero; long bestArea = 0;
            EnumWindows(delegate(IntPtr hWnd, IntPtr l) {
                uint owner; GetWindowThreadProcessId(hWnd, out owner);
                RECT r;
                if (owner != pid || !IsWindowVisible(hWnd) || !GetWindowRect(hWnd, out r)) return true;
                System.Text.StringBuilder name = new System.Text.StringBuilder(256);
                GetClassName(hWnd, name, name.Capacity);
                if (name.ToString() == "ConsoleWindowClass") return true;
                long area = (long)(r.Right - r.Left) * (r.Bottom - r.Top);
                if (area > bestArea) { bestArea = area; best = hWnd; }
                return true;
            }, IntPtr.Zero);
            return best;
        }

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
        [Parameter(Mandatory)][ValidateSet('native', 'managed', 'jvm', 'script')][string]$ModHost
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

function Set-ModTestPort {
    <#
    .SYNOPSIS
    Writes UdpPort=<port> into a mod's config file for a test, and returns the port. Save the file
    with Save-GameTestState first: this does not keep the value it replaces.
    .DESCRIPTION
    The last UdpPort line is replaced, keeping the key's spelling and the white space round the
    equals sign. With no such line, one is added after the last line of [Network] that is not
    blank, or under a new [Network] at the end. Every other byte stays, the byte order mark and the
    line endings included.

    A file that does not exist throws: a mod on the canonical config imports its older settings
    only while CameraUnlock.ini is absent, so a test that made the file would not be testing what a
    player gets. -Create is for the test that means it.
    #>
    param(
        [Parameter(Mandatory)][string]$IniPath,
        [Parameter(Mandatory)][int]$Port,
        [switch]$Create
    )
    if ($Port -eq 4242) { throw 'port 4242 is the one real trackers send to; test on Get-ModTestPort' }
    if ($Port -lt 1 -or $Port -gt 65535) { throw "$Port is not a UDP port" }
    if (-not (Test-Path $IniPath -PathType Leaf)) {
        if (-not $Create) { throw "$IniPath does not exist. Launch the mod once so it writes its config, or pass -Create to test from a file holding the port alone." }
        [IO.File]::WriteAllText($IniPath, "[Network]`r`nUdpPort=$Port`r`n", [Text.Encoding]::ASCII)
        return $Port
    }
    $bytes = [IO.File]::ReadAllBytes($IniPath)
    if ([Array]::IndexOf($bytes, [byte]0) -ge 0) { throw "$IniPath holds a NUL byte: it is UTF-16 or not text, and this edits neither" }
    $marked = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    # Latin-1 gives every byte back as it was, whatever the file's encoding: only ASCII is written into it.
    $latin = [Text.Encoding]::GetEncoding(28591)
    $skip = $(if ($marked) { 3 } else { 0 })
    $text = $latin.GetString($bytes, $skip, $bytes.Length - $skip)
    $ending = $(if ($text -match "`r`n" -or $text -notmatch "`n") { "`r`n" } else { "`n" })
    $key = [regex]'(?im)^([ \t]*UdpPort[ \t]*=[ \t]*)[^\r\n]*'
    $found = $key.Matches($text)
    if ($found.Count -gt 0) {
        $last = $found[$found.Count - 1]
        $text = $text.Substring(0, $last.Index) + $last.Groups[1].Value + $Port + $text.Substring($last.Index + $last.Length)
    } else {
        $section = [regex]::Match($text, '(?ims)^[ \t]*\[Network\][^\r\n]*(\r?\n(?![ \t]*\[)[^\r\n]*)*')
        if ($section.Success) {
            # Before the blank lines that close the section.
            $body = $section.Value -replace '(\r?\n[ \t]*)+$', ''
            $at = $section.Index + $body.Length
            $text = $text.Substring(0, $at) + $ending + "UdpPort=$Port" + $text.Substring($at)
        } else {
            if ($text.Length -gt 0 -and -not $text.EndsWith("`n")) { $text += $ending }
            $text += "[Network]${ending}UdpPort=$Port$ending"
        }
    }
    $out = $latin.GetBytes($text)
    if ($marked) { $out = [byte[]](0xEF, 0xBB, 0xBF) + $out }
    [IO.File]::WriteAllBytes($IniPath, [byte[]]$out)
    $Port
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
    # The mod reads the command file on a timer, and a move that lands on that read is refused.
    foreach ($attempt in 1..20) {
        try { Move-Item $temp $Session.CommandFile -Force -ErrorAction Stop; break }
        catch [System.IO.IOException] { if ($attempt -eq 20) { throw }; Start-Sleep -Milliseconds 25 }
    }
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
    # A session with a file of its own (Start-IsolatedGameSession -SessionFile) is one a later
    # process picks up, so the sequence it has reached is written there before anything throws.
    if ($Session.PSObject.Properties['SessionFile'] -and $Session.SessionFile) { Save-IsolatedGameSession -Session $Session }
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
    $window = [CameraUnlockIsolatedTest.Native]::GameWindow([uint32]$process.Id)
    if ($window -eq [IntPtr]::Zero) { throw "the game (pid $($process.Id)) has no visible window to capture" }
    $bitmap = [CameraUnlockIsolatedTest.Native]::Capture($window)
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
    if ($Session.HostDll) {
        # Windows can hold a DLL the game had loaded for a moment after the process is gone.
        $deadline = (Get-Date).AddSeconds(10)
        while ($true) {
            try { Remove-Item $Session.HostDll -Force -ErrorAction Stop; break }
            catch [UnauthorizedAccessException], [System.IO.IOException] {
                if ((Get-Date) -gt $deadline) { throw }
                Start-Sleep -Milliseconds 200
            }
        }
    }
}

# ---------------------------------------------------------------------------
# One session at a time
# ---------------------------------------------------------------------------
# <root>\<key>\lock\owner.json     the lock, with who holds it
# <root>\queue\<ticks>-<pid>.json  one ticket for each session waiting, oldest first
# Every look at them and every change to them happens inside one named mutex, so no two sessions
# ever decide on the same state. The lock itself is a folder, because it has to outlive the
# process that took it: a session that starts the game in one process and stops it in another
# holds the rig in between.

function Get-GameRigRoot {
    # CAMERAUNLOCK_RIG_ROOT is for this module's own tests, which must not meet a real session's lock.
    $(if ($env:CAMERAUNLOCK_RIG_ROOT) { $env:CAMERAUNLOCK_RIG_ROOT } else { Join-Path $env:LOCALAPPDATA 'CameraUnlock\rig' })
}

function Get-GameRigKey {
    param([string]$Game)
    $key = $Game.ToLowerInvariant() -replace '\.exe$', ''
    if ($key -notmatch '^[a-z0-9][a-z0-9 ._-]*$') { throw "'$Game' cannot name a rig: give the game's process name" }
    $key
}

function Invoke-GameRigExclusive {
    # Runs -Block with the rig's mutex held. The mutex is named for the root, so a test's root has its own.
    param([Parameter(Mandatory)][scriptblock]$Block)
    $sha = [Security.Cryptography.SHA1]::Create()
    try { $digest = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes((Get-GameRigRoot).ToLowerInvariant())) } finally { $sha.Dispose() }
    $mutex = New-Object System.Threading.Mutex($false, ('Global\CameraUnlockGameRig-' + (-join ($digest[0..7] | ForEach-Object { $_.ToString('x2') }))))
    try {
        # A holder that died inside the block leaves the mutex abandoned, and the wait still hands it over.
        try { [void]$mutex.WaitOne() } catch [System.Threading.AbandonedMutexException] { }
        try { & $Block } finally { $mutex.ReleaseMutex() }
    } finally { $mutex.Dispose() }
}

function Test-GameRigProcessAlive {
    # The start time is what tells the process that took a lock from a later one handed the same id.
    param([int]$Id, [string]$Start)
    $process = Get-Process -Id $Id -ErrorAction SilentlyContinue
    if (-not $process) { return $false }
    try { $process.StartTime.ToFileTimeUtc().ToString() -eq $Start }
    # A process whose start time cannot be read is another user's, and it is running.
    catch [System.ComponentModel.Win32Exception] { $true }
    catch [System.InvalidOperationException] { $false }
}

function Read-GameRigOwner {
    param([string]$Key)
    $file = Join-Path (Join-Path (Get-GameRigRoot) $Key) 'lock\owner.json'
    if (-not (Test-Path $file)) { return $null }
    Get-Content $file -Raw | ConvertFrom-Json
}

function Test-GameRigOwnerLive {
    # A lock is live while the process that took it runs, or while any process it named does: the
    # game a session started goes on holding the rig after the process that launched it has ended.
    param($Owner)
    if (Test-GameRigProcessAlive -Id $Owner.ProcessId -Start $Owner.ProcessStart) { return $true }
    foreach ($name in @($Owner.Processes)) {
        if (Get-Process -Name $name -ErrorAction SilentlyContinue) { return $true }
    }
    $false
}

function Format-GameRigOwner {
    param($Owner)
    $text = "$($Owner.Owner) (pid $($Owner.ProcessId), since $($Owner.Since)"
    if ($Owner.What) { $text += ", $($Owner.What)" }
    if ($Owner.WholeGpu) { $text += ', the whole graphics card' }
    "$text)"
}

function Get-GameRigTickets {
    # The live tickets, oldest first. A ticket whose process has gone is removed: called inside the mutex.
    $queue = Join-Path (Get-GameRigRoot) '_queue'
    if (-not (Test-Path $queue)) { return }
    foreach ($file in Get-ChildItem $queue -Filter *.json | Sort-Object Name) {
        $ticket = Get-Content $file.FullName -Raw | ConvertFrom-Json
        if (Test-GameRigProcessAlive -Id $ticket.ProcessId -Start $ticket.ProcessStart) {
            $ticket | Add-Member -NotePropertyName File -NotePropertyValue $file.FullName -PassThru
        } else {
            Remove-Item $file.FullName -Force
        }
    }
}

function Get-GameRig {
    <#
    .SYNOPSIS
    Who holds a game's rig and who is waiting for it, without taking it. Nothing when the rig is free
    and nobody waits. Leave -Game out for the rig of work that has no game (Enter-GameRig -WholeGpu).
    .DESCRIPTION
    Live is false for a lock whose taker has gone and whose game is not running: the next
    Enter-GameRig takes that one over, unless Unrestored names a saved state its session never
    put back.
    #>
    param([string]$Game)
    $key = $(if ($Game) { Get-GameRigKey $Game } else { 'gpu' })
    Invoke-GameRigExclusive {
        $owner = Read-GameRigOwner $key
        $waiting = @(Get-GameRigTickets | Where-Object { $_.Key -eq $key } | ForEach-Object { $_.Owner })
        if (-not $owner -and $waiting.Count -eq 0) { return }
        [pscustomobject]@{
            Key        = $key
            Holder     = $owner
            Live       = [bool]($owner -and (Test-GameRigOwnerLive $owner))
            Unrestored = $(if ($owner -and $owner.StateFolder -and (Test-Path (Join-Path $owner.StateFolder 'state.json'))) { $owner.StateFolder } else { $null })
            Waiting    = $waiting
        }
    }
}

function Enter-GameRig {
    <#
    .SYNOPSIS
    Takes the one lock on a game, waiting its turn behind whoever asked first, and returns the rig
    Exit-GameRig releases. A game, its saves and its settings are one thing on this machine,
    whichever repo, worktree or session wants them.
    .DESCRIPTION
    The lock lives in %LOCALAPPDATA%\CameraUnlock\rig\<game>, outside every repo, so two worktrees
    of one mod and two mods for one game meet the same lock. It records who took it: -Owner, the
    process id, when, and -What.

    Only the session that took a lock releases it. A lock is taken over from a session that never
    released it only when that session's process has gone AND the game is not running, and never
    while the files that session saved (-StateFolder) are still waiting to be put back: that
    throws, naming the folder to restore. While the game is running with no lock on it, someone
    started it by hand, and this waits for them too.

    -WholeGpu is for work that needs the graphics card to itself, an image generation run as much
    as a benchmark: it waits until no rig is held, and no rig is given out while it holds. Such
    work needs no game: leave -Game out. Run it from the PowerShell that took the lock, because
    the lock of a process that has ended, with no game running, is anyone's to take over.

    -Token takes back the lock an earlier process of the same session took: pass the Token of the
    rig that process got. This is how a session that starts the game in one process stops it in
    another.

    A session that never asks is not seen. Every session on the machine has to take the rig for
    the lock to mean anything.
    #>
    param(
        # The game's process name, as Start-IsolatedGame takes it.
        [string]$Game,
        # Who is asking, as another session or the person at the machine would know them.
        [Parameter(Mandatory)][string]$Owner,
        # What the rig is wanted for, one line, shown to whoever waits behind this.
        [string]$What = '',
        # Other process names that mean the game is still up: a launcher, a stub that hands over.
        [string[]]$Processes = @(),
        # Where this session saves the game's files (Save-GameTestState -Folder).
        [string]$StateFolder = '',
        [switch]$WholeGpu,
        # 0 takes the rig now or throws. The default waits an hour.
        [int]$WaitSeconds = 3600,
        [string]$Token = ''
    )
    if (-not $Game -and -not $WholeGpu -and -not $Token) { throw 'Enter-GameRig needs -Game, or -WholeGpu for work that has no game' }
    $key = $(if ($Game) { Get-GameRigKey $Game } else { 'gpu' })
    $names = [string[]]@(@($Game -replace '\.exe$', '') + $Processes | Where-Object { $_ } | Select-Object -Unique)
    $root = Get-GameRigRoot
    $rigFolder = Join-Path $root $key
    $lock = Join-Path $rigFolder 'lock'
    $queue = Join-Path $root '_queue'
    $start = (Get-Process -Id $PID).StartTime.ToFileTimeUtc().ToString()

    if ($Token) {
        return Invoke-GameRigExclusive {
            $held = Read-GameRigOwner $key
            if (-not $held -or $held.Token -ne $Token) {
                throw "the rig for $key is not held under that token$(if ($held) { ': it is held by ' + (Format-GameRigOwner $held) }). The lock this session took is gone."
            }
            if ($held.ProcessId -ne $PID -and (Test-GameRigProcessAlive -Id $held.ProcessId -Start $held.ProcessStart)) {
                throw "the rig for $key is held under that token by a process that is still running (pid $($held.ProcessId)), and it is that one's to release"
            }
            $held.ProcessId = $PID
            $held.ProcessStart = $start
            ConvertTo-Json $held | Set-Content (Join-Path $lock 'owner.json') -Encoding UTF8
            [pscustomobject]@{ Key = $key; Owner = $held.Owner; Token = $Token; Path = $lock; TakenOverFrom = $null }
        }
    }

    # This session's place in the queue, numbered inside the mutex so no two tickets tie and none
    # is older than one already there.
    $ticket = Invoke-GameRigExclusive {
        New-Item -ItemType Directory -Force $rigFolder, $queue | Out-Null
        $tickets = @(Get-GameRigTickets)
        $ticks = [DateTime]::UtcNow.Ticks
        if ($tickets.Count -gt 0) { $ticks = [Math]::Max($ticks, [long]$tickets[-1].Ticks + 1) }
        $file = Join-Path $queue ('{0:D20}-{1}.json' -f $ticks, $PID)
        ConvertTo-Json ([pscustomobject]@{ Ticks = "$ticks"; Key = $key; Owner = $Owner; ProcessId = $PID; ProcessStart = $start; WholeGpu = [bool]$WholeGpu }) |
            Set-Content $file -Encoding UTF8
        $file
    }
    $said = ''
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    try {
        while ($true) {
            # One turn: the rig, or what this session is waiting for.
            $turn = Invoke-GameRigExclusive {
                $stale = $null
                foreach ($folder in Get-ChildItem $root -Directory | Where-Object { $_.Name -ne '_queue' }) {
                    $held = Read-GameRigOwner $folder.Name
                    if (-not $held) { continue }
                    $live = Test-GameRigOwnerLive $held
                    if ($folder.Name -eq $key) {
                        if ($live) { return "the rig is in use: $(Format-GameRigOwner $held)" }
                        if ($held.StateFolder -and (Test-Path (Join-Path $held.StateFolder 'state.json'))) {
                            throw "the last session on $key, $(Format-GameRigOwner $held), ended without putting the game's files back. What it saved is in $($held.StateFolder): run Restore-GameTestState -Folder '$($held.StateFolder)', then ask again."
                        }
                        $stale = $held
                    } elseif ($live -and ($WholeGpu -or $held.WholeGpu)) {
                        return "the graphics card is in use: $($folder.Name) is held by $(Format-GameRigOwner $held)"
                    }
                }
                # Whoever asked before this session, for the same rig or for the whole card, goes first.
                foreach ($other in Get-GameRigTickets) {
                    if ($other.File -eq $ticket) { break }
                    if ($other.Key -eq $key -or $other.WholeGpu -or $WholeGpu) {
                        return "waiting behind $($other.Owner) (pid $($other.ProcessId)), who asked first"
                    }
                }
                $running = @($names | Where-Object { Get-Process -Name $_ -ErrorAction SilentlyContinue })
                if ($running.Count -gt 0) {
                    return "$($running -join ', ') is running and no session holds its rig: someone started it by hand"
                }

                if ($stale) { Remove-Item $lock -Recurse -Force }
                # The owner's record is written first and the folder renamed into place, which
                # happens whole or not at all: a lock never exists without its owner.
                $fresh = Join-Path $rigFolder ('taking-' + [Guid]::NewGuid().ToString('N'))
                New-Item -ItemType Directory $fresh | Out-Null
                $record = [pscustomobject]@{
                    Key = $key; Owner = $Owner; ProcessId = $PID; ProcessStart = $start; Since = (Get-Date).ToString('s')
                    What = $What; Processes = $names; WholeGpu = [bool]$WholeGpu; StateFolder = $StateFolder
                    Token = [Guid]::NewGuid().ToString('N')
                }
                ConvertTo-Json $record | Set-Content (Join-Path $fresh 'owner.json') -Encoding UTF8
                [IO.Directory]::Move($fresh, $lock)
                Remove-Item $ticket -Force
                [pscustomobject]@{ Key = $key; Owner = $Owner; Token = $record.Token; Path = $lock; TakenOverFrom = $stale }
            }
            if ($turn -isnot [string]) {
                if ($turn.TakenOverFrom) {
                    Write-Warning "the rig for $key was still held by $(Format-GameRigOwner $turn.TakenOverFrom), whose process has gone and whose game is not running. It has been taken over. Whatever that session changed and did not save with -StateFolder is as it left it."
                }
                return $turn
            }
            if ($WaitSeconds -le 0) { throw $turn }
            if ((Get-Date) -ge $deadline) { throw "$turn. Waited $WaitSeconds seconds for the rig for $key." }
            if ($turn -ne $said) { Write-Host "Enter-GameRig ($key, $Owner): $turn"; $said = $turn }
            Start-Sleep -Milliseconds 500
        }
    } finally {
        # Gone already when the rig was taken. Left by a wait that ran out or a turn that threw.
        Remove-Item $ticket -Force -ErrorAction SilentlyContinue
    }
}

function Exit-GameRig {
    <#
    .SYNOPSIS
    Releases the rig Enter-GameRig returned. Throws, and removes nothing, when the lock is no
    longer this session's: one another session holds is theirs to release.
    #>
    param([Parameter(Mandatory)]$Rig)
    Invoke-GameRigExclusive {
        $held = Read-GameRigOwner $Rig.Key
        if (-not $held) { throw "the rig for $($Rig.Key) is not held: the lock $($Rig.Owner) took is gone" }
        if ($held.Token -ne $Rig.Token) { throw "the rig for $($Rig.Key) is held by $(Format-GameRigOwner $held), not by the session releasing it. Left as it is." }
        if ($held.ProcessId -ne $PID) { throw "the rig for $($Rig.Key) was taken by pid $($held.ProcessId), not by this process. Take it back with Enter-GameRig -Token first." }
        # Renamed away first, so the lock goes in one step even when a file in it is slow to delete.
        $gone = Join-Path (Split-Path -Parent $Rig.Path) ('released-' + [Guid]::NewGuid().ToString('N'))
        [IO.Directory]::Move($Rig.Path, $gone)
        Remove-Item $gone -Recurse -Force
    }
}

# ---------------------------------------------------------------------------
# The files a test changes
# ---------------------------------------------------------------------------

function Invoke-FolderMirror {
    # robocopy /MIR makes Target the same as Source without ever emptying Target first: a file it
    # cannot write is tried again, and one that never frees is left as it was with the rest in place.
    param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$Target)
    & robocopy $Source $Target /MIR /R:10 /W:2 /NFL /NDL /NJH /NJS /NP | Out-Null
    $code = $LASTEXITCODE
    # robocopy answers 1 to 7 for a copy that worked, which a script run with -File would exit with.
    $global:LASTEXITCODE = 0
    if ($code -ge 8) { throw "robocopy could not make $Target the same as $Source (exit code $code)" }
}

function Save-GameTestState {
    <#
    .SYNOPSIS
    Copies the files and folders a test is about to change (the deployed mod, its config, the
    game's settings, a save folder) into -Folder and records which of them did not exist, so
    Restore-GameTestState puts every one back exactly, absent ones included. Refuses a folder that
    already holds a saved state: that one has not been restored yet.
    .DESCRIPTION
    -Folders takes whole folders, for what a game writes while it runs: a save folder it adds
    chunk files to cannot be put back file by file, because the files it added have to go. A
    folder is copied with everything under it.
    #>
    param(
        [Parameter(Position = 0)][string[]]$Files = @(),
        [Parameter(Mandatory, Position = 1)][string]$Folder,
        [string[]]$Folders = @()
    )
    if ($Files.Count + $Folders.Count -eq 0) { throw 'Save-GameTestState was given nothing to save: name -Files, -Folders or both' }
    foreach ($path in $Folders) { if (Test-Path $path -PathType Leaf) { throw "$path is a file: name it under -Files" } }
    $index = Join-Path $Folder 'state.json'
    if (Test-Path $index) { throw "$Folder already holds a saved state that was never restored. Restore it first." }
    New-Item -ItemType Directory -Force $Folder | Out-Null
    $entries = @(for ($i = 0; $i -lt $Files.Count; $i++) {
        if (Test-Path $Files[$i] -PathType Container) { throw "$($Files[$i]) is a folder: name it under -Folders" }
        $existed = Test-Path $Files[$i]
        if ($existed) { Copy-Item $Files[$i] (Join-Path $Folder "$i.bak") -Force }
        [pscustomobject]@{ Path = $Files[$i]; Existed = $existed; Copy = "$i.bak" }
    })
    $entries += @(for ($i = 0; $i -lt $Folders.Count; $i++) {
        $existed = Test-Path $Folders[$i]
        if ($existed) { Invoke-FolderMirror -Source $Folders[$i] -Target (Join-Path $Folder "$i.dir") }
        [pscustomobject]@{ Path = $Folders[$i]; Existed = $existed; Copy = "$i.dir"; Kind = 'folder' }
    })
    ConvertTo-Json @($entries) | Set-Content $index -Encoding UTF8
}

function Restore-GameTestState {
    <#
    .SYNOPSIS
    Puts back every file and folder Save-GameTestState recorded, removes the ones that did not
    exist before, and removes the saved state.
    .DESCRIPTION
    A folder is mirrored back, never removed and copied again: a file the stopped game still
    holds is tried for 20 seconds, and if it never frees the folder keeps everything else and
    this throws. Whenever it throws, the saved state is still in -Folder and it can be run again.
    -RetrySeconds gives a file the same patience: a game holds its own DLLs open for some seconds
    after its process has gone.
    #>
    param([Parameter(Mandatory, Position = 0)][string]$Folder, [int]$RetrySeconds = 0)
    $index = Join-Path $Folder 'state.json'
    # Windows PowerShell hands a JSON array back as one object, so it is unrolled here.
    $entries = @((Get-Content $index -Raw | ConvertFrom-Json) | ForEach-Object { $_ })
    foreach ($entry in $entries) {
        $copy = Join-Path $Folder $entry.Copy
        if ($entry.PSObject.Properties['Kind'] -and $entry.Kind -eq 'folder') {
            if ($entry.Existed) { Invoke-FolderMirror -Source $copy -Target $entry.Path }
            elseif (Test-Path $entry.Path) { Remove-Item $entry.Path -Recurse -Force }
            continue
        }
        $deadline = (Get-Date).AddSeconds($RetrySeconds)
        while ($true) {
            try {
                if ($entry.Existed) { Copy-Item $copy $entry.Path -Force -ErrorAction Stop }
                elseif ($RetrySeconds -gt 0) { if (Test-Path $entry.Path) { Remove-Item $entry.Path -Force -ErrorAction Stop } }
                else { Remove-Item $entry.Path -Force -ErrorAction SilentlyContinue }
                break
            } catch [UnauthorizedAccessException], [System.IO.IOException] {
                if ((Get-Date) -ge $deadline) { throw }
                Start-Sleep -Milliseconds 500
            }
        }
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

# ---------------------------------------------------------------------------
# Watching the game
# ---------------------------------------------------------------------------

function Wait-GameLogLine {
    <#
    .SYNOPSIS
    Waits for a line matching -Match in a log the game or the mod is writing, and returns it with
    its line number. Throws when it has not come within -TimeoutSeconds.
    .DESCRIPTION
    -Match is a regular expression, or the text itself with -SimpleMatch. The log is read while
    its writer holds it open, and need not exist yet. A log an earlier run left behind can hold the
    line already: delete it before the launch, or pass -After the line number an earlier wait
    returned to look only below it. With -Session the wait ends as soon as the game has gone, and
    the error says that is why.
    #>
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Match,
        [int]$TimeoutSeconds = 120,
        [switch]$SimpleMatch,
        [int]$After = 0,
        $Session
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($true) {
        # Looked up before the read, so a line the game wrote as it went is still found.
        $gone = $Session -and -not (Get-Process -Id $Session.ProcessId -ErrorAction SilentlyContinue)
        $lines = @()
        if (Test-Path $Path -PathType Leaf) {
            $stream = New-Object IO.FileStream($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
            try {
                $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8, $true)
                $lines = @($reader.ReadToEnd() -split "\r?\n")
            } finally { $stream.Dispose() }
        }
        for ($i = $After; $i -lt $lines.Count; $i++) {
            $hit = $(if ($SimpleMatch) { $lines[$i].Contains($Match) } else { $lines[$i] -match $Match })
            if ($hit) { return [pscustomobject]@{ Line = $lines[$i]; LineNumber = $i + 1 } }
        }
        $last = $(if ($lines.Count -gt 0) { "Its last line: $(@($lines | Where-Object { $_ })[-1])" } else { 'It does not exist.' })
        if ($gone) { throw "the game (pid $($Session.ProcessId)) went before $Path showed '$Match'. $last" }
        if ((Get-Date) -ge $deadline) { throw "$Path did not show '$Match' within $TimeoutSeconds seconds. $last" }
        Start-Sleep -Milliseconds 500
    }
}

function Start-GameProcessSampler {
    <#
    .SYNOPSIS
    Starts a hidden process that writes one CSV row every -IntervalSeconds for a running process:
    private bytes, working set, dedicated video memory and processor time. Returns the sampler
    Stop-GameProcessSampler takes. It ends by itself when the process it watches has gone.
    .DESCRIPTION
    Columns: time, elapsedSeconds, privateMB, workingSetMB, dedicatedVideoMB, cpuSeconds.
    dedicatedVideoMB is the sum of Windows' "GPU Process Memory" counters for the process and is
    empty in a row where Windows had none. cpuSeconds counts from the start of the process over
    all its threads, so the load between two rows is the difference over the time between them.
    #>
    param(
        [Parameter(Mandatory)][int]$ProcessId,
        [Parameter(Mandatory)][string]$Path,
        [double]$IntervalSeconds = 2
    )
    if (-not (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)) { throw "no process $ProcessId to sample" }
    Remove-Item $Path, "$Path.stop" -ErrorAction SilentlyContinue
    $script = Join-Path $PSScriptRoot 'Sample-GameProcess.ps1'
    $interval = $IntervalSeconds.ToString([Globalization.CultureInfo]::InvariantCulture)
    $sampler = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$script`"", '-ProcessId', $ProcessId, '-Path', "`"$Path`"", '-IntervalSeconds', $interval)
    $deadline = (Get-Date).AddSeconds(30)
    while (-not (Test-Path $Path)) {
        if ($sampler.HasExited) { throw "the sampler ended before it wrote $Path" }
        if ((Get-Date) -ge $deadline) { Stop-Process -Id $sampler.Id -Force; throw "the sampler did not start writing $Path within 30 seconds" }
        Start-Sleep -Milliseconds 100
    }
    [pscustomobject]@{ SamplerId = $sampler.Id; ProcessId = $ProcessId; Path = $Path; IntervalSeconds = $IntervalSeconds }
}

function Stop-GameProcessSampler {
    <#
    .SYNOPSIS
    Ends a sampler between two rows, so the CSV never stops in the middle of one, and returns the
    CSV's path. A sampler that has already ended with its process is not an error.
    #>
    param([Parameter(Mandatory)]$Sampler)
    $process = Get-Process -Id $Sampler.SamplerId -ErrorAction SilentlyContinue
    if ($process -and $process.ProcessName -eq 'powershell') {
        Set-Content "$($Sampler.Path).stop" '' -Encoding ASCII
        # Reading the video memory counter takes about a second, on top of the row's own wait.
        if (-not $process.WaitForExit([int](($Sampler.IntervalSeconds + 15) * 1000))) {
            Stop-Process -Id $Sampler.SamplerId -Force
            Remove-Item "$($Sampler.Path).stop" -ErrorAction SilentlyContinue
            throw "the sampler (pid $($Sampler.SamplerId)) did not stop when asked and was ended: the last row of $($Sampler.Path) may be cut short"
        }
    }
    $Sampler.Path
}

# ---------------------------------------------------------------------------
# A whole session
# ---------------------------------------------------------------------------

function Save-IsolatedGameSession {
    <#
    .SYNOPSIS
    Writes a session to its SessionFile, for the next process of the same session to read with
    Get-IsolatedGameSession. Invoke-GameInput does this itself after every script; call it after
    changing the session by hand (a new ProcessId once a launcher has handed over, a property of
    the mod's own).
    #>
    param([Parameter(Mandatory)]$Session)
    ConvertTo-Json $Session | Set-Content $Session.SessionFile -Encoding UTF8
}

function Get-IsolatedGameSession {
    <#
    .SYNOPSIS
    Reads back the session Start-IsolatedGameSession -SessionFile wrote, in a later process.
    #>
    param([Parameter(Mandatory)][string]$SessionFile)
    if (-not (Test-Path $SessionFile)) { throw "no session at ${SessionFile}: nothing was started, or it has been stopped" }
    Get-Content $SessionFile -Raw | ConvertFrom-Json
}

function Start-IsolatedGameSession {
    <#
    .SYNOPSIS
    Everything before the first input of a background test, in the order that is safe: takes the
    game's rig, saves what the test changes, runs -Prepare, sets the test port, starts the pose
    sender and launches. Returns the session. If any step fails, the game's files are put back
    and the rig released before the error leaves.
    .DESCRIPTION
    Invoke-IsolatedGameSession is this, a script block and Stop-IsolatedGameSession in one call,
    and is the one to use for a run that fits in one process. Call this and
    Stop-IsolatedGameSession apart, with -SessionFile, for a session driven over several
    processes: start, look at a capture, decide what to play next, stop. The rig stays held in
    between, by the running game.

    -Prepare runs with the rig held and the state saved, before the launch: build and deploy the
    dev build, change the game's settings. A build made there cannot land under another session's
    running game. -IniPath is the mod's config, where -Port is written; it is saved and put back
    with -Files. -PoseFile starts the pose sender on -Port.

    The session carries Port, PoseFile, Sender (the pose sender's process id), ModFolder,
    StateFolder, Owner, RigToken and SessionFile beside what Start-IsolatedGame returns.
    #>
    param(
        [Parameter(Mandatory)][string]$ProcessName,
        [Parameter(Mandatory)][string]$Launch,
        [Parameter(Mandatory)][string]$ModFolder,
        # Who is running the test, for the rig's owner record.
        [Parameter(Mandatory)][string]$Owner,
        [string]$What = '',
        # What the test changes: the deployed mod, its config, the game's settings; whole folders the game writes.
        [string[]]$Files = @(),
        [string[]]$Folders = @(),
        # Where they are kept until the session stops. Needed with -Files, -Folders or -IniPath.
        [string]$StateFolder = '',
        [scriptblock]$Prepare,
        [string]$IniPath = '',
        [int]$Port = 0,
        [string]$PoseFile = '',
        # The folder to launch from, for a game that finds its data through the working directory.
        [string]$WorkingDirectory = '',
        # Environment variables for the launch alone, put back once the game has started.
        [hashtable]$Environment = @{},
        [int]$StartTimeoutSeconds = 120,
        [int]$SettleSeconds = 60,
        [ValidateSet('native', 'managed')][string]$ModHost = 'native',
        [string]$HostDll = $script:HostDllBuild,
        # Other process names that mean the game is still up, for the rig.
        [string[]]$Processes = @(),
        [switch]$WholeGpu,
        [int]$WaitSeconds = 3600,
        [string]$SessionFile = ''
    )
    if (($IniPath -or $PoseFile) -and $Port -eq 0) { throw '-IniPath and -PoseFile need -Port: pass Get-ModTestPort -RepoName <the repo folder>' }
    $kept = @($Files + $(if ($IniPath -and $Files -notcontains $IniPath) { $IniPath }) | Where-Object { $_ })
    $saves = $kept.Count + $Folders.Count -gt 0
    if ($saves -and -not $StateFolder) { throw '-Files, -Folders and -IniPath need -StateFolder: where what the test changes is kept until it is put back' }
    if ($SessionFile -and (Test-Path $SessionFile)) { throw "$SessionFile holds a session that was never stopped. Stop it first (Stop-IsolatedGameSession -SessionFile)." }

    $rig = Enter-GameRig -Game $ProcessName -Owner $Owner -What $What -Processes $Processes -WholeGpu:$WholeGpu -WaitSeconds $WaitSeconds `
        -StateFolder $(if ($saves) { $StateFolder } else { '' })
    $session = [pscustomobject]@{
        ProcessId = 0; ProcessName = $ProcessName; CommandFile = (Join-Path $ModFolder $script:CommandFileName); Sequence = 0
        HostDll = $(if ($ModHost -eq 'managed') { Join-Path $ModFolder $script:HostDllName } else { $null })
        ModFolder = $ModFolder; Port = $Port; PoseFile = $PoseFile; Sender = 0
        StateFolder = $(if ($saves) { $StateFolder } else { '' }); Owner = $Owner; RigToken = $rig.Token; SessionFile = $SessionFile
    }
    try {
        if ($saves) { Save-GameTestState -Files $kept -Folders $Folders -Folder $StateFolder }
        if ($Prepare) { & $Prepare | Out-Host }
        if ($IniPath) { [void](Set-ModTestPort -IniPath $IniPath -Port $Port) }
        if ($PoseFile) {
            Set-TestPose -PoseFile $PoseFile
            $session.Sender = Start-TestPoseSender -Port $Port -PoseFile $PoseFile
        }
        $before = @{}
        foreach ($name in $Environment.Keys) {
            $before[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
            [Environment]::SetEnvironmentVariable($name, "$($Environment[$name])", 'Process')
        }
        if ($WorkingDirectory) { Push-Location $WorkingDirectory }
        try {
            $started = Start-IsolatedGame -ProcessName $ProcessName -Launch $Launch -ModFolder $ModFolder -StartTimeoutSeconds $StartTimeoutSeconds `
                -SettleSeconds $SettleSeconds -ModHost $ModHost -HostDll $HostDll
        } finally {
            if ($WorkingDirectory) { Pop-Location }
            foreach ($name in $before.Keys) { [Environment]::SetEnvironmentVariable($name, $before[$name], 'Process') }
        }
        $session.ProcessId = $started.ProcessId
        if ($SessionFile) { Save-IsolatedGameSession -Session $session }
        $session
    } catch {
        # The first error is the one to report. One from putting things back is said beside it.
        $failure = $_
        try { Complete-IsolatedGameSession -Session $session -Rig $rig -RestoreRetrySeconds 30 }
        catch { Write-Warning "and the session could not be put back: $_" }
        throw $failure
    }
}

function Complete-IsolatedGameSession {
    # Stops what the session started and puts back what it changed, each step whatever the one
    # before it did. The rig is released only once the game's files are back: a restore that
    # fails leaves it held, and Enter-GameRig then tells the next session what to restore.
    param($Session, $Rig, [scriptblock]$Collect, [int]$RestoreRetrySeconds)
    $failures = New-Object System.Collections.Generic.List[object]
    if ($Session.ProcessId) {
        try { Stop-IsolatedGame -Session $Session } catch { $failures.Add($_) }
    } else {
        # The game never started, and Start-IsolatedGame had already put these beside the mod.
        Remove-Item $Session.CommandFile, "$($Session.CommandFile).done" -ErrorAction SilentlyContinue
        if ($Session.HostDll) { Remove-Item $Session.HostDll -Force -ErrorAction SilentlyContinue }
    }
    if ($Session.Sender) {
        $sender = Get-Process -Id $Session.Sender -ErrorAction SilentlyContinue
        if ($sender -and $sender.ProcessName -eq 'powershell') { Stop-Process -Id $Session.Sender -Force }
    }
    if ($Collect) { try { & $Collect $Session | Out-Host } catch { $failures.Add($_) } }
    if ($Session.StateFolder -and (Test-Path (Join-Path $Session.StateFolder 'state.json'))) {
        Restore-GameTestState -Folder $Session.StateFolder -RetrySeconds $RestoreRetrySeconds
    }
    Exit-GameRig -Rig $Rig
    if ($Session.SessionFile) { Remove-Item $Session.SessionFile -Force -ErrorAction SilentlyContinue }
    if ($failures.Count -gt 0) { throw $failures[0] }
}

function Stop-IsolatedGameSession {
    <#
    .SYNOPSIS
    Ends a session Start-IsolatedGameSession began: stops the game and the pose sender, runs
    -Collect, puts back every file and folder the session saved, and releases the rig.
    .DESCRIPTION
    Every step runs whatever the one before it did, and the first failure is thrown at the end.
    -Collect runs after the game has stopped and before its files are put back, and is handed the
    session: copy out the logs and the config as the run left them.

    The rig is released only once the files are back. If the restore fails this throws with the
    rig still held and the saved state still in the session's StateFolder: run
    Restore-GameTestState on it. Until then Enter-GameRig refuses the game to everyone, naming
    that folder.

    A file is tried for -RestoreRetrySeconds, because a game holds its own DLLs for some seconds
    after its process has gone.
    #>
    param(
        $Session,
        # In place of -Session, in a later process than the one that started it.
        [string]$SessionFile = '',
        [scriptblock]$Collect,
        [int]$RestoreRetrySeconds = 30
    )
    if (-not $Session) {
        if (-not $SessionFile) { throw 'Stop-IsolatedGameSession needs -Session or -SessionFile' }
        $Session = Get-IsolatedGameSession -SessionFile $SessionFile
    }
    $rig = Enter-GameRig -Game $Session.ProcessName -Owner $Session.Owner -Token $Session.RigToken
    Complete-IsolatedGameSession -Session $Session -Rig $rig -Collect $Collect -RestoreRetrySeconds $RestoreRetrySeconds
}

function Invoke-IsolatedGameSession {
    <#
    .SYNOPSIS
    A whole background test round one script block: takes the game's rig, saves what the test
    changes, launches, runs -Run with the session, then stops the game, puts everything back
    and releases the rig, on every way out: the block throwing, the game not starting, the rig
    not coming free in time.
    .DESCRIPTION
    Takes what Start-IsolatedGameSession takes, and -Collect as Stop-IsolatedGameSession does.
    Returns what -Run returns. What the block does is the mod's own: the way into a save, the
    inputs, the poses, the captures.

    When the block throws and the teardown fails as well, the block's error is the one thrown and
    the teardown's is a warning.
    #>
    param(
        [Parameter(Mandatory)][string]$ProcessName,
        [Parameter(Mandatory)][string]$Launch,
        [Parameter(Mandatory)][string]$ModFolder,
        [Parameter(Mandatory)][string]$Owner,
        # Handed the session: Invoke-GameInput, Set-TestPose, Save-GameCapture, Wait-GameLogLine.
        [Parameter(Mandatory)][scriptblock]$Run,
        [scriptblock]$Collect,
        [string]$What = '',
        [string[]]$Files = @(),
        [string[]]$Folders = @(),
        [string]$StateFolder = '',
        [scriptblock]$Prepare,
        [string]$IniPath = '',
        [int]$Port = 0,
        [string]$PoseFile = '',
        [string]$WorkingDirectory = '',
        [hashtable]$Environment = @{},
        [int]$StartTimeoutSeconds = 120,
        [int]$SettleSeconds = 60,
        [ValidateSet('native', 'managed')][string]$ModHost = 'native',
        [string]$HostDll = $script:HostDllBuild,
        [string[]]$Processes = @(),
        [switch]$WholeGpu,
        [int]$WaitSeconds = 3600,
        [int]$RestoreRetrySeconds = 30
    )
    $start = @{} + $PSBoundParameters
    'Run', 'Collect', 'RestoreRetrySeconds' | ForEach-Object { $start.Remove($_) }
    $session = Start-IsolatedGameSession @start
    $thrown = $false
    try {
        & $Run $session
    } catch {
        $thrown = $true
        throw
    } finally {
        try { Stop-IsolatedGameSession -Session $session -Collect $Collect -RestoreRetrySeconds $RestoreRetrySeconds }
        catch {
            if (-not $thrown) { throw }
            Write-Warning "and the session could not be put back: $_"
        }
    }
}

Export-ModuleMember -Function Get-PeImports, Get-IsolatedInputCoverage, Get-GameInputPaths, Assert-IsolatedInputCovers,
    Get-ModTestPort, Copy-IsolatedInputHost, Start-IsolatedGame, Invoke-GameInput, Save-GameCapture, Test-CaptureDiffers, Test-IsolatedInputProof,
    Stop-IsolatedGame, Save-GameTestState, Restore-GameTestState, Start-TestPoseSender, Set-TestPose,
    Set-ModTestPort, Get-GameRig, Enter-GameRig, Exit-GameRig, Wait-GameLogLine, Start-GameProcessSampler, Stop-GameProcessSampler,
    Start-IsolatedGameSession, Stop-IsolatedGameSession, Invoke-IsolatedGameSession, Get-IsolatedGameSession, Save-IsolatedGameSession
