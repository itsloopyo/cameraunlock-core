// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <cameraunlock/re_engine/discovery.h>

// via.Application's entries: the stages the engine runs each frame, in order
// ("LateUpdateBehavior", "BeginRendering", "EndRendering"...), each a
// description and a function pointer the engine calls through. Where the list
// starts, how far apart its entries are and where each keeps its function are
// read off the running game (discovery::FindEntryList).
//
// A hook here swaps the entry's pointer for one of ours that calls whatever was
// there before: the game's own function, or REFramework's stub when it hooked
// the entry first. REFramework reads the pointer once, when it hooks, so if it
// hooks after us it calls ours, and ours calls the game. Nothing in the
// executable's code is changed.
namespace cameraunlock::re_engine {

using StageCallback = void (*)();

struct StageHook {
    std::string stage;
    StageCallback before = nullptr;
    StageCallback after = nullptr;
};

class AppEntries {
public:
    // Finds the list inside via.Application. Throws NotReady while the engine
    // has not made it, while the list does not come out as one answer, and until
    // two reads a retry apart agree.
    static void Discover();
    static AppEntries& Get();

    // Runs each hook's `before` and `after` around its stage, on the thread that
    // runs it; a stage hooked again runs the newer pair outside the older one.
    // Every stage is looked up and a slot found for each before any is hooked,
    // so a refusal hooks none. Throws when a stage is not named exactly once or
    // the slots run out.
    void Hook(const std::vector<StageHook>& hooks);

    // Every stage's name, in the order the engine runs them.
    std::vector<std::string> StageNames() const;

private:
    const char* Description(std::size_t i) const;
    void** Function(std::size_t i) const;

    std::uint8_t* app_ = nullptr;
    discovery::EntryList list_;
};

}  // namespace cameraunlock::re_engine
