// Tests for the log file's one-generation rotation naming.
//
// Open() truncates (CREATE_ALWAYS), and EmergencyLine writes the crash report
// into that same file, so the outgoing generation is renamed first. Getting the
// rename target wrong silently costs the crash report: the extension has to
// survive so the kept file is still a .log, and a dot in a DIRECTORY name must
// not be mistaken for the file's extension (game install paths really do carry
// dots, e.g. "S.T.A.L.K.E.R." or a versioned folder).

#include <cameraunlock/logging/file_log.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

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

void TestPreviousGenerationPath() {
    using cameraunlock::logging::PreviousGenerationPath;
    std::cout << "PreviousGenerationPath:\n";

    Check(PreviousGenerationPath(L"HeadTracking.log") == L"HeadTracking.prev.log",
          "bare filename keeps the .log extension");

    Check(PreviousGenerationPath(L"C:\\Game\\logs\\HeadTracking.log")
              == L"C:\\Game\\logs\\HeadTracking.prev.log",
          "full path rotates only the filename");

    Check(PreviousGenerationPath(L"C:\\S.T.A.L.K.E.R.\\HeadTracking.log")
              == L"C:\\S.T.A.L.K.E.R.\\HeadTracking.prev.log",
          "dots in a directory name do not confuse the extension split");

    Check(PreviousGenerationPath(L"C:\\Game 1.5\\HeadTracking")
              == L"C:\\Game 1.5\\HeadTracking.prev",
          "extensionless file under a dotted directory appends .prev");

    Check(PreviousGenerationPath(L"HeadTracking") == L"HeadTracking.prev",
          "extensionless filename appends .prev");

    Check(PreviousGenerationPath(L"HeadTracking.prev.log") == L"HeadTracking.prev.prev.log",
          "rotating twice does not collide with the live file");
}


#ifdef _WIN32
std::wstring TempPath(const wchar_t* leaf) {
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + leaf;
}

std::string ReadAll(const std::wstring& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The behaviour the naming tests cannot reach. Two properties matter:
// the outgoing launch has to end up in .prev.log, and a mod that closes and
// reopens the log mid-run (honouring a "log to file" setting) must NOT rotate
// again, or the run in progress becomes the previous generation.
void TestRotationAcrossOpens() {
    using namespace cameraunlock::logging;
    std::cout << "rotation on Open():\n";

    const std::wstring live = TempPath(L"cameraunlock_rotation_test.log");
    const std::wstring prev = PreviousGenerationPath(live);
    DeleteFileW(live.c_str());
    DeleteFileW(prev.c_str());

    // Stand in for a previous launch's log sitting on disk.
    {
        std::ofstream seed(live.c_str(), std::ios::binary);
        seed << "previous launch";
    }

    Open(live);
    Line("current launch");
    Close();

    Check(ReadAll(prev).find("previous launch") != std::string::npos,
          "the outgoing launch is kept as the previous generation");
    Check(ReadAll(live).find("current launch") != std::string::npos,
          "the live file holds the current launch");
    Check(ReadAll(live).find("previous launch") == std::string::npos,
          "the live file does not accumulate the previous launch");

    // Reopening within the same run must leave .prev alone.
    Open(live);
    Line("same run, reopened");
    Close();

    Check(ReadAll(prev).find("previous launch") != std::string::npos,
          "reopening mid-run does not rotate again");
    Check(ReadAll(prev).find("current launch") == std::string::npos,
          "the run in progress is never filed as the previous generation");

    DeleteFileW(live.c_str());
    DeleteFileW(prev.c_str());
}
#endif

}  // namespace

int RunFileLogTests() {
    std::cout << "\n=== File Log Tests ===\n";
    TestPreviousGenerationPath();
#ifdef _WIN32
    TestRotationAcrossOpens();
#endif
    return g_failures;
}
