#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cameraunlock/config/legacy_import.h>

// The differential corpus: every mutation of a legacy config file that design 6.2 lists, for a
// game's differential test to run through its legacy import and its migration. Test code only,
// header-only and pure; nothing in the library includes it. csharp/testing/IniMutations.cs is the
// C# twin, and data/fixtures/canonical-ini/mutations holds both to the same bytes.

namespace cameraunlock::config::testing {

/// A legacy switch or letter row that folds a Ctrl+Shift binding into a hotkey's list.
struct ChordSwitch {
    /// Empty for a reader that ignores sections, as in MutationKey.
    std::string section;
    std::string key;
    /// The values that turn it on and off, as the legacy reader reads them.
    std::string on;
    std::string off;
};

/// One key the frozen legacy reader reads.
struct MutationKey {
    /// Empty for a reader that ignores sections, as an empty LegacyKey section means: the key's
    /// line is then its first key line anywhere in the file.
    std::string section;
    std::string key;
    /// A valid value other than the shipped one.
    std::string alternate;
    /// One value outside each range the legacy reader refuses or clamps.
    std::vector<std::string> out_of_range;
    bool hotkey = false;
    /// For a hotkey, the chord rows that fold into its list.
    std::vector<ChordSwitch> chords;
};

/// One corpus input.
struct IniMutation {
    std::string name;
    std::string bytes;
};

namespace detail {

struct MutationLine {
    std::string content;
    std::string ending;
};

inline std::string_view MutationTrim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return text;
}

inline char MutationLower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

inline bool MutationSameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (MutationLower(a[i]) != MutationLower(b[i])) return false;
    }
    return true;
}

inline std::string MutationSwapCase(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) c = static_cast<char>(c ^ 0x20);
    }
    return out;
}

inline bool MutationIsHeader(std::string_view content) {
    const std::string_view t = MutationTrim(content);
    return !t.empty() && t.front() == '[';
}

// The header's name, or empty for an unclosed header or an empty name, which open no section.
inline std::string MutationHeaderName(std::string_view content) {
    const std::string_view t = MutationTrim(content);
    const std::size_t close = t.find(']');
    if (close == std::string_view::npos) return {};
    return std::string(MutationTrim(t.substr(1, close - 1)));
}

// The key of a key line, or empty for any other line.
inline std::string MutationKeyOf(std::string_view content) {
    const std::string_view t = MutationTrim(content);
    if (t.empty() || t.front() == ';' || t.front() == '#' || t.front() == '[') return {};
    const std::size_t eq = t.find('=');
    if (eq == std::string_view::npos) return {};
    return std::string(MutationTrim(t.substr(0, eq)));
}

inline std::string MutationLabel(std::string_view section, std::string_view key) {
    return section.empty() ? std::string(key) : "[" + std::string(section) + "] " + std::string(key);
}

class MutationDoc {
public:
    explicit MutationDoc(std::string_view data) {
        static constexpr std::string_view kMark = "\xEF\xBB\xBF";
        if (data.substr(0, kMark.size()) == kMark) {
            bom_ = true;
            data.remove_prefix(kMark.size());
        }
        std::size_t start = 0;
        std::size_t i = 0;
        while (i < data.size()) {
            if (data[i] == '\r' || data[i] == '\n') {
                const std::size_t width = data[i] == '\r' && i + 1 < data.size() && data[i + 1] == '\n' ? 2 : 1;
                lines_.push_back({std::string(data.substr(start, i - start)), std::string(data.substr(i, width))});
                i += width;
                start = i;
            } else {
                ++i;
            }
        }
        if (start < data.size()) lines_.push_back({std::string(data.substr(start)), {}});
        std::size_t crlf = 0, lf = 0, cr = 0;
        for (const MutationLine& line : lines_) {
            if (line.ending == "\r\n") ++crlf;
            if (line.ending == "\n") ++lf;
            if (line.ending == "\r") ++cr;
        }
        dominant_ = "\r\n";
        std::size_t best = crlf;
        if (lf > best) {
            dominant_ = "\n";
            best = lf;
        }
        if (cr > best) dominant_ = "\r";
    }

