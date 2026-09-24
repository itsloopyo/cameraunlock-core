#include <cameraunlock/config/legacy_import.h>

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

}  // namespace

std::string DescribeDroppedValue(const DroppedValue& dropped) {
    const char* reason = DropReason(dropped.rule);
    return "not carried: [" + dropped.section + "] " + dropped.key + "=" + dropped.value + ", " + reason;
}

}  // namespace cameraunlock::config
