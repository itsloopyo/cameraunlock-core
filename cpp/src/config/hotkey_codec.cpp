#include "cameraunlock/config/hotkey_codec.h"

#include "cameraunlock/input/key_bindings.h"

#include <stdexcept>

namespace cameraunlock::config {

CodecParseResult<std::string> HotkeyCodec::Parse(std::string_view text) const {
    CodecParseResult<std::string> result;
    const input::KeyBindingsParseResult read = input::ParseKeyBindings(text);
    if (!read.ok()) {
        result.error = read.error;
        return result;
    }
    result.value = input::FormatKeyBindings(read.bindings);
    return result;
}

std::string HotkeyCodec::Render(const std::string& value) const {
    const input::KeyBindingsParseResult read = input::ParseKeyBindings(value);
    if (!read.ok()) throw std::invalid_argument("'" + value + "' is not a hotkey list: " + read.error);
    const std::string canonical = input::FormatKeyBindings(read.bindings);
    if (canonical != value) {
        throw std::invalid_argument("'" + value + "' is not written as a hotkey list is written: expected '" +
                                    canonical + "'");
    }
    return canonical;
}

}  // namespace cameraunlock::config
