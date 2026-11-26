// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core

#include <cameraunlock/hooks/hook_manager.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace hooks = cameraunlock::hooks;

int g_failures = 0;

void Check(bool cond, const char* name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

template <int I>
__declspec(noinline) int Target(int x) {
    volatile int y = x;
    return y + I;
}

using TargetFn = int (*)(int);

template <int I>
TargetFn g_original = nullptr;

template <int I>
int Detour(int x) {
    return g_original<I>(x) + 1000;
}

// Called through a volatile pointer so the call is not folded at compile time.
template <int I>
int CallTarget(int x) {
    TargetFn volatile fn = &Target<I>;
    return fn(x);
}

std::string ThrownBy(void (*f)()) {
    try {
        f();
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return {};
}

void TestThrowingForms() {
    std::cout << "throwing hook forms:\n";
    hooks::InitializeHooksOrThrow();
    hooks::InitializeHooksOrThrow();
    Check(hooks::HookManager::Instance().IsInitialized(), "initialising twice is not an error");

    hooks::InstallHookOrThrow(&Target<1>, &Detour<1>, &g_original<1>, "Target<1>");
    Check(CallTarget<1>(5) == 1006, "InstallHookOrThrow detours and enables, with the original reachable");

    hooks::CreateHookOrThrow(reinterpret_cast<std::uintptr_t>(&Target<2>), &Detour<2>, &g_original<2>, "Target<2>");
    Check(CallTarget<2>(5) == 7, "CreateHookOrThrow from an address leaves the detour off");
    hooks::EnableHookOrThrow(reinterpret_cast<std::uintptr_t>(&Target<2>), "Target<2>");
    Check(CallTarget<2>(5) == 1007, "EnableHookOrThrow turns it on");

    const std::string twice = ThrownBy([] {
        hooks::CreateHookOrThrow(&Target<1>, &Detour<1>, &g_original<1>, "Target<1> again");
    });
    Check(twice == std::string("no detour on Target<1> again: ") +
                       hooks::HookStatusToString(hooks::HookStatus::ErrorAlreadyCreated),
          "a refused create throws naming the hook and the status");

    const std::string off = ThrownBy([] { hooks::EnableHookOrThrow(&Target<3>, "Target<3>"); });
    Check(off == std::string("detour on Target<3> not enabled: ") +
                     hooks::HookStatusToString(hooks::HookStatus::ErrorNotCreated),
          "enabling a hook never made throws naming the hook and the status");

    Check(hooks::HookManager::Instance().RemoveHook(reinterpret_cast<void*>(&Target<1>)) == hooks::HookStatus::Ok &&
              hooks::HookManager::Instance().RemoveHook(reinterpret_cast<void*>(&Target<2>)) == hooks::HookStatus::Ok,
          "the hooks come out again");
    Check(CallTarget<1>(5) == 6 && CallTarget<2>(5) == 7, "and the targets run unhooked");
}

template <int... I>
void CreateFromThreads(std::integer_sequence<int, I...>, std::atomic<int>& failed) {
    std::vector<std::thread> threads;
    (threads.emplace_back([&failed] {
         auto& hm = hooks::HookManager::Instance();
         void* at = reinterpret_cast<void*>(&Target<I>);
         for (int round = 0; round < 50; ++round) {
             if (hm.CreateHook(at, reinterpret_cast<void*>(&Detour<I>), reinterpret_cast<void**>(&g_original<I>)) !=
                     hooks::HookStatus::Ok ||
                 hm.RemoveHook(at) != hooks::HookStatus::Ok)
                 failed.fetch_add(1);
         }
     }),
     ...);
    for (std::thread& t : threads) t.join();
}

void TestThreads() {
    std::cout << "hooks from several threads:\n";
    auto& hm = hooks::HookManager::Instance();
    const std::size_t before = hm.GetHookCount();
    std::atomic<int> failed{0};
    CreateFromThreads(std::integer_sequence<int, 10, 11, 12, 13, 14, 15, 16, 17>{}, failed);
    Check(failed.load() == 0, "every create and remove from eight threads succeeds");
    Check(hm.GetHookCount() == before, "and the list of hooks is back where it was");
}

}  // namespace

int RunHookManagerTests() {
    std::cout << "\n=== Hook Manager Tests ===\n";
    TestThrowingForms();
    TestThreads();
    return g_failures;
}
