// CameraUnlockRig.exe: the guard of a background test's game. Rig tooling, never
// shipped (docs/isolated-input.md, "The guarded launch").
//
//   CameraUnlockRig.x64.exe guard <folder>
//   CameraUnlockRig.<arch>.exe inject <pid> fresh|running <log folder>
//   CameraUnlockRig.x64.exe watch <folder> <seconds>
//
// `guard` reads <folder>\guard.txt (UTF-16, one key=value a line):
//   limit=<seconds>       the game is ended once the guard has run this long
//   name=<exe name>       a process of the run, by its executable's name or its whole path; any number
//   shim=0                watch and end only, no shim
//   launch=<exe>          start this one itself, suspended, with the shim in
//   arguments=<text>      its command line after the program
//   directory=<folder>    its working directory
// and writes there:
//   guard.ready           it is watching; nothing of the game is started before this
//   guard.launched        the process id of what `launch` started
//   guard.failed          why `launch` could not be started
//   guard.verdict         the one line that says why it ended the game
//   guard.log             that line, and the processes it took in
// It ends everything it took in and goes once <folder>\guard.stop is there.
//
// `watch` only writes down, in <folder>\watch.log, every change of the foreground
// window and of the display mode until <seconds> are up or <folder>\watch.stop is
// there: the witness a test of the guarded launch reads.
//
// The guard ends the game on the first of: a window of one of the run's processes is the
// foreground window; the display mode of any screen changes; the limit is up; one
// of the run's processes cannot be given the shim.

#include <Windows.h>

#include <Psapi.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "inject.h"

namespace {

std::wstring g_folder;
std::wstring g_toolFolder;
HANDLE g_job = nullptr;
bool g_shim = true;
bool g_ended = false;
ULONGLONG g_started = 0;
ULONGLONG g_endedAt = 0;

struct Member {
    DWORD pid;
    HANDLE process;
    std::wstring exe;
};
std::vector<Member> g_members;

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

void WriteWhole(const wchar_t* name, const std::wstring& text, bool append = false) {
    const std::wstring path = g_folder + L"\\" + name;
    const HANDLE file = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, nullptr, append ? OPEN_ALWAYS : CREATE_ALWAYS, 0,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const std::string bytes = Utf8(text);
    DWORD written = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
}

// A file whose appearing is the signal: whoever waits for it never reads it half written.
void WriteMarker(const wchar_t* name, const std::wstring& text) {
    const std::wstring part = std::wstring(name) + L".part";
    WriteWhole(part.c_str(), text);
    MoveFileExW((g_folder + L"\\" + part).c_str(), (g_folder + L"\\" + name).c_str(), MOVEFILE_REPLACE_EXISTING);
}

std::wstring Stamp() {
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t text[40];
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return text;
}

std::wstring Format(const wchar_t* format, ...) {
    wchar_t text[1500];
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(text, _TRUNCATE, format, arguments);
    va_end(arguments);
    return text;
}

void Log(const std::wstring& text) { WriteWhole(L"guard.log", Stamp() + L" " + text + L"\r\n", true); }

bool Exists(const wchar_t* name) { return GetFileAttributesW((g_folder + L"\\" + name).c_str()) != INVALID_FILE_ATTRIBUTES; }

std::wstring LeafOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring PathOf(HANDLE process) {
    wchar_t path[MAX_PATH * 2];
    DWORD size = MAX_PATH * 2;
    return QueryFullProcessImageNameW(process, 0, path, &size) ? path : L"";
}

std::wstring ExeOf(HANDLE process) { return LeafOf(PathOf(process)); }

// A name with a folder in it is the whole path of the executable, and only that file is the run's.
bool SameName(const std::wstring& path, const std::wstring& name) {
    const std::wstring mine = name.find_first_of(L"\\/") == std::wstring::npos ? LeafOf(path) : path;
    if (!_wcsicmp(mine.c_str(), name.c_str())) return true;
    return !_wcsicmp(mine.c_str(), (name + L".exe").c_str());
}

// ---------------------------------------------------------------------------
// The display
// ---------------------------------------------------------------------------

struct Screen {
    std::wstring device;
    DEVMODEW mode;
};

std::vector<Screen> Screens() {
    std::vector<Screen> screens;
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); ++i) {
        if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        Screen screen{device.DeviceName, {}};
        screen.mode.dmSize = sizeof(DEVMODEW);
        if (EnumDisplaySettingsExW(device.DeviceName, ENUM_CURRENT_SETTINGS, &screen.mode, 0)) screens.push_back(screen);
    }
    return screens;
}

