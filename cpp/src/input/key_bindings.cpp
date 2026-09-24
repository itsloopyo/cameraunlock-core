#include <cameraunlock/input/key_bindings.h>
#include <cameraunlock/input/key_names.g.h>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace cameraunlock::input {

namespace {

constexpr int kMinCode = 0x01;
constexpr int kMaxCode = 0xFE;
static_assert(kKeyModifierCount == 3, "KeyModifiers declares Ctrl, Shift and Alt as the flags 1, 2 and 4");
constexpr unsigned kAllModifiers = (1u << kKeyModifierCount) - 1;

std::string_view Trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    return text.substr(begin, end - begin);
}

char FoldAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool EqualsAsciiIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (FoldAscii(a[i]) != FoldAscii(b[i])) return false;
    }
    return true;
}

std::vector<std::string_view> Split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t at = text.find(separator, start);
        if (at == std::string_view::npos) {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, at - start));
        start = at + 1;
    }
}

std::string Quote(std::string_view text) {
    return "'" + std::string(text) + "'";
}

// The index into kKeyModifiers of the modifier a token names, or -1. Its flag is 1 << index.
int ModifierIndex(std::string_view token) {
    for (std::size_t i = 0; i < kKeyModifierCount; ++i) {
        if (EqualsAsciiIgnoreCase(token, kKeyModifiers[i].name)) return static_cast<int>(i);
    }
    return -1;
}

const KeyNameEntry* KeyNamed(std::string_view token) {
    for (std::size_t i = 0; i < kKeyNameCount; ++i) {
        if (EqualsAsciiIgnoreCase(token, kKeyNames[i].name)) return &kKeyNames[i];
    }
    for (std::size_t i = 0; i < kKeyNameAliasCount; ++i) {
        if (EqualsAsciiIgnoreCase(token, kKeyNameAliases[i].alias)) return &kKeyNames[kKeyNameAliases[i].key];
    }
    return nullptr;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// The code of one key token, or 0 with `error` set.
int ReadKey(std::string_view token, std::string& error) {
    if (token.size() >= 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
        const std::string_view digits = token.substr(2);
        int code = 0;
        bool valid = !digits.empty() && digits.size() <= 2;
        for (char c : digits) {
            const int digit = HexDigit(c);
            if (digit < 0) valid = false;
            code = code * 16 + (digit < 0 ? 0 : digit);
        }
        if (!valid || code < kMinCode || code > kMaxCode) {
            error = Quote(token) + " is not a code from 0x01 to 0xFE: expected 0x and one or two hex digits";
            return 0;
        }
        return code;
    }

    const KeyNameEntry* key = KeyNamed(token);
    if (key == nullptr) {
        error = Quote(token) + " is not a key name: expected a name such as End, F9 or A, "
                "or a code from 0x01 to 0xFE";
        return 0;
    }
    if (key->vk == 0) {
        error = Quote(token) + " has no Windows key code: expected a keyboard key name, "
                "or a code from 0x01 to 0xFE";
        return 0;
    }
    return key->vk;
}

KeyBindingsParseResult Fail(std::string error) {
    KeyBindingsParseResult result;
    result.error = std::move(error);
    return result;
}

}  // namespace

KeyBindingsParseResult ParseKeyBindings(std::string_view text) {
    KeyBindingsParseResult result;
    if (Trim(text).empty()) return result;

    const std::vector<std::string_view> items = Split(text, ',');
    for (std::size_t n = 0; n < items.size(); ++n) {
        const std::string_view item = Trim(items[n]);
        if (item.empty()) {
            return Fail("item " + std::to_string(n + 1) + " of " + Quote(Trim(text)) +
                        " is empty: expected a key such as End or Ctrl+Shift+Y between commas");
        }

        const std::vector<std::string_view> tokens = Split(item, '+');
        KeyBinding binding;
        for (std::size_t t = 0; t + 1 < tokens.size(); ++t) {
            const int index = ModifierIndex(Trim(tokens[t]));
            if (index < 0) {
                return Fail(Quote(item) + ": expected Ctrl, Shift or Alt before each '+' and one key after the last");
            }
            const auto modifier = static_cast<KeyModifiers>(1u << index);
            if (HasModifiers(binding.modifiers, modifier)) {
                return Fail(Quote(item) + " names " + kKeyModifiers[index].name +
                            " twice: expected each modifier at most once");
            }
            binding.modifiers = binding.modifiers | modifier;
        }

        const std::string_view key = Trim(tokens.back());
        if (key.empty()) {
            return Fail(Quote(item) + ": expected Ctrl, Shift or Alt before each '+' and one key after the last");
        }
        if (ModifierIndex(key) >= 0) {
            return Fail(Quote(item) + " has no key: expected a key after the modifiers");
        }
        std::string error;
        binding.vk = ReadKey(key, error);
        if (binding.vk == 0) return Fail(error);

        if (std::find(result.bindings.begin(), result.bindings.end(), binding) != result.bindings.end()) {
            return Fail(Quote(item) + " is listed twice: expected each binding once");
        }
        result.bindings.push_back(binding);
    }
    return result;
}

std::string FormatVirtualKey(int vk) {
    if (vk < kMinCode || vk > kMaxCode) {
        throw std::invalid_argument("virtual-key code " + std::to_string(vk) + " is outside 0x01-0xFE");
    }
    for (std::size_t i = 0; i < kKeyNameCount; ++i) {
        if (kKeyNames[i].vk == vk) return kKeyNames[i].name;
    }
    static const char kHex[] = "0123456789ABCDEF";
    std::string text = "0x";
    if (vk >= 0x10) text.push_back(kHex[vk >> 4]);
    text.push_back(kHex[vk & 0xF]);
    return text;
}

std::string FormatKeyBindings(const std::vector<KeyBinding>& bindings) {
    std::string text;
    for (std::size_t n = 0; n < bindings.size(); ++n) {
        const KeyBinding& binding = bindings[n];
        const unsigned modifiers = static_cast<unsigned>(binding.modifiers);
        if ((modifiers & ~kAllModifiers) != 0) {
            throw std::invalid_argument("modifier value " + std::to_string(modifiers) + " is not a set of KeyModifiers");
        }
        if (std::find(bindings.begin(), bindings.begin() + static_cast<std::ptrdiff_t>(n), binding) !=
            bindings.begin() + static_cast<std::ptrdiff_t>(n)) {
            throw std::invalid_argument("binding " + std::to_string(n + 1) + " repeats an earlier one");
        }

        if (n > 0) text += ", ";
        for (std::size_t i = 0; i < kKeyModifierCount; ++i) {
            if ((modifiers & (1u << i)) != 0) {
                text += kKeyModifiers[i].name;
                text.push_back('+');
            }
        }
        text += FormatVirtualKey(binding.vk);
    }
    return text;
}

}  // namespace cameraunlock::input
