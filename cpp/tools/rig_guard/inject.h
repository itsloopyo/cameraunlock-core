// Putting the rig shim into a process, shared by the guard (CameraUnlockRig.exe)
// and by the shim itself, which puts a copy into every process its own starts.
//
// The four files sit in one folder under these names, and each finds the others
// there: CameraUnlockRig.x64.exe, CameraUnlockRig.x86.exe,
// CameraUnlockRigShim.x64.dll and CameraUnlockRigShim.x86.dll.

#pragma once

#include <Windows.h>

#include <string>

namespace rig {

#ifdef _WIN64
inline constexpr const wchar_t* kOwnArch = L"x64";
inline constexpr const wchar_t* kOtherArch = L"x86";
#else
inline constexpr const wchar_t* kOwnArch = L"x86";
inline constexpr const wchar_t* kOtherArch = L"x64";
#endif

// What the injector leaves for the shim to read as it loads.
struct ShimConfig {
    wchar_t logFolder[MAX_PATH * 2];
    // The process had already run when the shim was put in.
    BOOL running;
};

inline std::wstring ShimEventName(DWORD pid) { return L"Local\\CameraUnlock.RigShim." + std::to_wstring(pid); }
inline std::wstring ShimConfigName(DWORD pid) { return L"Local\\CameraUnlock.RigShim.Config." + std::to_wstring(pid); }

// The shim makes this event once every one of its detours is in, and keeps it.
inline bool IsShimmed(DWORD pid) {
    const HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, ShimEventName(pid).c_str());
    if (!event) return false;
    CloseHandle(event);
    return true;
}

inline std::wstring Win32Error(const wchar_t* what, DWORD error = GetLastError()) {
    return std::wstring(what) + L" failed with error " + std::to_wstring(error);
}

inline bool Is32Bit(HANDLE process) {
    BOOL wow = FALSE;
    IsWow64Process(process, &wow);
    return wow != FALSE;
}

inline bool SameArchAsUs(HANDLE process) {
#ifdef _WIN64
    return !Is32Bit(process);
#else
    return Is32Bit(process);
#endif
}

using NtProcess_t = LONG (NTAPI*)(HANDLE);
inline void ResumeWholeProcess(HANDLE process) {
    const auto resume = reinterpret_cast<NtProcess_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtResumeProcess"));
    resume(process);
}
inline void SuspendWholeProcess(HANDLE process) {
    const auto suspend = reinterpret_cast<NtProcess_t>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSuspendProcess"));
    suspend(process);
}

// Loads this build's shim into a process of this build's bitness. Returns why it
// could not, or nothing when the shim is in with every detour.
//
// `running` is for a process that was already going and has been suspended whole:
// one of its threads may have been stopped inside the loader, which the load then
// waits on for ever, so after half a second the process is let go again and the
// load finishes beside it.
inline std::wstring InjectSameArch(HANDLE process, DWORD pid, const std::wstring& toolFolder, const std::wstring& logFolder, bool running) {
    const std::wstring dll = toolFolder + L"\\CameraUnlockRigShim." + kOwnArch + L".dll";
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) return L"the shim is not built: " + dll;
    if (logFolder.size() >= MAX_PATH * 2) return L"the log folder's path is too long";

    const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(ShimConfig), ShimConfigName(pid).c_str());
    if (!mapping) return Win32Error(L"CreateFileMapping");
    auto* config = static_cast<ShimConfig*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(ShimConfig)));
    if (!config) {
        const std::wstring why = Win32Error(L"MapViewOfFile");
        CloseHandle(mapping);
        return why;
    }
    wcscpy_s(config->logFolder, logFolder.c_str());
    config->running = running;
    UnmapViewOfFile(config);

    std::wstring why;
    const SIZE_T bytes = (dll.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        why = Win32Error(L"VirtualAllocEx");
    } else if (!WriteProcessMemory(process, remote, dll.c_str(), bytes, nullptr)) {
        why = Win32Error(L"WriteProcessMemory");
    } else {
        // kernel32 sits at one address in every process of one bitness until the next boot.
        const auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        const HANDLE thread = CreateRemoteThread(process, nullptr, 0, load, remote, 0, nullptr);
        if (!thread) {
            why = Win32Error(L"CreateRemoteThread");
        } else {
            DWORD waited = WaitForSingleObject(thread, running ? 500 : 15000);
            if (waited == WAIT_TIMEOUT && running) {
                ResumeWholeProcess(process);
                waited = WaitForSingleObject(thread, 15000);
                // Held again, so the one resume the caller owes it leaves every thread as it was found.
                SuspendWholeProcess(process);
            }
            if (waited != WAIT_OBJECT_0) why = L"the shim had not loaded after 15 seconds";
            else if (!IsShimmed(pid)) why = L"the shim loaded and refused to start: its log in " + logFolder + L" says why";
            CloseHandle(thread);
        }
    }
    CloseHandle(mapping);
    return why;
}

// Runs a program hidden and waits for it. Returns its exit code, or -1 with `why` set.
inline int RunHidden(std::wstring commandLine, std::wstring& why) {
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION made{};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &made)) {
        why = Win32Error(L"CreateProcess") + L" for " + commandLine;
        return -1;
    }
    DWORD code = 1;
    if (WaitForSingleObject(made.hProcess, 30000) != WAIT_OBJECT_0) {
        TerminateProcess(made.hProcess, 1);
        why = L"the other bitness's injector had not finished after 30 seconds";
        code = static_cast<DWORD>(-1);
    } else {
        GetExitCodeProcess(made.hProcess, &code);
    }
    CloseHandle(made.hThread);
    CloseHandle(made.hProcess);
    return static_cast<int>(code);
}

// The shim into a process of either bitness: directly, or through the other
// bitness's CameraUnlockRig.exe, which is the only thing that knows where that
// bitness keeps LoadLibraryW.
inline std::wstring Inject(HANDLE process, DWORD pid, const std::wstring& toolFolder, const std::wstring& logFolder, bool running) {
    if (SameArchAsUs(process)) return InjectSameArch(process, pid, toolFolder, logFolder, running);
    const std::wstring helper = toolFolder + L"\\CameraUnlockRig." + kOtherArch + L".exe";
    if (GetFileAttributesW(helper.c_str()) == INVALID_FILE_ATTRIBUTES) return L"the " + std::wstring(kOtherArch) + L" injector is not built: " + helper;
    std::wstring why;
    const int code = RunHidden(L"\"" + helper + L"\" inject " + std::to_wstring(pid) + (running ? L" running \"" : L" fresh \"") + logFolder + L"\"", why);
    if (code == 0) return L"";
    if (!why.empty()) return why;
    return L"the " + std::wstring(kOtherArch) + L" injector could not put the shim in (exit code " + std::to_wstring(code) + L"): rigguard-inject.log in " + logFolder + L" says why";
}

inline std::wstring FolderOf(HMODULE module) {
    wchar_t path[MAX_PATH * 2];
    GetModuleFileNameW(module, path, MAX_PATH * 2);
    std::wstring folder = path;
    return folder.substr(0, folder.find_last_of(L'\\'));
}

}  // namespace rig