// Asking every screen for its mode takes milliseconds, so it is asked when Windows
// says the display changed (a top-level window is told, shown or not) and every
// two seconds besides.
bool g_displayChanged = false;

LRESULT CALLBACK ListenerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DISPLAYCHANGE) g_displayChanged = true;
    return DefWindowProcW(window, message, wParam, lParam);
}

bool ListenForDisplayChanges() {
    WNDCLASSW listener{};
    listener.lpfnWndProc = &ListenerProc;
    listener.hInstance = GetModuleHandleW(nullptr);
    listener.lpszClassName = L"CameraUnlockRigGuard";
    return RegisterClassW(&listener) &&
           CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, listener.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, listener.hInstance, nullptr);
}

bool DisplayCheckDue(ULONGLONG now) {
    static ULONGLONG next = 0;
    if (!g_displayChanged && now < next) return false;
    g_displayChanged = false;
    next = now + 2000;
    return true;
}

std::wstring Describe(const std::vector<Screen>& screens) {
    std::wstring text;
    for (const Screen& screen : screens) {
        if (!text.empty()) text += L", ";
        text += Format(L"%lux%lu@%lu %lu bit at %ld,%ld", screen.mode.dmPelsWidth, screen.mode.dmPelsHeight, screen.mode.dmDisplayFrequency,
                       screen.mode.dmBitsPerPel, screen.mode.dmPosition.x, screen.mode.dmPosition.y);
    }
    return text;
}

// ---------------------------------------------------------------------------
// The run's processes
// ---------------------------------------------------------------------------