    std::vector<MutationLine>& lines() { return lines_; }
    const std::vector<MutationLine>& lines() const { return lines_; }
    const std::string& dominant() const { return dominant_; }

    std::string Bytes(bool with_bom = true) const {
        std::string out = bom_ && with_bom ? "\xEF\xBB\xBF" : "";
        for (const MutationLine& line : lines_) out += line.content + line.ending;
        return out;
    }

    std::size_t Find(std::string_view section, std::string_view key) const {
        std::string current;
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            if (MutationIsHeader(lines_[i].content)) {
                current = MutationHeaderName(lines_[i].content);
                continue;
            }
            const std::string found = MutationKeyOf(lines_[i].content);
            const bool in_section = section.empty() || (!current.empty() && MutationSameName(current, section));
            if (!found.empty() && in_section && MutationSameName(found, key)) return i;
        }
        return npos;
    }

    std::size_t Require(std::string_view section, std::string_view key) const {
        const std::size_t i = Find(section, key);
        if (i == npos) throw std::logic_error(MutationLabel(section, key) + " has no line");
        return i;
    }

    std::size_t FirstHeader(std::string_view section) const {
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            if (MutationIsHeader(lines_[i].content) && MutationSameName(MutationHeaderName(lines_[i].content), section)) {
                return i;
            }
        }
        return npos;
    }

    std::size_t FirstAnyHeader() const {
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            if (MutationIsHeader(lines_[i].content)) return i;
        }
        return npos;
    }

    std::size_t BlockEnd(std::size_t header) const {
        std::size_t j = header + 1;
        while (j < lines_.size() && !MutationIsHeader(lines_[j].content)) ++j;
        return j;
    }

    std::size_t InsertPoint(std::string_view section) const {
        const std::size_t header = FirstHeader(section);
        std::size_t last = header;
        for (std::size_t j = header; j < BlockEnd(header); ++j) {
            if (!MutationTrim(lines_[j].content).empty()) last = j;
        }
        return last + 1;
    }

    // Just after the last non-blank line above the first header, or in the file when it has no
    // header; the start of the file when there is no such line.
    std::size_t TopInsertPoint() const {
        const std::size_t header = FirstAnyHeader();
        const std::size_t end = header == npos ? lines_.size() : header;
        std::size_t point = 0;
        for (std::size_t j = 0; j < end; ++j) {
            if (!MutationTrim(lines_[j].content).empty()) point = j + 1;
        }
        return point;
    }

    void Insert(std::size_t index, std::string content) {
        if (index == lines_.size() && !lines_.empty() && lines_.back().ending.empty()) {
            lines_.back().ending = dominant_;
            lines_.push_back({std::move(content), {}});
        } else {
            lines_.insert(lines_.begin() + static_cast<std::ptrdiff_t>(index), {std::move(content), dominant_});
        }
    }

    void Remove(std::size_t index) { lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(index)); }

    void AppendToSection(std::string_view section, std::string content) {
        if (section.empty()) {
            Insert(TopInsertPoint(), std::move(content));
        } else if (FirstHeader(section) == npos) {
            Insert(lines_.size(), "[" + std::string(section) + "]");
            Insert(lines_.size(), std::move(content));
        } else {
            Insert(InsertPoint(section), std::move(content));
        }
    }

    // The line up to and including its '=' and the spaces and tabs after it.
    static std::string Prefix(const std::string& content) {
        std::size_t j = content.find('=') + 1;
        while (j < content.size() && (content[j] == ' ' || content[j] == '\t')) ++j;
        return content.substr(0, j);
    }

    std::string Value(std::size_t i) const {
        const std::string& content = lines_[i].content;
        return std::string(MutationTrim(std::string_view(content).substr(content.find('=') + 1)));
    }

    void Set(std::size_t i, std::string_view value) { lines_[i].content = Prefix(lines_[i].content) + std::string(value); }

    void SetKey(std::string_view section, std::string_view key, std::string_view value) {
        const std::size_t i = Find(section, key);
        if (i == npos) {
            AppendToSection(section, std::string(key) + "=" + std::string(value));
        } else {
            Set(i, value);
        }
    }

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

