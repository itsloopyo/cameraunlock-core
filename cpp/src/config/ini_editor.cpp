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

void RequirePrintableAscii(const std::string& text, const char* what) {
    static const char kHex[] = "0123456789ABCDEF";
    for (const char c : text) {
        const unsigned char b = static_cast<unsigned char>(c);
        if (b >= 0x20 && b <= 0x7E) continue;
        throw std::invalid_argument(std::string("IniEdit ") + what + " holds the byte 0x" +
                                    kHex[b >> 4] + kHex[b & 0xF] +
                                    ", and only printable ASCII (0x20 to 0x7E) can be written");
    }
}

void RequireNoSurroundingSpace(const std::string& text, const char* what) {
    if (!text.empty() && (text.front() == ' ' || text.back() == ' ')) {
        throw std::invalid_argument(std::string("IniEdit ") + what + " '" + text +
                                    "' starts or ends with a space, which the reader trims away");
    }
}

void RequireName(const std::string& text, const char* what) {
    RequirePrintableAscii(text, what);
    if (text.empty()) throw std::invalid_argument(std::string("IniEdit ") + what + " is empty");
    RequireNoSurroundingSpace(text, what);
}

void ValidateEdits(const std::vector<IniEdit>& edits) {
    for (size_t i = 0; i < edits.size(); ++i) {
        const IniEdit& edit = edits[i];
        RequireName(edit.section, "section");
        RequireName(edit.key, "key");
        RequirePrintableAscii(edit.value, "value");
        RequireNoSurroundingSpace(edit.value, "value");
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

enum class LineKind { Other, Blank, Comment, Section, Key };

struct Line {
    size_t begin = 0;
    size_t content_end = 0;
    size_t end = 0;
    LineKind kind = LineKind::Other;
    // A section's name, or a key line's key.
    size_t name_begin = 0;
    size_t name_end = 0;
    // A header with no ']' is still a section boundary, but names no section.
    bool named = false;
    size_t value_begin = 0;
};

void ClassifyLine(const std::string& s, Line& line) {
    size_t first = line.begin;
    while (first < line.content_end && IsSpaceOrTab(s[first])) ++first;
    size_t last = line.content_end;
    while (last > first && IsSpaceOrTab(s[last - 1])) --last;

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
        if (close == kNpos || close >= last) return;
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
    if (eq == kNpos || eq >= last) return;
    size_t key_end = eq;
    while (key_end > first && IsSpaceOrTab(s[key_end - 1])) --key_end;
    if (key_end == first) return;

    size_t value_begin = eq + 1;
    while (value_begin < line.content_end && IsSpaceOrTab(s[value_begin])) ++value_begin;

    line.kind = LineKind::Key;
    line.name_begin = first;
    line.name_end = key_end;
    line.value_begin = value_begin;
}

// The canonical reader's line count up to `offset`: CRLF, LF and a lone CR each end a line.
int LineOfOffset(const std::string& s, size_t offset) {
    int line = 1;
    for (size_t i = 0; i < offset; ++i) {
        if (s[i] == '\n' || (s[i] == '\r' && (i + 1 >= s.size() || s[i + 1] != '\n'))) ++line;
    }
    return line;
}

IniEditResult Refuse(IniEditRefusal refusal, std::vector<int> lines) {
    IniEditResult result;
    result.refusal = refusal;
    result.lines = std::move(lines);
    return result;
}

IniEditResult RefuseKeyNotFound(const IniEdit& edit) {
    IniEditResult result = Refuse(IniEditRefusal::KeyNotFound, {});
    result.section = edit.section;
    result.key = edit.key;
    return result;
}

struct NewSection {
    std::string name;
    std::vector<std::string> lines;
};

bool IsLineEnd(char c) { return c == '\r' || c == '\n'; }

bool LastLineIsBlank(const std::string& s, size_t body_begin) {
    size_t end = s.size();
    if (end >= body_begin + 2 && s[end - 2] == '\r' && s[end - 1] == '\n') {
        end -= 2;
    } else if (end > body_begin && IsLineEnd(s[end - 1])) {
        --end;
    }
    size_t begin = end;
    while (begin > body_begin && !IsLineEnd(s[begin - 1])) --begin;
    for (size_t i = begin; i < end; ++i) {
        if (!IsSpaceOrTab(s[i])) return false;
    }
    return true;
}

// An ending written straight after a CR, or straight before an LF, would pair with it into
// one CRLF and the reader would lose a line; CRLF pairs with neither, so it is written there.
void AppendEol(std::string& out, const std::string& eol, bool lf_follows) {
    const bool pairs = (eol == "\n" && !out.empty() && out.back() == '\r') ||
                       (eol == "\r" && lf_follows);
    out.append(pairs ? "\r\n" : eol);
}

void AppendLine(std::string& out, const std::string& line, const std::string& eol,
                bool after_terminated_line, bool lf_follows) {
    if (after_terminated_line) {
        out.append(line);
        AppendEol(out, eol, lf_follows);
    } else {
        AppendEol(out, eol, line.empty() && lf_follows);
        out.append(line);
    }
}

}  // namespace

const char* IniEditRefusalName(IniEditRefusal refusal) {
    switch (refusal) {
        case IniEditRefusal::None: return "None";
        case IniEditRefusal::Utf16: return "Utf16";
        case IniEditRefusal::NulByte: return "NulByte";
        case IniEditRefusal::KeyNotFound: return "KeyNotFound";
    }
    throw std::invalid_argument("IniEditRefusal " + std::to_string(static_cast<int>(refusal)) +
                                " has no name");
}

IniEditResult EditIni(const std::string& original, const std::vector<IniEdit>& edits) {
    ValidateEdits(edits);

    const std::string& s = original;
    if (s.size() >= 2 && ((s[0] == '\xFF' && s[1] == '\xFE') || (s[0] == '\xFE' && s[1] == '\xFF'))) {
        return Refuse(IniEditRefusal::Utf16, {});
    }
    const size_t nul = s.find('\0');
    if (nul != kNpos) return Refuse(IniEditRefusal::NulByte, {LineOfOffset(s, nul)});
    const size_t body = s.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;

    std::vector<Line> lines;
    size_t crlf_count = 0;
    size_t lf_count = 0;
    size_t cr_count = 0;
    for (size_t begin = body; begin < s.size();) {
        Line line;
        line.begin = begin;
        size_t end = begin;
        while (end < s.size() && !IsLineEnd(s[end])) ++end;
        line.content_end = end;
        if (end == s.size()) {
            line.end = end;
        } else if (s[end] == '\r' && end + 1 < s.size() && s[end + 1] == '\n') {
            line.end = end + 2;
            ++crlf_count;
        } else {
            line.end = end + 1;
            if (s[end] == '\n') {
                ++lf_count;
            } else {
                ++cr_count;
            }
        }
        ClassifyLine(s, line);
        lines.push_back(line);
        begin = line.end;
    }
    const std::string eol = (crlf_count >= lf_count && crlf_count >= cr_count) ? "\r\n"
                            : lf_count >= cr_count                            ? "\n"
                                                                              : "\r";

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
        std::vector<bool> names_section(lines.size(), false);
        size_t first_header = kNpos;
        size_t last_header = kNpos;
        for (size_t header : headers) {
            const Line& h = lines[header];
            if (!h.named || !EqualsAsciiIgnoreCase(s, h.name_begin, h.name_end, edit.section)) continue;
            names_section[header] = true;
            if (first_header == kNpos) first_header = header;
            last_header = header;
        }
        if (first_header == kNpos) {
            if (!edit.insert_if_absent) return RefuseKeyNotFound(edit);
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

        const size_t block = edit.first_occurrence_wins ? first_header : last_header;
        size_t anchor = block;
        size_t first_key = kNpos;
        size_t last_key = kNpos;
        for (size_t i = first_header + 1; i < lines.size(); ++i) {
            const Line& line = lines[i];
            if (line.kind != LineKind::Key || !names_section[owner[i]]) continue;
            if (owner[i] == block) anchor = i;
            if (EqualsAsciiIgnoreCase(s, line.name_begin, line.name_end, edit.key)) {
                if (first_key == kNpos) first_key = i;
                last_key = i;
            }
        }
        if (first_key != kNpos) {
            replacements[edit.first_occurrence_wins ? first_key : last_key] = &edit.value;
            continue;
        }
        if (!edit.insert_if_absent) return RefuseKeyNotFound(edit);
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
            out.append(s, line.content_end, line.end - line.content_end);
        } else {
            out.append(s, line.begin, line.end - line.begin);
        }
        const bool terminated = line.end != line.content_end;
        const bool lf_next = line.end < s.size() && s[line.end] == '\n';
        for (size_t k = 0; k < insertions[i].size(); ++k) {
            AppendLine(out, insertions[i][k], eol, terminated,
                       k + 1 == insertions[i].size() && lf_next);
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
        const bool terminated = empty || IsLineEnd(out.back());
        for (size_t k = 0; k < appended.size(); ++k) {
            const bool lf_follows = k + 1 < appended.size() && appended[k + 1].empty() && eol == "\n";
            AppendLine(out, appended[k], eol, terminated, lf_follows);
        }
    }

    IniEditResult result;
    result.bytes = std::move(out);
    return result;
}

}  // namespace cameraunlock
