#include "cameraunlock/config/config_owner.h"

#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32

#include <windows.h>

#include "canonical_ini_internal.h"
#include "checked_file_writer_internal.h"

#include <cstddef>
#include <cstdint>
#include <system_error>
#include <vector>

#endif

namespace cameraunlock::config {

const char* ConfigLoadStatusName(ConfigLoadStatus status) {
    switch (status) {
        case ConfigLoadStatus::Canonical: return "Canonical";
        case ConfigLoadStatus::Migrated: return "Migrated";
        case ConfigLoadStatus::Created: return "Created";
        case ConfigLoadStatus::Deferred: return "Deferred";
        case ConfigLoadStatus::LegacyRefused: return "LegacyRefused";
        case ConfigLoadStatus::Unreadable: return "Unreadable";
    }
    throw std::invalid_argument("ConfigLoadStatus " + std::to_string(static_cast<int>(status)) + " has no name");
}

const char* ConfigSaveStatusName(ConfigSaveStatus status) {
    switch (status) {
        case ConfigSaveStatus::Saved: return "Saved";
        case ConfigSaveStatus::NotSaved: return "NotSaved";
        case ConfigSaveStatus::Uncertain: return "Uncertain";
    }
    throw std::invalid_argument("ConfigSaveStatus " + std::to_string(static_cast<int>(status)) + " has no name");
}

const char* ConfigReloadStatusName(ConfigReloadStatus status) {
    switch (status) {
        case ConfigReloadStatus::Unchanged: return "Unchanged";
        case ConfigReloadStatus::Applied: return "Applied";
        case ConfigReloadStatus::Unreadable: return "Unreadable";
    }
    throw std::invalid_argument("ConfigReloadStatus " + std::to_string(static_cast<int>(status)) + " has no name");
}

namespace {

bool IsPathSeparator(wchar_t c) { return c == L'\\' || c == L'/'; }

bool IsFullyQualified(const std::wstring& path) {
    const bool drive = path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
                       path[1] == L':' && IsPathSeparator(path[2]);
    const bool unc = path.size() >= 2 && IsPathSeparator(path[0]) && IsPathSeparator(path[1]);
    return drive || unc;
}

}  // namespace

DefaultsFile DefaultsFile::PerUser() {
    DefaultsFile file;
    file.kind_ = Kind::kPerUser;
    return file;
}

DefaultsFile DefaultsFile::At(std::wstring path) {
    if (!IsFullyQualified(path)) {
        throw std::invalid_argument("DefaultsFile::At takes a fully qualified path, and '" + detail::DefaultsUtf8(path) +
                                    "' is not one");
    }
    DefaultsFile file;
    file.kind_ = Kind::kAt;
    file.path_ = std::move(path);
    return file;
}

namespace detail {

DefaultsFile DefaultsFileFromProbe(DefaultsProbe probe) {
    DefaultsFile file;
    file.kind_ = DefaultsFile::Kind::kProbed;
    file.probe_ = std::move(probe);
    return file;
}

bool DefaultsFileIsSet(const DefaultsFile& file) { return file.kind_ != DefaultsFile::Kind::kUnset; }

}  // namespace detail

#ifdef _WIN32

namespace detail {

namespace {

constexpr DWORD kReadChunk = 64 * 1024;

bool IsAbsent(DWORD error) { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }

// The failures .NET's File.GetLastWriteTimeUtc reports as a missing file.
bool IsAbsentForTime(DWORD error) { return IsAbsent(error) || error == ERROR_NOT_READY; }

std::uint64_t FileTimeCount(const FILETIME& time) {
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

bool IsSeparator(wchar_t c) { return c == L'\\' || c == L'/'; }

char FoldAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool EqualsAsciiIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (FoldAscii(a[i]) != FoldAscii(b[i])) return false;
    }
    return true;
}

// Returns the Win32 error, 0 when the handle was read to its end.
std::uint32_t ReadToEnd(HANDLE handle, std::string& bytes) {
    bytes.clear();
    std::vector<char> chunk(kReadChunk);
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(handle, chunk.data(), kReadChunk, &read, nullptr)) return GetLastError();
        if (read == 0) return 0;
        bytes.append(chunk.data(), read);
    }
}

