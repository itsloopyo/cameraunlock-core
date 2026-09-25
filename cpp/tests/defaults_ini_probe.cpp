// `cameraunlock_tests --probe-defaults-ini <game folder> [--probe-save]`. Not part of the suite: it
// finds and creates the player's own Defaults.ini through DefaultsFile::PerUser(), so it only ever
// runs where that is a scratch home.

#include <cstring>
#include <iostream>

#ifdef _WIN32

#include "canonical_config_example.h"

#include <cameraunlock/config/config_owner.h>

#include <windows.h>

#include <bcrypt.h>
#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace cameraunlock::config;
using canonical_config_example::ModConfig;
using canonical_config_example::ModConfigTable;

// A value never holds a tab or a line break in the output, so each line splits cleanly.
std::string Field(std::string text) {
    std::string out;
    for (char c : text) {
        if (c == '\t') out += "\\t";
        else if (c == '\r') out += "\\r";
        else if (c == '\n') out += "\\n";
        else out.push_back(c);
    }
    return out;
}

void Line(const std::string& a, const std::string& b) { std::cout << Field(a) << '\t' << Field(b) << std::endl; }

void Line(const std::string& a, const std::string& b, const std::string& c) {
    std::cout << Field(a) << '\t' << Field(b) << '\t' << Field(c) << std::endl;
}

std::string Utf8(const std::wstring& text) { return detail::DefaultsUtf8(text); }

const char* PlatformName(detail::DefaultsPlatform platform) {
    switch (platform) {
        case detail::DefaultsPlatform::kWindows: return "Windows";
        case detail::DefaultsPlatform::kWine: return "Wine";
        case detail::DefaultsPlatform::kNative: return "Native";
    }
    throw std::invalid_argument("a DefaultsPlatform with no name");
}

const char* KindName(detail::DefaultsCandidateKind kind) {
    switch (kind) {
        case detail::DefaultsCandidateKind::kWindows: return "Windows";
        case detail::DefaultsCandidateKind::kWineHost: return "WineHost";
        case detail::DefaultsCandidateKind::kWinePrefix: return "WinePrefix";
        case detail::DefaultsCandidateKind::kNative: return "Native";
    }
    throw std::invalid_argument("a DefaultsCandidateKind with no name");
}

void Check(NTSTATUS status, const char* call) {
    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error(std::string(call) + " failed with NTSTATUS " + std::to_string(static_cast<long>(status)));
    }
}

std::string Sha256(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("cannot open " + Utf8(path.wstring()));
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    Check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0), "BCryptOpenAlgorithmProvider");
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32];
    NTSTATUS status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (BCRYPT_SUCCESS(status)) {
        status = BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                                static_cast<ULONG>(bytes.size()), 0);
        if (BCRYPT_SUCCESS(status)) status = BCryptFinishHash(hash, digest, sizeof(digest), 0);
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    Check(status, "SHA-256");
    static const char kHex[] = "0123456789abcdef";
    std::string hex;
    for (unsigned char b : digest) {
        hex.push_back(kHex[b >> 4]);
        hex.push_back(kHex[b & 0xF]);
    }
    return hex;
}

void Files(const fs::path& folder) {
    if (!fs::is_directory(folder)) return;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& file : files) Line("file", Utf8(file.wstring()), Sha256(file));
}

std::wstring Wide(const char* ansi) {
    const int length = MultiByteToWideChar(CP_ACP, 0, ansi, -1, nullptr, 0);
    if (length <= 0) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "MultiByteToWideChar");
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_ACP, 0, ansi, -1, &wide[0], length);
    wide.resize(static_cast<std::size_t>(length - 1));
    return wide;
}

