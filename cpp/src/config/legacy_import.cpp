#include <cameraunlock/config/legacy_import.h>
#include <cameraunlock/input/key_bindings.h>

namespace cameraunlock::config {

namespace {

constexpr long long kMinCode = 0x01;
constexpr long long kMaxCode = 0xFE;

const char* DropReason(DropRule rule) {
    switch (rule) {
        case DropRule::KeyCodeOutOfRange:
            return "it is not a key code from 0x01 to 0xFE, so the action is unbound";
        case DropRule::NonFiniteNumber:
            return "it is not a finite number, so the default is used";
        case DropRule::PoseShaping:
            return "sensitivity, deadzones, response curves and axis inversion are set in the tracker now, not in "
                   "this mod";
        case DropRule::Reticle:
            return "this mod no longer draws or toggles a reticle";
        case DropRule::FollowsDefault:
            return "this setting now follows the mod's default";
    }
    throw std::invalid_argument("drop rule " + std::to_string(static_cast<int>(rule)) + " is not a DropRule");
}

std::string CodeText(long long code) {
    if (code < 0) return std::to_string(code);
    static const char kHex[] = "0123456789ABCDEF";
    std::string digits;
    unsigned long long rest = static_cast<unsigned long long>(code);
    do {
        digits.insert(digits.begin(), kHex[rest & 0xF]);
        rest >>= 4;
    } while (rest != 0);
    return "0x" + digits;
}

}  // namespace

std::string DescribeDroppedValue(const DroppedValue& dropped) {
    const char* reason = DropReason(dropped.rule);
    return "not carried: [" + dropped.section + "] " + dropped.key + "=" + dropped.value + ", " + reason;
}

std::string LegacyVirtualKeyToBindings(long long code) {
    if (code < kMinCode || code > kMaxCode) return {};
    return input::FormatVirtualKey(static_cast<int>(code));
}

std::string LegacyVirtualKeyToBindings(long long code, const std::string& section, const std::string& key,
                                       std::vector<DroppedValue>& dropped) {
    if (code != 0 && (code < kMinCode || code > kMaxCode)) {
        dropped.push_back({DropRule::KeyCodeOutOfRange, section, key, CodeText(code)});
    }
    return LegacyVirtualKeyToBindings(code);
}

}  // namespace cameraunlock::config