OwnerFileRead ReadWithSharing(const std::wstring& path, DWORD share, HANDLE* held) {
    OwnerFileRead result;
    const HANDLE handle =
        CreateFileW(path.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (!IsAbsent(error)) result.error = error;
        return result;
    }
    result.present = true;
    result.error = ReadToEnd(handle, result.bytes);
    if (result.error != 0 || held == nullptr) {
        CloseHandle(handle);
        if (result.error != 0) result.bytes.clear();
        return result;
    }
    *held = handle;
    return result;
}

bool IsReadOnly(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0;
}

bool IsInUse(std::uint32_t error) { return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION; }

std::wstring Widen(const std::string& text, UINT code_page) {
    if (text.empty()) return std::wstring();
    const int length = MultiByteToWideChar(code_page, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "MultiByteToWideChar");
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(code_page, 0, text.data(), static_cast<int>(text.size()), &wide[0], length);
    return wide;
}

}  // namespace

OwnerHeldFile::~OwnerHeldFile() {
    if (handle_ != nullptr) CloseHandle(handle_);
}

OwnerFileRead OwnerHeldFile::Open(const std::wstring& path) {
    if (handle_ != nullptr) throw std::logic_error("OwnerHeldFile already holds a file");
    HANDLE held = nullptr;
    OwnerFileRead result = ReadWithSharing(path, FILE_SHARE_READ | FILE_SHARE_WRITE, &held);
    handle_ = held;
    return result;
}

std::uint32_t OwnerHeldFile::Reread(std::string& bytes) {
    if (handle_ == nullptr) throw std::logic_error("OwnerHeldFile holds no file");
    LARGE_INTEGER start{};
    if (!SetFilePointerEx(handle_, start, nullptr, FILE_BEGIN)) return GetLastError();
    return ReadToEnd(handle_, bytes);
}

void OwnerHeldFile::Close() {
    if (handle_ == nullptr) return;
    const HANDLE handle = handle_;
    handle_ = nullptr;
    if (!CloseHandle(handle)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CloseHandle");
    }
}

OwnerFileRead OwnerReadFile(const std::wstring& path) {
    return ReadWithSharing(path, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr);
}

std::uint64_t OwnerLastWriteTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return FileTimeCount(data.ftLastWriteTime);
    DWORD error = GetLastError();
    if (IsAbsentForTime(error)) return 0;
    // As .NET does: a file pending deletion refuses GetFileAttributesExW with access denied while
    // its folder still lists it with its time. FindFirstFileW would read a wildcard as a pattern.
    if (path.find_first_of(L"*?", path.find_last_of(L"\\/") + 1) == std::wstring::npos) {
        WIN32_FIND_DATAW found{};
        const HANDLE search = FindFirstFileW(path.c_str(), &found);
        if (search != INVALID_HANDLE_VALUE) {
            FindClose(search);
            return FileTimeCount(found.ftLastWriteTime);
        }
        error = GetLastError();
        if (IsAbsentForTime(error)) return 0;
    }
    throw std::system_error(static_cast<int>(error), std::system_category(), "GetFileAttributesExW " + OwnerUtf8(path));
}

std::wstring OwnerFullPath(const std::wstring& path, const char* option) {
    if (path.empty()) throw std::invalid_argument(std::string("the options name no ") + option);
    const bool drive = path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
                       path[1] == L':' && IsSeparator(path[2]);
    const bool unc = path.size() >= 2 && IsSeparator(path[0]) && IsSeparator(path[1]);
    if (!drive && !unc) {
        throw std::invalid_argument(std::string(option) + " '" + OwnerUtf8(path) + "' is not a fully qualified path");
    }
    const DWORD length = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (length == 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "GetFullPathNameW " + OwnerUtf8(path));
    }
    std::wstring full(length, L'\0');
    const DWORD written = GetFullPathNameW(path.c_str(), length, &full[0], nullptr);
    if (written == 0 || written >= length) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "GetFullPathNameW " + OwnerUtf8(path));
    }
    full.resize(written);
    return full;
}

bool OwnerSamePath(const std::wstring& a, const std::wstring& b) {
    const int compared =
        CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE);
    if (compared == 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CompareStringOrdinal");
    }
    return compared == CSTR_EQUAL;
}