private:
    bool bom_ = false;
    std::vector<MutationLine> lines_;
    std::string dominant_;
};

inline void MutationCheckText(const std::string& text, const std::string& what) {
    for (const char c : text) {
        if (c < 0x20 || c > 0x7E) throw std::invalid_argument(what + " holds a byte outside printable ASCII");
    }
}

inline void MutationCheckName(const std::string& section, const std::string& key, const std::string& what) {
    MutationCheckText(section, what + " section");
    MutationCheckText(key, what + " key");
    if (key.empty()) throw std::invalid_argument(what + " needs a key");
    if (MutationTrim(section).size() != section.size() || MutationTrim(key).size() != key.size()) {
        throw std::invalid_argument(what + " " + MutationLabel(section, key) + " starts or ends with a space");
    }
    if (section.find(']') != std::string::npos) {
        throw std::invalid_argument(what + " section " + section + " holds ']'");
    }
    if (key.find('=') != std::string::npos || key.front() == ';' || key.front() == '#' || key.front() == '[') {
        throw std::invalid_argument(what + " key " + key + " holds '=' or starts with ';', '#' or '['");
    }
}

// A section-less key's line is the first with its name in any section, so it clashes with every
// key of that name.
inline bool MutationClash(const MutationKey& a, const MutationKey& b) {
    return MutationSameName(a.key, b.key) &&
           (a.section.empty() || b.section.empty() || MutationSameName(a.section, b.section));
}

inline bool MutationNames(const LegacyKey& read, const MutationKey& k) {
    return MutationSameName(read.section, k.section) && MutationSameName(read.key, k.key);
}

inline void MutationCheckKeys(const std::vector<LegacyKey>& reads, const std::vector<MutationKey>& keys) {
    if (keys.empty()) throw std::invalid_argument("the corpus needs at least one key");
    for (std::size_t n = 0; n < keys.size(); ++n) {
        const MutationKey& k = keys[n];
        const std::string label = MutationLabel(k.section, k.key);
        MutationCheckName(k.section, k.key, "a key's");
        MutationCheckText(k.alternate, label + "'s alternate value");
        for (const std::string& value : k.out_of_range) MutationCheckText(value, label + "'s out-of-range value");
        for (const ChordSwitch& chord : k.chords) {
            MutationCheckName(chord.section, chord.key, "a chord's");
            MutationCheckText(chord.on, MutationLabel(chord.section, chord.key) + "'s on value");
            MutationCheckText(chord.off, MutationLabel(chord.section, chord.key) + "'s off value");
        }
        for (std::size_t m = 0; m < n; ++m) {
            if (MutationClash(keys[m], k)) {
                throw std::invalid_argument(label + " is listed twice, or once in a section and once without one");
            }
        }
        if (std::none_of(reads.begin(), reads.end(), [&k](const LegacyKey& r) { return MutationNames(r, k); })) {
            throw std::invalid_argument(label + " is not among the keys the import reads");
        }
    }
    for (std::size_t n = 0; n < reads.size(); ++n) {
        const LegacyKey& r = reads[n];
        const std::string label = MutationLabel(r.section, r.key);
        for (std::size_t m = 0; m < n; ++m) {
            if (MutationSameName(reads[m].section, r.section) && MutationSameName(reads[m].key, r.key)) {
                throw std::invalid_argument(label + " is read twice");
            }
        }
        if (std::none_of(keys.begin(), keys.end(), [&r](const MutationKey& k) { return MutationNames(r, k); })) {
            throw std::invalid_argument(label + " is read by the import and has no key descriptor");
        }
    }
}

