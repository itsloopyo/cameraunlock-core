#pragma once

// The Win32 API as the WebAssembly build of core sees it (CAMERAUNLOCK_WIN32_HOST). There are no
// Windows headers in that build, so this declares the types, constants and functions core's
// Win32 code uses, and nothing else. Each function is an import of module "cameraunlock_win32";
// the Java host behind core's jar implements it with the same call into Windows' own DLLs
// (java/src/com/cameraunlock/core/wasm/Win32Host.java), so core's file, window and key code
// behaves as it does in CameraUnlockCore.dll. docs/core-jar.md says how it is built.
//
// Differences from <windows.h> a reader of core's code meets:
//  - wchar_t is 32 bits here, so a W function's text is UTF-32. The host converts at the call.
//  - Pointers are 32 bits. HANDLE, HWND and HMONITOR are Windows' own values, which Windows keeps
//    within 32 significant bits; the host sign-extends them.
//  - FormatMessageW with FORMAT_MESSAGE_ALLOCATE_BUFFER allocates with malloc, and LocalFree
//    frees with free.
// cpp/tests/win32_host_constants.cpp holds every constant below to the Windows SDK's.

#ifndef CAMERAUNLOCK_WIN32_HOST
#error "win32_host.h is for the WebAssembly build: include cameraunlock/os/win32.h"
#endif

#include <cstddef>
#include <cstdint>
#include <cstdlib>

typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int UINT;
typedef std::uint32_t UINT32;
typedef long LONG;
typedef long long LONGLONG;
typedef long HRESULT;
typedef wchar_t WCHAR;
typedef WCHAR* LPWSTR;
typedef WCHAR* PWSTR;
typedef const WCHAR* LPCWSTR;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef void* HANDLE;
typedef long LPARAM;
typedef unsigned long DWORD_PTR;
typedef struct HWND__* HWND;
typedef struct HMONITOR__* HMONITOR;
typedef struct HINSTANCE__* HMODULE;
typedef void* HLOCAL;

#define WINAPI
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define SUCCEEDED(hr) (static_cast<HRESULT>(hr) >= 0)
#define MAKELANGID(primary, sub) ((static_cast<WORD>(sub) << 10) | static_cast<WORD>(primary))
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1)))
#define INVALID_FILE_ATTRIBUTES (static_cast<DWORD>(-1))

#define GENERIC_READ 0x80000000UL
#define GENERIC_WRITE 0x40000000UL
#define FILE_SHARE_READ 0x00000001UL
#define FILE_SHARE_WRITE 0x00000002UL
#define FILE_SHARE_DELETE 0x00000004UL
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_READONLY 0x00000001UL
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010UL
#define FILE_ATTRIBUTE_NORMAL 0x00000080UL
#define FILE_BEGIN 0
#define MOVEFILE_REPLACE_EXISTING 0x00000001UL
#define MOVEFILE_WRITE_THROUGH 0x00000008UL

#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_NOT_READY 21L
#define ERROR_WRITE_FAULT 29L
#define ERROR_SHARING_VIOLATION 32L
#define ERROR_LOCK_VIOLATION 33L
#define ERROR_FILE_EXISTS 80L
#define ERROR_INSUFFICIENT_BUFFER 122L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_UNABLE_TO_REMOVE_REPLACED 1175L
#define ERROR_UNABLE_TO_MOVE_REPLACEMENT 1176L
#define ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 1177L

#define CP_ACP 0
#define CP_UTF7 65000
#define CP_UTF8 65001
#define MB_ERR_INVALID_CHARS 0x00000008
#define WC_NO_BEST_FIT_CHARS 0x00000400
#define CSTR_EQUAL 2

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#define LANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01

#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12

#define GWL_STYLE (-16)
#define WS_CAPTION 0x00C00000L
#define MONITOR_DEFAULTTONEAREST 0x00000002
#define SWP_NOSIZE 0x0001
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

