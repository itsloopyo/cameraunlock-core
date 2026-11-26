// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/re_engine/app_entries.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <cameraunlock/logging/file_log.h>

#include <cameraunlock/re_engine/exe.h>
#include <cameraunlock/re_engine/tdb.h>

namespace cameraunlock::re_engine {
namespace {

std::optional<AppEntries> g_entries;
// The list as the last discovery attempt read it.
std::optional<discovery::EntryList> g_lastRead;

// The printable text a pointer stored at `at` points at, or empty.
std::string TextAt(std::uintptr_t at) {
    const auto text = Read<std::uintptr_t>(at);
    if (!text || !*text) return {};
    char buf[64] = {};
    for (std::size_t i = 0; i + 1 < sizeof(buf); ++i) {
        const auto c = Read<char>(*text + i);
        if (!c) return {};
        if (*c == 0) return buf;
        if (*c < 0x20 || *c > 0x7e) return {};
        buf[i] = *c;
    }
    return {};
}

// Whether the pointer stored at `at` points at committed executable memory.
bool CodeAt(std::uintptr_t at) {
    const auto p = Read<std::uintptr_t>(at);
    if (!p || !*p) return false;
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(reinterpret_cast<void*>(*p), &m, sizeof(m))) return false;
    constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return m.State == MEM_COMMIT && (m.Protect & kExecutable);
}

struct Slot {
    void** func = nullptr;
    const char* description = nullptr;
    void (*original)(void*) = nullptr;
    StageCallback before = nullptr;
    StageCallback after = nullptr;
};

constexpr std::size_t kSlots = 12;
std::array<Slot, kSlots> g_slots;

// An exception must not unwind through the engine's frames: the callback that
// threw is logged and taken off, and the stage runs on without it.
void Run(Slot& slot, StageCallback& callback) {
    try {
        callback();
    } catch (const std::exception& e) {
        logging::Line("FATAL: a %s hook threw, so it is taken off: %s", slot.description, e.what());
        callback = nullptr;
    }
}

template <std::size_t N>
void Stage(void* entry) {
    Slot& slot = g_slots[N];
    if (slot.before) Run(slot, slot.before);
    slot.original(entry);
    if (slot.after) Run(slot, slot.after);
}

template <std::size_t... N>
constexpr std::array<void (*)(void*), kSlots> MakeStages(std::index_sequence<N...>) {
    return {&Stage<N>...};
}
constexpr auto kStages = MakeStages(std::make_index_sequence<kSlots>{});

std::string Describe(const discovery::EntryList& list) {
    char text[128];
    std::snprintf(text, sizeof(text), "%zu stages 0x%zx apart from +0x%zx, the function at %s0x%llx", list.count, list.stride, list.first,
                  list.func < 0 ? "-" : "+", static_cast<unsigned long long>(list.func < 0 ? -list.func : list.func));
    return text;
}

// The global that holds via.Application's instance: the one its static
// methods read it from (hasInstance, get_DeltaTime and the other getters load
// it first thing). Code does not change, so it is found once.
std::uintptr_t InstanceGlobal(const Tdb& tdb, const Type& type) {
    constexpr std::size_t kLook = 0x40;
    std::vector<std::vector<std::uintptr_t>> reads;
    for (const Method& m : tdb.Methods(type)) {
        if (!m.isStatic || !m.fn) continue;
        std::uint8_t code[kLook + discovery::kReadAhead] = {};
        const auto at = reinterpret_cast<std::uintptr_t>(m.fn);
        if (!SafeRead(at, code, sizeof code)) throw std::runtime_error(std::string("via.Application.") + m.name + "'s code does not read");
        std::vector<std::uintptr_t> mine;
        for (const std::uintptr_t global : discovery::PointerReads(code, kLook, at))
            if (Game().Contains(global)) mine.push_back(global);
        reads.push_back(std::move(mine));
    }
    const discovery::InstanceGlobal picked = discovery::PickInstanceGlobal(reads);
    logging::Line("entries: via.Application's instance is read from exe+0x%llx by %d of the %d static methods that read a global",
                  static_cast<unsigned long long>(picked.global - Game().base), picked.readers, picked.reading);
    return picked.global;
}

}  // namespace

