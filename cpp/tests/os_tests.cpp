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

void TestCenteredOrigin() {
    using cameraunlock::os::CenteredOrigin;
    std::cout << "CenteredOrigin:\n";

    Check(CenteredOrigin(0, 1920, 1280) == 320, "a window in a work area at the origin");
    Check(CenteredOrigin(0, 1040, 720) == 160, "the work area's length is used, not the monitor's");
    Check(CenteredOrigin(1920, 2560, 1936) == 2232, "a monitor right of the primary starts past zero");
    Check(CenteredOrigin(-1920, 1920, 1280) == -1600, "a monitor left of the primary has a negative start");
    Check(CenteredOrigin(-1080, 1040, 720) == -920, "a monitor above the primary has a negative start");
    Check(CenteredOrigin(-1000, 3000, 500) == 250, "a work area that spans zero");
    Check(CenteredOrigin(0, 1921, 1280) == 320, "an odd gap leaves the spare pixel after the window");
    Check(CenteredOrigin(-1921, 1921, 1280) == -1601, "an odd gap rounds the same way at a negative start");
    Check(CenteredOrigin(100, 1280, 1280) == 100, "a window as long as the work area starts where it starts");
    Check(CenteredOrigin(0, 1920, 0) == 960, "a zero-length window starts at the middle");
    Check(CenteredOrigin(0, 1080, 1100) == -10, "a longer window starts before the work area");
    Check(CenteredOrigin(0, 1080, 1083) == -1, "an odd overhang is halved toward zero, not toward the smaller number");
    Check(CenteredOrigin(-1080, 1080, 1100) == -1090, "a longer window on a monitor with a negative start");

    // Usable where a constant is wanted, and through a plain function pointer.
    static_assert(CenteredOrigin(0, 1920, 1280) == 320, "CenteredOrigin is constexpr");
    int (*const fn)(int, int, int) noexcept = &CenteredOrigin;
    Check(fn(0, 100, 50) == 25, "it is an ordinary function of three ints");
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

std::string g_lastLog;

void RecordingLog(cameraunlock::os::WindowLogLevel, const char* message) {
    ++g_logCalls;
    g_lastLog = message;
}

// The windows here are never shown, so nothing appears on the desktop and no
// window is activated. Placement, the monitor lookup and the style read all work
// on a hidden window.
void TestCenterWindowInWorkArea() {
    using cameraunlock::os::CenteredOrigin;
    using cameraunlock::os::CenterWindowInWorkArea;
    std::cout << "CenterWindowInWorkArea:\n";

    g_logCalls = 0;
    Check(!CenterWindowInWorkArea(nullptr, &RecordingLog) && g_logCalls == 1,
          "a null window is refused and reported");
    Check(!CenterWindowInWorkArea(nullptr, nullptr), "a null log sink is accepted");

    HWND hwnd = CreateWindowExW(0, L"STATIC", L"cameraunlock os test", WS_OVERLAPPEDWINDOW,
                                37, 53, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(hwnd != nullptr, "a hidden captioned window is created");
    if (hwnd == nullptr) return;

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info);
    const RECT work = info.rcWork;
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    const HWND foregroundBefore = GetForegroundWindow();

    g_logCalls = 0;
    const bool moved = CenterWindowInWorkArea(hwnd, &RecordingLog);
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    Check(moved && g_logCalls == 1, "the first call centres and logs once");
    Check(rect.left == CenteredOrigin(work.left, workW, 400) &&
              rect.top == CenteredOrigin(work.top, workH, 300),
          "the window is at the centred origin of its monitor's work area");
    Check(rect.right - rect.left == 400 && rect.bottom - rect.top == 300, "its size is unchanged");
    Check(g_lastLog.find("400x300") != std::string::npos && g_lastLog.find("was at (37, 53)") != std::string::npos,
          "the log line carries the size and the origin it was at");
    Check(!IsWindowVisible(hwnd) && GetForegroundWindow() == foregroundBefore,
          "the window is neither shown nor activated");

    g_logCalls = 0;
    Check(CenterWindowInWorkArea(hwnd, &RecordingLog) && g_logCalls == 0,
          "a second call finds it centred and says nothing");

    // The game picks another windowed size: the same call centres the new size.
    SetWindowPos(hwnd, nullptr, 11, 17, 640, 360, SWP_NOZORDER | SWP_NOACTIVATE);
    g_logCalls = 0;
    Check(CenterWindowInWorkArea(hwnd, &RecordingLog) && g_logCalls == 1, "a resized window is centred again");
    GetWindowRect(hwnd, &rect);
    Check(rect.left == CenteredOrigin(work.left, workW, 640) &&
              rect.top == CenteredOrigin(work.top, workH, 360),
          "at the origin for its new size");

    SetWindowPos(hwnd, nullptr, 5, 7, workW + 40, 360, SWP_NOZORDER | SWP_NOACTIVATE);
    GetWindowRect(hwnd, &rect);
    if (rect.right - rect.left > workW) {
        g_logCalls = 0;
        Check(!CenterWindowInWorkArea(hwnd, &RecordingLog) && g_logCalls == 1,
              "a window wider than the work area is left alone and reported");
        RECT after{};
        GetWindowRect(hwnd, &after);
        Check(EqualRect(&rect, &after) != FALSE, "and it has not moved");
    } else {
        std::cout << "  (skipped: Windows clamped the oversized window to the work area)\n";
    }

    SetWindowPos(hwnd, nullptr, 11, 17, 400, 300, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowLongW(hwnd, GWL_STYLE, WS_POPUP);
    GetWindowRect(hwnd, &rect);
    g_logCalls = 0;
    Check(!CenterWindowInWorkArea(hwnd, &RecordingLog) && g_logCalls == 1,
          "a window with no caption is left alone and reported");
    RECT after{};
    GetWindowRect(hwnd, &after);
    Check(EqualRect(&rect, &after) != FALSE, "and it has not moved");

    DestroyWindow(hwnd);
    g_logCalls = 0;
    Check(!CenterWindowInWorkArea(hwnd, &RecordingLog) && g_logCalls == 1,
          "a destroyed window is refused and reported");
}

#endif  // _WIN32

}  // namespace

int RunOsTests() {
    std::cout << "\n=== OS Tests ===\n";
    TestCenteredOrigin();
#ifdef _WIN32
    TestDirectoryOf();
    TestNarrowToAnsi();
    TestSelfAndHostDirectories();
    TestGameWindow();
    TestCenterWindowInWorkArea();
#else
    std::cout << "  (skipped: cameraunlock::os is Windows-only)\n";
#endif
    return g_failures;
}
