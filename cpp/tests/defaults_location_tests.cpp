// Where Defaults.ini is, against data/fixtures/canonical-ini/global/resolve.tsv, which the C#
// DefaultsLocationFixtures runs too, plus the real probe on this machine and the folder creation in
// scratch folders.

#include <cameraunlock/config/defaults_location.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>

#include <random>
#endif

namespace {

namespace fs = std::filesystem;
using namespace cameraunlock::config::detail;

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

fs::path Root() { return fs::path(CAMERAUNLOCK_CANONICAL_INI_FIXTURES); }

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("cannot open " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> SplitAt(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find(separator, start);
        if (end == std::string::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

std::string Join(const std::vector<std::string>& fields, const std::string& separator) {
    std::string text;
    for (std::size_t i = 0; i < fields.size(); ++i) text += (i == 0 ? "" : separator) + fields[i];
    return text;
}

// The byte escape data/fixtures/canonical-ini/README.md defines.
std::string Escape(const std::string& bytes) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string text;
    for (const char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        if (b == '\\') {
            text += "\\\\";
        } else if (b >= 0x20 && b <= 0x7E) {
            text.push_back(c);
        } else {
            text += "\\x";
            text.push_back(kHex[b >> 4]);
            text.push_back(kHex[b & 0xF]);
        }
    }
    return text;
}

std::string Unescape(const std::string& field) {
    std::string bytes;
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] != '\\') {
            bytes.push_back(field[i]);
        } else if (i + 1 < field.size() && field[i + 1] == '\\') {
            bytes.push_back('\\');
            ++i;
        } else if (i + 3 < field.size() && field[i + 1] == 'x') {
            bytes.push_back(static_cast<char>(std::stoi(field.substr(i + 2, 2), nullptr, 16)));
            i += 3;
        } else {
            throw std::runtime_error("bad escape in fixture field " + field);
        }
    }
    return bytes;
}

// The fixture's UTF-8 as a wide string: UTF-16 where wchar_t is 16 bits, UTF-32 elsewhere.
std::wstring Wide(const std::string& utf8) {
    std::wstring wide;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        const int length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        std::uint32_t c = length == 1 ? lead : length == 2 ? (lead & 0x1F) : length == 3 ? (lead & 0x0F) : (lead & 0x07);
        for (int k = 1; k < length; ++k) c = (c << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        i += static_cast<std::size_t>(length);
        if (sizeof(wchar_t) == 2 && c >= 0x10000) {
            wide.push_back(static_cast<wchar_t>(0xD800 + ((c - 0x10000) >> 10)));
            wide.push_back(static_cast<wchar_t>(0xDC00 + ((c - 0x10000) & 0x3FF)));
        } else {
            wide.push_back(static_cast<wchar_t>(c));
        }
    }
    return wide;
}

void Apply(DefaultsProbe& probe, const std::vector<std::string>& row) {
    const std::string& name = row[1];
    const std::string value = Unescape(row[2]);
    if (name == "platform") {
        if (value == "windows") {
            probe.platform = DefaultsPlatform::kWindows;
        } else if (value == "wine") {
            probe.platform = DefaultsPlatform::kWine;
        } else if (value == "native") {
            probe.platform = DefaultsPlatform::kNative;
        } else {
            throw std::runtime_error("unknown platform " + value);
        }
    } else if (name == "known_folder") {
        probe.known_folder = Wide(value);
    } else if (name == "package") {
        probe.package_result = std::stol(value);
    } else if (name == "wine_version") {
        probe.wine_version = value;
    } else if (name == "host") {
        probe.host_system = value;
    } else if (name == "WINEHOMEDIR") {
        probe.wine_home_dir = Wide(value);
    } else if (name == "WINE_HOST_XDG_CONFIG_HOME") {
        probe.wine_host_xdg_config_home = Wide(value);
    } else if (name == "XDG_CONFIG_HOME") {
        probe.xdg_config_home = Wide(value);
    } else if (name == "HOME") {
        probe.home = Wide(value);
    } else if (name == "codepage") {
        if (value != "failed") throw std::runtime_error("codepage is only ever failed");
        probe.code_page_failed = true;
    } else if (name == "dos_file_name") {
        probe.dos_file_name = Wide(value);
    } else {
        throw std::runtime_error("unknown input " + name);
    }
}