ULONGLONG CreatedAt(HANDLE process) {
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
    return (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

// A member's handle is kept, so its process id is not given to another process while the guard
// runs: a stub that started the game and exited is still its parent here. A process that names a
// member as its parent and is older than it was started by whatever had that id before.
bool IsChildOfMember(DWORD parent, HANDLE child) {
    for (const Member& member : g_members) {
        if (member.pid == parent) return CreatedAt(member.process) <= CreatedAt(child);
    }
    return false;
}

bool IsMember(DWORD pid) {
    for (const Member& member : g_members) {
        if (member.pid == pid && WaitForSingleObject(member.process, 0) == WAIT_TIMEOUT) return true;
    }
    return false;
}

int EndAll() {
    int ended = 0;
    for (const Member& member : g_members) {
        if (WaitForSingleObject(member.process, 0) == WAIT_TIMEOUT) ++ended;
    }
    TerminateJobObject(g_job, 0xF06);
    // One that could not be put in the job is ended by its handle.
    for (const Member& member : g_members) TerminateProcess(member.process, 0xF06);
    return ended;
}

void EndTheGame(const std::wstring& why) {
    if (g_ended) return;
    g_ended = true;
    g_endedAt = GetTickCount64();
    const int ended = EndAll();
    const std::wstring line =
        Format(L"%ls. Ended %d process(es), %llu ms after the guard came up.", why.c_str(), ended, g_endedAt - g_started);
    WriteMarker(L"guard.verdict", Stamp() + L" " + line + L"\r\n");
    Log(line);
}

BOOL CALLBACK HasVisibleWindow(HWND window, LPARAM parameter) {
    auto* look = reinterpret_cast<std::pair<DWORD, bool>*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == look->first && IsWindowVisible(window)) look->second = true;
    return TRUE;
}

ULONGLONG AgeMs(HANDLE process) {
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const ULONGLONG from = CreatedAt(process);
    const ULONGLONG to = (static_cast<ULONGLONG>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    return to > from ? (to - from) / 10000 : 0;
}

// A process someone else started (Steam, a launcher's service) and that belongs to
// the run: held still, put in the job so its own children are in it from birth,
// given the shim, let go.
void TakeIn(DWORD pid, ULONGLONG created, const std::wstring& exe, const wchar_t* because) {
    const HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!process) {
        const DWORD error = GetLastError();
        // Gone already is no fault. One that is there and cannot be opened cannot be shimmed or ended.
        const HANDLE look = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
        if (!look) return;
        const bool alive = WaitForSingleObject(look, 0) == WAIT_TIMEOUT;
        CloseHandle(look);
        if (alive) EndTheGame(Format(L"UNGUARDED: %ls (pid %lu) is %ls and could not be opened (error %lu)", exe.c_str(), pid, because, error));
        return;
    }
    // The process id came from a list read a moment ago and may have gone to another program since.
    if (CreatedAt(process) != created) {
        CloseHandle(process);
        return;
    }
    if (g_ended) {
        TerminateProcess(process, 0xF06);
        Log(Format(L"%ls (pid %lu) appeared after the game was ended, and was ended too", exe.c_str(), pid));
        CloseHandle(process);
        return;
    }
    rig::SuspendWholeProcess(process);
    const ULONGLONG age = AgeMs(process);
    g_members.push_back({pid, process, exe});
    const bool inJob = AssignProcessToJobObject(g_job, process) != FALSE;
    const DWORD jobError = GetLastError();
    std::pair<DWORD, bool> look{pid, false};
    EnumWindows(&HasVisibleWindow, reinterpret_cast<LPARAM>(&look));
    std::wstring shim = L"no shim asked for";
    if (g_shim) {
        if (rig::IsShimmed(pid)) {
            shim = L"the shim was in already";
        } else {
            const std::wstring why = rig::Inject(process, pid, g_toolFolder, g_folder, true);
            if (!why.empty()) {
                EndTheGame(Format(L"UNGUARDED: %ls (pid %lu) could not be given the shim: %ls", exe.c_str(), pid, why.c_str()));
                return;
            }
            shim = L"shim in";
        }
    }
    rig::ResumeWholeProcess(process);
    Log(Format(L"took in %ls (pid %lu), %ls, seen %llu ms after it started: %ls%ls%ls", exe.c_str(), pid, because, age, shim.c_str(),
               look.second ? L"; it already had a visible window" : L"",
               inJob ? L"" : Format(L"; it could not be put in the job (error %lu), so its children are found by their parent", jobError).c_str()));
}

// Asked of the process itself: a snapshot of every process for each new one took a third of a
// core on a machine where other work starts processes all the time.
DWORD ParentOf(HANDLE process) {
    struct Basic {
        void* exitStatus;
        void* peb;
        ULONG_PTR affinity;
        void* priority;
        ULONG_PTR pid;
        ULONG_PTR parent;
    } basic{};
    using Query_t = LONG (NTAPI*)(HANDLE, int, void*, ULONG, ULONG*);
    const auto query = reinterpret_cast<Query_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    return query(process, 0, &basic, sizeof(basic), nullptr) == 0 ? static_cast<DWORD>(basic.parent) : 0;
}

std::vector<std::wstring> g_names;
std::vector<DWORD> g_seen;

// True when the process was made part of the run.
bool Consider(DWORD pid) {
    if (pid == 0 || pid == GetCurrentProcessId()) return false;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    const std::wstring path = PathOf(process);
    const std::wstring exe = LeafOf(path);
    const ULONGLONG created = CreatedAt(process);
    BOOL inJob = FALSE;
    IsProcessInJob(process, g_job, &inJob);
    if (inJob) {
        // Started by a process of the run, whose shim put a copy in it before it ran.
        const HANDLE keep = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
        const DWORD error = GetLastError();
        CloseHandle(process);
        if (!keep) {
            EndTheGame(Format(L"UNGUARDED: %ls (pid %lu) was started by a process of the run and could not be opened (error %lu)", exe.c_str(), pid, error));
            return false;
        }
        g_members.push_back({pid, keep, exe});
        if (g_ended) TerminateProcess(keep, 0xF06);
        Log(Format(L"%ls (pid %lu) was started by a process of the run", exe.c_str(), pid));
        return true;
    }
    const bool child = !g_members.empty() && IsChildOfMember(ParentOf(process), process);
    CloseHandle(process);
    for (const std::wstring& name : g_names) {
        if (SameName(path, name)) {
            TakeIn(pid, created, exe, L"named for this run");
            return true;
        }
    }
    if (child) TakeIn(pid, created, exe, L"a child of a process of the run");
    return child;
}

void Scan() {
    DWORD pids[8192];
    DWORD bytes = 0;
    if (!EnumProcesses(pids, sizeof(pids), &bytes)) {
        EndTheGame(Format(L"UNGUARDED: the processes could not be listed (error %lu), so nothing new could be taken in", GetLastError()));
        return;
    }
    std::vector<DWORD> now(pids, pids + bytes / sizeof(DWORD));
    std::sort(now.begin(), now.end());
    std::vector<DWORD> fresh;
    for (DWORD pid : now) {
        if (!std::binary_search(g_seen.begin(), g_seen.end(), pid)) fresh.push_back(pid);
    }
    // A child can come before its parent in the list, so the new ones are gone over until a pass takes none in.
    for (bool took = true; took;) {
        took = false;
        for (DWORD& pid : fresh) {
            if (pid && Consider(pid)) {
                pid = 0;
                took = true;
            }
        }
    }
    g_seen = std::move(now);
}

void CheckForeground() {
    if (g_ended) return;
    const HWND window = GetForegroundWindow();
    if (!window) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!IsMember(pid)) return;
    wchar_t title[200] = L"", className[200] = L"";
    GetWindowTextW(window, title, 200);
    GetClassNameW(window, className, 200);
    std::wstring exe;
    for (const Member& member : g_members) if (member.pid == pid) exe = member.exe;
    EndTheGame(Format(L"FOREGROUND: a window of %ls (pid %lu) was the foreground window: \"%ls\", class \"%ls\", handle %p", exe.c_str(), pid, title,
                      className, static_cast<void*>(window)));
}

