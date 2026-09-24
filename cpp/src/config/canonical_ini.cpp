#include "cameraunlock/config/canonical_ini.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace cameraunlock::config {

namespace {

constexpr std::string_view kStampSection = "CameraUnlock";
constexpr std::string_view kFormatKey = "ConfigFormat";

bool IsSpaceOrTab(char c) { return c == ' ' || c == '\t'; }

char FoldAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool EqualsAsciiIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (FoldAscii(a[i]) != FoldAscii(b[i])) return false;
    }
    return true;
}

std::string_view Trim(std::string_view s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && IsSpaceOrTab(s[begin])) ++begin;
    while (end > begin && IsSpaceOrTab(s[end - 1])) --end;
    return s.substr(begin, end - begin);
}

bool StartsWithUtf16Mark(std::string_view bytes) {
    return bytes.size() >= 2 &&
           ((bytes[0] == '\xFF' && bytes[1] == '\xFE') || (bytes[0] == '\xFE' && bytes[1] == '\xFF'));
}

// Calls visit(line, number) for each line, untrimmed and without its terminator.
template <class Visit>
void ForEachLine(std::string_view bytes, Visit visit) {
    size_t pos = 0;
    if (bytes.size() >= 3 && bytes.substr(0, 3) == "\xEF\xBB\xBF") pos = 3;
    int number = 0;
    while (pos < bytes.size()) {
        size_t end = pos;
        while (end < bytes.size() && bytes[end] != '\r' && bytes[end] != '\n') ++end;
        visit(bytes.substr(pos, end - pos), ++number);
        if (end < bytes.size() && bytes[end] == '\r' && end + 1 < bytes.size() && bytes[end + 1] == '\n') {
            pos = end + 2;
        } else {
            pos = end + 1;
        }
    }
}

int LineOfOffset(std::string_view bytes, size_t offset) {
    int line = 1;
    for (size_t i = 0; i < offset; ++i) {
        if (bytes[i] == '\n' || (bytes[i] == '\r' && (i + 1 >= bytes.size() || bytes[i + 1] != '\n'))) {
            ++line;
        }
    }
    return line;
}

struct Header {
    bool closed = false;
    std::string_view name;
    std::string_view trailing;
};

// `line` is trimmed and starts with '['.
Header ParseHeader(std::string_view line) {
    Header header;
    const size_t close = line.find(']', 1);
    if (close == std::string_view::npos) return header;
    header.closed = true;
    header.name = Trim(line.substr(1, close - 1));
    header.trailing = Trim(line.substr(close + 1));
    return header;
}

bool HasStampInBytes(std::string_view bytes) {
    bool found = false;
    ForEachLine(bytes, [&](std::string_view raw, int) {
        const std::string_view line = Trim(raw);
        if (found || line.empty() || line[0] != '[') return;
        const Header header = ParseHeader(line);
        found = header.closed && EqualsAsciiIgnoreCase(header.name, kStampSection);
    });
    return found;
}

// Every code unit above 0x7F becomes 0x80, which is not a byte the header rule or the
// name "CameraUnlock" treats specially, so the header rule reads the result exactly as it
// would read the units themselves. An odd last byte is not a unit and is dropped.
std::string NarrowUtf16(std::string_view bytes) {
    const bool little_endian = bytes[0] == '\xFF';
    std::string narrow;
    narrow.reserve(bytes.size() / 2);
    for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
        const unsigned lo = static_cast<unsigned char>(bytes[little_endian ? i : i + 1]);
        const unsigned hi = static_cast<unsigned char>(bytes[little_endian ? i + 1 : i]);
        const unsigned unit = (hi << 8) | lo;
        narrow.push_back(unit < 0x80 ? static_cast<char>(unit) : '\x80');
    }
    return narrow;
}

CanonicalDiagnostic MakeDiagnostic(CanonicalDiagnosticKind kind, int line, std::string_view section,
                                   std::string_view key, std::string_view value) {
    CanonicalDiagnostic d;
    d.kind = kind;
    d.lines.push_back(line);
    d.section = std::string(section);
    d.key = std::string(key);
    d.value = std::string(value);
    return d;
}

// ConfigFormat's number, or -1 when the value is not one: digits only, 1 or more.
int ParseFormatNumber(std::string_view value) {
    if (value.empty()) return -1;
    long long number = 0;
    for (char c : value) {
        if (c < '0' || c > '9') return -1;
        if (number < INT_MAX) number = std::min<long long>(number * 10 + (c - '0'), INT_MAX);
    }
    return number < 1 ? -1 : static_cast<int>(number);
}