void Probe(const std::wstring& folder, bool save) {
    const detail::DefaultsProbe probe = detail::ProbeDefaults();
    Line("input", "platform", PlatformName(probe.platform));
    Line("input", "known_folder", Utf8(probe.known_folder));
    Line("input", "package_result", probe.package_result ? std::to_string(*probe.package_result) : "");
    Line("input", "wine_version", probe.wine_version);
    Line("input", "host_system", probe.host_system);
    Line("input", "WINEHOMEDIR", Utf8(probe.wine_home_dir));
    Line("input", "WINE_HOST_XDG_CONFIG_HOME", Utf8(probe.wine_host_xdg_config_home));
    Line("input", "XDG_CONFIG_HOME", Utf8(probe.xdg_config_home));
    Line("input", "HOME", Utf8(probe.home));
    Line("input", "code_page_failed", probe.code_page_failed ? "true" : "false");
    Line("input", "dos_file_name", Utf8(probe.dos_file_name));

    const detail::DefaultsResolution resolution = detail::ResolveDefaults(probe);
    Line("resolution", "platform", PlatformName(resolution.platform));
    Line("resolution", "no_location", resolution.no_location);
    Line("resolution", "host_unusable", resolution.host_unusable);
    Line("resolution", "unix_folder", Utf8(resolution.unix_folder));
    Line("resolution", "wine", resolution.wine);
    Line("resolution", "package_result", std::to_string(resolution.package_result));
    std::vector<bool> exists;
    for (std::size_t i = 0; i < resolution.candidates.size(); ++i) {
        const detail::DefaultsCandidate& candidate = resolution.candidates[i];
        const std::string index = std::to_string(i);
        Line("candidate", index, KindName(candidate.kind));
        Line("candidate-may-create", index, candidate.may_create ? "true" : "false");
        Line("candidate-path", index, Utf8(candidate.path));
        Line("candidate-shown", index, candidate.shown);
        Line("candidate-shown-folder", index, candidate.shown_folder);
        Line("candidate-shown-parent", index, candidate.shown_parent);
        exists.push_back(detail::OwnerPathExists(candidate.path));
        Line("candidate-exists", index, exists.back() ? "true" : "false");
    }
    const detail::DefaultsChoice choice = detail::ChooseDefaults(
        resolution, exists, std::vector<detail::DefaultsCreationOutcome>(resolution.candidates.size()));
    Line("choice", "read", std::to_string(choice.read));
    Line("choice", "create", std::to_string(choice.create));
    Line("choice", "line", choice.line);
    Line("choice", "message", choice.message);

    ConfigOwnerOptions<ModConfig> options;
    options.path = (fs::path(folder) / L"CameraUnlock.ini").wstring();
    options.table = ModConfigTable();
    options.header.display_name = "Example Game";
    options.defaults = DefaultsFile::PerUser();
    options.status_sink = [](const std::string& message) { Line("sink", message); };
    ConfigOwner<ModConfig> owner(std::move(options));
    const ConfigLoadResult<ModConfig> load = owner.Load();
    Line("load", "status", ConfigLoadStatusName(load.status));
    Line("load", "reason", load.reason);
    for (const std::string& line : load.log) Line("log", line);

    if (save) {
        const bool yaw = load.config.world_space_yaw;
        const ConfigSaveResult saved = owner.Save([yaw](ModConfig& c) { c.world_space_yaw = !yaw; });
        Line("save", "status", ConfigSaveStatusName(saved.status));
        Line("save", "reason", saved.reason);
        for (const std::string& line : saved.log) Line("save-log", line);
    }

    Files(fs::path(folder));
    for (const detail::DefaultsCandidate& candidate : resolution.candidates) Files(fs::path(candidate.folder));
    Line("end", "ok");
}

int RunProbe(const char* folder, bool save) {
    // Text mode would write each line break as CRLF.
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        Line("error", "standard output cannot be switched to binary mode");
        return 1;
    }
    try {
        Probe(Wide(folder), save);
        return 0;
    } catch (const std::exception& e) {
        Line("error", e.what());
        return 1;
    }
}

}  // namespace

#else

namespace {

int RunProbe(const char*, bool) {
    std::cout << "error\tthe Defaults.ini probe runs on Windows or under Wine\n";
    return 1;
}

}  // namespace

#endif  // _WIN32

int RunDefaultsIniProbe(int argc, char** argv) {
    if (argc < 3 || argc > 4 || (argc == 4 && std::strcmp(argv[3], "--probe-save") != 0)) {
        std::cerr << "usage: cameraunlock_tests --probe-defaults-ini <game folder> [--probe-save]\n";
        return 2;
    }
    return RunProbe(argv[2], argc == 4);
}