void CALLBACK OnForeground(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) { CheckForeground(); }

// ---------------------------------------------------------------------------
// The launch
// ---------------------------------------------------------------------------

std::wstring Launch(const std::wstring& exe, const std::wstring& arguments, const std::wstring& directory, DWORD& pid) {
    std::wstring commandLine = L"\"" + exe + L"\"" + (arguments.empty() ? L"" : L" " + arguments);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    // What a program that passes its start-up show command to ShowWindow shows its first window with.
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNOACTIVATE;
    PROCESS_INFORMATION made{};
    const ULONGLONG began = GetTickCount64();
    if (!CreateProcessW(exe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, directory.empty() ? nullptr : directory.c_str(),
                        &startup, &made)) {
        return rig::Win32Error(L"CreateProcess") + L" for " + exe;
    }
    pid = made.dwProcessId;
    g_members.push_back({made.dwProcessId, made.hProcess, LeafOf(exe)});
    g_seen.insert(std::upper_bound(g_seen.begin(), g_seen.end(), pid), pid);
    std::wstring why;
    if (!AssignProcessToJobObject(g_job, made.hProcess)) why = rig::Win32Error(L"AssignProcessToJobObject");
    if (why.empty() && g_shim) why = rig::Inject(made.hProcess, made.dwProcessId, g_toolFolder, g_folder, false);
    if (!why.empty()) {
        TerminateProcess(made.hProcess, 0xF07);
        CloseHandle(made.hThread);
        return why;
    }
    ResumeThread(made.hThread);
    CloseHandle(made.hThread);
    Log(Format(L"started %ls (pid %lu) suspended, %ls, and let it go %llu ms later", exe.c_str(), pid,
               g_shim ? L"with the shim in before its first instruction" : L"with no shim asked for", GetTickCount64() - began));
    return L"";
}

struct Settings {
    ULONGLONG limitSeconds = 0;
    std::wstring launch, arguments, directory;
};

bool ReadSettings(Settings& settings) {
    const HANDLE file = CreateFileW((g_folder + L"\\guard.txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const DWORD size = GetFileSize(file, nullptr);
    std::wstring text(size / sizeof(wchar_t), L'\0');
    DWORD read = 0;
    ReadFile(file, text.data(), size, &read, nullptr);
    CloseHandle(file);
    if (!text.empty() && text[0] == 0xFEFF) text.erase(0, 1);
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find(L'\n', at);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == L'\r' || line.back() == L'\0')) line.pop_back();
        const size_t equals = line.find(L'=');
        if (equals == std::wstring::npos) continue;
        const std::wstring key = line.substr(0, equals), value = line.substr(equals + 1);
        if (key == L"limit") settings.limitSeconds = _wcstoui64(value.c_str(), nullptr, 10);
        else if (key == L"name") g_names.push_back(value);
        else if (key == L"shim") g_shim = value != L"0";
        else if (key == L"launch") settings.launch = value;
        else if (key == L"arguments") settings.arguments = value;
        else if (key == L"directory") settings.directory = value;
        else return false;
    }
    return settings.limitSeconds > 0;
}

