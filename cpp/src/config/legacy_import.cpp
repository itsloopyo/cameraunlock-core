#include <cameraunlock/config/legacy_import.h>
#include <cameraunlock/config/value_codecs.h>
#include <cameraunlock/config/value_guards.h>
#include <cameraunlock/input/key_bindings.h>

namespace cameraunlock::config {

namespace {

constexpr long long kMinCode = 0x01;
constexpr long long kMaxCode = 0xFE;

const char* DropReason(DropRule rule) {
    switch (rule) {
        case DropRule::NonFiniteNumber:
            return "it is not a finite number, so the default is used";
        case DropRule::PoseShaping:
            return "sensitivity, scales, deadzones, response curves and axis inversion are set in the tracker now, "
                   "not in this mod";
        case DropRule::Reticle:
            return "this mod no longer draws or toggles a reticle";
        case DropRule::FollowsDefault:
            return "this setting now follows the mod's default";
        case DropRule::KeyCodeOutOfRange:
            return "it is not a key code from 0x01 to 0xFE, so the action is unbound";
        case DropRule::ModifierKey:
            return "it is a Ctrl, Shift or Alt key, which fires at the start of every Ctrl+Shift chord, so it is "
                   "unbound";
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

std::string PoseShapingText(bool value) { return BoolCodec().Render(value); }

template <class F>
std::string PoseShapingText(F value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
    return FloatingCodec<F>().Render(value);
}

template <class T>
void RecordPoseShaping(T value, T shipped, const std::string& section, const std::string& key,
                       std::vector<PoseShapingValue>& pose_shaping, std::vector<DroppedValue>& dropped) {
    if constexpr (!std::is_same_v<T, bool>) {
        if (!std::isfinite(shipped)) {
            throw std::invalid_argument("[" + section + "] " + key + ": the shipped value is not finite");
        }
    }
    const bool folded = value == shipped;
    pose_shaping.push_back({section, key, PoseShapingText(value), PoseShapingText(shipped), folded});
    if (!folded) dropped.push_back({DropRule::PoseShaping, section, key, PoseShapingText(value)});
}

}  // namespace

std::string DescribeDroppedValue(const DroppedValue& dropped) {
    const char* reason = DropReason(dropped.rule);
    return "not carried: [" + dropped.section + "] " + dropped.key + "=" + dropped.value + ", " + reason;
}

std::string LegacyVirtualKeyToBindings(long long code) {
    if (code < kMinCode || code > kMaxCode || !IsBindableVirtualKey(static_cast<int>(code))) return {};
    return input::FormatKeyBindings({{input::KeyModifiers::kNone, static_cast<int>(code)}});
}

std::string LegacyVirtualKeyToBindings(long long code, const std::string& section, const std::string& key,
                                       std::vector<DroppedValue>& dropped) {
    if (code != 0 && (code < kMinCode || code > kMaxCode)) {
        dropped.push_back({DropRule::KeyCodeOutOfRange, section, key, CodeText(code)});
    } else if (code != 0 && !IsBindableVirtualKey(static_cast<int>(code))) {
        dropped.push_back({DropRule::ModifierKey, section, key, CodeText(code)});
    }
    return LegacyVirtualKeyToBindings(code);
}

void LegacyPoseShaping(bool value, bool shipped, const std::string& section, const std::string& key,
                       std::vector<PoseShapingValue>& pose_shaping, std::vector<DroppedValue>& dropped) {
    RecordPoseShaping(value, shipped, section, key, pose_shaping, dropped);
}

void LegacyPoseShaping(float value, float shipped, const std::string& section, const std::string& key,
                       std::vector<PoseShapingValue>& pose_shaping, std::vector<DroppedValue>& dropped) {
    RecordPoseShaping(value, shipped, section, key, pose_shaping, dropped);
}

void LegacyPoseShaping(double value, double shipped, const std::string& section, const std::string& key,
                       std::vector<PoseShapingValue>& pose_shaping, std::vector<DroppedValue>& dropped) {
    RecordPoseShaping(value, shipped, section, key, pose_shaping, dropped);
}

}  // namespace cameraunlock::config