void AppEntries::Discover() {
    const Tdb& tdb = Tdb::Get();
    const Type* type = tdb.FindType("via.Application");
    if (!type) throw std::runtime_error("the type database has no via.Application");
    static const std::uintptr_t global = InstanceGlobal(tdb, *type);
    const auto held = Read<void*>(global);
    if (!held) throw std::runtime_error("via.Application's instance global at " + discovery::Hex(global) + " does not read");
    void* app = *held;
    if (!app) {
        g_lastRead.reset();
        throw NotReady("via.Application has no instance yet");
    }
    const auto base = reinterpret_cast<std::uintptr_t>(app);
    const discovery::ObjectReader read{[base](std::size_t at) { return TextAt(base + at); },
                                       [base](std::size_t at) { return CodeAt(base + at); }};
    // While the engine is starting, the list may still be filling in, so what
    // does not come out as one answer is read again, and a list is taken once
    // two reads a retry apart agree.
    std::optional<discovery::EntryList> list;
    try {
        list = discovery::FindEntryList(tdb.Size(*type), read);
    } catch (const std::runtime_error& e) {
        g_lastRead.reset();
        throw NotReady(e.what());
    }
    if (!list) {
        g_lastRead.reset();
        throw NotReady("via.Application does not name WaitRendering, BeginRendering and EndRendering yet");
    }
    if (g_lastRead != list) {
        const std::string was = g_lastRead ? Describe(*g_lastRead) : "";
        g_lastRead = list;
        throw NotReady(was.empty() ? "via.Application's stage list was read once and is read again to see that it holds"
                                   : "via.Application's stage list read " + Describe(*list) + ", and " + was + " the time before");
    }
    AppEntries entries;
    entries.app_ = static_cast<std::uint8_t*>(app);
    entries.list_ = *list;
    logging::Line(
        "entries: via.Application's %zu stages are 0x%zx apart from +0x%zx of its 0x%x bytes at %p, each called through the pointer %s0x%llx "
        "from its description",
        list->count, list->stride, list->first, tdb.Size(*type), app, list->func < 0 ? "-" : "+",
        static_cast<unsigned long long>(list->func < 0 ? -list->func : list->func));
    g_entries.emplace(entries);
}

AppEntries& AppEntries::Get() {
    if (!g_entries) throw std::logic_error("AppEntries::Get before AppEntries::Discover");
    return *g_entries;
}

const char* AppEntries::Description(std::size_t i) const {
    return *reinterpret_cast<const char* const*>(app_ + list_.first + i * list_.stride);
}

void** AppEntries::Function(std::size_t i) const {
    return reinterpret_cast<void**>(app_ + list_.first + i * list_.stride + list_.func);
}

std::vector<std::string> AppEntries::StageNames() const {
    std::vector<std::string> names;
    for (std::size_t i = 0; i < list_.count; ++i) names.emplace_back(Description(i));
    return names;
}

void AppEntries::Hook(const std::vector<StageHook>& hooks) {
    std::vector<std::size_t> at;
    for (const StageHook& hook : hooks) {
        std::vector<std::size_t> named;
        for (std::size_t i = 0; i < list_.count; ++i)
            if (hook.stage == Description(i)) named.push_back(i);
        if (named.empty()) throw std::runtime_error("via.Application has no stage named " + hook.stage);
        if (named.size() > 1) throw std::runtime_error("via.Application names " + std::to_string(named.size()) + " stages " + hook.stage);
        at.push_back(named[0]);
    }
    const auto free = static_cast<std::size_t>(std::count_if(g_slots.begin(), g_slots.end(), [](const Slot& s) { return !s.func; }));
    if (free < hooks.size())
        throw std::runtime_error(std::to_string(hooks.size()) + " application stage hooks are asked for and " + std::to_string(free) +
                                 " slots are free");
    for (std::size_t i = 0; i < hooks.size(); ++i) {
        void** func = Function(at[i]);
        Slot& slot = *std::find_if(g_slots.begin(), g_slots.end(), [](const Slot& s) { return !s.func; });
        // A stage hooked before is wrapped again: the later hook's `before` runs
        // first and its `after` last.
        slot = {func, Description(at[i]), nullptr, hooks[i].before, hooks[i].after};
        void* const ours = reinterpret_cast<void*>(kStages[&slot - g_slots.data()]);
        // One aligned pointer swap: the engine reads either the old pointer or
        // ours, and a pointer REFramework swaps in meanwhile is the one wrapped.
        for (;;) {
            void* const was = *func;
            slot.original = reinterpret_cast<void (*)(void*)>(was);
            if (InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(func), ours, was) == was) break;
        }
    }
}

}  // namespace cameraunlock::re_engine