typedef struct _SYSTEMTIME {
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME;

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;

typedef struct _WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    WCHAR cFileName[MAX_PATH];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;

typedef struct _BY_HANDLE_FILE_INFORMATION {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD dwVolumeSerialNumber;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD nNumberOfLinks;
    DWORD nFileIndexHigh;
    DWORD nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION;

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;

typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT;

typedef struct tagMONITORINFO {
    DWORD cbSize;
    RECT rcMonitor;
    RECT rcWork;
    DWORD dwFlags;
} MONITORINFO;

typedef struct tagWINDOWPLACEMENT {
    UINT length;
    UINT flags;
    UINT showCmd;
    POINT ptMinPosition;
    POINT ptMaxPosition;
    RECT rcNormalPosition;
} WINDOWPLACEMENT;

typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES;

typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard } GET_FILEEX_INFO_LEVELS;

#define CAMERAUNLOCK_WIN32_IMPORT(name) __attribute__((import_module("cameraunlock_win32"), import_name(#name)))

extern "C" {

CAMERAUNLOCK_WIN32_IMPORT(GetLastError) DWORD GetLastError();

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
CAMERAUNLOCK_WIN32_IMPORT(IsWindowVisible) BOOL IsWindowVisible(HWND window);
CAMERAUNLOCK_WIN32_IMPORT(IsIconic) BOOL IsIconic(HWND window);
CAMERAUNLOCK_WIN32_IMPORT(IsZoomed) BOOL IsZoomed(HWND window);
CAMERAUNLOCK_WIN32_IMPORT(GetWindowLongW) LONG GetWindowLongW(HWND window, int index);
CAMERAUNLOCK_WIN32_IMPORT(GetWindowRect) BOOL GetWindowRect(HWND window, RECT* rect);
CAMERAUNLOCK_WIN32_IMPORT(GetWindowPlacement) BOOL GetWindowPlacement(HWND window, WINDOWPLACEMENT* placement);
CAMERAUNLOCK_WIN32_IMPORT(MonitorFromWindow) HMONITOR MonitorFromWindow(HWND window, DWORD flags);
CAMERAUNLOCK_WIN32_IMPORT(GetMonitorInfoW) BOOL GetMonitorInfoW(HMONITOR monitor, MONITORINFO* info);
CAMERAUNLOCK_WIN32_IMPORT(SetWindowPos)
BOOL SetWindowPos(HWND window, HWND insert_after, int x, int y, int width, int height, UINT flags);

// FormatMessageW's text for a system error, without FORMAT_MESSAGE_ALLOCATE_BUFFER: written to
// `buffer`, which holds `size` characters.
CAMERAUNLOCK_WIN32_IMPORT(FormatMessageW)
DWORD cameraunlock_win32_FormatMessageW(DWORD flags, LPCVOID source, DWORD message, DWORD language, LPWSTR buffer,
                                        DWORD size, void* arguments);
}

inline DWORD FormatMessageW(DWORD flags, LPCVOID source, DWORD message, DWORD language, LPWSTR buffer, DWORD size,
                            void* arguments) {
    if ((flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) == 0) {
        return cameraunlock_win32_FormatMessageW(flags, source, message, language, buffer, size, arguments);
    }
    constexpr DWORD kMost = 4096;
    auto* text = static_cast<LPWSTR>(std::malloc(kMost * sizeof(WCHAR)));
    if (text == nullptr) return 0;
    const DWORD length = cameraunlock_win32_FormatMessageW(flags & ~static_cast<DWORD>(FORMAT_MESSAGE_ALLOCATE_BUFFER),
                                                           source, message, language, text, kMost, arguments);
    if (length == 0) {
        std::free(text);
        return 0;
    }
    *reinterpret_cast<LPWSTR*>(buffer) = text;
    return length;
}

inline HLOCAL LocalFree(HLOCAL memory) {
    std::free(memory);
    return nullptr;
}

// FormatMessageA, as FormatMessageW above.
extern "C" CAMERAUNLOCK_WIN32_IMPORT(FormatMessageA) DWORD
cameraunlock_win32_FormatMessageA(DWORD flags, LPCVOID source, DWORD message, DWORD language, LPSTR buffer, DWORD size,
                                  void* arguments);

inline DWORD FormatMessageA(DWORD flags, LPCVOID source, DWORD message, DWORD language, LPSTR buffer, DWORD size,
                            void* arguments) {
    if ((flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) == 0) {
        return cameraunlock_win32_FormatMessageA(flags, source, message, language, buffer, size, arguments);
    }
    constexpr DWORD kMost = 4096;
    auto* text = static_cast<LPSTR>(std::malloc(kMost));
    if (text == nullptr) return 0;
    const DWORD length = cameraunlock_win32_FormatMessageA(flags & ~static_cast<DWORD>(FORMAT_MESSAGE_ALLOCATE_BUFFER),
                                                           source, message, language, text, kMost, arguments);
    if (length == 0) {
        std::free(text);
        return 0;
    }
    *reinterpret_cast<LPSTR*>(buffer) = text;
    return length;
}

// ---- Winsock, for core's UDP socket -------------------------------------------------------------

typedef unsigned int SOCKET;
typedef unsigned long u_long;
typedef unsigned short u_short;

#define INVALID_SOCKET (~static_cast<SOCKET>(0))
#define SOCKET_ERROR (-1)
#define AF_INET 2
#define SOCK_DGRAM 2
#define IPPROTO_UDP 17
#define FIONBIO 0x8004667EL
#define INADDR_ANY 0x00000000UL
#define INADDR_LOOPBACK 0x7F000001UL
#define INET_ADDRSTRLEN 22
#define MAKEWORD(low, high) (static_cast<WORD>((static_cast<BYTE>(low)) | (static_cast<WORD>(static_cast<BYTE>(high)) << 8)))

#define WSAEWOULDBLOCK 10035L
#define WSAEMSGSIZE 10040L
#define WSAEADDRINUSE 10048L

struct in_addr {
    std::uint32_t s_addr;
};

struct sockaddr {
    u_short sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    short sin_family;
    u_short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

// Only what WSAStartup writes: core reads none of it. The host leaves it as it is.
typedef struct WSAData {
    WORD wVersion;
    WORD wHighVersion;
    char rest[400];
} WSADATA;

inline u_short htons(u_short value) { return static_cast<u_short>((value << 8) | (value >> 8)); }
inline u_short ntohs(u_short value) { return htons(value); }
inline u_long htonl(u_long value) {
    return ((value & 0xFFUL) << 24) | ((value & 0xFF00UL) << 8) | ((value >> 8) & 0xFF00UL) | (value >> 24);
}
inline u_long ntohl(u_long value) { return htonl(value); }

// The dotted quad of an IPv4 address, as Winsock writes it. AF_INET only.
inline const char* inet_ntop(int family, const void* address, char* text, std::size_t size) {
    if (family != AF_INET || size < INET_ADDRSTRLEN) return nullptr;
    const auto* bytes = static_cast<const unsigned char*>(address);
    char* out = text;
    for (int i = 0; i < 4; ++i) {
        unsigned value = bytes[i];
        char digits[3];
        int count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (count > 0) *out++ = digits[--count];
        if (i < 3) *out++ = '.';
    }
    *out = '\0';
    return text;
}

// An IPv4 address from its dotted quad, as Winsock reads it. AF_INET only: 1 for an address, 0 for
// text that is not one.
inline int inet_pton(int family, const char* text, void* address) {
    if (family != AF_INET) return -1;
    unsigned char bytes[4];
    for (int i = 0; i < 4; ++i) {
        if (*text < '0' || *text > '9') return 0;
        unsigned value = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + static_cast<unsigned>(*text++ - '0');
            if (++digits > 3 || value > 255) return 0;
        }
        bytes[i] = static_cast<unsigned char>(value);
        if (i < 3 && *text++ != '.') return 0;
    }
    if (*text != '\0') return 0;
    for (int i = 0; i < 4; ++i) static_cast<unsigned char*>(address)[i] = bytes[i];
    return 1;
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
