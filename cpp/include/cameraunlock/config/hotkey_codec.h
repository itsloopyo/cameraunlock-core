#pragma once

#include <cameraunlock/config/value_codecs.h>

#include <string>
#include <string_view>

namespace cameraunlock::config {

/// A hotkey list as native mods read it (input::ParseKeyBindings), held as its canonical
/// text: `End, Ctrl+Shift+Y`, or empty for unbound. Parse reads every spelling
/// ParseKeyBindings reads and gives the canonical text of what it read, so
/// `ctrl+shift+y,end` reads as `Ctrl+Shift+Y, End` and `0x23` as `End`. The C# twin,
/// CameraUnlock.Core.Config.HotkeyCodec, reads the Unity dialect of the same syntax.
class HotkeyCodec {
public:
    using Value = std::string;

    CodecParseResult<std::string> Parse(std::string_view text) const;

    /// Throws std::invalid_argument for a value that is not the canonical text of a hotkey
    /// list, which would not read back as itself.
    std::string Render(const std::string& value) const;

    bool Equal(const std::string& a, const std::string& b) const { return a == b; }
};

}  // namespace cameraunlock::config
