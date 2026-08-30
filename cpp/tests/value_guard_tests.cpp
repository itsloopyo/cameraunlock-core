// Tests for the INI value guards.
//
// Each case here is a config typo that used to reach the camera transform
// unchallenged:
//
//   - "nan" / "inf" / 1e400. strtod accepts all three, and every comparison
//     against NaN is false, so the range clamps downstream are skipped and the
//     view matrix goes NaN with nothing in the log.
//   - "0,15". strtod parses a PREFIX, so a European decimal comma yields 0.0 -
//     inside the valid range, so it passed every check silently.
//   - "0.15 ; settle". GetPrivateProfileStringA does not strip inline comments,
//     so the comment arrives attached to the value.
//   - A negative position limit, which inverts PositionProcessor's clamp bounds
//     and pins the lean at a fixed offset instead of freeing it.

#include <cameraunlock/config/value_guards.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
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

// A stand-in for the mod's own logging macro, so the assertions can prove a
// correction is announced rather than applied silently. A setting the mod
// quietly ignores is what sends a user looking for a mod fault.
int g_logCalls = 0;
std::string g_lastMessage;

void CapturingLog(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    ++g_logCalls;
    g_lastMessage = buffer;
}

void ResetLog() {
    g_logCalls = 0;
    g_lastMessage.clear();
}

bool NearEqual(float a, float b) { return std::fabs(a - b) < 1e-6f; }

void TestSanitizeSmoothing() {
    using cameraunlock::config::SanitizeSmoothing;
    std::cout << "SanitizeSmoothing:\n";

    ResetLog();
    Check(NearEqual(SanitizeSmoothing("LocalSmoothing", 0.0f, 0.0f, &CapturingLog), 0.0f),
          "a configured 0.0 stays 0.0 - this is validation, never a floor");
    Check(g_logCalls == 0, "a valid value logs nothing");

    ResetLog();
    Check(NearEqual(SanitizeSmoothing("RemoteSmoothing", 0.15f, 0.15f, &CapturingLog), 0.15f),
          "a valid value passes through untouched");

    ResetLog();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(NearEqual(SanitizeSmoothing("RemoteSmoothing", nan, 0.15f, &CapturingLog), 0.15f),
          "NaN falls back to the key's OWN default, not a shared one");
    Check(g_logCalls == 1, "the fallback is announced");

    ResetLog();
    const float inf = std::numeric_limits<float>::infinity();
    Check(NearEqual(SanitizeSmoothing("LocalSmoothing", inf, 0.0f, &CapturingLog), 0.0f),
          "+inf falls back rather than clamping to 1.0");

    ResetLog();
    Check(NearEqual(SanitizeSmoothing("LocalSmoothing", 5.0f, 0.0f, &CapturingLog), 1.0f),
          "a finite value above the range clamps to 1.0");
    Check(g_logCalls == 1, "the clamp is announced");

    ResetLog();
    Check(NearEqual(SanitizeSmoothing("LocalSmoothing", -1.0f, 0.0f, &CapturingLog), 0.0f),
          "a finite value below the range clamps to 0.0");

    ResetLog();
    Check(NearEqual(SanitizeSmoothing("LocalSmoothing", 5.0f, 0.0f, nullptr), 1.0f),
          "a null sink still corrects the value");
    Check(g_logCalls == 0, "a null sink emits nothing");
}

void TestSanitizeSensitivity() {
    using cameraunlock::config::kMaxSensitivity;
    using cameraunlock::config::SanitizeSensitivity;
    std::cout << "SanitizeSensitivity:\n";

    ResetLog();
    Check(NearEqual(SanitizeSensitivity("YawSensitivity", -1.5f, 1.0f, &CapturingLog), -1.5f),
          "a negative sensitivity is a legitimate inversion and survives");

    ResetLog();
    Check(NearEqual(SanitizeSensitivity("YawSensitivity", 0.0f, 1.0f, &CapturingLog), 0.0f),
          "zero sensitivity - axis off - survives");

    ResetLog();
    Check(NearEqual(SanitizeSensitivity("YawSensitivity",
                                        std::numeric_limits<float>::quiet_NaN(), 1.0f,
                                        &CapturingLog),
                    1.0f),
          "NaN falls back to the default");

    ResetLog();
    Check(NearEqual(SanitizeSensitivity("YawSensitivity", 1e9f, 1.0f, &CapturingLog),
                    kMaxSensitivity),
          "a magnitude that would overflow the angle product clamps");
    Check(NearEqual(SanitizeSensitivity("YawSensitivity", -1e9f, 1.0f, nullptr),
                    -kMaxSensitivity),
          "the clamp is symmetric about zero");
}

