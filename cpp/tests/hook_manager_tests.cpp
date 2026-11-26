// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core

#include <cameraunlock/hooks/hook_manager.h>

#include <atomic>
#include <cstdint>
#include <iostream>
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
    TestThreads();
    return g_failures;
}
