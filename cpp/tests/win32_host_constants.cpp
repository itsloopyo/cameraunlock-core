// Holds win32_host_types.h, which the WebAssembly build compiles in place of <windows.h>, to the
// Windows SDK: each constant's value and type, the base types, and the layout of each structure
// that crosses between the module and Win32Host. Then runs the Winsock helpers it writes itself
// (byte order, inet_pton, inet_ntop) beside Winsock's own.
//
// The shim is compiled first, into namespace shim, and each of its macros is read into a constant
// there and undefined before the SDK's headers define their own. Its structures are laid out here
// by MSVC for x64, with the SDK's 16-bit WCHAR. The module's own layouts, where wchar_t is 32 bits,
// are held by static_asserts in win32_host.h to the offsets Win32Host.java reads and writes at.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>

namespace shim {
#include "cameraunlock/os/win32_host_types.h"
}

#define WIN32_HOST_CONSTANTS(X)                                                                                        \
    X(TRUE) X(FALSE) X(MAX_PATH) X(INVALID_FILE_ATTRIBUTES) X(GENERIC_READ) X(GENERIC_WRITE) X(FILE_SHARE_READ)       \
    X(FILE_SHARE_WRITE) X(FILE_SHARE_DELETE) X(CREATE_NEW) X(CREATE_ALWAYS) X(OPEN_EXISTING) X(FILE_ATTRIBUTE_READONLY) \
    X(FILE_ATTRIBUTE_DIRECTORY) X(FILE_ATTRIBUTE_NORMAL) X(FILE_BEGIN) X(MOVEFILE_REPLACE_EXISTING)                    \
    X(MOVEFILE_WRITE_THROUGH) X(ERROR_FILE_NOT_FOUND) X(ERROR_PATH_NOT_FOUND) X(ERROR_ACCESS_DENIED)                   \
    X(ERROR_NOT_ENOUGH_MEMORY) X(ERROR_NOT_READY) X(ERROR_WRITE_FAULT) X(ERROR_SHARING_VIOLATION)                       \
    X(ERROR_LOCK_VIOLATION) X(ERROR_FILE_EXISTS) X(ERROR_INSUFFICIENT_BUFFER) X(ERROR_ALREADY_EXISTS)                  \
    X(ERROR_UNABLE_TO_REMOVE_REPLACED) X(ERROR_UNABLE_TO_MOVE_REPLACEMENT) X(ERROR_UNABLE_TO_MOVE_REPLACEMENT_2)       \
    X(CP_ACP) X(CP_UTF7) X(CP_UTF8) X(MB_ERR_INVALID_CHARS) X(WC_NO_BEST_FIT_CHARS) X(CSTR_EQUAL)                       \
    X(FORMAT_MESSAGE_ALLOCATE_BUFFER) X(FORMAT_MESSAGE_IGNORE_INSERTS) X(FORMAT_MESSAGE_FROM_SYSTEM) X(LANG_NEUTRAL)   \
    X(SUBLANG_DEFAULT) X(VK_SHIFT) X(VK_CONTROL) X(VK_MENU) X(GWL_STYLE) X(WS_CAPTION) X(MONITOR_DEFAULTTONEAREST)    \
    X(SWP_NOSIZE) X(SWP_NOZORDER) X(SWP_NOACTIVATE) X(SOCKET_ERROR) X(AF_INET) X(SOCK_DGRAM) X(IPPROTO_UDP)            \
    X(FIONBIO) X(INADDR_ANY) X(INADDR_LOOPBACK) X(INET_ADDRSTRLEN) X(WSAEMSGSIZE) X(WSAEADDRINUSE)

// The shim's macros that are not plain constants, each checked on its own below.
#define WIN32_HOST_OTHER_MACROS(X) X(MAKELANGID) X(MAKEWORD) X(INVALID_HANDLE_VALUE) X(INVALID_SOCKET)