void TestSanitizePositionLimit() {
    using cameraunlock::config::kMaxPositionLimit;
    using cameraunlock::config::SanitizePositionLimit;
    std::cout << "SanitizePositionLimit:\n";

    ResetLog();
    Check(NearEqual(SanitizePositionLimit("LimitZ", 0.40f, 0.40f, &CapturingLog), 0.40f),
          "a shipped default survives");

    ResetLog();
    Check(NearEqual(SanitizePositionLimit("LimitZ", -0.40f, 0.40f, &CapturingLog), 0.0f),
          "a negative limit clamps to 0 rather than inverting the clamp bounds");
    Check(g_logCalls == 1, "the correction is announced");

    ResetLog();
    Check(NearEqual(SanitizePositionLimit("LimitZ", 10000.0f, 0.40f, &CapturingLog),
                    kMaxPositionLimit),
          "a mistyped 10000 for 0.10 clamps instead of translating out of the world");
}

void TestIsBindableVirtualKey() {
    using cameraunlock::config::IsBindableVirtualKey;
    std::cout << "IsBindableVirtualKey:\n";

    Check(IsBindableVirtualKey(0x21) && IsBindableVirtualKey(0x22),
          "the PageUp / PageDown fleet defaults are bindable");
    Check(IsBindableVirtualKey(0x79), "F10 is bindable");

    Check(!IsBindableVirtualKey(0x00), "0 is not a key");
    Check(!IsBindableVirtualKey(0x230), "a typo past 0xFE is refused, not silently dead");
    Check(!IsBindableVirtualKey(-1), "a negative code is refused");

    Check(!IsBindableVirtualKey(0x10) && !IsBindableVirtualKey(0x11) &&
              !IsBindableVirtualKey(0x12),
          "Shift / Control / Alt are refused - the chord guard tests them");
    Check(!IsBindableVirtualKey(0xA0) && !IsBindableVirtualKey(0xA5),
          "the left/right modifier halves are refused too");
}

void TestParseFloatStrict() {
    using cameraunlock::config::ParseFloatStrict;
    std::cout << "ParseFloatStrict:\n";

    float value = -1.0f;
    Check(ParseFloatStrict("0.15", value) && NearEqual(value, 0.15f), "a plain number parses");

    value = -1.0f;
    Check(!ParseFloatStrict("  ", value), "whitespace-only is rejected");

    value = -1.0f;
    Check(!ParseFloatStrict("0,15", value),
          "a European decimal comma is REJECTED, not silently read as 0.0");

    value = -1.0f;
    Check(!ParseFloatStrict("one point five", value), "prose is rejected");

    value = -1.0f;
    Check(!ParseFloatStrict("1.5 scale", value), "a trailing token is rejected");

    value = -1.0f;
    Check(!ParseFloatStrict("", value), "empty text is rejected");

    // The two strtod behaviours TryParseConfigFloat, the other half of this
    // library's config parsing, does not have. A number the two halves read
    // differently is precisely the drift the shared schema exists to stop.
    value = -1.0f;
    Check(!ParseFloatStrict("0x10", value), "hexadecimal is rejected, not read as 16");
    value = -1.0f;
    Check(!ParseFloatStrict("0x1p3", value), "a hex float is rejected too");
    value = -1.0f;
    Check(!ParseFloatStrict(" 1.5", value), "leading whitespace is rejected");

    // Left for SanitizeFinite: these parse WHOLE, so the parser has no grounds
    // to refuse them and the range guard is where they are caught.
    value = 0.0f;
    Check(ParseFloatStrict("nan", value) && std::isnan(value),
          "\"nan\" parses whole and is handed on non-finite");
    value = 0.0f;
    Check(ParseFloatStrict("1e400", value) && std::isinf(value),
          "an overflowing literal parses whole and is handed on as +inf");
}

#ifdef _WIN32

// GetPrivateProfileStringA resolves a relative path against the Windows
// directory, so the fixture has to be absolute. Built in ANSI throughout,
// because IniReader is an ANSI-only layer and the fixture has to be a path it
// can actually open.
std::string TempIniPath() {
    char dir[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, dir);
    return std::string(dir) + "cameraunlock_value_guard_tests.ini";
}

void WriteIni(const std::string& path, const char* body) {
    HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, body, static_cast<DWORD>(std::strlen(body)), &written, nullptr);
    CloseHandle(file);
}

void TestReadRawValue() {
    using cameraunlock::config::ReadRawValue;
    std::cout << "ReadRawValue:\n";

    const std::string path = TempIniPath();
    WriteIni(path,
             "[Tracking]\r\n"
             "Plain=0.15\r\n"
             "Semicolon=0.15 ; settle time\r\n"
             "Hash=0.15 # settle time\r\n"
             "Padded=   0.15   \r\n"
             "CommentOnly= ; nothing set\r\n"
             "Empty=\r\n");

    cameraunlock::IniReader ini;
    Check(ini.Open(path), "fixture INI opens");

    Check(ReadRawValue(ini, "Tracking", "Plain") == "0.15", "a bare value comes back as-is");
    Check(ReadRawValue(ini, "Tracking", "Semicolon") == "0.15",
          "a trailing ; comment is stripped");
    Check(ReadRawValue(ini, "Tracking", "Hash") == "0.15", "a trailing # comment is stripped");
    Check(ReadRawValue(ini, "Tracking", "Padded") == "0.15", "surrounding whitespace is trimmed");
    Check(ReadRawValue(ini, "Tracking", "CommentOnly").empty(),
          "a key holding only a comment reads as empty, not as the comment text");
    Check(ReadRawValue(ini, "Tracking", "Empty").empty(), "an empty key reads as empty");
    Check(ReadRawValue(ini, "Tracking", "Absent").empty(), "an absent key reads as empty");

    DeleteFileA(path.c_str());
}

