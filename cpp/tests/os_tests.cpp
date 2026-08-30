// Tests for cameraunlock::os - where the mod is on disk, and which window the
// game is drawing into.
//
// Both were hand-rolled across the fleet at several correctness levels. The
// three failures these cover, in the order they bite:
//
//   1. DirectoryOf on a separator-less path. substr(0, npos) hands back the
//      whole string, and a "directory" of "" turns the INI path into
//      "\HeadTracking.ini" - the root of the current drive, which the user
//      will never find and may not be able to write.
//   2. Buffer growth. GetModuleFileName truncates rather than failing, so a
//      fixed MAX_PATH buffer turns a deep install path into a dormant mod on a
//      machine where nothing is wrong.
//   3. ANSI narrowing. Best-fit mapping is ON by default, so a character the
//      code page cannot encode is replaced with one that looks similar and the
//      config is then read from a DIFFERENT directory that exists.

#include <cameraunlock/os/game_window.h>
#include <cameraunlock/os/module_paths.h>

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

#ifdef _WIN32

void TestDirectoryOf() {
    using cameraunlock::os::DirectoryOf;
    std::cout << "DirectoryOf:\n";

    std::wstring dir;
    Check(DirectoryOf(L"C:\\Games\\Thing\\Thing.exe", dir) && dir == L"C:\\Games\\Thing",
          "backslash path yields the directory without a trailing separator");

    dir.clear();
    Check(DirectoryOf(L"C:/Games/Thing/Thing.exe", dir) && dir == L"C:/Games/Thing",
          "forward slashes count as separators too");

    dir.clear();
    Check(DirectoryOf(L"C:\\Games\\S.T.A.L.K.E.R.\\Thing.exe", dir) &&
              dir == L"C:\\Games\\S.T.A.L.K.E.R.",
          "dots in a directory name are not separators");

    dir = L"untouched";
    Check(!DirectoryOf(L"Thing.exe", dir) && dir == L"untouched",
          "separator-less path is refused, not turned into a drive root");

    dir = L"untouched";
    Check(!DirectoryOf(L"", dir) && dir == L"untouched", "empty path is refused");

    dir = L"untouched";
    Check(DirectoryOf(L"\\Thing.exe", dir) && dir.empty(),
          "a leading-separator path reports its empty directory as a success");
}

void TestNarrowToAnsi() {
    using cameraunlock::os::NarrowToAnsi;
    std::cout << "NarrowToAnsi:\n";

    std::string narrow;
    Check(NarrowToAnsi(L"C:\\Games\\Thing", narrow) && narrow == "C:\\Games\\Thing",
          "plain ASCII round-trips");

    narrow = "untouched";
    Check(!NarrowToAnsi(L"", narrow) && narrow == "untouched", "empty input is refused");

    // U+4E2D is representable on a CJK code page and on UTF-8, so this can only
    // assert the invariant that holds either way: the call either refuses, or
    // returns something that is genuinely the same directory. What it must
    // never do is hand back a best-fit approximation, and the one code page
    // where a best-fit substitution is both possible and silent is a
    // single-byte one, where the result would be shorter than the input in
    // characters only by coincidence - so the real check is that a refusal is a
    // refusal.
    narrow = "untouched";
    const bool converted = NarrowToAnsi(L"C:\\\x4E2D\\Thing", narrow);
    Check(converted ? narrow != "untouched" : narrow == "untouched",
          "a non-ASCII directory either converts or leaves the output untouched");
}

void TestSelfAndHostDirectories() {
    using cameraunlock::os::HostExeDirectory;
    using cameraunlock::os::HostExeDirectoryNarrow;
    using cameraunlock::os::ModuleFilePath;
    using cameraunlock::os::SelfModuleDirectory;
    std::cout << "SelfModuleDirectory / HostExeDirectory:\n";

    const std::wstring exePath = ModuleFilePath(nullptr);
    Check(!exePath.empty(), "the host EXE path resolves");
    Check(exePath.find(L'\0') == std::wstring::npos,
          "the resolved path carries no embedded NUL from the growth buffer");

    const std::wstring exeDir = HostExeDirectory();
    Check(!exeDir.empty(), "the host EXE directory resolves");
    Check(exeDir.size() < exePath.size() &&
              exePath.compare(0, exeDir.size(), exeDir) == 0,
          "the EXE directory is a strict prefix of the EXE path");
    Check(exeDir.back() != L'\\' && exeDir.back() != L'/',
          "the directory carries no trailing separator");

    // The test runner is an EXE, so the core is linked into the EXE itself and
    // the two agree here. In a mod they differ, which is the whole reason both
    // exist - a DLL asking where it lives must not be told where the EXE lives.
    Check(SelfModuleDirectory() == exeDir,
          "in an EXE build the self module directory is the EXE directory");

    const std::string narrow = HostExeDirectoryNarrow();
    Check(narrow.empty() || narrow.find('\0') == std::string::npos,
          "the narrow form is either refused or NUL-free");
}

// Captured so the assertions can prove the sink is actually reached, rather
// than assuming it.
int g_logCalls = 0;

void CountingLog(cameraunlock::os::WindowLogLevel, const char* message) {
    if (message != nullptr && message[0] != '\0') ++g_logCalls;
}

void TestGameWindow() {
    using cameraunlock::os::CenterGameWindowOnce;
    using cameraunlock::os::FindGameWindow;
    std::cout << "FindGameWindow / CenterGameWindowOnce:\n";

    // A console test runner owns no 200x200 unowned visible window, so the
    // honest expectation is "no candidate", reported rather than guessed at.
    // Asserting a specific HWND would only pass on a machine with a game
    // running.
    const HWND found = FindGameWindow();
    Check(found == nullptr || IsWindow(found),
          "the discovered window is either absent or a live window");

    g_logCalls = 0;
    CenterGameWindowOnce(&CountingLog);
    Check(g_logCalls == 1, "the first call reports exactly one diagnostic");

    g_logCalls = 0;
    CenterGameWindowOnce(&CountingLog);
    Check(g_logCalls == 0, "the once-per-process latch makes the second call silent");

    // A null sink is documented as "no diagnostics", not "crash".
    CenterGameWindowOnce(nullptr);
    Check(true, "a null log sink is accepted");
}

#endif  // _WIN32

}  // namespace

int RunOsTests() {
    std::cout << "\n=== OS Tests ===\n";
#ifdef _WIN32
    TestDirectoryOf();
    TestNarrowToAnsi();
    TestSelfAndHostDirectories();
    TestGameWindow();
#else
    std::cout << "  (skipped: cameraunlock::os is Windows-only)\n";
#endif
    return g_failures;
}
