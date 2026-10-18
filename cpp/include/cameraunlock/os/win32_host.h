#pragma once

// The Win32 API as the WebAssembly build of core sees it (CAMERAUNLOCK_WIN32_HOST). There are no
// Windows headers in that build, so this declares the types, constants and functions core's
// Win32 code uses, and nothing else. Each function is an import of module "cameraunlock_win32";
// the Java host behind core's jar implements it with the same call into Windows' own DLLs
// (java/src/com/cameraunlock/core/Win32Host.java), so core's file, window and key code
// behaves as it does in CameraUnlockCore.dll. docs/core-jar.md says how it is built.
//
// Differences from <windows.h> a reader of core's code meets:
//  - wchar_t is 32 bits here, so a W function's text is UTF-32. The host converts at the call.
//  - Pointers are 32 bits. HANDLE, HWND, HMONITOR and SOCKET are Windows' own values, which
//    Windows keeps within 32 significant bits; the host sign-extends them, and refuses one that
//    does not fit.
//  - FormatMessageW and FormatMessageA with FORMAT_MESSAGE_ALLOCATE_BUFFER allocate with malloc,
//    and LocalFree frees with free.
// The types and constants are in win32_host_types.h, which cpp/tests/win32_host_constants.cpp
// holds to the Windows SDK's.

#ifndef CAMERAUNLOCK_WIN32_HOST
#error "win32_host.h is for the WebAssembly build: include cameraunlock/os/win32.h"
#endif

#include <cstddef>
#include <cstdlib>

#include "cameraunlock/os/win32_host_types.h"

// The layouts as the module has them, which java/src/com/cameraunlock/core/Win32Host.java reads and
// writes at these offsets: wchar_t is 32 bits here, so a WCHAR array is twice the SDK's.
static_assert(sizeof(BY_HANDLE_FILE_INFORMATION) == 52, "Win32Host copies 52 bytes");
static_assert(sizeof(WIN32_FILE_ATTRIBUTE_DATA) == 36, "Win32Host copies 36 bytes");
static_assert(offsetof(WIN32_FIND_DATAW, cFileName) == 44, "Win32Host's FIND_DATA_HEAD");
static_assert(offsetof(WIN32_FIND_DATAW, cAlternateFileName) == 44 + 4 * 260, "Win32Host's FIND_DATA_NAME in UTF-32");
static_assert(sizeof(WIN32_FIND_DATAW) == 44 + 4 * (260 + 14), "Win32Host's FIND_DATA_ALTERNATE in UTF-32");
static_assert(sizeof(SYSTEMTIME) == 16, "Win32Host copies 16 bytes");
static_assert(sizeof(RECT) == 16, "Win32Host copies 16 bytes");
static_assert(sizeof(MONITORINFO) == 40 && offsetof(MONITORINFO, rcWork) == 20, "Win32Host copies 40 bytes");
static_assert(sizeof(sockaddr_in) == 16, "Win32Host's SOCKADDR_IN");
static_assert(sizeof(LARGE_INTEGER) == 8, "Win32Host writes 8 bytes");
static_assert(sizeof(u_long) == 4, "Win32Host copies ioctlsocket's 4 bytes");

#define CAMERAUNLOCK_WIN32_IMPORT(name) __attribute__((import_module("cameraunlock_win32"), import_name(#name)))

extern "C" {

CAMERAUNLOCK_WIN32_IMPORT(GetLastError) DWORD GetLastError();
CAMERAUNLOCK_WIN32_IMPORT(SetLastError) void SetLastError(DWORD error);

CAMERAUNLOCK_WIN32_IMPORT(CreateFileW)
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, SECURITY_ATTRIBUTES* security, DWORD disposition,
                   DWORD flags, HANDLE template_file);