void ReadFormat(CanonicalIni& doc, int stamp_header_line) {
    doc.format_version = kConfigFormat;
    const CanonicalSection* stamp = doc.FindSection(kStampSection);
    if (stamp == nullptr) return;
    const CanonicalValue* format = stamp->Find(kFormatKey);
    if (format == nullptr) {
        doc.diagnostics.push_back(
            MakeDiagnostic(CanonicalDiagnosticKind::ConfigFormatMissing, stamp_header_line, stamp->name, "", ""));
        return;
    }
    const int number = ParseFormatNumber(format->value);
    if (number < 0) {
        doc.diagnostics.push_back(MakeDiagnostic(CanonicalDiagnosticKind::ConfigFormatInvalid, format->line,
                                                 stamp->name, format->key, format->value));
        return;
    }
    doc.format_version = number;
    if (number > kConfigFormat) {
        doc.diagnostics.push_back(MakeDiagnostic(CanonicalDiagnosticKind::ConfigFormatNewer, format->line,
                                                 stamp->name, format->key, format->value));
    }
}

std::string JoinLines(const std::vector<int>& lines) {
    std::string text;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) text += (i + 1 == lines.size()) ? " and " : ", ";
        text += std::to_string(lines[i]);
    }
    return text;
}

}  // namespace

const CanonicalValue* CanonicalSection::Find(std::string_view key) const {
    for (const CanonicalValue& value : values) {
        if (EqualsAsciiIgnoreCase(value.key, key)) return &value;
    }
    return nullptr;
}

const CanonicalSection* CanonicalIni::FindSection(std::string_view name) const {
    for (const CanonicalSection& section : sections) {
        if (EqualsAsciiIgnoreCase(section.name, name)) return &section;
    }
    return nullptr;
}

const CanonicalValue* CanonicalIni::Find(std::string_view section, std::string_view key) const {
    const CanonicalSection* found = FindSection(section);
    return found == nullptr ? nullptr : found->Find(key);
}

CanonicalIni ParseCanonicalIni(std::string_view bytes) {
    CanonicalIni doc;
    if (StartsWithUtf16Mark(bytes)) {
        doc.status = CanonicalReadStatus::Utf16;
        return doc;
    }
    const size_t nul = bytes.find('\0');
    if (nul != std::string_view::npos) {
        doc.status = CanonicalReadStatus::NulByte;
        doc.unreadable_line = LineOfOffset(bytes, nul);
        return doc;
    }

    // Index into doc.sections, or -1 above the first header and below one naming none.
    int current = -1;
    int stamp_header_line = 0;
    ForEachLine(bytes, [&](std::string_view raw, int number) {
        const std::string_view line = Trim(raw);
        if (line.empty() || line[0] == ';' || line[0] == '#') return;

        if (line[0] == '[') {
            const Header header = ParseHeader(line);
            current = -1;
            if (!header.closed) {
                doc.diagnostics.push_back(
                    MakeDiagnostic(CanonicalDiagnosticKind::UnclosedSectionHeader, number, "", "", line));
                return;
            }
            if (!header.trailing.empty()) {
                doc.diagnostics.push_back(MakeDiagnostic(CanonicalDiagnosticKind::TextAfterSectionHeader, number,
                                                         header.name, "", header.trailing));
            }
            if (header.name.empty()) {
                doc.diagnostics.push_back(
                    MakeDiagnostic(CanonicalDiagnosticKind::EmptySectionName, number, "", "", line));
                return;
            }
            for (size_t i = 0; i < doc.sections.size(); ++i) {
                if (EqualsAsciiIgnoreCase(doc.sections[i].name, header.name)) current = static_cast<int>(i);
            }
            if (current < 0) {
                current = static_cast<int>(doc.sections.size());
                doc.sections.push_back(CanonicalSection{std::string(header.name), {}});
                if (stamp_header_line == 0 && EqualsAsciiIgnoreCase(header.name, kStampSection)) {
                    stamp_header_line = number;
                }
            }
            return;
        }

        const size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            doc.diagnostics.push_back(MakeDiagnostic(CanonicalDiagnosticKind::MissingEquals, number, "", "", line));
            return;
        }
        const std::string_view key = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        if (key.empty()) {
            doc.diagnostics.push_back(MakeDiagnostic(CanonicalDiagnosticKind::EmptyKey, number, "", "", line));
            return;
        }
        if (current < 0) {
            doc.diagnostics.push_back(
                MakeDiagnostic(CanonicalDiagnosticKind::KeyOutsideSection, number, "", key, value));
            return;
        }
        CanonicalSection& section = doc.sections[static_cast<size_t>(current)];
        for (CanonicalValue& existing : section.values) {
            if (!EqualsAsciiIgnoreCase(existing.key, key)) continue;
            existing.earlier_lines.push_back(existing.line);
            existing.key = std::string(key);
            existing.value = std::string(value);
            existing.line = number;
            return;
        }
        section.values.push_back(CanonicalValue{std::string(key), std::string(value), number, {}});
    });

    for (const CanonicalSection& section : doc.sections) {
        for (const CanonicalValue& value : section.values) {
            if (value.earlier_lines.empty()) continue;
            CanonicalDiagnostic d =
                MakeDiagnostic(CanonicalDiagnosticKind::DuplicateKey, 0, section.name, value.key, value.value);
            d.lines = value.earlier_lines;
            d.lines.push_back(value.line);
            doc.diagnostics.push_back(std::move(d));
        }
    }
    ReadFormat(doc, stamp_header_line);

    std::stable_sort(doc.diagnostics.begin(), doc.diagnostics.end(),
                     [](const CanonicalDiagnostic& a, const CanonicalDiagnostic& b) {
                         if (a.lines.front() != b.lines.front()) return a.lines.front() < b.lines.front();
                         return static_cast<int>(a.kind) < static_cast<int>(b.kind);
                     });
    return doc;
}

