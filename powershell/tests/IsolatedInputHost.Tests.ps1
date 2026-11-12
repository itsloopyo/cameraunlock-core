#!/usr/bin/env pwsh
#Requires -Version 5.1
# ============================================================================
# Tests for the isolated-input host of a managed mod
# ============================================================================
# Run: pixi run test-isolated-input-host
#
# The route a C# mod takes, in a process that is not a game: Windows PowerShell
# loads the net472 CameraUnlock.Core, IsolatedInput.StartIfAsked loads the host
# DLL (cpp/tools/isolated_input_host), and a command file holds a key down. The
# key state functions then have to report that key, and only that key, whatever
# is held on the real keyboard.
#
# The detours go into a child process this test starts and ends. That process
# has one window, made off screen and never activated, whose thread hides its
# cursor before the host starts: it stands in for a game that hid its cursor
# before the mod was loaded. What a game's window receives is not covered here.
# ============================================================================

param([switch]$Child, [string]$Folder, [string]$CoreDll, [string]$HostDll)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:Failures = 0
function Check {
    param([string]$Name, [bool]$Condition, [string]$Detail = '')
    if ($Condition) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name - $Detail" -ForegroundColor Red
        $script:Failures++
    }
}

if (-not $Child) {
    $core = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $builtCore = Join-Path $core 'csharp\src\CameraUnlock.Core\bin\Debug\net472\CameraUnlock.Core.dll'
    $builtHost = Join-Path $core 'cpp\tools\isolated_input_host\build\Release\CameraUnlockIsolatedInput.dll'
    foreach ($built in $builtCore, $builtHost) {
        if (-not (Test-Path $built)) { throw "$built is not built. Run this through: pixi run test-isolated-input-host" }
    }
    $scratch = Join-Path $env:TEMP ("isolated-input-host-" + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force $scratch | Out-Null
    try {
        # Windows PowerShell, whatever runs this: the plugin route is .NET Framework and 64-bit.
        $powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
        & $powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Child -Folder $scratch -CoreDll $builtCore -HostDll $builtHost
        $exit = $LASTEXITCODE
        Write-Host '--- CameraUnlockIsolatedInput.log'
        Get-Content (Join-Path $scratch 'mod\CameraUnlockIsolatedInput.log') | ForEach-Object { Write-Host $_ }
    } finally {
        Remove-Item $scratch -Recurse -Force -ErrorAction SilentlyContinue
    }
    exit $exit
}

Import-Module (Join-Path (Split-Path -Parent $PSScriptRoot) 'IsolatedGameTest.psm1') -Force
Add-Type -Path $CoreDll
Add-Type -Namespace CameraUnlockHostTest -Name User32 -MemberDefinition @'
[DllImport("user32.dll")] public static extern short GetKeyState(int vk);
[DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vk);
[DllImport("user32.dll")] public static extern bool GetKeyboardState(byte[] state);
[DllImport("kernel32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr LoadLibraryW(string name);
[DllImport("xinput1_4.dll")] public static extern int XInputGetState(int index, byte[] state);
[DllImport("xinput1_4.dll")] public static extern int XInputGetCapabilities(int index, int flags, byte[] capabilities);
[DllImport("user32.dll")] public static extern int ShowCursor(bool show);
[DllImport("user32.dll")] public static extern IntPtr SetCursor(IntPtr cursor);
[DllImport("user32.dll")] public static extern IntPtr LoadCursorW(IntPtr instance, IntPtr name);
[DllImport("user32.dll")] public static extern bool GetCursorInfo(int[] info);
[DllImport("user32.dll")] public static extern IntPtr GetFocus();
'@
# Only an XInput DLL the process has loaded when the host starts is detoured, as in a game.
if ([CameraUnlockHostTest.User32]::LoadLibraryW('xinput1_4.dll') -eq [IntPtr]::Zero) { throw 'xinput1_4.dll did not load' }

function Throws {
    param([scriptblock]$Block, [string]$Pattern)
    try { & $Block | Out-Null; return $false } catch { return $_.Exception.Message -match $Pattern }
}

function Send {
    param([int]$Sequence, [string[]]$Commands)
    $commandFile = Join-Path $mod 'CameraUnlockInput.txt'
    [IO.File]::WriteAllLines("$commandFile.tmp", @("$Sequence") + $Commands)
    Move-Item "$commandFile.tmp" $commandFile -Force
}

function WaitPlayed {
    param([int]$Sequence)
    $commandFile = Join-Path $mod 'CameraUnlockInput.txt'
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        $done = Get-Content "$commandFile.done" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($done -and $done.StartsWith("$Sequence")) { return }
        Start-Sleep -Milliseconds 20
    }
    throw "script $Sequence was not played within 10 seconds"
}

function Play {
    param([int]$Sequence, [string[]]$Commands)
    Send $Sequence $Commands
    WaitPlayed $Sequence
}

function KeyboardState {
    $state = New-Object byte[] 256
    if (-not [CameraUnlockHostTest.User32]::GetKeyboardState($state)) { throw 'GetKeyboardState failed' }
    , $state
}

# CURSORINFO as six ints: cbSize, flags, hCursor (two), ptScreenPos.
function CursorInfo {
    param([int]$Size = 24)
    $info = New-Object int[] 6
    $info[0] = $Size
    $read = [CameraUnlockHostTest.User32]::GetCursorInfo($info)
    [pscustomobject]@{ Read = $read; Showing = ($info[1] -band 1) -ne 0; Cursor = [long]$info[2] -bor ([long]$info[3] -shl 32) }
}

function KeysDown {
    $state = KeyboardState
    @($state | Where-Object { $_ -band 0x80 }).Count
}

$lines = New-Object System.Collections.Generic.List[string]
$log = [Action[string]] { param($line) $lines.Add($line) }
$mod = Join-Path $Folder 'mod'
New-Item -ItemType Directory -Force $mod | Out-Null
$keyA = 0x41
$keyB = 0x42

Check 'without the command file nothing starts' (-not [CameraUnlock.Core.Dev.IsolatedInput]::StartIfAsked($mod, $log))
Check 'and nothing is logged' ($lines.Count -eq 0)

Set-Content (Join-Path $mod 'CameraUnlockInput.txt') '0' -Encoding ASCII
Check 'with the command file and no host DLL it throws naming the DLL' `
    (Throws { [CameraUnlock.Core.Dev.IsolatedInput]::StartIfAsked($mod, $log) } 'host DLL is missing: .*CameraUnlockIsolatedInput\.dll')

Copy-IsolatedInputHost -ModFolder $mod -HostDll $HostDll | Out-Null
# The copy StartIfAsked loads, not the build: a second file is a second module with a state of its own.
Add-Type -Namespace CameraUnlockHostTest -Name Host -MemberDefinition @"
[DllImport(@"$(Join-Path $mod 'CameraUnlockIsolatedInput.dll')")] public static extern int CameraUnlockHoldIsolatedKey(int vk, int down);
[DllImport(@"$(Join-Path $mod 'CameraUnlockIsolatedInput.dll')")] public static extern int CameraUnlockPublishToGameLink(byte[] json);
[DllImport(@"$(Join-Path $mod 'CameraUnlockIsolatedInput.dll')")] public static extern int CameraUnlockTakeModCommand(byte[] buffer, int size);
"@
# A free port for the game link, which starts with the detours when the port file is there.
$probe = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, 0)
$probe.Start()
$linkPort = $probe.LocalEndpoint.Port
$probe.Stop()
Set-Content (Join-Path $mod 'CameraUnlockLink.txt') "$linkPort" -Encoding ASCII
Check 'a key is not held before the host has started' ([CameraUnlockHostTest.Host]::CameraUnlockHoldIsolatedKey(0x43, 1) -eq 1)
Add-Type -ReferencedAssemblies System.Windows.Forms, System.Drawing -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;
public class NoActivateForm : Form {
    protected override bool ShowWithoutActivation { get { return true; } }
    // The WM_ACTIVATE and WM_SETFOCUS the host posts would have Windows give this
    // thread the focus. Kingdom Come: Deliverance's thread still had none after them.
    protected override void WndProc(ref Message m) {
        if (m.Msg == 0x0006 || m.Msg == 0x0007) return;
        base.WndProc(ref m);
    }
    protected override CreateParams CreateParams {
        get { CreateParams p = base.CreateParams; p.ExStyle |= 0x08000000 | 0x80; return p; }
    }
}
public static class CursorWindow {
    [DllImport("user32.dll")] static extern int ShowCursor(bool show);
    [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] static extern IntPtr GetFocus();
    static NoActivateForm Window;
    public static IntPtr Handle;
    public static int HiddenCount;
    // Hides the cursor on a thread of its own and makes that thread's window, still hidden.
    public static void Start() {
        ManualResetEvent made = new ManualResetEvent(false);
        Thread thread = new Thread(() => {
            HiddenCount = ShowCursor(false);
            NoActivateForm form = new NoActivateForm();
            form.StartPosition = FormStartPosition.Manual;
            form.FormBorderStyle = FormBorderStyle.None;
            form.ShowInTaskbar = false;
            form.Bounds = new System.Drawing.Rectangle(-32000, -32000, 200, 200);
            Handle = form.Handle;
            Window = form;
            made.Set();
            Application.Run();
        });
        thread.IsBackground = true;
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        made.WaitOne();
    }
    public static void Show() { ShowWindow(Handle, 4); }
    public static IntPtr FocusOnItsThread() { return (IntPtr)Window.Invoke(new Func<IntPtr>(GetFocus)); }
}
'@
[CursorWindow]::Start()
Check 'the stand-in window''s thread hid its cursor before the host started' ([CursorWindow]::HiddenCount -eq -1) "count $([CursorWindow]::HiddenCount)"
# What other isolated games and the harness read so as never to hand the foreground to this one.
Check 'the process does not read as isolated before the host starts' (-not (Test-IsolatedInputProcess -ProcessId $PID))
Check 'with both it starts' ([CameraUnlock.Core.Dev.IsolatedInput]::StartIfAsked($mod, $log))
Check 'and logs where the input now comes from' ($lines.Count -eq 1 -and $lines[0] -match 'CameraUnlockInput\.txt') ($lines -join ' | ')
Check 'a second start in the same process throws' `
    (Throws { [CameraUnlock.Core.Dev.IsolatedInput]::StartIfAsked($mod, $log) } 'already started')
Check 'once started, the process reads as isolated' (Test-IsolatedInputProcess -ProcessId $PID)

Check 'no key is down before a script holds one' ((KeysDown) -eq 0) "$(KeysDown)"

# The first read of the file only records its number, so one poll has to pass before a script.
Start-Sleep -Milliseconds 300
Play 1 'down A'
$state = [CameraUnlockHostTest.User32]::GetKeyState($keyA)
Check 'GetKeyState reports the held key down, toggled on' ($state -eq -32767) "$state"
Check 'GetKeyboardState reports it down, toggled on' ((KeyboardState)[$keyA] -eq 0x81) "$((KeyboardState)[$keyA])"
Check 'GetKeyboardState reports no other key down' ((KeysDown) -eq 1) "$(KeysDown)"
Check 'GetAsyncKeyState reports it down' (([CameraUnlockHostTest.User32]::GetAsyncKeyState($keyA) -band 0x8000) -ne 0)
Check 'GetKeyState reports a key the script does not hold as up' ([CameraUnlockHostTest.User32]::GetKeyState($keyB) -eq 0)

Play 2 'up A'
$state = [CameraUnlockHostTest.User32]::GetKeyState($keyA)
Check 'GetKeyState reports the released key up, still toggled on' ($state -eq 1) "$state"
Check 'GetKeyboardState reports it up, still toggled on' ((KeyboardState)[$keyA] -eq 0x01) "$((KeyboardState)[$keyA])"

Play 3 'tap A'
$state = [CameraUnlockHostTest.User32]::GetKeyState($keyA)
Check 'a second press turns the toggle off' ($state -eq 0) "$state"

$keyC = 0x43
Check 'a key held through the export is down to GetAsyncKeyState' `
    ([CameraUnlockHostTest.Host]::CameraUnlockHoldIsolatedKey($keyC, 1) -eq 0 -and ([CameraUnlockHostTest.User32]::GetAsyncKeyState($keyC) -band 0x8000) -ne 0)
Check 'and to GetKeyState, toggled on' ([CameraUnlockHostTest.User32]::GetKeyState($keyC) -eq -32767)
Check 'and let go through it, up again' `
    ([CameraUnlockHostTest.Host]::CameraUnlockHoldIsolatedKey($keyC, 0) -eq 0 -and ([CameraUnlockHostTest.User32]::GetAsyncKeyState($keyC) -band 0x8000) -eq 0)
Check 'a key code that is none is refused' ([CameraUnlockHostTest.Host]::CameraUnlockHoldIsolatedKey(256, 1) -eq 2 -and [CameraUnlockHostTest.Host]::CameraUnlockHoldIsolatedKey(0, 1) -eq 2)

function Utf8Z([string]$Text) { [Text.Encoding]::UTF8.GetBytes($Text + [char]0) }

$link = New-Object System.Net.Sockets.TcpClient
$link.Connect('127.0.0.1', $linkPort)
$link.ReceiveTimeout = 5000
$linkReader = New-Object IO.StreamReader($link.GetStream(), [Text.Encoding]::UTF8)
$linkWriter = New-Object IO.StreamWriter($link.GetStream(), (New-Object Text.UTF8Encoding $false))
$linkWriter.NewLine = "`n"
$linkWriter.AutoFlush = $true
$hello = $linkReader.ReadLine()
Check 'the game link greets a client with hello' ($hello -match '^\{"type":"hello","protocol":1,"pid":\d+\}$') $hello

$linkWriter.WriteLine('down B')
$deadline = (Get-Date).AddSeconds(5)
while ((Get-Date) -lt $deadline -and ([CameraUnlockHostTest.User32]::GetAsyncKeyState($keyB) -band 0x8000) -eq 0) { Start-Sleep -Milliseconds 20 }
Check 'a command from the link holds its key' (([CameraUnlockHostTest.User32]::GetAsyncKeyState($keyB) -band 0x8000) -ne 0)
# This process has no window, so the key's message has nowhere to go, and the link says so
# as the command file does.
$reply = $linkReader.ReadLine()
Check 'and says the key could not reach a game window' ($reply -match '"line":"down B".*no game window') $reply
$linkWriter.WriteLine('up B')
$reply = $linkReader.ReadLine()
Check 'the key is let go the same way' ($reply -match '"line":"up B"' -and ([CameraUnlockHostTest.User32]::GetAsyncKeyState($keyB) -band 0x8000) -eq 0) $reply

$linkWriter.WriteLine('wait 100')
$reply = $linkReader.ReadLine()
Check 'the link refuses wait, naming the line' ($reply -match '"line":"wait 100"' -and $reply -match 'as it arrives') $reply
$linkWriter.WriteLine('tap NoSuchKey')
$reply = $linkReader.ReadLine()
Check 'a line that does not parse comes back as an error' ($reply -match '"type":"error","line":"tap NoSuchKey"') $reply

$linkWriter.WriteLine('mod pad 1 join')
$buffer = New-Object byte[] 4
$deadline = (Get-Date).AddSeconds(5)
$taken = 0
while ((Get-Date) -lt $deadline -and ($taken = [CameraUnlockHostTest.Host]::CameraUnlockTakeModCommand($buffer, $buffer.Length)) -eq 0) { Start-Sleep -Milliseconds 20 }
Check 'a mod command too long for the buffer says the size it needs' ($taken -eq -11) "$taken"
$buffer = New-Object byte[] 11
$taken = [CameraUnlockHostTest.Host]::CameraUnlockTakeModCommand($buffer, $buffer.Length)
Check 'and is kept for the next call' ($taken -eq 10 -and [Text.Encoding]::UTF8.GetString($buffer, 0, 10) -eq 'pad 1 join') "$taken"
Check 'then nothing waits' ([CameraUnlockHostTest.Host]::CameraUnlockTakeModCommand($buffer, $buffer.Length) -eq 0)

Check 'the mod publishes to the client' ([CameraUnlockHostTest.Host]::CameraUnlockPublishToGameLink((Utf8Z '{"type":"state","hp":3}')) -eq 0)
$published = $linkReader.ReadLine()
Check 'and the client reads it' ($published -eq '{"type":"state","hp":3}') $published
Check 'what is not one JSON object is not published' ([CameraUnlockHostTest.Host]::CameraUnlockPublishToGameLink((Utf8Z 'hp 3')) -eq 1)
$link.Close()

$notConnected = 1167
$capabilities = New-Object byte[] 20
Check 'XInputGetCapabilities answers no controller before a script plugs one in' `
    ([CameraUnlockHostTest.User32]::XInputGetCapabilities(0, 1, $capabilities) -eq $notConnected)
$state = New-Object byte[] 16
Send 4 'pad 0 a down'
$deadline = (Get-Date).AddSeconds(5)
do { $result = [CameraUnlockHostTest.User32]::XInputGetState(0, $state) } while ($result -ne 0 -and (Get-Date) -lt $deadline)
Check 'a pad command plugs the pad in with nothing held' ($result -eq 0 -and [BitConverter]::ToUInt16($state, 4) -eq 0) "result $result, buttons $([BitConverter]::ToUInt16($state, 4))"
Start-Sleep -Milliseconds 300
[void][CameraUnlockHostTest.User32]::XInputGetState(0, $state)
Check 'and holds its first command back while the game has not read the pad enough' ([BitConverter]::ToUInt16($state, 4) -eq 0) "buttons $([BitConverter]::ToUInt16($state, 4))"
for ($read = 0; $read -lt 3; $read++) { [void][CameraUnlockHostTest.User32]::XInputGetState(0, $state) }
WaitPlayed 4
$result = [CameraUnlockHostTest.User32]::XInputGetCapabilities(0, 1, $capabilities)
Check 'XInputGetCapabilities describes the script pad as a gamepad' `
    ($result -eq 0 -and $capabilities[0] -eq 1 -and $capabilities[1] -eq 1) "result $result, type $($capabilities[0]), subtype $($capabilities[1])"
Check 'with its buttons, triggers and sticks there' `
    ([BitConverter]::ToUInt16($capabilities, 4) -ne 0 -and $capabilities[6] -ne 0 -and $capabilities[7] -ne 0 -and [BitConverter]::ToInt16($capabilities, 8) -ne 0)
$result = [CameraUnlockHostTest.User32]::XInputGetState(0, $state)
Check 'XInputGetState reports its A button held' ($result -eq 0 -and ([BitConverter]::ToUInt16($state, 4) -band 0x1000) -ne 0) "result $result"
Check 'XInputGetCapabilities answers no controller at an index the script has not named' `
    ([CameraUnlockHostTest.User32]::XInputGetCapabilities(1, 1, $capabilities) -eq $notConnected)

# The child is not in front and its one window is still hidden, so Windows would answer
# GetCursorInfo for the window that is, and nothing this process does would hide that cursor.
$arrow = [CameraUnlockHostTest.User32]::LoadCursorW([IntPtr]::Zero, [IntPtr]32512).ToInt64()
$info = CursorInfo
Check 'GetCursorInfo answers the arrow, shown, before the game sets a cursor' ($info.Read -and $info.Showing -and $info.Cursor -eq $arrow) "$info"
# The stand-in window is hidden and was never activated, so Windows names no focus on its thread.
$focus = [CursorWindow]::FocusOnItsThread()
Check 'GetFocus on the window''s thread answers what Windows does until the window is taken for the game''s' ($focus -eq [IntPtr]::Zero) "$focus"

# Shown without activation, the window is taken for the game's, and its thread's
# display count is read there: below zero, though this thread's is at zero.
[CursorWindow]::Show()
$read = 'the game window''s thread has a cursor display count of -1, so GetCursorInfo answers the cursor hidden'
$deadline = (Get-Date).AddSeconds(5)
do {
    Start-Sleep -Milliseconds 100
    $hostLog = Get-Content (Join-Path $mod 'CameraUnlockIsolatedInput.log') -Raw
} while ($hostLog -notmatch $read -and (Get-Date) -lt $deadline)
Check 'the host reads the display count on the game window''s thread once it finds the window' ($hostLog -match $read) $hostLog
$info = CursorInfo
Check 'GetCursorInfo then answers the cursor that thread hid before the host started as hidden' ($info.Read -and -not $info.Showing) "$info"
$focus = [CursorWindow]::FocusOnItsThread()
Check 'GetFocus on the game window''s thread then answers that window' ($focus -eq [CursorWindow]::Handle) "$focus, window $([CursorWindow]::Handle)"
$focus = [CameraUnlockHostTest.User32]::GetFocus()
Check 'and on another thread answers what Windows does: none' ($focus -eq [IntPtr]::Zero) "$focus"
$count = [CameraUnlockHostTest.User32]::ShowCursor($true)
Check 'and a ShowCursor after that is followed again' ($count -eq 1 -and (CursorInfo).Showing) "count $count"
[CameraUnlockHostTest.User32]::ShowCursor($false) | Out-Null
$count = [CameraUnlockHostTest.User32]::ShowCursor($false)
$info = CursorInfo
Check 'ShowCursor(FALSE) takes the display count below zero and GetCursorInfo answers it hidden' ($count -eq -1 -and $info.Read -and -not $info.Showing) "count $count, $info"
$count = [CameraUnlockHostTest.User32]::ShowCursor($true)
Check 'ShowCursor(TRUE) brings it back to zero, shown' ($count -eq 0 -and (CursorInfo).Showing) "count $count"
[CameraUnlockHostTest.User32]::SetCursor([IntPtr]::Zero) | Out-Null
$info = CursorInfo
Check 'SetCursor(NULL) hides it, with no cursor handle' ($info.Read -and -not $info.Showing -and $info.Cursor -eq 0) "$info"
$previous = [CameraUnlockHostTest.User32]::SetCursor([IntPtr]$arrow)
$info = CursorInfo
Check 'SetCursor with a cursor shows that cursor' ($info.Showing -and $info.Cursor -eq $arrow) "$info"
Check 'and SetCursor still answers the cursor it replaced' ($previous -eq [IntPtr]::Zero) "$previous"
[CameraUnlockHostTest.User32]::ShowCursor($false) | Out-Null
Check 'a cursor set while the count is below zero stays hidden' (-not (CursorInfo).Showing)
[CameraUnlockHostTest.User32]::ShowCursor($true) | Out-Null
Check 'GetCursorInfo with a wrong cbSize is refused, as Windows refuses it' (-not (CursorInfo -Size 20).Read)

# DirectInput 7: dinput.dll is loaded only now, after the host started, as a game may load it
# late. One mouse is made through IDirectInput7A's CreateDeviceEx as IDirectInputDevice7A and
# one through DirectInputCreateA for DirectInput 5; one keyboard through IDirectInput7A's
# CreateDevice and one through DirectInputCreateW for DirectInput 3. All are read buffered in
# 16-byte records.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class DirectInput7 {
    [DllImport("dinput.dll")] static extern int DirectInputCreateEx(IntPtr instance, int version, ref Guid iid, out IntPtr directInput, IntPtr outer);
    [DllImport("dinput.dll")] static extern int DirectInputCreateA(IntPtr instance, int version, out IntPtr directInput, IntPtr outer);
    [DllImport("dinput.dll")] static extern int DirectInputCreateW(IntPtr instance, int version, out IntPtr directInput, IntPtr outer);
    [DllImport("kernel32.dll")] static extern IntPtr GetModuleHandleW(IntPtr name);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int CreateDeviceFn(IntPtr self, ref Guid device, out IntPtr made, IntPtr outer);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int CreateDeviceExFn(IntPtr self, ref Guid device, ref Guid iid, out IntPtr made, IntPtr outer);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int ThisFn(IntPtr self);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int GetDeviceDataFn(IntPtr self, int size, byte[] data, ref int count, int flags);
    static T Slot<T>(IntPtr o, int slot) where T : class {
        return Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(Marshal.ReadIntPtr(o), slot * IntPtr.Size), typeof(T)) as T;
    }
    static Guid DirectInput7A = new Guid("9A4CB684-236D-11D3-8E9D-00C04F6844AE");
    static Guid Device7A = new Guid("57D7C6BC-2356-11D3-8E9D-00C04F6844AE");
    static Guid SysMouse = new Guid("6F1D2B60-D5A0-11CF-BFC7-444553540000");
    static Guid SysKeyboard = new Guid("6F1D2B61-D5A0-11CF-BFC7-444553540000");
    public static IntPtr Mouse, Keyboard, MouseA5, KeyboardW3;
    static int MakeOne(int made, IntPtr di, ref Guid device, out IntPtr result) {
        result = IntPtr.Zero;
        if (made != 0) return made;
        made = Slot<CreateDeviceFn>(di, 3)(di, ref device, out result, IntPtr.Zero);
        Slot<ThisFn>(di, 2)(di);
        return made;
    }
    public static int Make() {
        IntPtr instance = GetModuleHandleW(IntPtr.Zero);
        IntPtr di;
        int result = DirectInputCreateEx(instance, 0x0700, ref DirectInput7A, out di, IntPtr.Zero);
        if (result != 0) return result;
        result = Slot<CreateDeviceExFn>(di, 9)(di, ref SysMouse, ref Device7A, out Mouse, IntPtr.Zero);
        if (result != 0) { Slot<ThisFn>(di, 2)(di); return result; }
        result = MakeOne(0, di, ref SysKeyboard, out Keyboard);
        if (result != 0) return result;
        result = DirectInputCreateA(instance, 0x0500, out di, IntPtr.Zero);
        result = MakeOne(result, di, ref SysMouse, out MouseA5);
        if (result != 0) return result;
        result = DirectInputCreateW(instance, 0x0300, out di, IntPtr.Zero);
        return MakeOne(result, di, ref SysKeyboard, out KeyboardW3);
    }
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int SetDataFormatFn(IntPtr self, IntPtr format);
    static IntPtr Unmanaged(Guid guid) {
        IntPtr at = Marshal.AllocHGlobal(16);
        Marshal.Copy(guid.ToByteArray(), 0, at, 16);
        return at;
    }
    // A DIDATAFORMAT laid out as c_dfDIMouse or c_dfDIKeyboard is, in memory that is never freed.
    static IntPtr Format(int dataSize, IntPtr[] guids, int[] offsets, uint[] types) {
        int objectSize = IntPtr.Size == 8 ? 24 : 16;
        IntPtr objects = Marshal.AllocHGlobal(objectSize * guids.Length);
        for (int i = 0; i < guids.Length; i++) {
            IntPtr o = objects + objectSize * i;
            Marshal.WriteIntPtr(o, guids[i]);
            Marshal.WriteInt32(o, IntPtr.Size, offsets[i]);
            Marshal.WriteInt32(o, IntPtr.Size + 4, unchecked((int)types[i]));
            Marshal.WriteInt32(o, IntPtr.Size + 8, 0);
        }
        int formatSize = IntPtr.Size == 8 ? 32 : 24;
        IntPtr format = Marshal.AllocHGlobal(formatSize);
        Marshal.WriteInt32(format, 0, formatSize);
        Marshal.WriteInt32(format, 4, objectSize);
        Marshal.WriteInt32(format, 8, 2);
        Marshal.WriteInt32(format, 12, dataSize);
        Marshal.WriteInt32(format, 16, guids.Length);
        Marshal.WriteIntPtr(format, formatSize - IntPtr.Size, objects);
        return format;
    }
    const uint AnyInstance = 0x00FFFF00, Axis = 0x03, Button = 0x0C, Optional = 0x80000000;
    public static int SetFormats() {
        IntPtr x = Unmanaged(new Guid("A36D02E0-C9F3-11CF-BFC7-444553540000"));
        IntPtr y = Unmanaged(new Guid("A36D02E1-C9F3-11CF-BFC7-444553540000"));
        IntPtr z = Unmanaged(new Guid("A36D02E2-C9F3-11CF-BFC7-444553540000"));
        IntPtr mouse = Format(16, new IntPtr[] { x, y, z, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero },
                              new int[] { 0, 4, 8, 12, 13, 14, 15 },
                              new uint[] { Axis | AnyInstance, Axis | AnyInstance, Axis | AnyInstance | Optional,
                                           Button | AnyInstance, Button | AnyInstance, Button | AnyInstance | Optional,
                                           Button | AnyInstance | Optional });
        IntPtr key = Unmanaged(new Guid("55728220-D33C-11CF-BFC7-444553540000"));
        IntPtr[] keys = new IntPtr[256];
        int[] keyOffsets = new int[256];
        uint[] keyTypes = new uint[256];
        for (int i = 0; i < 256; i++) { keys[i] = key; keyOffsets[i] = i; keyTypes[i] = Button | ((uint)i << 8) | Optional; }
        IntPtr keyboard = Format(256, keys, keyOffsets, keyTypes);
        int result = 0;
        foreach (IntPtr device in new IntPtr[] { Mouse, MouseA5 }) if (result == 0) result = Slot<SetDataFormatFn>(device, 11)(device, mouse);
        foreach (IntPtr device in new IntPtr[] { Keyboard, KeyboardW3 }) if (result == 0) result = Slot<SetDataFormatFn>(device, 11)(device, keyboard);
        return result;
    }
    public static int Acquire(IntPtr device) { return Slot<ThisFn>(device, 7)(device); }
    // The records read, as offset and data pairs. Bytes past the last record are checked
    // to be as they were left, so a record wider than 16 bytes is caught.
    public static int Read(IntPtr device, out uint[] pairs, out bool untouchedPast) {
        byte[] data = new byte[16 * 32];
        for (int i = 0; i < data.Length; i++) data[i] = 0xCC;
        int count = 32;
        int result = Slot<GetDeviceDataFn>(device, 10)(device, 16, data, ref count, 0);
        pairs = new uint[2 * Math.Max(count, 0)];
        for (int i = 0; i < count; i++) {
            pairs[2 * i] = BitConverter.ToUInt32(data, 16 * i);
            pairs[2 * i + 1] = BitConverter.ToUInt32(data, 16 * i + 4);
        }
        untouchedPast = true;
        for (int i = 16 * Math.Max(count, 0); i < data.Length; i++) untouchedPast = untouchedPast && data[i] == 0xCC;
        return result;
    }
    public static void Release() {
        foreach (IntPtr device in new IntPtr[] { Mouse, Keyboard, MouseA5, KeyboardW3 }) Slot<ThisFn>(device, 2)(device);
    }
}
'@
function HasRecord([uint32[]]$Pairs, [uint32]$Offset, [uint32]$Data) {
    for ($i = 0; $i -lt $Pairs.Count; $i += 2) { if ($Pairs[$i] -eq $Offset -and $Pairs[$i + 1] -eq $Data) { return $true } }
    $false
}
function PairsText([uint32[]]$Pairs) { ($Pairs | ForEach-Object { '0x{0:X}' -f $_ }) -join ' ' }

