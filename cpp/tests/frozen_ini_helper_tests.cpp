// Per-helper fixture tests for the INI helpers the legacy imports call: IniReader's typed
// reads over GetPrivateProfileStringA/IntA, and the value guards. Each game's import
// migrates a user's pre-canonical file through these helpers exactly as its published
// build read it, so the helpers are frozen, and every table below pins what one of them
// returns for the same set of awkward values.

#include <cameraunlock/config/ini_reader.h>
#include <cameraunlock/config/value_guards.h>

#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <limits>
#include <string>

#ifdef _WIN32
#include <windows.h>

#include <clocale>
#include <stdexcept>
#endif

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

int g_logCalls = 0;
std::string g_lastMessage;

void CapturingLog(const char* fmt, ...) {
    char buffer[2048];
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

bool SameFloat(double actual, double expected) {
    if (std::isnan(expected)) return std::isnan(actual);
    return actual == expected && std::signbit(actual) == std::signbit(expected);
}

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

void TestSanitizeFunctions() {
    using cameraunlock::config::kMaxPositionLimit;
    using cameraunlock::config::kMaxSensitivity;
    using cameraunlock::config::SanitizePositionLimit;
    using cameraunlock::config::SanitizeSensitivity;
    using cameraunlock::config::SanitizeSmoothing;
    std::cout << "Sanitize functions:\n";

    Check(kMaxSensitivity == 100.0f && kMaxPositionLimit == 10.0f, "the bounds are 100 and 10");

    const struct {
        const char* name;
        float (*fn)(const char*, float, float, cameraunlock::config::LogSink);
        float lo;
        float hi;
    } guards[] = {
        {"SanitizeSmoothing", &SanitizeSmoothing, 0.0f, 1.0f},
        {"SanitizeSensitivity", &SanitizeSensitivity, -100.0f, 100.0f},
        {"SanitizePositionLimit", &SanitizePositionLimit, 0.0f, 10.0f},
    };
    for (const auto& g : guards) {
        const std::string n = g.name;

        ResetLog();
        Check(g.fn("K", g.lo, 0.5f, &CapturingLog) == g.lo && g.fn("K", g.hi, 0.5f, &CapturingLog) == g.hi &&
                  g_logCalls == 0,
              n + ": both bounds are inside the range and log nothing");

        ResetLog();
        Check(g.fn("K", kNan, 0.5f, &CapturingLog) == 0.5f && g_logCalls == 1 &&
                  g_lastMessage == "config: K is not a finite number; using 0.5",
              n + ": NaN gives the fallback and says so");
        ResetLog();
        Check(g.fn("K", -kInf, 0.5f, &CapturingLog) == 0.5f && g_logCalls == 1,
              n + ": -inf gives the fallback, not the lower bound");

        ResetLog();
        char hi[32];
        std::snprintf(hi, sizeof(hi), "%g", static_cast<double>(g.hi));
        Check(g.fn("K", kNan, 500.0f, &CapturingLog) == g.hi &&
                  g_lastMessage == std::string("config: K is not a finite number; using ") + hi,
              n + ": a fallback outside the range is clamped too");

        ResetLog();
        const float over = g.hi + 1.0f;
        Check(g.fn("K", over, 0.5f, &CapturingLog) == g.hi && g_logCalls == 1, n + ": above the range clamps");
        ResetLog();
        Check(g.fn("K", g.lo - 1.0f, 0.5f, &CapturingLog) == g.lo && g_logCalls == 1,
              n + ": below the range clamps");

        ResetLog();
        Check(g.fn("K", kNan, 0.5f, nullptr) == 0.5f && g_logCalls == 0, n + ": a null sink corrects silently");
    }

    ResetLog();
    SanitizeSmoothing("LocalSmoothing", 5.0f, 0.0f, &CapturingLog);
    Check(g_lastMessage == "config: LocalSmoothing=5 is outside [0, 1]; clamped to 1",
          "the clamp message names the key, the value, the range and the result");
}

void TestIsBindableVirtualKey() {
    using cameraunlock::config::IsBindableVirtualKey;
    std::cout << "IsBindableVirtualKey:\n";

    int wrong = 0;
    for (int vk = -2; vk <= 0x200; ++vk) {
        const bool modifier = (vk >= 0x10 && vk <= 0x12) || (vk >= 0xA0 && vk <= 0xA5);
        const bool expected = vk >= 0x01 && vk <= 0xFE && !modifier;
        if (IsBindableVirtualKey(vk) != expected) ++wrong;
    }
    Check(wrong == 0, "exactly 0x01-0xFE without 0x10-0x12 and 0xA0-0xA5, over -2..0x200");
    Check(!IsBindableVirtualKey(INT_MIN) && !IsBindableVirtualKey(INT_MAX), "INT_MIN and INT_MAX are refused");
}

void TestParseFloatStrict() {
    using cameraunlock::config::ParseFloatStrict;
    std::cout << "ParseFloatStrict:\n";

    const struct {
        const char* text;
        bool ok;
        float value;
    } rows[] = {
        {"0.15", true, 0.15f},
        {"-1", true, -1.0f},
        {"+1.5", true, 1.5f},
        {"010", true, 10.0f},
        {"1e5", true, 1e5f},
        {"1e-50", true, 0.0f},
        {"nan", true, kNan},
        {"inf", true, kInf},
        {"-inf", true, -kInf},
        {"1e400", true, kInf},
        {"", false, 0.0f},
        {" 1.5", false, 0.0f},
        {"1.5 ", false, 0.0f},
        {"0,15", false, 0.0f},
        {"abc", false, 0.0f},
        {"1abc", false, 0.0f},
        {"0x230", false, 0.0f},
        {"0X10", false, 0.0f},
        {"\"12\"", false, 0.0f},
        {"12 ; c", false, 0.0f},
    };
    for (const auto& row : rows) {
        float out = -7.0f;
        const bool ok = ParseFloatStrict(row.text, out);
        const bool pass = ok == row.ok && (ok ? SameFloat(out, row.value) : out == -7.0f);
        Check(pass, std::string("'") + row.text + (row.ok ? "' parses" : "' is refused and leaves out alone"));
    }
}

#ifdef _WIN32

const std::string& IniPath() {
    static const std::string path = [] {
        char dir[MAX_PATH] = {};
        const DWORD length = GetTempPathA(MAX_PATH, dir);
        if (length == 0 || length >= MAX_PATH) {
            throw std::runtime_error("GetTempPathA failed: " + std::to_string(GetLastError()));
        }
        return std::string(dir) + "cameraunlock_frozen_ini_helper_tests.ini";
    }();
    return path;
}

void WriteIni(const std::string& bytes) {
    const HANDLE file = CreateFileA(IniPath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("cannot create " + IniPath() + ": " + std::to_string(GetLastError()));
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    const DWORD error = GetLastError();
    CloseHandle(file);
    if (!ok || written != bytes.size()) {
        throw std::runtime_error("cannot write " + IniPath() + ": " + std::to_string(error));
    }
}

// 1500 characters whose first 1023 read differently from the whole: the whole of kLong
// reads as 1e5 and is not a number to a whole-token parse, its first 1023 characters are
// exactly 1.0; the whole of kLongHex reads as hex 7, its first 1023 characters as 0.
const std::string kLong = "1." + std::string(1021, '0') + "e5" + std::string(475, 'x');
const std::string kLongHex = "0x" + std::string(1021, '0') + "7" + std::string(476, 'x');

void WriteFixture() {
    WriteIni(
        "[Values]\r\n"
        "Empty=\r\n"
        "Nan=nan\r\n"
        "Inf=inf\r\n"
        "MinusInf=-inf\r\n"
        "Huge=1e400\r\n"
        "MinusOne=-1\r\n"
        "Abc=abc\r\n"
        "OneAbc=1abc\r\n"
        "Hex=0x230\r\n"
        "Octal=010\r\n"
        "Comma=0,15\r\n"
        "Quoted=\"12\"\r\n"
        "SingleQuoted='12'\r\n"
        "QuotedComment=\"12\" ; c\r\n"
        "Comment=12 ; c\r\n"
        "HashComment=12 # c\r\n"
        "Padded=   12   \r\n"
        "Max=4294967295\r\n"
        "FullHex=0xFFFFFFFF\r\n"
        "Path=D:\\Games\\Half#Life\r\n"
        "Long=" + kLong + "\r\n"
        "LongHex=" + kLongHex + "\r\n");
}

cameraunlock::IniReader OpenFixture() {
    cameraunlock::IniReader ini;
    if (!ini.Open(IniPath())) throw std::runtime_error("IniReader cannot open " + IniPath());
    return ini;
}

void TestReadString() {
    std::cout << "IniReader::ReadString:\n";
    WriteFixture();
    const cameraunlock::IniReader ini = OpenFixture();

    const struct {
        const char* key;
        std::string expected;
    } rows[] = {
        {"Absent", "D"},
        {"Empty", ""},
        {"Nan", "nan"},
        {"Inf", "inf"},
        {"MinusInf", "-inf"},
        {"Huge", "1e400"},
        {"MinusOne", "-1"},
        {"Abc", "abc"},
        {"OneAbc", "1abc"},
        {"Hex", "0x230"},
        {"Octal", "010"},
        {"Comma", "0,15"},
        {"Quoted", "12"},
        {"SingleQuoted", "12"},
        {"QuotedComment", "\"12\" ; c"},
        {"Comment", "12 ; c"},
        {"HashComment", "12 # c"},
        {"Padded", "12"},
        {"Max", "4294967295"},
        {"FullHex", "0xFFFFFFFF"},
        {"Path", "D:\\Games\\Half#Life"},
        {"Long", kLong.substr(0, 1023)},
        {"LongHex", kLongHex.substr(0, 1023)},
    };
    for (const auto& row : rows) {
        Check(ini.ReadString("Values", row.key, "D") == row.expected, std::string("ReadString ") + row.key);
    }
    Check(ini.ReadString("Missing", "Nan", "D") == "D", "ReadString of an absent section gives the default");
}

void TestReadInt() {
    std::cout << "IniReader::ReadInt and ReadUInt:\n";
    WriteFixture();
    const cameraunlock::IniReader ini = OpenFixture();

    const struct {
        const char* key;
        int asInt;
        unsigned int asUInt;
    } rows[] = {
        {"Absent", 99, 99u},
        {"Empty", 99, 99u},
        {"Nan", 0, 0u},
        {"Inf", 0, 0u},
        {"MinusInf", 0, 0u},
        {"Huge", 1, 1u},
        {"MinusOne", -1, 99u},
        {"Abc", 0, 0u},
        {"OneAbc", 1, 1u},
        {"Hex", 0x230, 0x230u},
        {"Octal", 10, 10u},
        {"Comma", 0, 0u},
        {"Quoted", 12, 12u},
        {"SingleQuoted", 12, 12u},
        {"QuotedComment", 0, 0u},
        {"Comment", 12, 12u},
        {"HashComment", 12, 12u},
        {"Padded", 12, 12u},
        {"Max", -1, 99u},
        {"FullHex", -1, 99u},
        {"Long", 1, 1u},
        {"LongHex", 0, 0u},
    };
    for (const auto& row : rows) {
        Check(ini.ReadInt("Values", row.key, 99) == row.asInt, std::string("ReadInt ") + row.key);
        Check(ini.ReadUInt("Values", row.key, 99u) == row.asUInt, std::string("ReadUInt ") + row.key);
    }
}

void TestReadFloating() {
    std::cout << "IniReader::ReadDouble and ReadFloat:\n";
    WriteFixture();
    const cameraunlock::IniReader ini = OpenFixture();

    const struct {
        const char* key;
        double expected;
    } rows[] = {
        {"Absent", 7.0},
        {"Empty", 7.0},
        {"Nan", std::numeric_limits<double>::quiet_NaN()},
        {"Inf", std::numeric_limits<double>::infinity()},
        {"MinusInf", -std::numeric_limits<double>::infinity()},
        {"Huge", std::numeric_limits<double>::infinity()},
        {"MinusOne", -1.0},
        {"Abc", 7.0},
        {"OneAbc", 1.0},
        {"Hex", 560.0},
        {"Octal", 10.0},
        {"Comma", 0.0},
        {"Quoted", 12.0},
        {"SingleQuoted", 12.0},
        {"QuotedComment", 7.0},
        {"Comment", 12.0},
        {"HashComment", 12.0},
        {"Padded", 12.0},
        {"Max", 4294967295.0},
        {"FullHex", 4294967295.0},
        {"Long", 1.0},
        {"LongHex", 0.0},
    };
    for (const auto& row : rows) {
        Check(SameFloat(ini.ReadDouble("Values", row.key, 7.0), row.expected),
              std::string("ReadDouble ") + row.key);
        Check(SameFloat(ini.ReadFloat("Values", row.key, 7.0f), static_cast<float>(row.expected)),
              std::string("ReadFloat ") + row.key);
    }
}

void TestReadHex() {
    std::cout << "IniReader::ReadHex:\n";
    WriteFixture();
    const cameraunlock::IniReader ini = OpenFixture();

    const struct {
        const char* key;
        int expected;
    } rows[] = {
        {"Absent", 99},
        {"Empty", 99},
        {"Nan", 99},
        {"Inf", 99},
        {"MinusInf", 99},
        {"Huge", 0x1E400},
        {"MinusOne", -1},
        {"Abc", 0xABC},
        {"OneAbc", 0x1ABC},
        {"Hex", 0x230},
        {"Octal", 0x10},
        {"Comma", 0},
        {"Quoted", 0x12},
        {"SingleQuoted", 0x12},
        {"QuotedComment", 99},
        {"Comment", 0x12},
        {"HashComment", 0x12},
        {"Padded", 0x12},
        {"Max", INT_MAX},
        {"FullHex", INT_MAX},
        {"Long", 1},
        {"LongHex", 0},
    };
    for (const auto& row : rows) {
        Check(ini.ReadHex("Values", row.key, 99) == row.expected, std::string("ReadHex ") + row.key);
    }
}

void TestReadBool() {
    std::cout << "IniReader::ReadBool:\n";

    const char* const trueTokens[] = {"1", "true", "True", "TRUE", "yes", "Yes", "YES", "on", "On", "ON",
                                      "\"true\"", "  yes  "};
    const char* const falseTokens[] = {"0", "false", "False", "FALSE", "no", "No", "NO", "off", "Off", "OFF",
                                       "'off'"};
    const char* const otherTokens[] = {"", "tRue", "true ; c", "\"true\" ; c", "2", "-1", "y", "enabled", "nan"};

    std::string body = "[Bools]\r\n";
    int n = 0;
    for (const char* t : trueTokens) body += "T" + std::to_string(n++) + "=" + t + "\r\n";
    n = 0;
    for (const char* t : falseTokens) body += "F" + std::to_string(n++) + "=" + t + "\r\n";
    n = 0;
    for (const char* t : otherTokens) body += "O" + std::to_string(n++) + "=" + t + "\r\n";
    WriteIni(body);
    const cameraunlock::IniReader ini = OpenFixture();

    n = 0;
    for (const char* t : trueTokens) {
        const std::string key = "T" + std::to_string(n++);
        Check(ini.ReadBool("Bools", key.c_str(), false), std::string("ReadBool '") + t + "' is true");
    }
    n = 0;
    for (const char* t : falseTokens) {
        const std::string key = "F" + std::to_string(n++);
        Check(!ini.ReadBool("Bools", key.c_str(), true), std::string("ReadBool '") + t + "' is false");
    }
    n = 0;
    for (const char* t : otherTokens) {
        const std::string key = "O" + std::to_string(n++);
        Check(ini.ReadBool("Bools", key.c_str(), true) && !ini.ReadBool("Bools", key.c_str(), false),
              std::string("ReadBool '") + t + "' gives the default");
    }
    Check(ini.ReadBool("Bools", "Absent", true) && !ini.ReadBool("Bools", "Absent", false),
          "ReadBool of an absent key gives the default");

    WriteFixture();
    const cameraunlock::IniReader values = OpenFixture();
    const char* const valueKeys[] = {"Empty", "Nan", "Inf", "MinusInf", "Huge", "MinusOne", "Abc", "OneAbc",
                                     "Hex", "Octal", "Comma", "Quoted", "SingleQuoted", "QuotedComment",
                                     "Comment", "HashComment", "Padded", "Max", "FullHex", "Path", "Long",
                                     "LongHex"};
    for (const char* key : valueKeys) {
        Check(values.ReadBool("Values", key, true) && !values.ReadBool("Values", key, false),
              std::string("ReadBool ") + key + " gives the default");
    }
}

void TestUnreadableFile() {
    std::cout << "IniReader without a readable file:\n";

    cameraunlock::IniReader closed;
    Check(closed.ReadString("Values", "Nan", "D") == "D" && closed.ReadInt("Values", "Nan", 99) == 99 &&
              closed.ReadDouble("Values", "Nan", 7.0) == 7.0 && closed.ReadHex("Values", "Nan", 99) == 99 &&
              closed.ReadBool("Values", "Nan", true),
          "a reader that never opened gives every default");

    WriteFixture();
    cameraunlock::IniReader ini = OpenFixture();
    if (!DeleteFileA(IniPath().c_str())) {
        throw std::runtime_error("cannot delete " + IniPath() + ": " + std::to_string(GetLastError()));
    }
    Check(!cameraunlock::IniReader().Open(IniPath()), "Open refuses a missing file");
    Check(ini.ReadString("Values", "Nan", "D") == "D" && ini.ReadString("Values", "Nan", "").empty(),
          "ReadString gives the default once the file is gone");
    Check(ini.ReadInt("Values", "Nan", 99) == 99 && ini.ReadUInt("Values", "Nan", 99u) == 99u,
          "ReadInt and ReadUInt give the default once the file is gone");
    Check(ini.ReadDouble("Values", "Nan", 7.0) == 7.0 && ini.ReadFloat("Values", "Nan", 7.0f) == 7.0f,
          "ReadDouble and ReadFloat give the default once the file is gone");
    Check(ini.ReadHex("Values", "Nan", 99) == 99 && ini.ReadBool("Values", "Nan", true),
          "ReadHex and ReadBool give the default once the file is gone");
}

void TestLocale() {
    std::cout << "Locale:\n";
    WriteIni("[Values]\r\nHalf=1.5\r\nComma=1,5\r\n");
    const cameraunlock::IniReader ini = OpenFixture();

    const std::string previous = std::setlocale(LC_NUMERIC, nullptr);
    if (std::setlocale(LC_NUMERIC, "de-DE") == nullptr) throw std::runtime_error("no de-DE locale");
    const double half = ini.ReadDouble("Values", "Half", 7.0);
    const double comma = ini.ReadDouble("Values", "Comma", 7.0);
    float strict = -7.0f;
    const bool strictOk = cameraunlock::config::ParseFloatStrict("1.5", strict);
    std::setlocale(LC_NUMERIC, previous.c_str());

    Check(half == 1.5, "ReadDouble reads 1.5 under a decimal-comma locale");
    Check(comma == 1.0, "ReadDouble reads 1,5 as 1 under a decimal-comma locale");
    Check(!strictOk && strict == -7.0f,
          "ParseFloatStrict follows the process locale: 1.5 is refused under a decimal-comma locale");
}

void TestReadRawValue() {
    using cameraunlock::config::ReadRawValue;
    std::cout << "ReadRawValue:\n";
    WriteFixture();
    const cameraunlock::IniReader ini = OpenFixture();

    const struct {
        const char* key;
        std::string expected;
    } rows[] = {
        {"Absent", ""},
        {"Empty", ""},
        {"Nan", "nan"},
        {"MinusOne", "-1"},
        {"Comma", "0,15"},
        {"Quoted", "12"},
        {"SingleQuoted", "12"},
        {"QuotedComment", "\"12\""},
        {"Comment", "12"},
        {"HashComment", "12"},
        {"Padded", "12"},
        {"Path", "D:\\Games\\Half"},
        {"Long", kLong.substr(0, 1023)},
    };
    for (const auto& row : rows) {
        Check(ReadRawValue(ini, "Values", row.key) == row.expected, std::string("ReadRawValue ") + row.key);
    }
}

void TestReadFloatChecked() {
    using cameraunlock::config::ReadFloatChecked;
    std::cout << "ReadFloatChecked:\n";
    WriteFixture();
    const cameraunlock::IniReader ini = OpenFixture();

    const struct {
        const char* key;
        float expected;
        const char* message;
    } rows[] = {
        {"Absent", 7.0f, nullptr},
        {"Empty", 7.0f, nullptr},
        {"Nan", 7.0f, "config: [Values] Nan is not a finite number; using 7"},
        {"Inf", 7.0f, "config: [Values] Inf is not a finite number; using 7"},
        {"MinusInf", 7.0f, "config: [Values] MinusInf is not a finite number; using 7"},
        {"Huge", 7.0f, "config: [Values] Huge is not a finite number; using 7"},
        {"MinusOne", -1.0f, nullptr},
        {"Abc", 7.0f,
         "config: [Values] Abc=abc is not a number, so the default 7 is used instead. Use a dot for the "
         "decimal point."},
        {"OneAbc", 7.0f,
         "config: [Values] OneAbc=1abc is not a number, so the default 7 is used instead. Use a dot for the "
         "decimal point."},
        {"Hex", 7.0f,
         "config: [Values] Hex=0x230 is not a number, so the default 7 is used instead. Use a dot for the "
         "decimal point."},
        {"Octal", 10.0f, nullptr},
        {"Comma", 7.0f,
         "config: [Values] Comma=0,15 is not a number, so the default 7 is used instead. Use a dot for the "
         "decimal point."},
        {"Quoted", 12.0f, nullptr},
        {"SingleQuoted", 12.0f, nullptr},
        {"QuotedComment", 7.0f,
         "config: [Values] QuotedComment=\"12\" is not a number, so the default 7 is used instead. Use a dot "
         "for the decimal point."},
        {"Comment", 12.0f, nullptr},
        {"HashComment", 12.0f, nullptr},
        {"Padded", 12.0f, nullptr},
        {"Max", 1000.0f, "config: [Values] Max=4.29497e+09 is outside [-1000, 1000]; clamped to 1000"},
        {"Long", 1.0f, nullptr},
    };
    for (const auto& row : rows) {
        ResetLog();
        const float value = ReadFloatChecked(ini, "Values", row.key, 7.0f, -1000.0f, 1000.0f, &CapturingLog);
        const bool logged = row.message == nullptr ? g_logCalls == 0 : g_logCalls == 1 && g_lastMessage == row.message;
        Check(SameFloat(value, row.expected) && logged, std::string("ReadFloatChecked ") + row.key);
        if (!logged) std::cout << "         logged: " << g_lastMessage << "\n";
    }

    ResetLog();
    Check(ReadFloatChecked(ini, "Values", "Abc", 7.0f, -1000.0f, 1000.0f, nullptr) == 7.0f && g_logCalls == 0,
          "ReadFloatChecked with a null sink falls back silently");
    Check(ReadFloatChecked(ini, "Values", "Huge", 50.0f, 0.0f, 1.0f, nullptr) == 1.0f,
          "ReadFloatChecked clamps a fallback outside the range");
}

#endif  // _WIN32

}  // namespace

int RunFrozenIniHelperTests() {
    std::cout << "\n=== Frozen INI Helper Tests ===\n";
    TestSanitizeFunctions();
    TestIsBindableVirtualKey();
    TestParseFloatStrict();
#ifdef _WIN32
    TestReadString();
    TestReadInt();
    TestReadFloating();
    TestReadHex();
    TestReadBool();
    TestLocale();
    TestReadRawValue();
    TestReadFloatChecked();
    TestUnreadableFile();
#endif
    return g_failures;
}