CAMERAUNLOCK_WIN32_IMPORT(ReadFile) BOOL ReadFile(HANDLE file, LPVOID buffer, DWORD to_read, DWORD* read, void* overlapped);
CAMERAUNLOCK_WIN32_IMPORT(WriteFile)
BOOL WriteFile(HANDLE file, LPCVOID buffer, DWORD to_write, DWORD* written, void* overlapped);
CAMERAUNLOCK_WIN32_IMPORT(SetFilePointerEx)
BOOL SetFilePointerEx(HANDLE file, LARGE_INTEGER distance, LARGE_INTEGER* position, DWORD method);
CAMERAUNLOCK_WIN32_IMPORT(FlushFileBuffers) BOOL FlushFileBuffers(HANDLE file);
CAMERAUNLOCK_WIN32_IMPORT(CloseHandle) BOOL CloseHandle(HANDLE handle);
CAMERAUNLOCK_WIN32_IMPORT(GetFileInformationByHandle)
BOOL GetFileInformationByHandle(HANDLE file, BY_HANDLE_FILE_INFORMATION* information);
CAMERAUNLOCK_WIN32_IMPORT(GetFileAttributesW) DWORD GetFileAttributesW(LPCWSTR path);
CAMERAUNLOCK_WIN32_IMPORT(GetFileAttributesExW)
BOOL GetFileAttributesExW(LPCWSTR path, GET_FILEEX_INFO_LEVELS level, LPVOID information);
CAMERAUNLOCK_WIN32_IMPORT(FindFirstFileW) HANDLE FindFirstFileW(LPCWSTR path, WIN32_FIND_DATAW* found);
CAMERAUNLOCK_WIN32_IMPORT(FindClose) BOOL FindClose(HANDLE search);
CAMERAUNLOCK_WIN32_IMPORT(GetFullPathNameW) DWORD GetFullPathNameW(LPCWSTR path, DWORD length, LPWSTR buffer, LPWSTR* file_part);
CAMERAUNLOCK_WIN32_IMPORT(ReplaceFileW)
BOOL ReplaceFileW(LPCWSTR replaced, LPCWSTR replacement, LPCWSTR backup, DWORD flags, LPVOID exclude, LPVOID reserved);
CAMERAUNLOCK_WIN32_IMPORT(MoveFileExW) BOOL MoveFileExW(LPCWSTR existing, LPCWSTR replacement, DWORD flags);
CAMERAUNLOCK_WIN32_IMPORT(DeleteFileW) BOOL DeleteFileW(LPCWSTR path);
CAMERAUNLOCK_WIN32_IMPORT(CreateDirectoryW) BOOL CreateDirectoryW(LPCWSTR path, SECURITY_ATTRIBUTES* security);

CAMERAUNLOCK_WIN32_IMPORT(GetACP) UINT GetACP();
CAMERAUNLOCK_WIN32_IMPORT(MultiByteToWideChar)
int MultiByteToWideChar(UINT code_page, DWORD flags, LPCSTR text, int length, LPWSTR wide, int wide_length);
CAMERAUNLOCK_WIN32_IMPORT(WideCharToMultiByte)
int WideCharToMultiByte(UINT code_page, DWORD flags, LPCWSTR wide, int wide_length, LPSTR text, int length,
                        LPCSTR default_char, BOOL* used_default);
CAMERAUNLOCK_WIN32_IMPORT(CompareStringOrdinal)
int CompareStringOrdinal(LPCWSTR first, int first_length, LPCWSTR second, int second_length, BOOL ignore_case);

CAMERAUNLOCK_WIN32_IMPORT(QueryPerformanceCounter) BOOL QueryPerformanceCounter(LARGE_INTEGER* count);
CAMERAUNLOCK_WIN32_IMPORT(QueryPerformanceFrequency) BOOL QueryPerformanceFrequency(LARGE_INTEGER* frequency);
CAMERAUNLOCK_WIN32_IMPORT(GetLocalTime) void GetLocalTime(SYSTEMTIME* time);
CAMERAUNLOCK_WIN32_IMPORT(GetCurrentProcessId) DWORD GetCurrentProcessId();
CAMERAUNLOCK_WIN32_IMPORT(Sleep) void Sleep(DWORD milliseconds);

CAMERAUNLOCK_WIN32_IMPORT(GetAsyncKeyState) short GetAsyncKeyState(int key);
CAMERAUNLOCK_WIN32_IMPORT(GetForegroundWindow) HWND GetForegroundWindow();
CAMERAUNLOCK_WIN32_IMPORT(GetWindowThreadProcessId) DWORD GetWindowThreadProcessId(HWND window, DWORD* process);
CAMERAUNLOCK_WIN32_IMPORT(IsWindow) BOOL IsWindow(HWND window);
CAMERAUNLOCK_WIN32_IMPORT(IsIconic) BOOL IsIconic(HWND window);
CAMERAUNLOCK_WIN32_IMPORT(IsZoomed) BOOL IsZoomed(HWND window);
CAMERAUNLOCK_WIN32_IMPORT(GetWindowLongW) LONG GetWindowLongW(HWND window, int index);
CAMERAUNLOCK_WIN32_IMPORT(GetWindowRect) BOOL GetWindowRect(HWND window, RECT* rect);
CAMERAUNLOCK_WIN32_IMPORT(MonitorFromWindow) HMONITOR MonitorFromWindow(HWND window, DWORD flags);
CAMERAUNLOCK_WIN32_IMPORT(GetMonitorInfoW) BOOL GetMonitorInfoW(HMONITOR monitor, MONITORINFO* info);
CAMERAUNLOCK_WIN32_IMPORT(SetWindowPos)
BOOL SetWindowPos(HWND window, HWND insert_after, int x, int y, int width, int height, UINT flags);