int Guard() {
    Settings settings;
    if (!ReadSettings(settings)) {
        WriteMarker(L"guard.failed", L"guard.txt is missing, has no limit, or has a key the guard does not know\r\n");
        return 2;
    }
    g_started = GetTickCount64();
    g_job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    // If the guard itself dies, the game goes with it: nothing runs unwatched.
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!g_job || !SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        WriteMarker(L"guard.failed", rig::Win32Error(L"making the job") + L"\r\n");
        return 2;
    }

    // Whatever is running now is not this run's, whatever its name.
    DWORD pids[8192];
    DWORD bytes = 0;
    EnumProcesses(pids, sizeof(pids), &bytes);
    g_seen.assign(pids, pids + bytes / sizeof(DWORD));
    std::sort(g_seen.begin(), g_seen.end());

    const std::wstring display = Describe(Screens());
    const HWINEVENTHOOK hook =
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, &OnForeground, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    const HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due;
    due.QuadPart = -20000;
    if (!hook || !timer || !SetWaitableTimer(timer, &due, 2, nullptr, nullptr, FALSE) || !ListenForDisplayChanges()) {
        WriteMarker(L"guard.failed", rig::Win32Error(L"starting the watch") + L"\r\n");
        return 2;
    }
    std::wstring names;
    for (const std::wstring& name : g_names) names += (names.empty() ? L"" : L", ") + name;
    Log(Format(L"guard up (pid %lu): display %ls; limit %llu s; processes named %ls; %ls", GetCurrentProcessId(), display.c_str(), settings.limitSeconds,
               names.empty() ? L"none" : names.c_str(), g_shim ? L"shim on" : L"shim off, watching only"));
    WriteMarker(L"guard.ready", std::to_wstring(GetCurrentProcessId()) + L"\r\n");

    if (!settings.launch.empty()) {
        DWORD pid = 0;
        const std::wstring why = Launch(settings.launch, settings.arguments, settings.directory, pid);
        if (!why.empty()) {
            EndAll();
            Log(L"the launch failed: " + why);
            WriteMarker(L"guard.failed", why + L"\r\n");
            return 1;
        }
        WriteMarker(L"guard.launched", std::to_wstring(pid) + L"\r\n");
    }

    ULONGLONG nextSlow = 0, nextScan = 0, lastUp = GetTickCount64();
    bool displayChecked = false;
    for (;;) {
        MsgWaitForMultipleObjects(1, &timer, FALSE, 50, QS_ALLINPUT);
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        const ULONGLONG now = GetTickCount64();
        if (now >= nextScan) {
            nextScan = now + 15;
            Scan();
        }
        CheckForeground();
        if (now < nextSlow) continue;
        nextSlow = now + 100;
        if (!g_ended) {
            const std::wstring current = DisplayCheckDue(now) ? Describe(Screens()) : display;
            if (current != display) EndTheGame(Format(L"DISPLAY: the display went from %ls to %ls", display.c_str(), current.c_str()));
            else if (now - g_started > settings.limitSeconds * 1000) EndTheGame(Format(L"LIMIT: the run's %llu seconds are up", settings.limitSeconds));
        } else if (!displayChecked && now - g_endedAt > 1500) {
            displayChecked = true;
            const std::wstring after = Describe(Screens());
            if (after != display) {
                Log(Format(L"1.5 s after the game was ended the display is %ls, not the %ls the guard came up with. The guard sets no mode itself.",
                           after.c_str(), display.c_str()));
            }
        }
        if (Exists(L"guard.stop")) {
            const int ended = EndAll();
            Log(Format(L"guard down: asked to stop, %d process(es) of the run were still up and were ended", ended));
            return 0;
        }
        // A guard nobody stops would take in the next run's process of the same name.
        bool anyUp = g_members.empty();
        for (const Member& member : g_members) anyUp = anyUp || WaitForSingleObject(member.process, 0) == WAIT_TIMEOUT;
        if (anyUp) lastUp = now;
        if (now - lastUp > 120000) {
            Log(L"guard down: every process of the run has been gone for two minutes and nobody asked it to stop");
            return 0;
        }
        // Nobody came to stop it: the run's own script is gone.
        if (g_ended && now - g_endedAt > 120000) {
            Log(L"guard down: nobody asked it to stop in the two minutes after it ended the game");
            return 0;
        }
    }
}

