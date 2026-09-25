#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Where Defaults.ini is: the probes, a pure resolver over what they found, the choice between the
// candidates by the files that exist, and the creation of the CameraUnlock folder. Internal to
// core, for the config owner. CameraUnlock.Core.Config.DefaultsLocation is the C# twin, and
// data/fixtures/canonical-ini/global/resolve.tsv holds both to the same candidates and lines. Paths
// are wide, as the Win32 calls take them; the shown forms and the lines are UTF-8.

namespace cameraunlock::config::detail {

// What GetCurrentPackageFullName returns for a process that is not packaged.
inline constexpr long kDefaultsNoPackage = 15700;
inline constexpr std::uint32_t kDefaultsPathNotFound = 3;

enum class DefaultsPlatform {
    // Windows, including a Wine that hides its exports.
    kWindows,
    // A Windows program under Wine or Proton: ntdll exports wine_get_version.
    kWine,
    // Linux or macOS with a native runtime. Only a C# mod runs there; the resolver takes it so both
    // languages run every case.
    kNative,
};

enum class DefaultsCandidateKind { kWindows, kWineHost, kWinePrefix, kNative };

// What the probes found, and all ResolveDefaults reads. Every text is empty when the probe found
// nothing, so an unset variable and an empty one are the same.
struct DefaultsProbe {
    DefaultsPlatform platform = DefaultsPlatform::kWindows;
    // The roaming AppData known folder, on Windows and under Wine.
    std::wstring known_folder;
    // On Windows, what GetCurrentPackageFullName returned for a zero length, or nullopt when
    // kernel32 has no such function.
    std::optional<long> package_result;
    // Under Wine, what wine_get_version returned, and the system name wine_get_host_version gave.
    std::string wine_version;
    std::string host_system;
    // Under Wine, WINEHOMEDIR (the Unix home as an NT path) and WINE_HOST_XDG_CONFIG_HOME.
    std::wstring wine_home_dir;
    std::wstring wine_host_xdg_config_home;
    // Under Wine and natively.
    std::wstring xdg_config_home;
    // Natively.
    std::wstring home;
    // Under Wine, true when the host folder's Unix text could not be turned into bytes in Wine's
    // Unix code page, and what wine_get_dos_file_name returned for those bytes.
    bool code_page_failed = false;
    std::wstring dos_file_name;
};

// One place Defaults.ini may be: the file, its CameraUnlock folder and that folder's parent, each
// also in the form the log shows, with the profile or home folder replaced.
struct DefaultsCandidate {
    DefaultsCandidateKind kind = DefaultsCandidateKind::kWindows;
    // Whether the folder and the file may be created here when no candidate's file exists.
    bool may_create = false;
    std::wstring path;
    std::wstring folder;
    std::wstring parent;
    std::string shown;
    std::string shown_folder;
    std::string shown_parent;
};

struct DefaultsResolution {
    DefaultsPlatform platform = DefaultsPlatform::kWindows;
    std::vector<DefaultsCandidate> candidates;
    // Why there is no candidate; empty when there is one.
    std::string no_location;
    // Under Wine, why there is no host candidate; empty otherwise.
    std::string host_unusable;
    // Under Wine, the host folder's Unix text the probe converts; empty when none is converted.
    std::wstring unix_folder;
    // Under Wine, `Wine <version> on <system>`, without the system when Wine gave none.
    std::string wine;
    // On Windows, what GetCurrentPackageFullName returned, and kDefaultsNoPackage when kernel32 has
    // no such function, as under Wine and natively, where it is not asked.
    long package_result = kDefaultsNoPackage;
};

enum class DefaultsCreation {
    kNotTried,
    // Created with the built-in values.
    kCreated,
    // Another program created the file between the check and the commit, and its file is read.
    kAppeared,
    // The folder's parent does not exist, so the folder was not created.
    kParentMissing,
    kFolderFailed,
    // The folder is there and the file could not be created in it.
    kFileFailed,
};

struct DefaultsCreationOutcome {
    DefaultsCreation kind = DefaultsCreation::kNotTried;
    // Why the folder or the file could not be created, in the owner's words for an I/O error.
    std::string why;
};

struct DefaultsChoice {
    // The index of the candidate whose file is read, or -1.
    int read = -1;
    // The index of the candidate to create next, or -1. The caller creates it and chooses again.
    int create = -1;
    // The one log line about where Defaults.ini is, for a file that then reads; empty while
    // `create` names a candidate.
    std::string line;
    // The in-game message when two files exist and one is not read; empty otherwise.
    std::string message;
};

// The candidates for Defaults.ini in the order they are tried, or none with the reason. Pure. On
// Windows, the roaming AppData known folder, which a packaged process never creates in. Under
// Wine, the host's config folder where Wine maps it to a drive letter, then the prefix's roaming
// AppData folder. Natively, $XDG_CONFIG_HOME or $HOME/.config, then
// $HOME/Library/Application Support, none of them created.
DefaultsResolution ResolveDefaults(const DefaultsProbe& probe);

// What to do with the candidates. Pure. The first candidate whose file exists is read, and a later
// one that also exists draws a message. With none, each candidate that may be created is tried in
// order: `create` names the next, the caller creates it and calls again with its outcome. Once one
// is created, or nothing is left to try, the choice gives the one log line. Throws
// std::invalid_argument when `exists` or `outcomes` does not hold one entry per candidate.
DefaultsChoice ChooseDefaults(const DefaultsResolution& resolution, const std::vector<bool>& exists,
                              const std::vector<DefaultsCreationOutcome>& outcomes);

// A wide text as UTF-8, an unpaired surrogate written as U+FFFD, as C# encodes a string.
std::string DefaultsUtf8(const std::wstring& text);

#ifdef _WIN32

// Probes what ResolveDefaults reads. The known folder comes from SHGetKnownFolderPath, and every
// function a module may lack (the known-folder pair, GetCurrentPackageFullName and the wine
// exports) is found through LoadLibraryW or GetModuleHandleW and GetProcAddress, so no consumer
// gains a static import of shell32 or ole32 and none fails to load where one is missing.
DefaultsProbe ProbeDefaults();

// Creates `folder`, the CameraUnlock folder, with CreateDirectoryW, so only that one level: its
// parent must already exist. Returns 0 when the folder is there afterwards, whoever made it;
// kDefaultsPathNotFound when its parent does not exist; any other Win32 error it failed with.
std::uint32_t CreateDefaultsFolder(const std::wstring& folder);

#endif

}  // namespace cameraunlock::config::detail
