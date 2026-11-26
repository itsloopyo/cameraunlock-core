// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <cameraunlock/memory/memory_view.h>
#include <cameraunlock/unreal/ue_objects.h>
#include <cameraunlock/unreal/ue_reflection.h>

// Unreal Engine 4 and 5 reflection found in a running game with no per-build
// data: the name table, the object array, the UObject header, the reflection
// layout and UObject::ProcessEvent, each read off what the engine holds. This
// replaces the per-build RVAs and offsets ue_runtime.h's SetRuntime takes.
// 64-bit builds only.
namespace cameraunlock::unreal {

// UObject::ProcessEvent: the UObject virtual that tests a UFunction's
// FunctionFlags for FUNC_Native. Returns its address, or 0 with `why`;
// `notLoaded` is set when an object it reads is not created yet.
std::uintptr_t FindProcessEvent(const Reflection& refl, const memory::CodeMap& code, const memory::ModuleMap& module,
                                std::string& why, bool& notLoaded);

// Everything DiscoverReflection finds.
struct ReflectionDiscovery {
    Objects objects;
    ReflectionLayout layout;
    std::uintptr_t processEvent = 0;
};

enum class ReflectionStage { Names, Objects, Header, Types, Reflection, ProcessEvent, Done };

struct ReflectionDiscoveryResult {
    std::optional<ReflectionDiscovery> found;
    ReflectionStage reached = ReflectionStage::Names;
    std::string why;
    // The stage failed on an object the engine has not created yet, so asking
    // again once it has started further may succeed.
    bool notLoaded = false;
    // The objects, once the array, names and header were found, for diagnostics after a later stage fails.
    std::optional<Objects> partial;
    // Milliseconds each completed stage took, in order.
    std::vector<std::pair<ReflectionStage, long long>> timings;
};

const char* StageName(ReflectionStage stage);

// Runs every probe in order and stops at the first that fails. A failure before
// Types usually means the engine has not finished starting, and is worth asking
// again; one after it is a fact about this build. `stageDone` hears each stage
// as it completes, with the milliseconds it took. The result's Objects point
// at `mem`, which must outlive them; a Reflection or UmgCrosshairMover built
// on them holds them by reference, so the result must stay where it is.
ReflectionDiscoveryResult DiscoverReflection(const memory::Memory& mem, const memory::ModuleMap& module,
                                             const memory::CodeMap& code,
                                             const std::function<void(ReflectionStage, long long)>& stageDone = {});

// One line per value, offsets as RVAs from the module base.
std::vector<std::string> Describe(const ReflectionDiscovery& d, const memory::ModuleMap& module);

}  // namespace cameraunlock::unreal
