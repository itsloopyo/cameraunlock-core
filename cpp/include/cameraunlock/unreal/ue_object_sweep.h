// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <cameraunlock/unreal/ue_objects.h>
#include <cameraunlock/unreal/ue_reflection.h>

// What the features that look for their objects in the object array share: the
// pace of a sweep, the flags a template object carries, whether a class is one
// a consumer names, and whether an object found earlier is still the one in
// its slot.
namespace cameraunlock::unreal {

// The object array is read this many slots a frame, so no frame pays for all
// of it, and a finished pass is begun again after kSweepAgainMs. An object
// made since (a crosshair for a newly equipped item) is found within about
// that long.
inline constexpr std::int32_t kSweepSlice = 16384;
inline constexpr std::uint64_t kSweepAgainMs = 1000;

// EObjectFlags' RF_WasLoaded. Not read off the objects as RF_ClassDefaultObject
// and RF_ArchetypeObject are (FindObjectHeader): every object that has it has
// RF_LoadCompleted (0x100000) too, so nothing in a running game tells the two apart.
inline constexpr std::uint32_t kWasLoaded = 0x80000;

// Whether `cls` or a class above it has a name one of `patterns` matches.
inline bool ClassMatches(const Objects& o, const Reflection& refl, std::uintptr_t cls,
                         const std::vector<std::string>& patterns) {
    for (std::uintptr_t c = cls; c; c = refl.Super(c)) {
        const std::string name = o.Name(c);
        for (const std::string& pattern : patterns)
            if (NameMatches(pattern, name)) return true;
    }
    return false;
}

// Whether `object` is still the object in slot `index`, the slot it stated
// when it was found: a destroyed object's slot is freed or given to another.
inline bool Live(const Objects& o, std::uintptr_t object, std::int32_t index) {
    return o.mem->Or0<std::int32_t>(object + kUObjectIndex) == index && ObjectAt(*o.mem, o.array, index) == object;
}

}  // namespace cameraunlock::unreal