$made = [DirectInput7]::Make()
Check 'two mice and two keyboards are made through dinput.dll' ($made -eq 0) ('0x{0:X8}' -f $made)
# Nothing below can run on devices that were not made.
if ($made -eq 0) {
    $read = 'DirectInput 7 keyboard and mouse devices answer from the command file'
    $deadline = (Get-Date).AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
        $hostLog = Get-Content (Join-Path $mod 'CameraUnlockIsolatedInput.log') -Raw
    } while ($hostLog -notmatch $read -and (Get-Date) -lt $deadline)
    Check 'dinput.dll loaded after the host started is detoured once the command-file thread sees it' ($hostLog -match $read) $hostLog
    $result = [DirectInput7]::SetFormats()
    Check 'all four take the mouse and keyboard data formats' ($result -eq 0) ('0x{0:X8}' -f $result)
    $hostLog = Get-Content (Join-Path $mod 'CameraUnlockIsolatedInput.log') -Raw
    $mouseFormats = [regex]::Matches($hostLog, 'DirectInput mouse data format: 16 bytes, 7 objects, [1-9]\d* of them answered').Count
    $keyboardFormats = [regex]::Matches($hostLog, 'DirectInput keyboard data format: 256 bytes, 256 objects, [1-9]\d* of them answered').Count
    Check 'and the format read back answers the mice''s axes and buttons and every key' ($mouseFormats -eq 2 -and $keyboardFormats -eq 2) $hostLog
    $devices = [DirectInput7]::Mouse, [DirectInput7]::Keyboard, [DirectInput7]::MouseA5, [DirectInput7]::KeyboardW3
    Check 'Acquire succeeds on all four' (@($devices | Where-Object { [DirectInput7]::Acquire($_) -ne 0 }).Count -eq 0)
    $pairs = $null; $past = $false
    $result = [DirectInput7]::Read([DirectInput7]::Mouse, [ref]$pairs, [ref]$past)
    Check 'the mouse answers GetDeviceData with nothing before a script moves it' ($result -eq 0 -and $pairs.Count -eq 0) "0x$('{0:X8}' -f $result), $(PairsText $pairs)"
    foreach ($device in $devices | Select-Object -Skip 1) { [void][DirectInput7]::Read($device, [ref]$pairs, [ref]$past) }

    Play 5 'move 7 -3', 'mouse left click'
    $result = [DirectInput7]::Read([DirectInput7]::Mouse, [ref]$pairs, [ref]$past)
    Check 'the mouse answers the move as DIMOFS_X and DIMOFS_Y records' `
        ($result -eq 0 -and (HasRecord $pairs 0 7) -and (HasRecord $pairs 4 ([uint32]::MaxValue - 2))) (PairsText $pairs)
    Check 'and the click as DIMOFS_BUTTON0 down and up' ((HasRecord $pairs 12 0x80) -and (HasRecord $pairs 12 0)) (PairsText $pairs)
    Check 'in 16-byte records, writing nothing past the last one' $past
    $result = [DirectInput7]::Read([DirectInput7]::MouseA5, [ref]$pairs, [ref]$past)
    Check 'the mouse made through DirectInputCreateA for DirectInput 5 answers the same' `
        ($result -eq 0 -and (HasRecord $pairs 0 7) -and (HasRecord $pairs 12 0x80) -and $past) (PairsText $pairs)
    Play 6 'down A'
    $result = [DirectInput7]::Read([DirectInput7]::Keyboard, [ref]$pairs, [ref]$past)
    Check 'the keyboard made through CreateDevice answers a held key as a DIK_A record' `
        ($result -eq 0 -and $pairs.Count -eq 2 -and (HasRecord $pairs 0x1E 0x80) -and $past) (PairsText $pairs)
    $result = [DirectInput7]::Read([DirectInput7]::KeyboardW3, [ref]$pairs, [ref]$past)
    Check 'and so does the keyboard made through DirectInputCreateW for DirectInput 3' `
        ($result -eq 0 -and $pairs.Count -eq 2 -and (HasRecord $pairs 0x1E 0x80) -and $past) (PairsText $pairs)
    Play 7 'up A'
    [DirectInput7]::Release()
}

$hostLog = Get-Content (Join-Path $mod 'CameraUnlockIsolatedInput.log') -Raw
Check 'the host log names a DirectInput read through GetDeviceData in 16-byte records' ($hostLog -match 'through GetDeviceData \(16 bytes\)')
Check 'the host log says where the game link listens' ($hostLog -match "game link: listening on 127\.0\.0\.1:$linkPort")
Check 'the host log says the detours are in' ($hostLog -match 'takes its keyboard and mouse from the command file')
Check 'the host log names GetKeyState once it is read' ($hostLog -match 'through GetKeyState')
Check 'the host log names GetKeyboardState once it is read' ($hostLog -match 'through GetKeyboardState')
Check 'the host log names GetCursorInfo once it is read' ($hostLog -match 'GetCursorInfo is read')
Check 'the host log says when the plugged pad was read and its first command played' ($hostLog -match 'pad 0 plugged in; the game read it \d+ times in \d+ ms, then its first command was played')

if ($script:Failures -gt 0) {
    Write-Host "$($script:Failures) check(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host 'IsolatedInputHost: all checks passed' -ForegroundColor Green
exit 0