bool OwnerFileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool OwnerPathExists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::uint64_t DefaultsLastWriteTime(const std::wstring& path) {
    try {
        return OwnerLastWriteTime(path);
    } catch (const std::system_error&) {
        return UINT64_MAX;
    }
}

std::string DefaultsFolderWhy(std::uint32_t error) {
    if (error == ERROR_ACCESS_DENIED) return "the folder cannot be written";
    return "it could not be written (" + OwnerErrorText(error) + ")";
}

DefaultsResolution ResolveDefaultsFile(const DefaultsFile& file) {
    switch (file.kind_) {
        case DefaultsFile::Kind::kPerUser: return ResolveDefaults(ProbeDefaults());
        case DefaultsFile::Kind::kProbed: return ResolveDefaults(*file.probe_);
        case DefaultsFile::Kind::kAt: break;
        case DefaultsFile::Kind::kUnset: throw std::invalid_argument("the DefaultsFile names no file");
    }
    const std::wstring path = OwnerFullPath(file.path_, "defaults");
    const std::size_t file_separator = path.find_last_of(L"\\/");
    const std::wstring folder = path.substr(0, file_separator);
    const std::size_t folder_separator = folder.find_last_of(L"\\/");
    const std::wstring parent = folder_separator == std::wstring::npos ? folder : folder.substr(0, folder_separator);
    DefaultsCandidate candidate;
    candidate.kind = DefaultsCandidateKind::kWindows;
    candidate.may_create = true;
    candidate.path = path;
    candidate.folder = folder;
    candidate.parent = parent;
    candidate.shown = DefaultsUtf8(path);
    candidate.shown_folder = DefaultsUtf8(folder);
    candidate.shown_parent = DefaultsUtf8(parent);
    DefaultsResolution resolution;
    resolution.platform = DefaultsPlatform::kWindows;
    resolution.candidates.push_back(std::move(candidate));
    return resolution;
}

LegacyInput OwnerLegacyInput(const std::wstring& path) {
    LegacyInput input;
    input.path = path;
    // WideCharToMultiByte refuses the no-best-fit flag and the used-default flag on these code
    // pages, where no character is lost; the round trip below still catches an unpaired surrogate.
    const UINT code_page = GetACP();
    const bool unicode = code_page == CP_UTF8 || code_page == CP_UTF7 || code_page == 54936;
    const DWORD flags = unicode ? 0 : WC_NO_BEST_FIT_CHARS;
    BOOL used_default = FALSE;
    const int length =
        WideCharToMultiByte(CP_ACP, flags, path.data(), static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "WideCharToMultiByte");
    }
    input.ansi_path.assign(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(CP_ACP, flags, path.data(), static_cast<int>(path.size()), &input.ansi_path[0], length,
                            nullptr, unicode ? nullptr : &used_default) != length) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "WideCharToMultiByte");
    }
    input.ansi_lossy = used_default != FALSE || Widen(input.ansi_path, CP_ACP) != path;
    return input;
}

std::string OwnerUtf8(const std::wstring& text) {
    if (text.empty()) return std::string();
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "WideCharToMultiByte");
    std::string narrow(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &narrow[0], length, nullptr, nullptr);
    return narrow;
}

std::string OwnerFileName(const std::wstring& path) {
    const std::size_t separator = path.find_last_of(L"\\/");
    return OwnerUtf8(separator == std::wstring::npos ? path : path.substr(separator + 1));
}

std::string OwnerAnsiForLog(const std::string& ansi) { return OwnerUtf8(Widen(ansi, CP_ACP)); }

CheckedWriteResult OwnerWrite(const std::wstring& path, const std::optional<std::string>& expected,
                              const std::string& candidate, const ConfigOwnerHook& hook, const char* prefix) {
    if (!hook) return WriteFileChecked(path, expected, candidate);
    return ::cameraunlock::detail::WriteFileCheckedWithFault(
        path, expected, candidate, [&hook, prefix](CheckedWriteStep step, const std::wstring& at) -> std::uint32_t {
            return hook(std::string(prefix) + CheckedWriteStepName(step), at);
        });
}

void OwnerStep(const ConfigOwnerHook& hook, const char* label, const std::wstring& path) {
    if (hook && hook(label, path) != 0) {
        throw std::logic_error(std::string("the test hook failed ") + label + ", which is not a writer step");
    }
}

