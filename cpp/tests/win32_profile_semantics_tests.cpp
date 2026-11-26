// What GetPrivateProfileStringA and GetPrivateProfileIntA return for the file shapes a
// pre-canonical config can hold. The legacy imports read those files through IniReader and
// depend on the OS reading them as it did when each game's build was published, so a
// failure here means the OS has moved under them.

#include <iostream>
#include <string>

#ifdef _WIN32

#include <windows.h>

#include <stdexcept>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

constexpr char kAbsent[] = "<absent>";

std::string ScratchPath(const char* name) {
    char dir[MAX_PATH] = {};
    const DWORD length = GetTempPathA(MAX_PATH, dir);
    if (length == 0 || length >= MAX_PATH) {
        throw std::runtime_error("GetTempPathA failed: " + std::to_string(GetLastError()));
    }
    return std::string(dir) + name;
}

const std::string& IniPath() {
    static const std::string path = ScratchPath("cameraunlock_win32_profile_semantics.ini");
    return path;
}

void WriteBytes(const std::string& path, const std::string& bytes) {
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("cannot create " + path + ": " + std::to_string(GetLastError()));
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    const DWORD error = GetLastError();
    CloseHandle(file);
    if (!ok || written != bytes.size()) {
        throw std::runtime_error("cannot write " + path + ": " + std::to_string(error));
    }
}

void WriteIni(const std::string& bytes) { WriteBytes(IniPath(), bytes); }

// The returned length, not the terminator, bounds the value, so a NUL the OS kept inside
// a value is visible.
std::string Read(const char* section, const char* key, DWORD size = 256) {
    std::vector<char> buffer(size);
    const DWORD length =
        GetPrivateProfileStringA(section, key, kAbsent, buffer.data(), size, IniPath().c_str());
    return std::string(buffer.data(), length);
}

int ReadInt(const char* section, const char* key, int fallback = 77) {
    return static_cast<int>(GetPrivateProfileIntA(section, key, fallback, IniPath().c_str()));
}

std::vector<std::string> KeysOf(const char* section) {
    std::vector<char> buffer(4096);
    const DWORD length = GetPrivateProfileStringA(section, nullptr, "", buffer.data(),
                                                  static_cast<DWORD>(buffer.size()),
                                                  IniPath().c_str());
    std::vector<std::string> keys;
    std::string current;
    for (DWORD i = 0; i < length; ++i) {
        if (buffer[i] == '\0') {
            keys.push_back(current);
            current.clear();
        } else {
            current += buffer[i];
        }
    }
    if (!current.empty()) keys.push_back(current);
    return keys;
}

std::string Utf16LeWithMark(const std::string& ascii) {
    std::string bytes = "\xFF\xFE";
    for (const char c : ascii) {
        bytes += c;
        bytes += '\0';
    }
    return bytes;
}

void TestRepeats() {
    std::cout << "Repeated keys and sections:\n";

    WriteIni("[General]\r\nK=first\r\nK=second\r\n");
    Check(Read("General", "K") == "first", "the same key twice in one section: the first wins");

    WriteIni("[General]\r\nA=1\r\n[Other]\r\nX=1\r\n[General]\r\nA=2\r\nB=3\r\n");
    Check(Read("General", "A") == "1", "the same section twice: the first block's value is read");
    Check(Read("General", "B") == kAbsent,
          "a key only in the second block of a repeated section returns the default");
}