void TestReadFloatChecked() {
    using cameraunlock::config::ReadFloatChecked;
    std::cout << "ReadFloatChecked:\n";

    const std::string path = TempIniPath();
    WriteIni(path,
             "[Tracking]\r\n"
             "Good=0.25\r\n"
             "Commented=0.25 ; settle\r\n"
             "Comma=0,15\r\n"
             "Prose=one point five\r\n"
             "TooBig=5.0\r\n"
             "NotFinite=nan\r\n");

    cameraunlock::IniReader ini;
    Check(ini.Open(path), "fixture INI opens");

    ResetLog();
    Check(NearEqual(ReadFloatChecked(ini, "Tracking", "Good", 0.15f, 0.0f, 1.0f, &CapturingLog),
                    0.25f),
          "a valid value is read");
    Check(g_logCalls == 0, "a valid value logs nothing");

    ResetLog();
    Check(NearEqual(
              ReadFloatChecked(ini, "Tracking", "Commented", 0.15f, 0.0f, 1.0f, &CapturingLog),
              0.25f),
          "an inline comment does not defeat the strict parse");
    Check(g_logCalls == 0, "a commented but valid value logs nothing");

    ResetLog();
    Check(NearEqual(ReadFloatChecked(ini, "Tracking", "Comma", 0.15f, 0.0f, 1.0f, &CapturingLog),
                    0.15f),
          "\"0,15\" falls back to the default instead of silently reading 0.0");
    Check(g_logCalls == 1 && g_lastMessage.find("decimal point") != std::string::npos,
          "the decimal-comma diagnostic tells the user what to type");

    ResetLog();
    Check(NearEqual(ReadFloatChecked(ini, "Tracking", "Prose", 0.15f, 0.0f, 1.0f, &CapturingLog),
                    0.15f),
          "unparseable prose falls back");

    ResetLog();
    Check(NearEqual(ReadFloatChecked(ini, "Tracking", "TooBig", 0.15f, 0.0f, 1.0f, &CapturingLog),
                    1.0f),
          "an out-of-range value clamps");
    Check(g_logCalls == 1 && g_lastMessage.find("[Tracking]") != std::string::npos,
          "the clamp diagnostic names the section, not just the key");

    ResetLog();
    Check(NearEqual(
              ReadFloatChecked(ini, "Tracking", "NotFinite", 0.15f, 0.0f, 1.0f, &CapturingLog),
              0.15f),
          "\"nan\" falls back to the default");

    ResetLog();
    Check(NearEqual(ReadFloatChecked(ini, "Tracking", "Absent", 0.15f, 0.0f, 1.0f, &CapturingLog),
                    0.15f),
          "an absent key yields the default");
    Check(g_logCalls == 0, "an absent key is not a user error and logs nothing");

    DeleteFileA(path.c_str());
}

void TestWarnRetiredSmoothingKey() {
    using cameraunlock::config::WarnRetiredSmoothingKey;
    std::cout << "WarnRetiredSmoothingKey:\n";

    const std::string path = TempIniPath();

    // Absent first: the latch must not be spent on a config that never carried
    // the retired key.
    WriteIni(path, "[Tracking]\r\nLocalSmoothing=0.0\r\n");
    {
        cameraunlock::IniReader ini;
        ini.Open(path);
        ResetLog();
        WarnRetiredSmoothingKey(ini, "Tracking", "Smoothing", &CapturingLog);
        Check(g_logCalls == 0, "an absent retired key is silent");
    }

    WriteIni(path, "[Tracking]\r\nSmoothing=0.5\r\n");
    {
        cameraunlock::IniReader ini;
        ini.Open(path);
        ResetLog();
        WarnRetiredSmoothingKey(ini, "Tracking", "Smoothing", &CapturingLog);
        Check(g_logCalls == 1, "a present retired key warns");
        Check(g_lastMessage.find("IGNORED") != std::string::npos &&
                  g_lastMessage.find("not migrated") != std::string::npos,
              "the warning says the value is ignored AND not migrated");

        // Config is reloadable, and repeating this on every reload buries it.
        ResetLog();
        WarnRetiredSmoothingKey(ini, "Tracking", "Smoothing", &CapturingLog);
        Check(g_logCalls == 0, "the warning is once per process, not once per load");
    }

    DeleteFileA(path.c_str());
}

#endif  // _WIN32

}  // namespace

int RunValueGuardTests() {
    std::cout << "\n=== Config Value Guard Tests ===\n";
    TestSanitizeSmoothing();
    TestSanitizeSensitivity();
    TestSanitizePositionLimit();
    TestIsBindableVirtualKey();
    TestParseFloatStrict();
#ifdef _WIN32
    TestReadRawValue();
    TestReadFloatChecked();
    TestWarnRetiredSmoothingKey();
#endif
    return g_failures;
}