bool HasCanonicalStamp(std::string_view bytes) {
    if (StartsWithUtf16Mark(bytes)) return HasStampInBytes(NarrowUtf16(bytes));
    return HasStampInBytes(bytes);
}

const char* CanonicalReadStatusName(CanonicalReadStatus status) {
    switch (status) {
        case CanonicalReadStatus::Readable: return "Readable";
        case CanonicalReadStatus::Utf16: return "Utf16";
        case CanonicalReadStatus::NulByte: return "NulByte";
    }
    throw std::invalid_argument("CanonicalReadStatus " + std::to_string(static_cast<int>(status)) +
                                " has no name");
}

const char* CanonicalDiagnosticKindName(CanonicalDiagnosticKind kind) {
    switch (kind) {
        case CanonicalDiagnosticKind::TextAfterSectionHeader: return "TextAfterSectionHeader";
        case CanonicalDiagnosticKind::UnclosedSectionHeader: return "UnclosedSectionHeader";
        case CanonicalDiagnosticKind::EmptySectionName: return "EmptySectionName";
        case CanonicalDiagnosticKind::EmptyKey: return "EmptyKey";
        case CanonicalDiagnosticKind::MissingEquals: return "MissingEquals";
        case CanonicalDiagnosticKind::KeyOutsideSection: return "KeyOutsideSection";
        case CanonicalDiagnosticKind::DuplicateKey: return "DuplicateKey";
        case CanonicalDiagnosticKind::ConfigFormatMissing: return "ConfigFormatMissing";
        case CanonicalDiagnosticKind::ConfigFormatInvalid: return "ConfigFormatInvalid";
        case CanonicalDiagnosticKind::ConfigFormatNewer: return "ConfigFormatNewer";
    }
    throw std::invalid_argument("CanonicalDiagnosticKind " + std::to_string(static_cast<int>(kind)) +
                                " has no name");
}

std::string DescribeCanonicalDiagnostic(const CanonicalDiagnostic& d) {
    const std::string line = "Line " + JoinLines(d.lines) + ": ";
    const std::string format = std::to_string(kConfigFormat);
    switch (d.kind) {
        case CanonicalDiagnosticKind::TextAfterSectionHeader:
            return line + "\"" + d.value + "\" after [" + d.section +
                   "] is ignored. Comments go on their own line.";
        case CanonicalDiagnosticKind::UnclosedSectionHeader:
            return line + "\"" + d.value +
                   "\" has no closing ], so the settings below it are ignored up to the next section header.";
        case CanonicalDiagnosticKind::EmptySectionName:
            return line + "\"" + d.value +
                   "\" names no section, so the settings below it are ignored up to the next section header.";
        case CanonicalDiagnosticKind::EmptyKey:
            return line + "\"" + d.value + "\" has no setting name before the =, so it is ignored.";
        case CanonicalDiagnosticKind::MissingEquals:
            return line + "\"" + d.value + "\" is not a setting (it has no =), so it is ignored.";
        case CanonicalDiagnosticKind::KeyOutsideSection:
            return line + d.key + " is not under a section header, so it is ignored.";
        case CanonicalDiagnosticKind::DuplicateKey:
            return "[" + d.section + "] " + d.key + " is set on lines " + JoinLines(d.lines) + ". Line " +
                   std::to_string(d.lines.back()) + " is used.";
        case CanonicalDiagnosticKind::ConfigFormatMissing:
            return line + "[" + d.section + "] has no ConfigFormat, so the file is read as format " + format + ".";
        case CanonicalDiagnosticKind::ConfigFormatInvalid:
            return line + d.key + "=" + d.value + " is not a format number, so the file is read as format " +
                   format + ".";
        case CanonicalDiagnosticKind::ConfigFormatNewer:
            return line + d.key + "=" + d.value +
                   " was written by a newer version of the mod. This version reads format " + format + ".";
    }
    throw std::invalid_argument(std::string("CanonicalDiagnosticKind ") + CanonicalDiagnosticKindName(d.kind) +
                                " has no description");
}

}  // namespace cameraunlock::config