bool OwnerWriteFailed(const CheckedWriteResult& result) {
    return result.status == CheckedWriteStatus::Failed || result.cleanup_error != 0;
}

std::string OwnerWriteLog(const std::wstring& path, const CheckedWriteResult& result) {
    if (result.status != CheckedWriteStatus::Failed) {
        return "Writing " + OwnerUtf8(path) + " stopped because the target changed (" +
               CheckedWriteStatusName(result.status) + "), and its temporary " + OwnerUtf8(result.temporary_path) +
               " could not be deleted: " + OwnerErrorText(result.cleanup_error);
    }
    std::string message = "Writing " + OwnerUtf8(path) + " failed at " + CheckedWriteStepName(result.failed_step) +
                          ": " + OwnerErrorText(result.error);
    if (result.outcome_uncertain) {
        message += " Windows did not finish the replacement, so the target may be missing or renamed. The new "
                   "contents are in " +
                   OwnerUtf8(result.temporary_path) + ", left in place.";
    }
    if (result.completion_error != 0) message += " Moving them into place failed: " + OwnerErrorText(result.completion_error);
    if (result.cleanup_error != 0) {
        message += " Cleaning up its temporary " + OwnerUtf8(result.temporary_path) +
                   " also failed: " + OwnerErrorText(result.cleanup_error);
    }
    return message;
}

std::string OwnerWriteWhy(const std::wstring& path, const CheckedWriteResult& result) {
    if (result.outcome_uncertain) {
        return "Windows did not finish replacing " + OwnerUtf8(path) + ", so it may be missing; the new settings are in " +
               OwnerUtf8(result.temporary_path);
    }
    const bool failed = result.status == CheckedWriteStatus::Failed;
    const std::uint32_t error = failed ? result.error : result.cleanup_error;
    const CheckedWriteStep step = failed ? result.failed_step : CheckedWriteStep::RemoveTemporary;
    if (IsInUse(error)) return "the file is in use by another program";
    if (error == ERROR_ACCESS_DENIED && step == CheckedWriteStep::CreateTemporary) return "the folder cannot be written";
    if (error == ERROR_ACCESS_DENIED && IsReadOnly(path)) return "the file is read-only";
    return "it could not be written (" + OwnerErrorText(error) + ")";
}

std::string OwnerReadWhy(std::uint32_t error) {
    if (IsInUse(error)) return "the file is in use by another program";
    return "it could not be read (" + OwnerErrorText(error) + ")";
}

std::string OwnerConflict(CheckedWriteStatus status) {
    switch (status) {
        case CheckedWriteStatus::TargetAppeared: return "another program created the file at the same time";
        case CheckedWriteStatus::TargetMissing: return "the file was deleted at the same time";
        default: return "the file was changed by another program at the same time";
    }
}

std::string OwnerErrorText(std::uint32_t error) {
    std::string text = "Windows error " + std::to_string(error);
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    if (length == 0) return text;
    std::wstring wide(message, length);
    LocalFree(message);
    while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n' || wide.back() == L' ')) wide.pop_back();
    return text + ": " + OwnerUtf8(wide);
}

void OwnerLogNotCarried(const std::string& snapshot, const std::vector<LegacyKey>& keys, const std::string& input,
                        std::vector<std::string>& log) {
    if (StartsWithUtf16Mark(snapshot)) {
        log.push_back(input +
                      ": is saved as UTF-16, so its lines this build does not read are not listed; the original keeps "
                      "them.");
        return;
    }
    for (const CanonicalKeyLine& line : CanonicalKeyLines(snapshot)) {
        bool imported = false;
        for (const LegacyKey& key : keys) {
            if (!EqualsAsciiIgnoreCase(line.key, key.key)) continue;
            if (key.section.empty() || (line.section && EqualsAsciiIgnoreCase(*line.section, key.section))) {
                imported = true;
                break;
            }
        }
        if (imported) continue;
        const std::string section = line.section ? "[" + *line.section + "] " : std::string();
        log.push_back(input + ": not carried: " + section + line.key + "=" + line.value + " on line " +
                      std::to_string(line.line) + ", this build does not read it");
    }
}

}  // namespace detail

#endif  // _WIN32

}  // namespace cameraunlock::config