namespace shim {
#define CAPTURE(name) constexpr auto name##_ = name;
WIN32_HOST_CONSTANTS(CAPTURE)
#undef CAPTURE
constexpr int makelangid_neutral = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
constexpr int makelangid_most = MAKELANGID(0x3FF, 0x3F);
constexpr WORD makeword_2_2 = MAKEWORD(2, 2);
constexpr WORD makeword_wide = MAKEWORD(0x1FF, 0x2FE);
const std::intptr_t invalid_handle_value = reinterpret_cast<std::intptr_t>(INVALID_HANDLE_VALUE);
constexpr SOCKET invalid_socket = INVALID_SOCKET;
// The SDK's s_addr is a macro for a member of a union.
constexpr std::size_t in_addr_s_addr_offset = offsetof(in_addr, s_addr);
constexpr std::size_t in_addr_s_addr_size = sizeof(in_addr::s_addr);
}  // namespace shim

#undef TRUE
#undef FALSE
#undef MAX_PATH
#undef MAKELANGID
#undef INVALID_HANDLE_VALUE
#undef INVALID_FILE_ATTRIBUTES
#undef GENERIC_READ
#undef GENERIC_WRITE
#undef FILE_SHARE_READ
#undef FILE_SHARE_WRITE
#undef FILE_SHARE_DELETE
#undef CREATE_NEW
#undef CREATE_ALWAYS
#undef OPEN_EXISTING
#undef FILE_ATTRIBUTE_READONLY
#undef FILE_ATTRIBUTE_DIRECTORY
#undef FILE_ATTRIBUTE_NORMAL
#undef FILE_BEGIN
#undef MOVEFILE_REPLACE_EXISTING
#undef MOVEFILE_WRITE_THROUGH
#undef ERROR_FILE_NOT_FOUND
#undef ERROR_PATH_NOT_FOUND
#undef ERROR_ACCESS_DENIED
#undef ERROR_NOT_ENOUGH_MEMORY
#undef ERROR_NOT_READY
#undef ERROR_WRITE_FAULT
#undef ERROR_SHARING_VIOLATION
#undef ERROR_LOCK_VIOLATION
#undef ERROR_FILE_EXISTS
#undef ERROR_INSUFFICIENT_BUFFER
#undef ERROR_ALREADY_EXISTS
#undef ERROR_UNABLE_TO_REMOVE_REPLACED
#undef ERROR_UNABLE_TO_MOVE_REPLACEMENT
#undef ERROR_UNABLE_TO_MOVE_REPLACEMENT_2
#undef CP_ACP
#undef CP_UTF7
#undef CP_UTF8
#undef MB_ERR_INVALID_CHARS
#undef WC_NO_BEST_FIT_CHARS
#undef CSTR_EQUAL
#undef FORMAT_MESSAGE_ALLOCATE_BUFFER
#undef FORMAT_MESSAGE_IGNORE_INSERTS
#undef FORMAT_MESSAGE_FROM_SYSTEM
#undef LANG_NEUTRAL
#undef SUBLANG_DEFAULT
#undef VK_SHIFT
#undef VK_CONTROL
#undef VK_MENU
#undef GWL_STYLE
#undef WS_CAPTION
#undef MONITOR_DEFAULTTONEAREST
#undef SWP_NOSIZE
#undef SWP_NOZORDER
#undef SWP_NOACTIVATE
#undef INVALID_SOCKET
#undef SOCKET_ERROR
#undef AF_INET
#undef SOCK_DGRAM
#undef IPPROTO_UDP
#undef FIONBIO
#undef INADDR_ANY
#undef INADDR_LOOPBACK
#undef INET_ADDRSTRLEN
#undef MAKEWORD
#undef WSAEMSGSIZE
#undef WSAEADDRINUSE

namespace {

constexpr bool Same(const char* a, const char* b) { return *a == *b && (*a == '\0' || Same(a + 1, b + 1)); }

}  // namespace

// A macro still defined here would hide the SDK's, where the SDK defines its own only if there is
// none, and the checks below would compare the shim with itself.
#define SPELLING(x) #x
#define EXPANSION(x) SPELLING(x)
#define UNDEFINED(name) static_assert(Same(EXPANSION(name), #name), #name " is still defined: #undef it above");
WIN32_HOST_CONSTANTS(UNDEFINED)
UNDEFINED(INVALID_HANDLE_VALUE)
UNDEFINED(INVALID_SOCKET)
#undef UNDEFINED

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace {

template <typename T, bool = std::is_enum<T>::value>
struct Plain {
    using type = T;
};
template <typename T>
struct Plain<T, true> {
    using type = std::underlying_type_t<T>;
};

}  // namespace