DefaultsCreation Creation(const std::string& name) {
    if (name == "created") return DefaultsCreation::kCreated;
    if (name == "appeared") return DefaultsCreation::kAppeared;
    if (name == "parent_missing") return DefaultsCreation::kParentMissing;
    if (name == "folder_failed") return DefaultsCreation::kFolderFailed;
    if (name == "file_failed") return DefaultsCreation::kFileFailed;
    throw std::runtime_error("unknown outcome " + name);
}

const char* KindName(DefaultsCandidateKind kind) {
    switch (kind) {
        case DefaultsCandidateKind::kWindows: return "windows";
        case DefaultsCandidateKind::kWineHost: return "wine_host";
        case DefaultsCandidateKind::kWinePrefix: return "wine_prefix";
        case DefaultsCandidateKind::kNative: return "native";
    }
    throw std::runtime_error("unknown candidate kind");
}

std::string Position(int index) { return index < 0 ? "-" : std::to_string(index); }

using Block = std::vector<std::vector<std::string>>;

std::vector<Block> Blocks() {
    std::vector<Block> blocks;
    for (const std::string& line : SplitAt(ReadBytes(Root() / "global" / "resolve.tsv"), '\n')) {
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> fields = SplitAt(line, '\t');
        if (fields[0] == "case") blocks.emplace_back();
        if (blocks.empty()) throw std::runtime_error("resolve.tsv has a row before its first case: " + line);
        blocks.back().push_back(std::move(fields));
    }
    return blocks;
}

std::vector<std::string> ActualRows(const Block& block) {
    std::vector<std::string> actual;
    DefaultsProbe probe;
    std::size_t row = 0;
    for (; row < block.size() && block[row][0] != "choice"; ++row) {
        if (block[row][0] == "case" || block[row][0] == "input") actual.push_back(Join(block[row], "\t"));
        if (block[row][0] == "input") Apply(probe, block[row]);
    }
    const DefaultsResolution resolution = ResolveDefaults(probe);
    if (!resolution.unix_folder.empty()) actual.push_back("unix\t" + Escape(DefaultsUtf8(resolution.unix_folder)));
    if (!resolution.host_unusable.empty()) actual.push_back("host\t" + Escape(resolution.host_unusable));
    for (const DefaultsCandidate& candidate : resolution.candidates) {
        actual.push_back(std::string("candidate\t") + KindName(candidate.kind) + "\t" +
                         (candidate.may_create ? "yes" : "no") + "\t" + Escape(DefaultsUtf8(candidate.path)) + "\t" +
                         Escape(candidate.shown));
    }
    if (resolution.candidates.empty()) actual.push_back("none\t" + Escape(resolution.no_location));

    while (row < block.size()) {
        actual.push_back("choice");
        ++row;
        std::vector<bool> exists(resolution.candidates.size(), false);
        std::vector<DefaultsCreationOutcome> outcomes(resolution.candidates.size());
        for (; row < block.size() && block[row][0] != "choice"; ++row) {
            const std::vector<std::string>& f = block[row];
            if (f[0] == "exists") {
                exists.at(std::stoul(f[1])) = true;
            } else if (f[0] == "outcome") {
                DefaultsCreationOutcome& outcome = outcomes.at(std::stoul(f[1]));
                outcome.kind = Creation(f[2]);
                if (f.size() > 3) outcome.why = Unescape(f[3]);
            } else {
                continue;
            }
            actual.push_back(Join(f, "\t"));
        }
        const DefaultsChoice choice = ChooseDefaults(resolution, exists, outcomes);
        actual.push_back("read\t" + Position(choice.read));
        actual.push_back("create\t" + Position(choice.create));
        if (!choice.line.empty()) actual.push_back("line\t" + Escape(choice.line));
        if (!choice.message.empty()) actual.push_back("message\t" + Escape(choice.message));
    }
    return actual;
}

void TestCases() {
    std::cout << "\n[global/resolve.tsv]\n";
    const std::vector<Block> blocks = Blocks();
    Check(!blocks.empty(), "resolve.tsv holds cases");
    for (const Block& block : blocks) {
        std::vector<std::string> expected;
        for (const std::vector<std::string>& fields : block) expected.push_back(Join(fields, "\t"));
        const std::vector<std::string> actual = ActualRows(block);
        Check(actual == expected, block[0][1] + (actual == expected ? ""
                                                                     : "\n    expected:\n      " + Join(expected, "\n      ") +
                                                                           "\n    actual:\n      " + Join(actual, "\n      ")));
    }
}