void TestNames() {
    std::cout << "Section and key names:\n";

    WriteIni("[GeNeRaL]\r\nMyKey=v\r\n");
    Check(Read("general", "MYKEY") == "v", "section and key names are case-insensitive");

    WriteIni("  [ General ]  \r\n   Key   =   a  b   \r\n\tTabbed\t=\ta\tb\t\r\n");
    Check(Read("General", "Key") == "a  b",
          "spaces around a header's name, a key and a value are trimmed, inner spaces kept");
    Check(Read("General", "Tabbed") == "a\tb", "tabs are trimmed the same way, an inner tab kept");

    WriteIni("[General] ; c\r\nK=v\r\n");
    Check(Read("General", "K") == "v", "a header followed by a comment is recognised");

    WriteIni("[Open\r\nK=v\r\n");
    Check(Read("Open", "K") == "v", "a header with no ']' opens the section it names");

    WriteIni("Early=1\r\n[General]\r\nK=2\r\n");
    // A section name that trims to nothing must sit in writable memory: given the literal ""
    // or " ", both calls raise an access violation on Windows 11 26200 once the file holds
    // a header.
    char emptySection[] = "";
    Check(Read(emptySection, "Early") == kAbsent, "a key before any header is unreachable with section \"\"");
    Check(Read("General", "Early") == kAbsent, "and unreachable through the first section");

    WriteIni("[General]\r\n  ;Spaced=1\r\n;Bare=1\r\nK=v\r\n");
    Check(Read("General", ";Spaced") == kAbsent && Read("General", ";Bare") == kAbsent,
          "a ';' line is a comment, after leading whitespace too");
    Check(KeysOf("General") == std::vector<std::string>{"K"}, "neither comment line is a key");

    WriteIni("[General]\r\n#Key=1\r\n");
    Check(Read("General", "#Key") == "1", "'#Key=1' is a key named #Key");

    WriteIni("[General]\r\nK=a=b\r\nInner Key=v\r\nBare\r\n");
    Check(Read("General", "K") == "a=b", "a value keeps every '=' after the first");
    Check(Read("General", "Inner Key") == "v", "a key with an inner space is matched");
    Check(Read("General", "Bare") == kAbsent, "a line with no '=' is not a key");
    Check(KeysOf("General") == std::vector<std::string>{"K", "Inner Key"},
          "the enumerated keys leave the line with no '=' out");
}

void TestValues() {
    std::cout << "Values:\n";

    WriteIni("[General]\r\nB=true ; c\r\nH=1 # c\r\n");
    Check(Read("General", "B") == "true ; c", "an inline ';' comment stays in the value");
    Check(Read("General", "H") == "1 # c", "an inline '#' comment stays in the value");

    WriteIni("[General]\r\nQ=\"1.5\"\r\nS='x'\r\nQC=\"1.5\" ; c\r\nOpen=\"a\r\n");
    Check(Read("General", "Q") == "1.5", "a wholly double-quoted value loses its quotes");
    Check(Read("General", "S") == "x", "a wholly single-quoted value loses its quotes");
    Check(Read("General", "QC") == "\"1.5\" ; c", "a quoted value with text after it is returned as written");
    Check(Read("General", "Open") == "\"a", "an unclosed quote is returned as written");

    WriteIni("[General]\r\nKey=\r\n");
    Check(Read("General", "Key").empty(), "a present empty key reads as \"\", not the default");
    Check(ReadInt("General", "Key") == 77, "GetPrivateProfileIntA returns the default for it");
}

void TestLineEndings() {
    std::cout << "Line endings:\n";

    WriteIni("[General]\nA=1\nB=2\n");
    Check(Read("General", "B") == "2", "an LF-only file is read");

    WriteIni("[General]\r\nA=1\r\nB=2");
    Check(Read("General", "B") == "2", "a last line with no newline is read");

    WriteIni("[General]\r\nK=a\rL=2\r\n");
    Check(Read("General", "K") == "a", "a lone CR ends a line: K=a");
    Check(Read("General", "L") == "2", "and the text after it is the next line: L=2");
}

void TestEncoding() {
    std::cout << "Byte order marks and encodings:\n";

    WriteIni("\xEF\xBB\xBF[General]\r\nK=v\r\n");
    Check(Read("General", "K") == kAbsent,
          "a UTF-8 byte order mark before the first header hides that section");

    WriteIni("\xEF\xBB\xBF; c\r\n[General]\r\nK=v\r\n");
    Check(Read("General", "K") == "v", "a UTF-8 byte order mark before a comment line is harmless");

    WriteIni(Utf16LeWithMark("[General]\r\nK=v\r\n"));
    Check(Read("General", "K") == "v", "UTF-16 LE with a byte order mark is read");
}