// Every change of the foreground window and of the display mode, written down for
// <seconds>: what a test of the guarded launch reads afterwards. It names the
// process and the window class of each foreground window and never its title,
// which is the person's own business.
std::vector<std::wstring> g_watchLines;
HWND g_watchLast = nullptr;

void NoteForeground() {
    const HWND window = GetForegroundWindow();
    if (window == g_watchLast) return;
    g_watchLast = window;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    std::wstring exe = L"?";
    if (const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        exe = ExeOf(process);
        CloseHandle(process);
    }
    wchar_t className[200] = L"";
    if (window) GetClassNameW(window, className, 200);
    g_watchLines.push_back(Stamp() + Format(L" foreground pid=%lu exe=%ls class=%ls", pid, exe.c_str(), className));
}

void CALLBACK OnWatchedForeground(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) { NoteForeground(); }

int Watch(ULONGLONG seconds) {
    const HWINEVENTHOOK hook =
        SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, &OnWatchedForeground, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    const HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due;
    due.QuadPart = -10000;
    if (!hook || !timer || !SetWaitableTimer(timer, &due, 1, nullptr, nullptr, FALSE) || !ListenForDisplayChanges()) return 2;
    std::wstring display = Describe(Screens());
    g_watchLines.push_back(Stamp() + L" display " + display);
    NoteForeground();
    WriteMarker(L"watch.ready", L"");
    const ULONGLONG started = GetTickCount64();
    ULONGLONG nextSlow = 0, polls = 0;
    while (GetTickCount64() - started < seconds * 1000) {
        MsgWaitForMultipleObjects(1, &timer, FALSE, 20, QS_ALLINPUT);
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        NoteForeground();
        ++polls;
        const ULONGLONG now = GetTickCount64();
        if (now < nextSlow) continue;
        nextSlow = now + 50;
        if (Exists(L"watch.stop")) break;
        if (!DisplayCheckDue(now)) continue;
        const std::wstring current = Describe(Screens());
        if (current != display) {
            display = current;
            g_watchLines.push_back(Stamp() + L" display " + display);
        }
    }
    g_watchLines.push_back(Stamp() + Format(L" watched %llu ms with %llu polls of the foreground", GetTickCount64() - started, polls));
    std::wstring all;
    for (const std::wstring& line : g_watchLines) all += line + L"\r\n";
    WriteWhole(L"watch.log", all);
    return 0;
}

int InjectOne(DWORD pid, bool running, const std::wstring& logFolder) {
    g_folder = logFolder;
    const HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    const std::wstring why = process ? rig::InjectSameArch(process, pid, g_toolFolder, logFolder, running) : rig::Win32Error(L"OpenProcess");
    if (why.empty()) return 0;
    WriteWhole(L"rigguard-inject.log", Stamp() + Format(L" pid %lu: ", pid) + why + L"\r\n", true);
    return 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    g_toolFolder = rig::FolderOf(nullptr);
    if (argc == 3 && !wcscmp(argv[1], L"guard")) {
        g_folder = argv[2];
        return Guard();
    }
    if (argc == 4 && !wcscmp(argv[1], L"watch")) {
        g_folder = argv[2];
        return Watch(_wcstoui64(argv[3], nullptr, 10));
    }
    if (argc == 5 && !wcscmp(argv[1], L"inject")) return InjectOne(wcstoul(argv[2], nullptr, 10), !wcscmp(argv[3], L"running"), argv[4]);
    fputws(L"CameraUnlockRig guard <folder> | watch <folder> <seconds> | inject <pid> fresh|running <log folder>\n", stderr);
    return 2;
}
