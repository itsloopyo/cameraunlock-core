#pragma once

// Not installed and not API: the canonical reader's line rules, for the config owner's
// migration log.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cameraunlock::config::detail {

struct CanonicalKeyLine {
    /// Empty for a key above the first header or below one that names no section.
    std::optional<std::string> section;
    std::string key;
    std::string value;
    /// 1-based.
    int line = 0;
};

/// Every key line in document order under ParseCanonicalIni's line, header and key rules,
/// repeats and keys outside a section included. Bytes are not checked, so a NUL is an
/// ordinary byte here; a UTF-16 document gives nothing useful and is the caller's to skip.
std::vector<CanonicalKeyLine> CanonicalKeyLines(std::string_view bytes);

/// True when the bytes start with a UTF-16 byte order mark, FF FE or FE FF.
bool StartsWithUtf16Mark(std::string_view bytes);

}  // namespace cameraunlock::config::detail
