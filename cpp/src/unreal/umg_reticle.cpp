#include <cameraunlock/unreal/umg_reticle.h>

#include <string>

namespace cameraunlock::unreal {

namespace {

// The engine's prefix for a class default object. Its widget tree is a template
// that is never drawn, so moving one is a silent no-op that looks like success.
constexpr const char* kCdoPrefix = "Default__";

bool IsCdo(const std::string& name) {
    return name.compare(0, std::char_traits<char>::length(kCdoPrefix), kCdoPrefix) == 0;
}

// Walk outward looking for a class name. Bounded because a corrupt or
// mid-destruction object can present a cycle, and this runs over every live
// UObject.
constexpr int kMaxOuterDepth = 8;

bool AncestorClassContains(std::uintptr_t obj, const char* wanted) {
    std::uintptr_t cur = OuterObject(obj);
    for (int depth = 0; depth < kMaxOuterDepth && cur != 0; ++depth) {
        if (ContainsCI(ClassName(cur), wanted)) {
            // The instance, not the template it was cloned from.
            return !IsCdo(ObjectName(cur));
        }
        cur = OuterObject(cur);
    }
    return false;
}

}  // namespace

std::uintptr_t FindWidgetInstance(const char* widgetClass, const char* widgetName,
                                  const char* ownerClassContains) {
    if (!widgetClass || !widgetName || !ownerClassContains) return 0;

    std::uintptr_t found = 0;
    ForEachUObject([&](std::uintptr_t obj) {
        if (!EqualsCI(ClassName(obj), widgetClass)) return false;
        const std::string name = ObjectName(obj);
        if (!EqualsCI(name, widgetName) || IsCdo(name)) return false;
        if (!AncestorClassContains(obj, ownerClassContains)) return false;
        found = obj;
        return true;
    });
    return found;
}

}  // namespace cameraunlock::unreal