void TestControlBytes() {
    std::cout << "Control bytes:\n";

    const struct {
        char byte;
        const char* name;
    } skipped[] = {{'\0', "NUL"}, {'\x1A', "SUB"}, {'\x0B', "VT"}, {'\x0C', "FF"}};
    for (const auto& c : skipped) {
        const std::string b(1, c.byte);
        WriteIni("[General]\r\n" + b + "K=5\r\n");
        Check(Read("General", "K") == "5", std::string(c.name) + " before a key is skipped");
        WriteIni("[General]\r\nK" + b + "=5\r\n");
        Check(Read("General", "K") == "5", std::string(c.name) + " after a key is skipped");
        WriteIni(b + "[General]\r\nK=5\r\n");
        Check(Read("General", "K") == "5", std::string(c.name) + " before a header is skipped");
    }

    WriteIni("[General]\r\nK=a" + std::string(1, '\0') + "b\r\n");
    Check(Read("General", "K") == std::string("a\0b", 3), "NUL in the middle of a value is kept");
    WriteIni("[General]\r\nK=a\x1A" "b\r\n");
    Check(Read("General", "K") == "a\x1A" "b", "SUB in the middle of a value is kept");

    const struct {
        char byte;
        const char* name;
    } kept[] = {{'\xA0', "0xA0"}, {'\x85', "0x85"}};
    for (const auto& c : kept) {
        const std::string b(1, c.byte);
        WriteIni("[General]\r\n" + b + "K=5\r\n");
        Check(Read("General", "K") == kAbsent, std::string(c.name) + " before a key is not skipped");
        WriteIni(b + "[General]\r\nK=5\r\n");
        Check(Read("General", "K") == kAbsent, std::string(c.name) + " before a header is not skipped");
    }
}

void TestProfileInt() {
    std::cout << "GetPrivateProfileIntA:\n";

    WriteIni(
        "[General]\r\n"
        "Hex=0x10\r\n"
        "Negative=-5\r\n"
        "Leading= 7\r\n"
        "Trailing=7abc\r\n"
        "Letters=abc\r\n"
        "Plus=+3\r\n"
        "Octal=010\r\n"
        "Commented=12 ; x\r\n"
        "Max=4294967295\r\n");
    Check(ReadInt("General", "Hex") == 16, "0x10 reads as 16");
    Check(ReadInt("General", "Negative") == -5, "-5 reads as -5");
    Check(ReadInt("General", "Leading") == 7, "' 7' reads as 7");
    Check(ReadInt("General", "Trailing") == 7, "7abc reads as 7");
    Check(ReadInt("General", "Letters") == 0, "abc reads as 0, not the default");
    Check(ReadInt("General", "Plus") == 3, "+3 reads as 3");
    Check(ReadInt("General", "Octal") == 10, "010 reads as ten, not octal");
    Check(ReadInt("General", "Commented") == 12, "'12 ; x' reads as 12");
    Check(ReadInt("General", "Max") == -1, "4294967295 reads as -1");
    Check(ReadInt("General", "Absent") == 77, "an absent key returns the default");
}

void TestTruncation() {
    std::cout << "Long values:\n";

    std::string value;
    for (int i = 0; i < 1500; ++i) value += static_cast<char>('a' + i % 26);
    WriteIni("[General]\r\nLong=" + value + "\r\n");
    Check(Read("General", "Long", 1024) == value.substr(0, 1023),
          "a 1500-character value read into a 1024-byte buffer comes back as its first 1023");
}

void TestNoStaleCache() {
    std::cout << "Rewrites:\n";

    WriteIni("[General]\r\nK=old\r\n");
    Check(Read("General", "K") == "old", "the first version is read");
    WriteIni("[General]\r\nK=rewritten\r\n");
    Check(Read("General", "K") == "rewritten", "a rewrite is read at once");

    const std::string replacement = ScratchPath("cameraunlock_win32_profile_semantics.new");
    WriteBytes(replacement, "[General]\r\nK=replaced\r\n");
    if (!ReplaceFileA(IniPath().c_str(), replacement.c_str(), nullptr, 0, nullptr, nullptr)) {
        throw std::runtime_error("ReplaceFileA failed: " + std::to_string(GetLastError()));
    }
    Check(Read("General", "K") == "replaced", "a rename-replace is read at once");

    WriteBytes(replacement, "[General]\r\nK=moved\r\n");
    if (!MoveFileExA(replacement.c_str(), IniPath().c_str(), MOVEFILE_REPLACE_EXISTING)) {
        throw std::runtime_error("MoveFileExA failed: " + std::to_string(GetLastError()));
    }
    Check(Read("General", "K") == "moved", "a move over the file is read at once");
}

}  // namespace

int RunWin32ProfileSemanticsTests() {
    std::cout << "\n=== Win32 Profile Semantics Tests ===\n";
    TestRepeats();
    TestNames();
    TestValues();
    TestLineEndings();
    TestEncoding();
    TestControlBytes();
    TestProfileInt();
    TestTruncation();
    TestNoStaleCache();
    DeleteFileA(IniPath().c_str());
    return g_failures;
}

#else

int RunWin32ProfileSemanticsTests() { return 0; }

#endif  // _WIN32