#define CONSTANT(name)                                                                                    \
    static_assert(shim::name##_ == name, #name " has the SDK's value");                                \
    static_assert(std::is_same<std::remove_const_t<decltype(shim::name##_)>,                           \
                               Plain<std::remove_const_t<decltype(name)>>::type>::value,                \
                  #name " has the SDK's type");
WIN32_HOST_CONSTANTS(CONSTANT)
#undef CONSTANT

static_assert(shim::makelangid_neutral == MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), "MAKELANGID");
static_assert(shim::makelangid_most == MAKELANGID(0x3FF, 0x3F), "MAKELANGID at its widest");
static_assert(shim::makeword_2_2 == MAKEWORD(2, 2), "MAKEWORD");
static_assert(shim::makeword_wide == MAKEWORD(0x1FF, 0x2FE), "MAKEWORD keeps a byte of each half");
static_assert(static_cast<std::uint32_t>(INVALID_SOCKET) == shim::invalid_socket,
              "INVALID_SOCKET is the SDK's, in the 32 bits the module keeps a socket in");
static_assert(static_cast<int>(shim::GetFileExInfoStandard) == static_cast<int>(GetFileExInfoStandard), "GetFileExInfoStandard");

#define TYPE(name) static_assert(std::is_same<shim::name, ::name>::value, #name " is the SDK's type");
TYPE(DWORD)
TYPE(BOOL)
TYPE(BYTE)
TYPE(WORD)
TYPE(UINT)
TYPE(LONG)
TYPE(LONGLONG)
TYPE(WCHAR)
TYPE(LPWSTR)
TYPE(LPCWSTR)
TYPE(LPSTR)
TYPE(LPCSTR)
TYPE(LPVOID)
TYPE(LPCVOID)
TYPE(HANDLE)
TYPE(HLOCAL)
TYPE(u_long)
TYPE(u_short)
#undef TYPE
static_assert(std::is_unsigned<shim::SOCKET>::value && std::is_unsigned<::SOCKET>::value, "SOCKET is unsigned in both");

#define SIZE(type) static_assert(sizeof(shim::type) == sizeof(::type) && alignof(shim::type) == alignof(::type), #type " has the SDK's size");
#define FIELD(type, member)                                                                                          \
    static_assert(offsetof(shim::type, member) == offsetof(::type, member), #type "::" #member " is where the SDK has it"); \
    static_assert(sizeof(shim::type::member) == sizeof(::type::member), #type "::" #member " has the SDK's size");         \
    static_assert(std::is_signed<decltype(shim::type::member)>::value == std::is_signed<decltype(::type::member)>::value, \
                  #type "::" #member " has the SDK's signedness");

SIZE(FILETIME)
FIELD(FILETIME, dwLowDateTime)
FIELD(FILETIME, dwHighDateTime)

SIZE(SYSTEMTIME)
FIELD(SYSTEMTIME, wYear)
FIELD(SYSTEMTIME, wMonth)
FIELD(SYSTEMTIME, wDayOfWeek)
FIELD(SYSTEMTIME, wDay)
FIELD(SYSTEMTIME, wHour)
FIELD(SYSTEMTIME, wMinute)
FIELD(SYSTEMTIME, wSecond)
FIELD(SYSTEMTIME, wMilliseconds)

SIZE(LARGE_INTEGER)
FIELD(LARGE_INTEGER, LowPart)
FIELD(LARGE_INTEGER, HighPart)
FIELD(LARGE_INTEGER, u)
static_assert(offsetof(shim::LARGE_INTEGER, u.LowPart) == offsetof(::LARGE_INTEGER, u.LowPart) &&
                  offsetof(shim::LARGE_INTEGER, u.HighPart) == offsetof(::LARGE_INTEGER, u.HighPart),
              "LARGE_INTEGER::u is laid out as the SDK's");
FIELD(LARGE_INTEGER, QuadPart)

SIZE(WIN32_FILE_ATTRIBUTE_DATA)
FIELD(WIN32_FILE_ATTRIBUTE_DATA, dwFileAttributes)
FIELD(WIN32_FILE_ATTRIBUTE_DATA, ftCreationTime)
FIELD(WIN32_FILE_ATTRIBUTE_DATA, ftLastAccessTime)
FIELD(WIN32_FILE_ATTRIBUTE_DATA, ftLastWriteTime)
FIELD(WIN32_FILE_ATTRIBUTE_DATA, nFileSizeHigh)
FIELD(WIN32_FILE_ATTRIBUTE_DATA, nFileSizeLow)

SIZE(WIN32_FIND_DATAW)
FIELD(WIN32_FIND_DATAW, dwFileAttributes)
FIELD(WIN32_FIND_DATAW, ftCreationTime)
FIELD(WIN32_FIND_DATAW, ftLastAccessTime)
FIELD(WIN32_FIND_DATAW, ftLastWriteTime)
FIELD(WIN32_FIND_DATAW, nFileSizeHigh)
FIELD(WIN32_FIND_DATAW, nFileSizeLow)
FIELD(WIN32_FIND_DATAW, dwReserved0)
FIELD(WIN32_FIND_DATAW, dwReserved1)
FIELD(WIN32_FIND_DATAW, cFileName)
FIELD(WIN32_FIND_DATAW, cAlternateFileName)

SIZE(BY_HANDLE_FILE_INFORMATION)
FIELD(BY_HANDLE_FILE_INFORMATION, dwFileAttributes)
FIELD(BY_HANDLE_FILE_INFORMATION, ftCreationTime)
FIELD(BY_HANDLE_FILE_INFORMATION, ftLastAccessTime)
FIELD(BY_HANDLE_FILE_INFORMATION, ftLastWriteTime)
FIELD(BY_HANDLE_FILE_INFORMATION, dwVolumeSerialNumber)
FIELD(BY_HANDLE_FILE_INFORMATION, nFileSizeHigh)
FIELD(BY_HANDLE_FILE_INFORMATION, nFileSizeLow)
FIELD(BY_HANDLE_FILE_INFORMATION, nNumberOfLinks)
FIELD(BY_HANDLE_FILE_INFORMATION, nFileIndexHigh)
FIELD(BY_HANDLE_FILE_INFORMATION, nFileIndexLow)

SIZE(RECT)
FIELD(RECT, left)
FIELD(RECT, top)
FIELD(RECT, right)
FIELD(RECT, bottom)

SIZE(MONITORINFO)
FIELD(MONITORINFO, cbSize)
FIELD(MONITORINFO, rcMonitor)
FIELD(MONITORINFO, rcWork)
FIELD(MONITORINFO, dwFlags)

SIZE(sockaddr)
FIELD(sockaddr, sa_family)
FIELD(sockaddr, sa_data)

SIZE(sockaddr_in)
FIELD(sockaddr_in, sin_family)
FIELD(sockaddr_in, sin_port)
FIELD(sockaddr_in, sin_addr)
FIELD(sockaddr_in, sin_zero)

SIZE(in_addr)
static_assert(shim::in_addr_s_addr_offset == offsetof(in_addr, s_addr) && shim::in_addr_s_addr_size == sizeof(in_addr{}.s_addr),
              "in_addr::s_addr is where the SDK has it");
#undef FIELD
#undef SIZE

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool passed, const std::string& name) {
    ++g_checks;
    if (!passed) {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

// Every macro the shim defines is checked above: a new one has to be added there.
void CheckEveryMacroIsHeld() {
    std::set<std::string> held;
#define HELD(name) held.insert(#name);
    WIN32_HOST_CONSTANTS(HELD)
    WIN32_HOST_OTHER_MACROS(HELD)
#undef HELD
    std::ifstream file(CAMERAUNLOCK_WIN32_HOST_TYPES_H);
    Check(file.good(), std::string("read ") + CAMERAUNLOCK_WIN32_HOST_TYPES_H);
    std::stringstream text;
    text << file.rdbuf();
    const std::string source = text.str();
    const std::regex define(R"(#define\s+(\w+))");
    int defines = 0;
    for (std::sregex_iterator match(source.begin(), source.end(), define), end; match != end; ++match) {
        ++defines;
        Check(held.count((*match)[1].str()) == 1, (*match)[1].str() + " is held to the SDK here");
    }
    Check(defines == static_cast<int>(held.size()), "the shim defines as many macros as are held here");
}

void CheckByteOrder() {
    for (unsigned value : {0x0000u, 0x1234u, 0xABCDu, 0xFFFFu, 0x00FFu, 4242u}) {
        const auto v = static_cast<u_short>(value);
        Check(shim::htons(v) == htons(v) && shim::ntohs(v) == ntohs(v), "htons and ntohs of " + std::to_string(value));
    }
    for (u_long value : {0x00000000UL, 0x12345678UL, 0x7F000001UL, 0xFFFFFFFFUL, 0x80000001UL, 0xC0000201UL}) {
        Check(shim::htonl(value) == htonl(value) && shim::ntohl(value) == ntohl(value), "htonl and ntohl of " + std::to_string(value));
    }
}

void CheckInetPton() {
    const char* inputs[] = {"127.0.0.1", "0.0.0.0", "255.255.255.255", "192.0.2.1", "10.20.100.9", "256.0.0.1",
                            "1.2.3.256", "01.2.3.4", "001.2.3.4", "0001.2.3.4", "1.2.3.04", "00.0.0.0", "1.0.0.00",
                            "010.010.010.010", "1.2.3.0255", "1.2.3", "1.2.3.4.5", "1.2.3.4 ", " 1.2.3.4", "1.2.3.4x",
                            "1..3.4", ".1.2.3", "1.2.3.", "", "0x7f.0.0.1", "127.1", "1.2.3.-4", "+1.2.3.4", "1.2.3.4\n",
                            "999.1.1.1", "1.2.3.00"};
    for (const char* input : inputs) {
        unsigned char ours[4] = {0xEE, 0xEE, 0xEE, 0xEE};
        unsigned char winsock[4] = {0xEE, 0xEE, 0xEE, 0xEE};
        const int our_result = shim::inet_pton(AF_INET, input, ours);
        const int winsock_result = inet_pton(AF_INET, input, winsock);
        Check(our_result == winsock_result && (our_result != 1 || std::memcmp(ours, winsock, 4) == 0),
              std::string("inet_pton(\"") + input + "\") as Winsock reads it");
    }
}

void CheckInetNtop() {
    const unsigned char addresses[][4] = {{127, 0, 0, 1}, {0, 0, 0, 0}, {255, 255, 255, 255}, {10, 20, 100, 9}, {192, 0, 2, 1}};
    for (const auto& address : addresses) {
        for (std::size_t size = 0; size <= 22; ++size) {
            char ours[32] = {};
            char winsock[32] = {};
            const char* our_result = shim::inet_ntop(AF_INET, address, ours, size);
            const char* winsock_result = inet_ntop(AF_INET, address, winsock, size);
            Check((our_result == nullptr) == (winsock_result == nullptr) &&
                      (our_result == nullptr || std::strcmp(ours, winsock) == 0),
                  std::string("inet_ntop of ") + (winsock_result != nullptr ? winsock : "an address") + " into " +
                      std::to_string(size) + " bytes, as Winsock writes it");
        }
    }
}

}  // namespace

int main() {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        std::cout << "WSAStartup failed: " << WSAGetLastError() << "\n";
        return 1;
    }
    Check(shim::invalid_handle_value == reinterpret_cast<std::intptr_t>(INVALID_HANDLE_VALUE), "INVALID_HANDLE_VALUE");
    CheckEveryMacroIsHeld();
    CheckByteOrder();
    CheckInetPton();
    CheckInetNtop();
    WSACleanup();
    std::cout << "win32_host_types.h against the SDK: " << g_checks - g_failures << " of " << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
