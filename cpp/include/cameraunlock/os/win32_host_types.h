#pragma once

// The types, constants and structures of win32_host.h, and the Winsock helpers it writes itself.
// Nothing here is an import, so a Windows build can compile this beside the SDK's own, which is
// how cpp/tests/win32_host_constants.cpp holds each of them to the SDK.

#include <cstddef>
#include <cstdint>

typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int UINT;
typedef long LONG;
typedef long long LONGLONG;
typedef wchar_t WCHAR;
typedef WCHAR* LPWSTR;
typedef const WCHAR* LPCWSTR;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef void* HANDLE;
typedef struct HWND__* HWND;
typedef struct HMONITOR__* HMONITOR;
typedef void* HLOCAL;

#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define MAKELANGID(primary, sub) ((static_cast<WORD>(sub) << 10) | static_cast<WORD>(primary))
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(-1)))
#define INVALID_FILE_ATTRIBUTES (static_cast<DWORD>(-1))

#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_READONLY 0x00000001
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_NORMAL 0x00000080
#define FILE_BEGIN 0
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define MOVEFILE_WRITE_THROUGH 0x00000008

#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_PATH_NOT_FOUND 3L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_NOT_ENOUGH_MEMORY 8L
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
    struct {
        DWORD LowPart;
        LONG HighPart;
    } u;
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

typedef struct tagMONITORINFO {
    DWORD cbSize;
    RECT rcMonitor;
    RECT rcWork;
    DWORD dwFlags;
} MONITORINFO;

typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES;

typedef enum _GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard } GET_FILEEX_INFO_LEVELS;

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
#define INADDR_LOOPBACK 0x7F000001
#define INET_ADDRSTRLEN 22
#define MAKEWORD(low, high) (static_cast<WORD>((static_cast<BYTE>(low)) | (static_cast<WORD>(static_cast<BYTE>(high)) << 8)))

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
    u_short sin_family;
    u_short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

// WSAStartup's out parameter. The host hands Windows a buffer of its own and copies nothing back,
// and core reads none of it, so nothing here is ever written.
typedef struct WSAData {
    char unwritten;
} WSADATA;

inline u_short htons(u_short value) { return static_cast<u_short>((value << 8) | (value >> 8)); }
inline u_short ntohs(u_short value) { return htons(value); }
inline u_long htonl(u_long value) {
    return ((value & 0xFFUL) << 24) | ((value & 0xFF00UL) << 8) | ((value >> 8) & 0xFF00UL) | (value >> 24);
}
inline u_long ntohl(u_long value) { return htonl(value); }

// The dotted quad of an IPv4 address, as Winsock writes it: nullptr where the text and its
// terminator do not fit in `size` bytes. AF_INET only.
inline const char* inet_ntop(int family, const void* address, char* text, std::size_t size) {
    if (family != AF_INET) return nullptr;
    const auto* bytes = static_cast<const unsigned char*>(address);
    char quad[16];
    std::size_t length = 0;
    for (int i = 0; i < 4; ++i) {
        unsigned value = bytes[i];
        char digits[3];
        int count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (count > 0) quad[length++] = digits[--count];
        if (i < 3) quad[length++] = '.';
    }
    if (length + 1 > size) return nullptr;
    for (std::size_t i = 0; i < length; ++i) text[i] = quad[i];
    text[length] = '\0';
    return text;
}

// An IPv4 address from its dotted quad, which, as Winsock reads it, is four decimal numbers of at
// most 255 with no leading zero. AF_INET only: 1 for an address, 0 for text that is not one.
inline int inet_pton(int family, const char* text, void* address) {
    if (family != AF_INET) return -1;
    unsigned char bytes[4];
    for (int i = 0; i < 4; ++i) {
        if (*text < '0' || *text > '9') return 0;
        if (*text == '0' && text[1] >= '0' && text[1] <= '9') return 0;
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
