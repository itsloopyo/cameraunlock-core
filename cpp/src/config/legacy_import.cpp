#include <cameraunlock/config/legacy_import.h>
#include <cameraunlock/config/value_codecs.h>

namespace cameraunlock::config {

namespace {

const char* DropReason(DropRule rule) {
    switch (rule) {
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
