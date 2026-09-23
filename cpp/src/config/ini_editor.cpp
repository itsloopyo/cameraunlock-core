#include "cameraunlock/config/ini_editor.h"

#include <cstddef>
#include <stdexcept>
#include <utility>

namespace cameraunlock {

namespace {

constexpr size_t kNpos = std::string::npos;

bool IsSpaceOrTab(char c) { return c == ' ' || c == '\t'; }

char FoldAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool EqualsAsciiIgnoreCase(const std::string& text, size_t begin, size_t end,
                           const std::string& other) {
    if (end - begin != other.size()) return false;
    for (size_t i = 0; i < other.size(); ++i) {
        if (FoldAscii(text[begin + i]) != FoldAscii(other[i])) return false;
    }
    return true;
}

bool EqualsAsciiIgnoreCase(const std::string& a, const std::string& b) {
    return EqualsAsciiIgnoreCase(a, 0, a.size(), b);
}

// Well-formed UTF-8 as Unicode's table 3-7 defines it: no overlongs, no surrogates,
// nothing past U+10FFFF. Returns the offset of the first byte that starts a bad
// sequence, or kNpos.
size_t FirstInvalidUtf8(const std::string& s, size_t pos) {
    const size_t n = s.size();
    size_t i = pos;
    while (i < n) {
        const unsigned char lead = static_cast<unsigned char>(s[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        size_t length = 0;
        unsigned char low = 0x80;
        unsigned char high = 0xBF;
        if (lead >= 0xC2 && lead <= 0xDF) {
            length = 2;
        } else if (lead == 0xE0) {
            length = 3;
            low = 0xA0;
        } else if ((lead >= 0xE1 && lead <= 0xEC) || lead == 0xEE || lead == 0xEF) {
            length = 3;
        } else if (lead == 0xED) {
            length = 3;
            high = 0x9F;
        } else if (lead == 0xF0) {
            length = 4;
            low = 0x90;
        } else if (lead >= 0xF1 && lead <= 0xF3) {
            length = 4;
        } else if (lead == 0xF4) {
            length = 4;
            high = 0x8F;
        } else {
            return i;
        }
        if (n - i < length) return i;
        const unsigned char second = static_cast<unsigned char>(s[i + 1]);
        if (second < low || second > high) return i;
        for (size_t k = 2; k < length; ++k) {
            const unsigned char next = static_cast<unsigned char>(s[i + k]);
            if (next < 0x80 || next > 0xBF) return i;
        }
        i += length;
    }
    return kNpos;
}

void RequireWritable(const std::string& text, const char* what) {
    if (FirstInvalidUtf8(text, 0) != kNpos) {
        throw std::invalid_argument(std::string("IniEdit ") + what + " is not valid UTF-8");
    }
    if (text.find_first_of(std::string("\r\n\0", 3)) != kNpos) {
        throw std::invalid_argument(std::string("IniEdit ") + what +
                                    " holds a CR, LF or NUL, which cannot be written on one line");
    }
    if (text.find('\x1A') != kNpos) {
        throw std::invalid_argument(std::string("IniEdit ") + what +
                                    " holds a SUB (0x1A), where a text-mode reader stops reading");
    }
}

// Every character some runtime's String.Trim() strips, so the C# half and this one
// refuse the same bytes. .NET Framework 3.5 trims a fixed list that includes U+200B and
// U+FEFF and leaves out U+180E, U+202F and U+205F; .NET Framework 4 and later trim
// char.IsWhiteSpace. This is the union.
bool IsReaderWhitespace(unsigned c) {
    return c == ' ' || (c >= 0x09 && c <= 0x0D) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           c == 0x180E || (c >= 0x2000 && c <= 0x200B) || c == 0x2028 || c == 0x2029 ||
           c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

// The code point starting at s[i], which must begin well-formed UTF-8.
unsigned CodePointAt(const std::string& s, size_t i) {
    auto byte = [&](size_t k) { return static_cast<unsigned>(static_cast<unsigned char>(s[k])); };
    const unsigned b = byte(i);
    if (b < 0x80) return b;
    if (b < 0xE0) return ((b & 0x1F) << 6) | (byte(i + 1) & 0x3F);
    if (b < 0xF0) return ((b & 0x0F) << 12) | ((byte(i + 1) & 0x3F) << 6) | (byte(i + 2) & 0x3F);
    return ((b & 0x07) << 18) | ((byte(i + 1) & 0x3F) << 12) | ((byte(i + 2) & 0x3F) << 6) |
           (byte(i + 3) & 0x3F);
}

bool StartsWithReaderWhitespace(const std::string& s, size_t begin, size_t end) {
    return begin < end && IsReaderWhitespace(CodePointAt(s, begin));
}

bool EndsWithReaderWhitespace(const std::string& s, size_t begin, size_t end) {
    if (begin >= end) return false;
    size_t i = end - 1;
    while (i > begin && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return IsReaderWhitespace(CodePointAt(s, i));
}

void RequireNoSurroundingWhitespace(const std::string& text, const char* what) {
    if (StartsWithReaderWhitespace(text, 0, text.size()) ||
        EndsWithReaderWhitespace(text, 0, text.size())) {
        throw std::invalid_argument(std::string("IniEdit ") + what + " '" + text +
                                    "' has surrounding white space, which a reader trims away");
    }
}

void RequireName(const std::string& text, const char* what) {
    RequireWritable(text, what);
    if (text.empty()) throw std::invalid_argument(std::string("IniEdit ") + what + " is empty");
    RequireNoSurroundingWhitespace(text, what);
}

// The value lands on a key=value line, perhaps with an inline comment after it. The flat
// readers trim, cut the comment off, then take off one pair of surrounding quotes, so a
// value any of that would change cannot be written.
void RequireValue(const std::string& text) {
    RequireWritable(text, "value");
    RequireNoSurroundingWhitespace(text, "value");
    bool in_quotes = false;
    char quote = '\0';
    for (const char c : text) {
        if (in_quotes) {
            if (c == quote) in_quotes = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_quotes = true;
            quote = c;
            continue;
        }
        if (c == ';' || c == '#') {
            throw std::invalid_argument("IniEdit value '" + text +
                                        "' holds ';' or '#' outside quotes, which a reader "
                                        "takes as the start of a comment");
        }
    }
    if (in_quotes) {
        throw std::invalid_argument("IniEdit value '" + text +
                                    "' leaves a quote open, so a comment after it on the line "
                                    "would read as part of the value");
    }
    const char first = text.size() >= 2 ? text.front() : '\0';
    if ((first == '"' || first == '\'') && text.back() == first) {
        throw std::invalid_argument("IniEdit value '" + text +
                                    "' is wrapped in quotes, which a reader strips");
    }
}

void ValidateEdits(const std::vector<IniEdit>& edits) {
    for (size_t i = 0; i < edits.size(); ++i) {
        const IniEdit& edit = edits[i];
        RequireName(edit.section, "section");
        RequireName(edit.key, "key");
        RequireValue(edit.value);
        if (edit.section.find(']') != kNpos) {
            throw std::invalid_argument("IniEdit section '" + edit.section +
                                        "' holds ']', which ends a section header");
        }
        const char first = edit.key.front();
        if (first == '[' || first == ';' || first == '#' || edit.key.find('=') != kNpos) {
            throw std::invalid_argument("IniEdit key '" + edit.key +
                                        "' would not read back as a key: it holds '=' or "
                                        "starts '[', ';' or '#'");
        }
        for (size_t j = 0; j < i; ++j) {
            if (EqualsAsciiIgnoreCase(edits[j].section, edit.section) &&
                EqualsAsciiIgnoreCase(edits[j].key, edit.key)) {
                throw std::invalid_argument("IniEdit batch edits [" + edit.section + "] " +
                                            edit.key + " twice");
            }
        }
    }
}

enum class LineKind { Blank, Comment, Section, Key, Other };

struct Line {
    size_t begin = 0;
    size_t content_end = 0;
    size_t end = 0;
    // The first byte that is not a space or tab.
    size_t first = 0;
    LineKind kind = LineKind::Other;
    // A section's name, or a key line's key.
    size_t name_begin = 0;
    size_t name_end = 0;
    // A header with no ']' is still a section boundary, but names no section.
    bool named = false;
    size_t value_begin = 0;
    size_t value_end = 0;
};

void ClassifyLine(const std::string& s, Line& line) {
    size_t first = line.begin;
    while (first < line.content_end && IsSpaceOrTab(s[first])) ++first;
    size_t last = line.content_end;
    while (last > first && IsSpaceOrTab(s[last - 1])) --last;
    line.first = first;

    if (first == last) {
        line.kind = LineKind::Blank;
        return;
    }
    const char lead = s[first];
    if (lead == ';' || lead == '#') {
        line.kind = LineKind::Comment;
        return;
    }
    if (lead == '[') {
        line.kind = LineKind::Section;
        const size_t close = s.find(']', first + 1);
        if (close == kNpos || close >= line.content_end) return;
        size_t name_begin = first + 1;
        size_t name_end = close;
        while (name_begin < name_end && IsSpaceOrTab(s[name_begin])) ++name_begin;
        while (name_end > name_begin && IsSpaceOrTab(s[name_end - 1])) --name_end;
        line.named = true;
        line.name_begin = name_begin;
        line.name_end = name_end;
        return;
    }

    const size_t eq = s.find('=', first);
    if (eq == kNpos || eq >= line.content_end || eq == first) {
        line.kind = LineKind::Other;
        return;
    }
    size_t key_end = eq;
    while (key_end > first && IsSpaceOrTab(s[key_end - 1])) --key_end;

    size_t value_begin = eq + 1;
    while (value_begin < line.content_end && IsSpaceOrTab(s[value_begin])) ++value_begin;

    // The inline comment rule of ConfigParsingUtils.StripInlineComment and its C++ twin:
    // the first ';' or '#' outside a quoted run.
    size_t comment = line.content_end;
    bool in_quotes = false;
    char quote = '\0';
    for (size_t i = value_begin; i < line.content_end; ++i) {
        const char c = s[i];
        if (in_quotes) {
            if (c == quote) in_quotes = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_quotes = true;
            quote = c;
            continue;
        }
        if (c == ';' || c == '#') {
            comment = i;
            break;
        }
    }
    size_t value_end = comment;
    while (value_end > value_begin && IsSpaceOrTab(s[value_end - 1])) --value_end;

    line.kind = LineKind::Key;
    line.name_begin = first;
    line.name_end = key_end;
    line.value_begin = value_begin;
    line.value_end = value_end;
}

int LineNumberAt(const std::string& s, size_t body_begin, size_t offset) {
    int number = 1;
    for (size_t i = body_begin; i < offset; ++i) {
        if (s[i] == '\n') ++number;
    }
    return number;
}

IniEditResult Refuse(IniEditRefusal refusal, std::vector<int> lines) {
    IniEditResult result;
    result.refusal = refusal;
    result.lines = std::move(lines);
    return result;
}

IniEditResult RefuseEdit(IniEditRefusal refusal, const IniEdit& edit, std::vector<int> lines) {
    IniEditResult result = Refuse(refusal, std::move(lines));
    result.section = edit.section;
    result.key = edit.key;
    return result;
}

struct NewSection {
    std::string name;
    std::vector<std::string> lines;
};

bool LastLineIsBlank(const std::string& s, size_t body_begin) {
    size_t end = s.size();
    if (end > body_begin && s[end - 1] == '\n') {
        --end;
        if (end > body_begin && s[end - 1] == '\r') --end;
    }
    size_t begin = end;
    while (begin > body_begin && s[begin - 1] != '\n') --begin;
    for (size_t i = begin; i < end; ++i) {
        if (!IsSpaceOrTab(s[i])) return false;
    }
    return true;
}

}  // namespace

const char* IniEditRefusalName(IniEditRefusal refusal) {
    switch (refusal) {
        case IniEditRefusal::None: return "None";
        case IniEditRefusal::Utf16: return "Utf16";
        case IniEditRefusal::InvalidUtf8: return "InvalidUtf8";
        case IniEditRefusal::NulByte: return "NulByte";
        case IniEditRefusal::LoneCarriageReturn: return "LoneCarriageReturn";
        case IniEditRefusal::DuplicateSection: return "DuplicateSection";
        case IniEditRefusal::DuplicateKey: return "DuplicateKey";
        case IniEditRefusal::KeyNotFound: return "KeyNotFound";
        case IniEditRefusal::AmbiguousWhitespace: return "AmbiguousWhitespace";
        case IniEditRefusal::SubByte: return "SubByte";
    }
    throw std::invalid_argument("IniEditRefusal " + std::to_string(static_cast<int>(refusal)) +
                                " has no name");
}

IniEditResult EditIni(const std::string& original, const std::vector<IniEdit>& edits) {
    ValidateEdits(edits);

    const std::string& s = original;
    if (s.size() >= 2 && ((static_cast<unsigned char>(s[0]) == 0xFF &&
                           static_cast<unsigned char>(s[1]) == 0xFE) ||
                          (static_cast<unsigned char>(s[0]) == 0xFE &&
                           static_cast<unsigned char>(s[1]) == 0xFF))) {
        return Refuse(IniEditRefusal::Utf16, {});
    }
    const size_t body = (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
                         static_cast<unsigned char>(s[1]) == 0xBB &&
                         static_cast<unsigned char>(s[2]) == 0xBF)
                            ? 3
                            : 0;

    const size_t invalid = FirstInvalidUtf8(s, body);
    if (invalid != kNpos) {
        return Refuse(IniEditRefusal::InvalidUtf8, {LineNumberAt(s, body, invalid)});
    }
    const size_t nul = s.find('\0', body);
    if (nul != kNpos) {
        return Refuse(IniEditRefusal::NulByte, {LineNumberAt(s, body, nul)});
    }
    const size_t sub = s.find('\x1A', body);
    if (sub != kNpos) {
        return Refuse(IniEditRefusal::SubByte, {LineNumberAt(s, body, sub)});
    }
    for (size_t i = body; i < s.size(); ++i) {
        if (s[i] == '\r' && (i + 1 == s.size() || s[i + 1] != '\n')) {
            return Refuse(IniEditRefusal::LoneCarriageReturn, {LineNumberAt(s, body, i)});
        }
    }

    std::vector<Line> lines;
    std::vector<int> ambiguous;
    size_t crlf_count = 0;
    size_t lf_count = 0;
    for (size_t begin = body; begin < s.size();) {
        Line line;
        line.begin = begin;
        const size_t newline = s.find('\n', begin);
        if (newline == kNpos) {
            line.content_end = s.size();
            line.end = s.size();
        } else {
            line.end = newline + 1;
            if (newline > begin && s[newline - 1] == '\r') {
                line.content_end = newline - 1;
                ++crlf_count;
            } else {
                line.content_end = newline;
                ++lf_count;
            }
        }
        ClassifyLine(s, line);
        if (StartsWithReaderWhitespace(s, line.first, line.content_end) ||
            (line.kind == LineKind::Key &&
             EndsWithReaderWhitespace(s, line.name_begin, line.name_end))) {
            ambiguous.push_back(static_cast<int>(lines.size()) + 1);
        }
        lines.push_back(line);
        begin = line.end;
    }
    if (!ambiguous.empty()) return Refuse(IniEditRefusal::AmbiguousWhitespace, std::move(ambiguous));
    const std::string eol = crlf_count >= lf_count ? "\r\n" : "\n";

    // The section each line belongs to, as the index of its header line.
    std::vector<size_t> headers;
    std::vector<size_t> owner(lines.size(), kNpos);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].kind == LineKind::Section) headers.push_back(i);
        owner[i] = headers.empty() ? kNpos : headers.back();
    }

    std::vector<const std::string*> replacements(lines.size(), nullptr);
    std::vector<std::vector<std::string>> insertions(lines.size());
    std::vector<NewSection> new_sections;

    for (const IniEdit& edit : edits) {
        std::vector<size_t> matched;
        for (size_t header : headers) {
            const Line& h = lines[header];
            if (h.named && EqualsAsciiIgnoreCase(s, h.name_begin, h.name_end, edit.section)) {
                matched.push_back(header);
            }
        }
        if (matched.empty()) {
            if (!edit.insert_if_absent) return RefuseEdit(IniEditRefusal::KeyNotFound, edit, {});
            NewSection* target = nullptr;
            for (NewSection& pending : new_sections) {
                if (EqualsAsciiIgnoreCase(pending.name, edit.section)) target = &pending;
            }
            if (target == nullptr) {
                new_sections.push_back(NewSection{edit.section, {}});
                target = &new_sections.back();
            }
            target->lines.push_back(edit.key + "=" + edit.value);
            continue;
        }

        size_t anchor = matched.front();
        std::vector<size_t> keys;
        for (size_t header : matched) {
            for (size_t i = header + 1; i < lines.size() && owner[i] == header; ++i) {
                const Line& line = lines[i];
                if (line.kind != LineKind::Blank && line.kind != LineKind::Comment) anchor = i;
                if (line.kind == LineKind::Key &&
                    EqualsAsciiIgnoreCase(s, line.name_begin, line.name_end, edit.key)) {
                    keys.push_back(i);
                }
            }
        }
        if (keys.size() > 1) {
            std::vector<int> numbers;
            for (size_t index : keys) numbers.push_back(static_cast<int>(index) + 1);
            return RefuseEdit(IniEditRefusal::DuplicateKey, edit, std::move(numbers));
        }
        if (keys.size() == 1) {
            replacements[keys.front()] = &edit.value;
            continue;
        }
        if (!edit.insert_if_absent) return RefuseEdit(IniEditRefusal::KeyNotFound, edit, {});
        if (matched.size() > 1) {
            std::vector<int> numbers;
            for (size_t header : matched) numbers.push_back(static_cast<int>(header) + 1);
            return RefuseEdit(IniEditRefusal::DuplicateSection, edit, std::move(numbers));
        }
        insertions[anchor].push_back(edit.key + "=" + edit.value);
    }

    std::string out;
    out.reserve(s.size() + 64 * edits.size());
    out.append(s, 0, body);
    for (size_t i = 0; i < lines.size(); ++i) {
        const Line& line = lines[i];
        if (replacements[i] != nullptr) {
            out.append(s, line.begin, line.value_begin - line.begin);
            out.append(*replacements[i]);
            out.append(s, line.value_end, line.end - line.value_end);
        } else {
            out.append(s, line.begin, line.end - line.begin);
        }
        const bool terminated = line.end != line.content_end;
        for (const std::string& inserted : insertions[i]) {
            if (terminated) {
                out.append(inserted);
                out.append(eol);
            } else {
                out.append(eol);
                out.append(inserted);
            }
        }
    }

    if (!new_sections.empty()) {
        std::vector<std::string> appended;
        const bool empty = out.size() == body;
        if (!empty && !LastLineIsBlank(out, body)) appended.emplace_back();
        for (size_t i = 0; i < new_sections.size(); ++i) {
            if (i > 0) appended.emplace_back();
            appended.push_back("[" + new_sections[i].name + "]");
            for (const std::string& line : new_sections[i].lines) appended.push_back(line);
        }
        const bool terminated = empty || out.back() == '\n';
        for (const std::string& line : appended) {
            if (terminated) {
                out.append(line);
                out.append(eol);
            } else {
                out.append(eol);
                out.append(line);
            }
        }
    }

    IniEditResult result;
    result.bytes = std::move(out);
    return result;
}

}  // namespace cameraunlock
