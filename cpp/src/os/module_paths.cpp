#include <cameraunlock/os/module_paths.h>

#ifdef _WIN32

#include <cstddef>

namespace cameraunlock::os {

namespace {

// No Win32 path can exceed this, so the doubling below terminates.
constexpr std::size_t kMaxNtPathChars = 32767;

// Only its address is used, and it has to be in this translation unit for that
// address to name the module the core was linked into.
void SelfAnchor() {}

}  // namespace

bool DirectoryOf(const std::wstring& module_path, std::wstring& directory) {
    const std::size_t separator = module_path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return false;

    directory = module_path.substr(0, separator);
    return true;
}

bool NarrowToAnsi(const std::wstring& wide, std::string& narrow) {
    if (wide.empty()) return false;

    const UINT codePage = GetACP();
    const bool unicodeCodePage =
        (codePage == CP_UTF8 || codePage == CP_UTF7 || codePage == 54936 /* GB18030 */);
    const DWORD flags = unicodeCodePage ? 0 : WC_NO_BEST_FIT_CHARS;
    BOOL usedDefault = FALSE;
    BOOL* const usedDefaultOut = unicodeCodePage ? nullptr : &usedDefault;

    const int bytes = WideCharToMultiByte(codePage, flags, wide.c_str(),
                                          static_cast<int>(wide.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return false;

    std::string converted(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(codePage, flags, wide.c_str(), static_cast<int>(wide.size()),
                            &converted[0], bytes, nullptr, usedDefaultOut) != bytes) {
        return false;
    }
    if (usedDefault) return false;

    narrow = converted;
    return true;
}

std::wstring ModuleFilePath(HMODULE module) {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        // Cleared first because a success leaves whatever error was already
        // pending, and the truncation test below reads it.
        SetLastError(ERROR_SUCCESS);
        const DWORD length =
            GetModuleFileNameW(module, &path[0], static_cast<DWORD>(path.size()));
        if (length == 0) return std::wstring();
        // Pre-1607 Windows truncates and returns the buffer size with no error
        // set; 1607 and later returns the buffer size and sets
        // ERROR_INSUFFICIENT_BUFFER. Both are covered.
        if (length < path.size() && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            path.resize(length);
            return path;
        }
        if (path.size() >= kMaxNtPathChars) return std::wstring();
        path.resize(path.size() * 2);
    }
}

std::wstring SelfModuleDirectory(HMODULE module) {
    if (module == nullptr) {
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&SelfAnchor), &module) ||
            module == nullptr) {
            return std::wstring();
        }
    }

    const std::wstring path = ModuleFilePath(module);
    if (path.empty()) return std::wstring();

    std::wstring directory;
    if (!DirectoryOf(path, directory)) return std::wstring();
    return directory;
}

std::wstring HostExeDirectory() {
    const std::wstring path = ModuleFilePath(nullptr);
    if (path.empty()) return std::wstring();

    std::wstring directory;
    if (!DirectoryOf(path, directory)) return std::wstring();
    return directory;
}

std::string SelfModuleDirectoryNarrow(HMODULE module) {
    std::string narrow;
    if (!NarrowToAnsi(SelfModuleDirectory(module), narrow)) return std::string();
    return narrow;
}

std::string HostExeDirectoryNarrow() {
    std::string narrow;
    if (!NarrowToAnsi(HostExeDirectory(), narrow)) return std::string();
    return narrow;
}

}  // namespace cameraunlock::os

#endif  // _WIN32