// FormatMessage's text for a system error, without FORMAT_MESSAGE_ALLOCATE_BUFFER: written to
// `buffer`, which holds `size` characters.
CAMERAUNLOCK_WIN32_IMPORT(FormatMessageW)
DWORD cameraunlock_win32_FormatMessageW(DWORD flags, LPCVOID source, DWORD message, DWORD language, LPWSTR buffer,
                                        DWORD size, void* arguments);
CAMERAUNLOCK_WIN32_IMPORT(FormatMessageA)
DWORD cameraunlock_win32_FormatMessageA(DWORD flags, LPCVOID source, DWORD message, DWORD language, LPSTR buffer,
                                        DWORD size, void* arguments);
}

namespace cameraunlock::win32_host {

// FORMAT_MESSAGE_ALLOCATE_BUFFER, which the host does not take: the text goes into a buffer of
// ours, doubled while Windows says it is too small, up to 64K units (bytes for the A function,
// UTF-16 units for the W one).
template <typename Char>
DWORD FormatAllocated(DWORD (*format)(DWORD, LPCVOID, DWORD, DWORD, Char*, DWORD, void*), DWORD flags, LPCVOID source,
                      DWORD message, DWORD language, Char** out, void* arguments) {
    flags &= ~static_cast<DWORD>(FORMAT_MESSAGE_ALLOCATE_BUFFER);
    for (DWORD size = 4096; size <= 65536; size *= 2) {
        auto* text = static_cast<Char*>(std::malloc(size * sizeof(Char)));
        if (text == nullptr) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return 0;
        }
        const DWORD length = format(flags, source, message, language, text, size, arguments);
        if (length != 0) {
            *out = text;
            return length;
        }
        std::free(text);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return 0;
    }
    return 0;
}

}  // namespace cameraunlock::win32_host

inline DWORD FormatMessageW(DWORD flags, LPCVOID source, DWORD message, DWORD language, LPWSTR buffer, DWORD size,
                            void* arguments) {
    if ((flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) == 0) {
        return cameraunlock_win32_FormatMessageW(flags, source, message, language, buffer, size, arguments);
    }
    return cameraunlock::win32_host::FormatAllocated(cameraunlock_win32_FormatMessageW, flags, source, message, language,
                                                     reinterpret_cast<LPWSTR*>(buffer), arguments);
}

inline DWORD FormatMessageA(DWORD flags, LPCVOID source, DWORD message, DWORD language, LPSTR buffer, DWORD size,
                            void* arguments) {
    if ((flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) == 0) {
        return cameraunlock_win32_FormatMessageA(flags, source, message, language, buffer, size, arguments);
    }
    return cameraunlock::win32_host::FormatAllocated(cameraunlock_win32_FormatMessageA, flags, source, message, language,
                                                     reinterpret_cast<LPSTR*>(buffer), arguments);
}

inline HLOCAL LocalFree(HLOCAL memory) {
    std::free(memory);
    return nullptr;
}

extern "C" {
CAMERAUNLOCK_WIN32_IMPORT(WSAStartup) int WSAStartup(WORD version, WSADATA* data);
CAMERAUNLOCK_WIN32_IMPORT(WSACleanup) int WSACleanup();
CAMERAUNLOCK_WIN32_IMPORT(WSAGetLastError) int WSAGetLastError();
CAMERAUNLOCK_WIN32_IMPORT(socket) SOCKET socket(int family, int type, int protocol);
CAMERAUNLOCK_WIN32_IMPORT(ioctlsocket) int ioctlsocket(SOCKET socket, long command, u_long* argument);
CAMERAUNLOCK_WIN32_IMPORT(WSAIoctl)
int WSAIoctl(SOCKET socket, DWORD code, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size, DWORD* returned,
             void* overlapped, void* completion);
CAMERAUNLOCK_WIN32_IMPORT(bind) int bind(SOCKET socket, const struct sockaddr* address, int length);
CAMERAUNLOCK_WIN32_IMPORT(closesocket) int closesocket(SOCKET socket);
}
