#pragma once

#include <cameraunlock/config/value_codecs.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cameraunlock::config {

/// A list of player numbers from 1 to 8, held as its canonical text: `2, 3`, or empty for none.
/// Parse reads an empty value, or one of only spaces and tabs, as the empty list. Otherwise it
/// splits the value at `,` and trims each item of spaces and tabs; an item is one or more ASCII
/// digits for a number from 1 to 8, and a number given twice is refused. The list keeps the order
/// written, and its canonical text is the numbers joined by `, `, so `3,02` reads as `3, 2`. The
/// C# twin is CameraUnlock.Core.Config.PlayerListCodec.
class PlayerListCodec {
public:
    using Value = std::string;

    CodecParseResult<std::string> Parse(std::string_view text) const {
        CodecParseResult<std::string> result;
        std::vector<int> players;
        std::size_t start = 0;
        const bool blank = text.find_first_not_of(" \t") == std::string_view::npos;
        while (!blank) {
            const std::size_t comma = text.find(',', start);
            std::string_view item =
                text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
            const std::size_t first = item.find_first_not_of(" \t");
            item = first == std::string_view::npos
                       ? std::string_view()
                       : item.substr(first, item.find_last_not_of(" \t") - first + 1);

            int number = 0;
            bool digits = !item.empty();
            for (const char c : item) {
                if (c < '0' || c > '9') {
                    digits = false;
                    break;
                }
                // Held at 9 once past 8, so a long run of digits cannot overflow.
                number = number * 10 + (c - '0');
                if (number > 8) number = 9;
            }
            if (!digits || number < 1 || number > 8) {
                result.error = "'" + std::string(item) + "' is not a player number from 1 to 8";
                return result;
            }
            for (const int earlier : players) {
                if (earlier == number) {
                    result.error = "player " + std::to_string(number) + " is listed twice";
                    return result;
                }
            }
            players.push_back(number);
            if (comma == std::string_view::npos) break;
            start = comma + 1;
        }
        for (std::size_t i = 0; i < players.size(); ++i) {
            if (i != 0) result.value += ", ";
            result.value += std::to_string(players[i]);
        }
        return result;
    }

    /// Throws std::invalid_argument for a value that is not the canonical text of a player list,
    /// which would not read back as itself.
    std::string Render(const std::string& value) const {
        const CodecParseResult<std::string> read = Parse(value);
        if (!read.ok()) throw std::invalid_argument("'" + value + "' is not a player list: " + read.error);
        if (read.value != value) {
            throw std::invalid_argument("'" + value + "' is not written as a player list is written: expected '" +
                                        read.value + "'");
        }
        return value;
    }

    /// The canonical texts of two lists are equal exactly when the lists are.
    bool Equal(const std::string& a, const std::string& b) const { return a == b; }
};

}  // namespace cameraunlock::config
