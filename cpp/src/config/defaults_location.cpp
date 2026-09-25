#include <cameraunlock/config/defaults_location.h>

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace cameraunlock::config::detail {
namespace {

constexpr wchar_t kFolderName[] = L"CameraUnlock";
constexpr wchar_t kFileName[] = L"Defaults.ini";
constexpr char kNoKnownFolder[] = "Windows reported no roaming AppData folder";
constexpr char kThisPrefix[] = " (this Wine prefix)";

bool IsDrivePath(const std::wstring& path) {
    return path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
           path[1] == L':' && path[2] == L'\\';
}

bool IsUnixAbsolute(const std::wstring& path) { return !path.empty() && path[0] == L'/'; }

std::wstring TrimEnd(std::wstring text, wchar_t c) {
    while (!text.empty() && text.back() == c) text.pop_back();
    return text;
}

// The root is the home, shown as ~, or with no home known the XDG folder the path came from,
// shown as its variable, so an account name in either never reaches the log.
std::string Show(const std::wstring& path, const std::wstring& root, const std::string& root_name,
                 wchar_t separator) {
    const bool under_root = !root.empty() && path.compare(0, root.size(), root) == 0 &&
                            (path.size() == root.size() || path[root.size()] == separator);
    return under_root ? root_name + DefaultsUtf8(path.substr(root.size())) : DefaultsUtf8(path);
}

// The first XDG spelling that is absolute, as the dirs crate reads it, joined with the folder
// name; empty on Darwin, with no host system, or with neither spelling absolute.
std::wstring HostUnixFolder(const DefaultsProbe& probe, std::string& variable) {
    variable.clear();
    if (probe.host_system.empty() || probe.host_system == "Darwin") return std::wstring();
    const std::wstring* xdg = nullptr;
    if (IsUnixAbsolute(probe.wine_host_xdg_config_home)) {
        variable = "WINE_HOST_XDG_CONFIG_HOME";
        xdg = &probe.wine_host_xdg_config_home;
    } else if (IsUnixAbsolute(probe.xdg_config_home)) {
        variable = "XDG_CONFIG_HOME";
        xdg = &probe.xdg_config_home;
    } else {
        return std::wstring();
    }
    return TrimEnd(*xdg, L'/') + L"/" + kFolderName;
}

// WINEHOMEDIR is an NT path: \??\X:\... when a drive maps the home, whose DOS form drops the
// \??\, and \??\unix\... when none does.
std::wstring DosHome(const std::wstring& wine_home_dir) {
    static const std::wstring kPrefix = L"\\??\\";
    if (wine_home_dir.compare(0, kPrefix.size(), kPrefix) != 0) return std::wstring();
    const std::wstring dos = wine_home_dir.substr(kPrefix.size());
    return IsDrivePath(dos) ? TrimEnd(dos, L'\\') : std::wstring();
}

DefaultsCandidate AppData(DefaultsCandidateKind kind, const std::wstring& known_folder, bool may_create) {
    DefaultsCandidate candidate;
    candidate.kind = kind;
    candidate.may_create = may_create;
    candidate.folder = known_folder + L"\\" + kFolderName;
    candidate.path = candidate.folder + L"\\" + kFileName;
    candidate.parent = known_folder;
    candidate.shown = "%AppData%\\CameraUnlock\\Defaults.ini";
    candidate.shown_folder = "%AppData%\\CameraUnlock";
    candidate.shown_parent = "%AppData%";
    return candidate;
}

// With no home the root is the candidate's parent, the XDG folder `variable` named.
DefaultsCandidate Located(DefaultsCandidateKind kind, bool may_create, const std::wstring& folder, wchar_t separator,
                          const std::wstring& home, const std::string& variable) {
    DefaultsCandidate candidate;
    candidate.kind = kind;
    candidate.may_create = may_create;
    candidate.folder = folder;
    candidate.path = folder + separator + kFileName;
    candidate.parent = folder.substr(0, folder.find_last_of(separator));
    const std::wstring& root = home.empty() ? candidate.parent : home;
    const std::string root_name = home.empty() ? "$" + variable : "~";
    candidate.shown = Show(candidate.path, root, root_name, separator);
    candidate.shown_folder = Show(candidate.folder, root, root_name, separator);
    candidate.shown_parent = Show(candidate.parent, root, root_name, separator);
    return candidate;
}

DefaultsResolution ResolveWine(const DefaultsProbe& probe) {
    DefaultsResolution resolution;
    resolution.platform = probe.platform;
    resolution.wine = "Wine " + probe.wine_version + (probe.host_system.empty() ? "" : " on " + probe.host_system);
    std::string variable;
    resolution.unix_folder = HostUnixFolder(probe, variable);
    const std::wstring home = DosHome(probe.wine_home_dir);
    std::wstring host_folder;
    if (probe.host_system.empty()) {
        resolution.host_unusable = "Wine did not report the host system";
    } else if (!resolution.unix_folder.empty()) {
        const std::string named = "$" + variable + "/CameraUnlock";
        if (probe.code_page_failed) {
            resolution.host_unusable = named + " could not be converted to the Unix code page";
        } else if (probe.dos_file_name.empty()) {
            resolution.host_unusable = "Wine could not convert " + named + " to a Windows path";
        } else if (!IsDrivePath(probe.dos_file_name)) {
            resolution.host_unusable = named + " has no drive letter in this Wine prefix";
        } else {
            host_folder = probe.dos_file_name;
        }
    } else if (probe.wine_home_dir.empty()) {
        resolution.host_unusable = "WINEHOMEDIR is not set";
    } else if (home.empty()) {
        resolution.host_unusable = "the home folder has no drive letter in this Wine prefix";
    } else {
        host_folder = home + (probe.host_system == "Darwin" ? L"\\Library\\Application Support\\" : L"\\.config\\") +
                      kFolderName;
    }

    if (!host_folder.empty()) {
        resolution.candidates.push_back(Located(DefaultsCandidateKind::kWineHost, true, host_folder, L'\\', home, variable));
    }
    if (!probe.known_folder.empty()) {
        resolution.candidates.push_back(AppData(DefaultsCandidateKind::kWinePrefix, probe.known_folder, true));
    }
    if (resolution.candidates.empty()) resolution.no_location = kNoKnownFolder;
    return resolution;
}

DefaultsResolution ResolveNative(const DefaultsProbe& probe) {
    DefaultsResolution resolution;
    resolution.platform = probe.platform;
    const bool has_home = IsUnixAbsolute(probe.home);
    const std::wstring home = has_home ? TrimEnd(probe.home, L'/') : std::wstring();
    if (IsUnixAbsolute(probe.xdg_config_home)) {
        resolution.candidates.push_back(Located(DefaultsCandidateKind::kNative, false,
                                                TrimEnd(probe.xdg_config_home, L'/') + L"/" + kFolderName, L'/', home,
                                                "XDG_CONFIG_HOME"));
    } else if (has_home) {
        resolution.candidates.push_back(
            Located(DefaultsCandidateKind::kNative, false, home + L"/.config/" + kFolderName, L'/', home, ""));
    }
    if (has_home) {
        resolution.candidates.push_back(Located(DefaultsCandidateKind::kNative, false,
                                                home + L"/Library/Application Support/" + kFolderName, L'/', home, ""));
    }
    if (resolution.candidates.empty()) resolution.no_location = "HOME is not set to an absolute path";
    return resolution;
}


bool HasHost(const DefaultsResolution& resolution) {
    return !resolution.candidates.empty() && resolution.candidates[0].kind == DefaultsCandidateKind::kWineHost;
}

std::string HostFolder(const DefaultsResolution& resolution) {
    return HasHost(resolution) ? resolution.candidates[0].shown_folder : "none";
}

std::string HostWhy(const DefaultsResolution& resolution, const std::vector<DefaultsCreationOutcome>& outcomes) {
    if (!HasHost(resolution)) return resolution.host_unusable;
    switch (outcomes[0].kind) {
        case DefaultsCreation::kParentMissing:
            return resolution.candidates[0].shown_parent + " does not exist";
        case DefaultsCreation::kFolderFailed:
            return "it could not be created: " + outcomes[0].why;
        case DefaultsCreation::kFileFailed:
            return "Defaults.ini was not created there: " + outcomes[0].why;
        default:
            return "it holds no Defaults.ini, and one there would be shared by every Wine prefix";
    }
}

std::string HostSentence(const DefaultsResolution& resolution, const std::vector<DefaultsCreationOutcome>& outcomes) {
    return " The host's config folder " + HostFolder(resolution) + " could not be used: " +
           HostWhy(resolution, outcomes) + ".";
}

std::string Found(const DefaultsResolution& resolution, std::size_t index, const std::string& what,
                  const std::vector<DefaultsCreationOutcome>& outcomes) {
    const DefaultsCandidate& candidate = resolution.candidates[index];
    switch (candidate.kind) {
        case DefaultsCandidateKind::kWineHost:
            return "Defaults.ini: " + candidate.shown + " (" + resolution.wine + ", the host's config folder, " + what +
                   ")";
        case DefaultsCandidateKind::kWinePrefix:
            return "Defaults.ini: " + candidate.shown + " (" + resolution.wine + ", this Wine prefix, " + what +
                   "): the host's config folder " + HostFolder(resolution) + " could not be used: " +
                   HostWhy(resolution, outcomes) + ".";
        default:
            return "Defaults.ini: " + candidate.shown + " (" + what + ")";
    }
}

std::string Failure(const DefaultsCandidate& candidate, const DefaultsCreationOutcome& outcome, const char* suffix) {
    switch (outcome.kind) {
        case DefaultsCreation::kParentMissing:
            return candidate.shown_folder + suffix + " was not created, because " + candidate.shown_parent +
                   " does not exist.";
        case DefaultsCreation::kFolderFailed:
            return candidate.shown_folder + suffix + " could not be created: " + outcome.why + ".";
        case DefaultsCreation::kFileFailed:
            return candidate.shown + suffix + " was not created: " + outcome.why + ".";
        default:
            throw std::invalid_argument("ChooseDefaults: an outcome that is not a failure reached the failure line");
    }
}

DefaultsChoice Final(std::string line) {
    DefaultsChoice choice;
    choice.line = std::move(line);
    return choice;
}

DefaultsChoice ReadAt(std::size_t index, std::string line) {
    DefaultsChoice choice;
    choice.read = static_cast<int>(index);
    choice.line = std::move(line);
    return choice;
}

}  // namespace

std::string DefaultsUtf8(const std::wstring& text) {
    std::string utf8;
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t c = static_cast<std::uint32_t>(text[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.size()) {
                const std::uint32_t low = static_cast<std::uint32_t>(text[i + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                }
            }
        }
        if ((c >= 0xD800 && c <= 0xDFFF) || c > 0x10FFFF) c = 0xFFFD;
        if (c < 0x80) {
            utf8.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            utf8.push_back(static_cast<char>(0xC0 | (c >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            utf8.push_back(static_cast<char>(0xE0 | (c >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xF0 | (c >> 18)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return utf8;
}

std::string DefaultsNamed(const DefaultsCandidate& candidate) {
    return candidate.kind == DefaultsCandidateKind::kWinePrefix ? candidate.shown + kThisPrefix : candidate.shown;
}

DefaultsResolution ResolveDefaults(const DefaultsProbe& probe) {
    switch (probe.platform) {
        case DefaultsPlatform::kWindows: {
            DefaultsResolution resolution;
            resolution.platform = probe.platform;
            resolution.package_result = probe.package_result.value_or(kDefaultsNoPackage);
            if (probe.known_folder.empty()) {
                resolution.no_location = kNoKnownFolder;
            } else {
                resolution.candidates.push_back(AppData(DefaultsCandidateKind::kWindows, probe.known_folder,
                                                        resolution.package_result == kDefaultsNoPackage));
            }
            return resolution;
        }
        case DefaultsPlatform::kWine:
            return ResolveWine(probe);
        case DefaultsPlatform::kNative:
            return ResolveNative(probe);
    }
    throw std::invalid_argument("ResolveDefaults: the probe names no platform");
}

DefaultsChoice ChooseDefaults(const DefaultsResolution& resolution, const std::vector<bool>& exists,
                              const std::vector<DefaultsCreationOutcome>& outcomes) {
    const std::vector<DefaultsCandidate>& candidates = resolution.candidates;
    if (exists.size() != candidates.size() || outcomes.size() != candidates.size()) {
        throw std::invalid_argument("ChooseDefaults: the vectors must hold one entry per candidate");
    }

    if (candidates.empty()) {
        std::string line = "Defaults.ini: no location: " + resolution.no_location + ".";
        if (resolution.platform == DefaultsPlatform::kWine) line += HostSentence(resolution, outcomes);
        return Final(line + kDefaultsBuiltIn);
    }

    for (std::size_t read = 0; read < candidates.size(); ++read) {
        if (!exists[read]) continue;
        for (std::size_t ignored = read + 1; ignored < candidates.size(); ++ignored) {
            if (!exists[ignored]) continue;
            DefaultsChoice choice = ReadAt(read, "Defaults.ini: " + DefaultsNamed(candidates[read]) + " is read, and " +
                                                     DefaultsNamed(candidates[ignored]) + " is not.");
            choice.message = "Two Defaults.ini files: this game reads " + DefaultsNamed(candidates[read]) + " and ignores " +
                             DefaultsNamed(candidates[ignored]) + ".";
            return choice;
        }
        return ReadAt(read, Found(resolution, read, "read", outcomes));
    }

    if (resolution.platform == DefaultsPlatform::kNative) {
        std::string shown;
        for (const DefaultsCandidate& candidate : candidates) {
            shown += (shown.empty() ? "" : " or ") + candidate.shown;
        }
        return Final("Defaults.ini: no file at " + shown +
                     "; on this system the mod reads Defaults.ini but does not create it." + kDefaultsBuiltIn);
    }

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (!candidates[i].may_create) continue;
        switch (outcomes[i].kind) {
            case DefaultsCreation::kNotTried: {
                DefaultsChoice choice;
                choice.create = static_cast<int>(i);
                return choice;
            }
            case DefaultsCreation::kCreated:
                return ReadAt(i, Found(resolution, i, "created with the built-in values", outcomes));
            case DefaultsCreation::kAppeared:
                return ReadAt(i, Found(resolution, i, "created by another program at the same time, and read", outcomes));
            default:
                break;
        }
    }

    if (resolution.platform == DefaultsPlatform::kWindows) {
        const DefaultsCandidate& only = candidates[0];
        if (only.may_create) return Final("Defaults.ini: " + Failure(only, outcomes[0], "") + kDefaultsBuiltIn);
        return Final("Defaults.ini: not created, because this game runs as a packaged app (GetCurrentPackageFullName returned " +
                     std::to_string(resolution.package_result) + "); " + only.shown +
                     " is created by the next game that is not packaged, or by Lopari.");
    }

    const std::size_t prefix = candidates.size() - 1;
    const std::string failure = candidates[prefix].kind == DefaultsCandidateKind::kWinePrefix
                                    ? Failure(candidates[prefix], outcomes[prefix], kThisPrefix)
                                    : std::string("no location: ") + kNoKnownFolder + ".";
    return Final("Defaults.ini: " + failure + HostSentence(resolution, outcomes) + kDefaultsBuiltIn);
}

}  // namespace cameraunlock::config::detail

#ifdef _WIN32

#include <windows.h>

namespace cameraunlock::config::detail {
namespace {

constexpr UINT kUnixCodePage = 65010;
constexpr GUID kRoamingAppData = {0x3EB685DB, 0x65F9, 0x4CF6, {0xA0, 0x3A, 0xE3, 0xEF, 0x65, 0x72, 0x9F, 0x3D}};

template <class Function>
Function Export(HMODULE module, const char* name) {
    return module ? reinterpret_cast<Function>(reinterpret_cast<void*>(GetProcAddress(module, name))) : nullptr;
}

std::wstring Variable(const wchar_t* name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0) return std::wstring();
    std::wstring value(size, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, &value[0], size);
    if (length >= size) throw std::runtime_error("GetEnvironmentVariableW: the variable changed while it was read");
    value.resize(length);
    return value;
}

// The modules stay loaded: a reference held for the life of the process costs nothing, and
// unloading shell32 from a game that did not load it is not worth the risk.
std::wstring KnownFolder() {
    using GetKnownFolderPath = HRESULT(WINAPI*)(const GUID&, DWORD, HANDLE, PWSTR*);
    using TaskMemFree = void(WINAPI*)(LPVOID);
    const auto get = Export<GetKnownFolderPath>(LoadLibraryW(L"shell32.dll"), "SHGetKnownFolderPath");
    const auto task_mem_free = Export<TaskMemFree>(LoadLibraryW(L"ole32.dll"), "CoTaskMemFree");
    if (!get || !task_mem_free) return std::wstring();
    PWSTR path = nullptr;
    const HRESULT result = get(kRoamingAppData, 0, nullptr, &path);
    std::wstring folder = SUCCEEDED(result) && path ? std::wstring(path) : std::wstring();
    task_mem_free(path);
    return folder;
}

std::string Ansi(const char* text) { return text ? std::string(text) : std::string(); }

// The Unix text as bytes in Wine's Unix code page, the inverse of how Wine decoded the
// environment; nullopt when the conversion failed.
std::optional<std::string> UnixBytes(const std::wstring& text) {
    const int length =
        WideCharToMultiByte(kUnixCodePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return std::nullopt;
    std::string bytes(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(kUnixCodePage, 0, text.data(), static_cast<int>(text.size()), &bytes[0], length, nullptr,
                            nullptr) != length) {
        return std::nullopt;
    }
    return bytes;
}

// Empty when kernel32 has no wine_get_dos_file_name or it returned NULL.
std::wstring DosFileName(const std::string& unix_bytes) {
    using GetDosFileName = WCHAR*(__cdecl*)(const char*);
    const auto convert = Export<GetDosFileName>(GetModuleHandleW(L"kernel32.dll"), "wine_get_dos_file_name");
    if (!convert) return std::wstring();
    WCHAR* dos = convert(unix_bytes.c_str());
    if (!dos) return std::wstring();
    std::wstring result(dos);
    HeapFree(GetProcessHeap(), 0, dos);
    return result;
}

}  // namespace

DefaultsProbe ProbeDefaults() {
    DefaultsProbe probe;
    probe.known_folder = KnownFolder();
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    using WineGetVersion = const char*(__cdecl*)();
    const auto version = Export<WineGetVersion>(ntdll, "wine_get_version");
    if (!version) {
        probe.platform = DefaultsPlatform::kWindows;
        using GetPackageName = LONG(WINAPI*)(UINT32*, PWSTR);
        const auto package = Export<GetPackageName>(GetModuleHandleW(L"kernel32.dll"), "GetCurrentPackageFullName");
        if (package) {
            UINT32 length = 0;
            probe.package_result = package(&length, nullptr);
        }
        return probe;
    }

    probe.platform = DefaultsPlatform::kWine;
    probe.wine_version = Ansi(version());
    using WineGetHostVersion = void(__cdecl*)(const char**, const char**);
    if (const auto host_version = Export<WineGetHostVersion>(ntdll, "wine_get_host_version")) {
        const char* sysname = nullptr;
        const char* release = nullptr;
        host_version(&sysname, &release);
        probe.host_system = Ansi(sysname);
    }
    probe.wine_home_dir = Variable(L"WINEHOMEDIR");
    probe.wine_host_xdg_config_home = Variable(L"WINE_HOST_XDG_CONFIG_HOME");
    probe.xdg_config_home = Variable(L"XDG_CONFIG_HOME");

    std::string variable;
    const std::wstring unix_folder = HostUnixFolder(probe, variable);
    if (unix_folder.empty()) return probe;
    const std::optional<std::string> bytes = UnixBytes(unix_folder);
    if (!bytes) {
        probe.code_page_failed = true;
        return probe;
    }
    probe.dos_file_name = DosFileName(*bytes);
    return probe;
}

std::uint32_t CreateDefaultsFolder(const std::wstring& folder) {
    if (CreateDirectoryW(folder.c_str(), nullptr)) return 0;
    const DWORD error = GetLastError();
    return error == ERROR_ALREADY_EXISTS ? 0 : error;
}

}  // namespace cameraunlock::config::detail

#endif  // _WIN32
