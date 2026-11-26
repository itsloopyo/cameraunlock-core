#pragma once

// Not installed and not API: the ASCII byte rules the config and key binding sources share.
// detail::EqualsAsciiIgnoreCase, which folds as FoldAscii does, is in value_codecs.h.

#include <cstddef>
#include <string>
#include <string_view>

namespace cameraunlock::config::detail {

inline bool IsSpaceOrTab(char c) { return c == ' ' || c == '\t'; }

inline char FoldAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

inline std::string_view TrimSpaceOrTab(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && IsSpaceOrTab(text[begin])) ++begin;
    while (end > begin && IsSpaceOrTab(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

/// The canonical reader's 1-based line of `offset`: CRLF, LF and a lone CR each end a line.
inline int CanonicalLineOfOffset(std::string_view bytes, std::size_t offset) {
    int line = 1;
    for (std::size_t i = 0; i < offset; ++i) {
        if (bytes[i] == '\n' || (bytes[i] == '\r' && (i + 1 >= bytes.size() || bytes[i + 1] != '\n'))) {
            ++line;
        }
    }
    return line;
}

inline std::string Quote(std::string_view text) { return "'" + std::string(text) + "'"; }

/// "0x" and two capital hex digits.
inline std::string HexByte(unsigned char byte) {
    static const char kDigits[] = "0123456789ABCDEF";
    std::string text = "0x";
    text += kDigits[byte >> 4];
    text += kDigits[byte & 0x0F];
    return text;
}

}  // namespace cameraunlock::config::detail