void TestChooseArguments() {
    std::cout << "\n[ChooseDefaults refuses vectors of the wrong length]\n";
    DefaultsProbe probe;
    probe.known_folder = L"C:\\Users\\Ann\\AppData\\Roaming";
    const DefaultsResolution resolution = ResolveDefaults(probe);
    bool threw = false;
    try {
        ChooseDefaults(resolution, {}, {DefaultsCreationOutcome{}});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Check(threw, "ChooseDefaults throws when exists holds no entry for the candidate");
}

#ifdef _WIN32

// The profile's AppData entry under User Shell Folders, expanded: the roaming folder by a path that
// does not go through SHGetKnownFolderPath or the APPDATA variable.
std::wstring RegistryRoamingFolder() {
    std::wstring data(32768, L'\0');
    DWORD size = static_cast<DWORD>(data.size() * sizeof(wchar_t));
    const LSTATUS status =
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
                     L"AppData", RRF_RT_REG_SZ, nullptr, &data[0], &size);
    if (status != ERROR_SUCCESS) throw std::runtime_error("RegGetValueW failed with " + std::to_string(status));
    data.resize(size / sizeof(wchar_t) - 1);
    return data;
}

void TestRealProbe() {
    std::cout << "\n[the probe finds this machine's roaming folder and creates nothing]\n";
    const std::wstring roaming = RegistryRoamingFolder();
    const fs::path folder = fs::path(roaming) / L"CameraUnlock";
    const bool before = fs::exists(folder);

    const DefaultsProbe probe = ProbeDefaults();
    const DefaultsResolution resolution = ResolveDefaults(probe);

    Check(probe.platform == DefaultsPlatform::kWindows, "the platform is Windows");
    Check(probe.known_folder == roaming, "the known folder is the one the registry names");
    Check(probe.package_result && *probe.package_result == kDefaultsNoPackage,
          "GetCurrentPackageFullName says the process is not packaged");
    Check(resolution.candidates.size() == 1 && resolution.candidates[0].may_create &&
              resolution.candidates[0].path == (folder / L"Defaults.ini").wstring(),
          "the one candidate is the roaming Defaults.ini, and it may be created");
    Check(fs::exists(folder) == before, "the roaming CameraUnlock folder is as it was");
}

void TestCreateFolder() {
    std::cout << "\n[the folder is created one level only]\n";
    std::random_device random;
    const fs::path dir = fs::temp_directory_path() / (L"cu-defaults-location-" + std::to_wstring(random()));
    fs::create_directory(dir);

    const fs::path folder = dir / L"CameraUnlock";
    Check(CreateDefaultsFolder(folder.wstring()) == 0 && fs::is_directory(folder), "the folder is created");
    Check(CreateDefaultsFolder(folder.wstring()) == 0, "a folder that is there counts as done");
    const fs::path missing = dir / L"missing";
    Check(CreateDefaultsFolder((missing / L"CameraUnlock").wstring()) == kDefaultsPathNotFound && !fs::exists(missing),
          "a missing parent is ERROR_PATH_NOT_FOUND, and is not created");
    Check(CreateDefaultsFolder(dir.wstring() + L"\\bad?name") == ERROR_INVALID_NAME, "any other error is returned");
    const fs::path file = dir / L"file";
    std::ofstream(file).close();
    Check(CreateDefaultsFolder(file.wstring()) == 0 && fs::is_regular_file(file),
          "a file of that name gives ERROR_ALREADY_EXISTS, counted as done, and stays a file");
    std::size_t entries = 0;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir)) {
        (void)entry;
        ++entries;
    }
    Check(entries == 2, "nothing besides CameraUnlock and the file was created");

    fs::remove_all(dir);
}

#endif

}  // namespace

int RunDefaultsLocationTests() {
    std::cout << "\n=== Defaults.ini location ===\n";
    g_failures = 0;
    try {
        TestCases();
        TestChooseArguments();
#ifdef _WIN32
        TestRealProbe();
        TestCreateFolder();
#endif
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] threw: " << e.what() << "\n";
        ++g_failures;
    }
    return g_failures;
}