// Code page 1252 at 0x80-0x9F, as Windows decodes it: the five codes it leaves undefined stay
// the same code point.
inline std::uint32_t MutationCp1252(unsigned char byte) {
    static constexpr std::uint16_t kHigh[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
    return byte >= 0x80 && byte <= 0x9F ? kHigh[byte - 0x80] : byte;
}

// Strict UTF-8 into code points. False for anything that is not.
inline bool MutationDecodeUtf8(std::string_view data, std::vector<std::uint32_t>& out) {
    for (std::size_t i = 0; i < data.size();) {
        const auto b0 = static_cast<unsigned char>(data[i]);
        std::size_t width;
        std::uint32_t cp;
        std::uint32_t min;
        if (b0 < 0x80) {
            out.push_back(b0);
            ++i;
            continue;
        } else if ((b0 & 0xE0) == 0xC0) {
            width = 2;
            cp = b0 & 0x1Fu;
            min = 0x80;
        } else if ((b0 & 0xF0) == 0xE0) {
            width = 3;
            cp = b0 & 0x0Fu;
            min = 0x800;
        } else if ((b0 & 0xF8) == 0xF0) {
            width = 4;
            cp = b0 & 0x07u;
            min = 0x10000;
        } else {
            return false;
        }
        if (i + width > data.size()) return false;
        for (std::size_t j = 1; j < width; ++j) {
            const auto b = static_cast<unsigned char>(data[i + j]);
            if ((b & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (b & 0x3Fu);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        out.push_back(cp);
        i += width;
    }
    return true;
}

// Decoded as UTF-8 when they are strict UTF-8, else as code page 1252, then written as UTF-16 LE
// after the mark FF FE.
inline std::string MutationUtf16(std::string_view data) {
    std::vector<std::uint32_t> cps;
    if (!MutationDecodeUtf8(data, cps)) {
        cps.clear();
        for (const char c : data) cps.push_back(MutationCp1252(static_cast<unsigned char>(c)));
    }
    std::string out = "\xFF\xFE";
    const auto unit = [&out](std::uint32_t u) {
        out.push_back(static_cast<char>(u & 0xFF));
        out.push_back(static_cast<char>(u >> 8));
    };
    for (const std::uint32_t cp : cps) {
        if (cp >= 0x10000) {
            unit(0xD800 + ((cp - 0x10000) >> 10));
            unit(0xDC00 + ((cp - 0x10000) & 0x3FF));
        } else {
            unit(cp);
        }
    }
    return out;
}

}  // namespace detail

/// Every corpus input design 6.2 lists, built from `base` (a legacy file's bytes) and the keys
/// the frozen reader reads, as (name, bytes) in a fixed order with names that never repeat.
/// `reads` is the import's LegacyImport::keys, and `keys` describes each of them in the order
/// the outputs follow. data/fixtures/canonical-ini/README.md defines each mutation byte for byte.
///
/// Lines end at CRLF, LF or a lone CR, and a UTF-8 mark at the start is set aside and put back.
/// A key missing from `base` is added first, set to its alternate value, at the end of its
/// section or in a new section at the end (a section-less key after the last line above the
/// first header), and every mutation starts from that file. Throws std::invalid_argument for no
/// keys, `reads` and `keys` naming different keys, a key read or listed twice, a section-less
/// key beside another key of its name, a name or value outside printable ASCII, a section or key
/// with a space at either end, a section holding ']', a key holding '=' or starting with ';', '#'
/// or '[', or a first key whose line cannot be padded to 199 characters.
inline std::vector<IniMutation> GenerateIniMutations(std::string_view base, const std::vector<LegacyKey>& reads,
                                                     const std::vector<MutationKey>& keys) {
    using detail::MutationDoc;
    detail::MutationCheckKeys(reads, keys);

    MutationDoc full(base);
    for (const MutationKey& k : keys) {
        if (full.Find(k.section, k.key) == MutationDoc::npos) full.AppendToSection(k.section, k.key + "=" + k.alternate);
    }

    std::vector<IniMutation> out;
    const auto emit = [&out](std::string name, std::string bytes) { out.push_back({std::move(name), std::move(bytes)}); };
    const auto label = [](const std::string& section, const std::string& key) { return detail::MutationLabel(section, key); };
    const std::string invalid = "abc";

    static const std::pair<const char*, std::string> kValues[] = {
        {"empty", ""},          {"space", " "},        {"\"\"", "\"\""},  {"abc", "abc"},
        {"nan", "nan"},         {"inf", "inf"},        {"-inf", "-inf"},  {"1e400", "1e400"},
        {"0,15", "0,15"},       {"0x10", "0x10"},      {"010", "010"},    {"-1", "-1"},
        {"+1", "+1"},           {"space then 1", " 1"}, {"1 then space", "1 "}, {"1abc", "1abc"},
        {"True", "True"},       {"TRUE", "TRUE"},      {"tRue", "tRue"},  {"yes", "yes"},
        {"on", "on"},           {"2", "2"},            {"1100 characters", std::string(1100, '1')},
    };
    static const char* const kHotkeyValues[] = {"0x230", "0", "End"};

    for (const MutationKey& k : keys) {
        const std::string n = label(k.section, k.key) + ": ";
        const std::size_t i = full.Require(k.section, k.key);
        const std::string value = full.Value(i);
        const std::string prefix = MutationDoc::Prefix(full.lines()[i].content);

        MutationDoc d = full;
        d.Remove(i);
        emit(n + "removed", d.Bytes());
        d = full;
        d.Insert(i, prefix + k.alternate);
        emit(n + "duplicate before, another value", d.Bytes());
        d = full;
        d.Insert(i + 1, prefix + k.alternate);
        emit(n + "duplicate after, another value", d.Bytes());
        d = full;
        d.Insert(i + 1, prefix + invalid);
        emit(n + "valid, then an invalid duplicate", d.Bytes());
        d = full;
        d.Insert(i, prefix + invalid);
        emit(n + "invalid, then a valid duplicate", d.Bytes());

        d = full;
        {
            std::string& content = d.lines()[i].content;
            const std::size_t eq = content.find('=');
            content = detail::MutationSwapCase(std::string_view(content).substr(0, eq)) + content.substr(eq);
        }
        emit(n + "key case swapped", d.Bytes());

        if (!k.section.empty()) {
            d = full;
            std::size_t header = i;
            while (!detail::MutationIsHeader(d.lines()[header].content)) --header;
            std::string& content = d.lines()[header].content;
            const std::size_t open = content.find('[');
            const std::size_t close = content.find(']', open);
            content = content.substr(0, open + 1) +
                      detail::MutationSwapCase(std::string_view(content).substr(open + 1, close - open - 1)) +
                      content.substr(close);
            emit(n + "section case swapped", d.Bytes());
        }

        d = full;
        if (k.section.empty()) {
            std::string moved = d.lines()[i].content;
            d.Remove(i);
            d.Insert(d.lines().size(), "[Elsewhere]");
            d.Insert(d.lines().size(), std::move(moved));
        } else {
            std::string moved = d.lines()[i].content;
            d.Remove(i);
            std::string target;
            for (const detail::MutationLine& line : d.lines()) {
                if (!detail::MutationIsHeader(line.content)) continue;
                const std::string name = detail::MutationHeaderName(line.content);
                if (!name.empty() && !detail::MutationSameName(name, k.section)) {
                    target = name;
                    break;
                }
            }
            if (!target.empty()) {
                d.Insert(d.InsertPoint(target), std::move(moved));
            } else {
                const char* other = detail::MutationSameName(k.section, "Elsewhere") ? "Other" : "Elsewhere";
                d.Insert(d.lines().size(), std::string("[") + other + "]");
                d.Insert(d.lines().size(), std::move(moved));
            }
        }
        emit(n + "moved to another section", d.Bytes());

        const std::size_t first_header = full.FirstAnyHeader();
        if (first_header != MutationDoc::npos && first_header < i) {
            d = full;
            std::string moved = d.lines()[i].content;
            d.Remove(i);
            d.Insert(first_header, std::move(moved));
            emit(n + "before the first header", d.Bytes());
        }

        for (const char* tail : {"; x", ";x", " # x"}) {
            d = full;
            d.lines()[i].content += tail;
            emit(n + "'" + tail + "' appended", d.Bytes());
        }
        d = full;
        d.Set(i, "\"" + value + "\"");
        emit(n + "in double quotes", d.Bytes());
        d = full;
        d.Set(i, "'" + value + "'");
        emit(n + "in single quotes", d.Bytes());

        for (const auto& [name, text] : kValues) {
            d = full;
            d.Set(i, text);
            emit(n + "value " + name, d.Bytes());
        }
        for (const std::string& text : k.out_of_range) {
            d = full;
            d.Set(i, text);
            emit(n + "out of range " + text, d.Bytes());
        }
        if (k.hotkey) {
            for (const char* text : kHotkeyValues) {
                d = full;
                d.Set(i, text);
                emit(n + "hotkey " + text, d.Bytes());
            }
            for (const ChordSwitch& chord : k.chords) {
                d = full;
                d.SetKey(chord.section, chord.key, chord.on);
                emit(n + "chord " + label(chord.section, chord.key) + " on", d.Bytes());
                d = full;
                d.SetKey(chord.section, chord.key, chord.off);
                emit(n + "chord " + label(chord.section, chord.key) + " off", d.Bytes());
            }
        }
    }

    for (const MutationKey& a : keys) {
        for (const MutationKey& b : keys) {
            if (&a == &b) continue;
            const std::string n = label(a.section, a.key) + " alternate, " + label(b.section, b.key);
            MutationDoc d = full;
            d.Set(d.Require(a.section, a.key), a.alternate);
            d.Remove(d.Require(b.section, b.key));
            emit(n + " removed", d.Bytes());
            d = full;
            d.Set(d.Require(a.section, a.key), a.alternate);
            d.Set(d.Require(b.section, b.key), invalid);
            emit(n + " invalid", d.Bytes());
        }
    }

    std::vector<std::string> sections;
    for (const detail::MutationLine& line : full.lines()) {
        if (!detail::MutationIsHeader(line.content)) continue;
        const std::string name = detail::MutationHeaderName(line.content);
        if (name.empty()) continue;
        if (std::none_of(sections.begin(), sections.end(),
                         [&name](const std::string& seen) { return detail::MutationSameName(seen, name); })) {
            sections.push_back(name);
        }
    }
    for (const std::string& s : sections) {
        const std::string n = "[" + s + "]: ";
        const std::size_t header = full.FirstHeader(s);
        const std::pair<const char*, std::string> headers[] = {
            {"header with spaces", "[ " + s + " ]"}, {"header with a comment", "[" + s + "] ; c"}, {"header not closed", "[" + s}};
        for (const auto& [name, content] : headers) {
            MutationDoc d = full;
            d.lines()[header].content = content;
            emit(n + name, d.Bytes());
        }
        MutationDoc d = full;
        const std::size_t end = d.BlockEnd(header);
        std::vector<detail::MutationLine> copy(d.lines().begin() + static_cast<std::ptrdiff_t>(header),
                                               d.lines().begin() + static_cast<std::ptrdiff_t>(end));
        if (d.lines()[end - 1].ending.empty()) d.lines()[end - 1].ending = d.dominant();
        for (detail::MutationLine& line : copy) {
            const std::string found = detail::MutationKeyOf(line.content);
            if (found.empty()) continue;
            for (const MutationKey& k : keys) {
                if ((k.section.empty() || detail::MutationSameName(k.section, s)) &&
                    detail::MutationSameName(k.key, found)) {
                    line.content = MutationDoc::Prefix(line.content) + k.alternate;
                    break;
                }
            }
        }
        d.lines().insert(d.lines().begin() + static_cast<std::ptrdiff_t>(end), copy.begin(), copy.end());
        emit(n + "section repeated", d.Bytes());
    }

    const std::string n = "file: ";
    const MutationKey& first = keys.front();
    const std::string mark = "\xEF\xBB\xBF";
    if (const std::size_t header = full.FirstAnyHeader(); header != MutationDoc::npos) {
        MutationDoc d = full;
        d.lines().erase(d.lines().begin(), d.lines().begin() + static_cast<std::ptrdiff_t>(header));
        emit(n + "UTF-8 mark before a header", mark + d.Bytes(false));
    }
    emit(n + "UTF-8 mark before a comment", mark + "; comment" + full.dominant() + full.Bytes(false));
    const std::pair<const char*, const char*> endings[] = {{"CRLF", "\r\n"}, {"LF", "\n"}, {"lone CR", "\r"}};
    for (const auto& [name, ending] : endings) {
        MutationDoc d = full;
        for (detail::MutationLine& line : d.lines()) {
            if (!line.ending.empty()) line.ending = ending;
        }
        emit(n + name, d.Bytes());
    }
    {
        MutationDoc d = full;
        std::size_t t = 0;
        for (detail::MutationLine& line : d.lines()) {
            if (!line.ending.empty()) line.ending = endings[t++ % 3].second;
        }
        emit(n + "mixed line endings", d.Bytes());
    }
    {
        MutationDoc d = full;
        while (!d.lines().empty() && d.lines().back().content.empty()) d.lines().pop_back();
        if (!d.lines().empty()) d.lines().back().ending.clear();
        emit(n + "no final newline", d.Bytes());
    }
    for (const std::size_t width : {std::size_t{199}, std::size_t{200}, std::size_t{254}, std::size_t{255}}) {
        MutationDoc d = full;
        for (detail::MutationLine& line : d.lines()) {
            if (!line.ending.empty()) line.ending = "\r\n";
        }
        const std::size_t i = d.Require(first.section, first.key);
        const std::string key = detail::MutationKeyOf(d.lines()[i].content);
        const std::string value = d.Value(i);
        if (key.size() + 1 + value.size() > width) {
            throw std::invalid_argument(label(first.section, first.key) + "=" + value + " is longer than " +
                                        std::to_string(width) + " characters");
        }
        d.lines()[i].content = key + "=" + std::string(width - key.size() - 1 - value.size(), ' ') + value;
        emit(n + "CRLF, a " + std::to_string(width) + "-character line", d.Bytes());
    }
    const std::pair<const char*, std::string> bytes[] = {{"0x1A byte", "\x1A"}, {"NUL byte", std::string(1, '\0')}};
    for (const auto& [name, content] : bytes) {
        MutationDoc d = full;
        d.Insert(d.Require(first.section, first.key) + 1, content);
        emit(n + name, d.Bytes());
    }
    emit(n + "trailing NUL padding", full.Bytes() + std::string(64, '\0'));
    {
        MutationDoc d = full;
        for (detail::MutationLine& line : d.lines()) {
            if (detail::MutationKeyOf(line.content).empty()) continue;
            const std::size_t eq = line.content.find('=');
            std::string_view before = std::string_view(line.content).substr(0, eq);
            std::string_view after = std::string_view(line.content).substr(eq + 1);
            while (!before.empty() && (before.back() == ' ' || before.back() == '\t')) before.remove_suffix(1);
            while (!after.empty() && (after.front() == ' ' || after.front() == '\t')) after.remove_prefix(1);
            line.content = std::string(before) + "\t=\t" + std::string(after);
        }
        emit(n + "tab separators", d.Bytes());
    }
    for (const char* comment : {"#", ";"}) {
        MutationDoc d = full;
        for (const MutationKey& k : keys) d.Insert(d.Require(k.section, k.key) + 1, comment + k.key + "=" + k.alternate);
        emit(n + comment + "Key= lines", d.Bytes());
    }
    {
        MutationDoc d = full;
        for (const MutationKey& k : keys) d.Insert(d.Require(k.section, k.key) + 1, "    " + k.alternate);
        emit(n + "indented continuation lines", d.Bytes());
    }
    emit(n + "UTF-16 LE with a mark", detail::MutationUtf16(full.Bytes(false)));
    {
        MutationDoc d = full;
        d.Insert(d.Require(first.section, first.key), "; caf\xE9");
        emit(n + "cp1252 byte in a comment", d.Bytes());
    }
    {
        MutationDoc d = full;
        for (const MutationKey& k : keys) d.lines()[d.Require(k.section, k.key)].content += "\xE9";
        emit(n + "cp1252 byte in a value", d.Bytes());
    }
    return out;
}

}  // namespace cameraunlock::config::testing
