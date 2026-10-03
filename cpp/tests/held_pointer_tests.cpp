// Tests for FindHeldPointer (cameraunlock/rendering/held_pointer.h), which the
// D3D12 overlay uses to pick the command queue a swap chain presents through.
// Drawing on any other queue submits the overlay after the flip, and nothing
// reaches the screen.

#include <cameraunlock/rendering/held_pointer.h>

#include <iostream>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

using cameraunlock::rendering::FindHeldPointer;
using cameraunlock::rendering::kHeldAmbiguous;
using cameraunlock::rendering::kHeldNone;

}  // namespace

int RunHeldPointerTests() {
    std::cout << "Held pointer:\n";
    int a = 0, b = 0, c = 0;
    const void* candidates[3] = {&a, &b, &c};

    const void* object[8] = {};
    Check(FindHeldPointer(object, sizeof(object), candidates, 3) == kHeldNone,
          "an object holding no candidate answers none");

    object[5] = &b;
    std::size_t offset = 0;
    Check(FindHeldPointer(object, sizeof(object), candidates, 3, &offset) == 1,
          "the candidate the object holds is the answer, whatever its place in the list");
    Check(offset == 5 * sizeof(void*), "and the offset it was found at is reported");

    object[7] = &b;
    Check(FindHeldPointer(object, sizeof(object), candidates, 3, &offset) == 1
              && offset == 5 * sizeof(void*),
          "the same candidate held twice is still that candidate, at its first offset");

    object[2] = &c;
    Check(FindHeldPointer(object, sizeof(object), candidates, 3) == kHeldAmbiguous,
          "two different candidates in one object is ambiguous, not the first match");

    object[2] = nullptr;
    object[7] = nullptr;
    Check(FindHeldPointer(object, 5 * sizeof(void*), candidates, 3) == kHeldNone,
          "a candidate past the bytes given is not read");
    Check(FindHeldPointer(object, 6 * sizeof(void*) - 1, candidates, 3) == kHeldNone,
          "a pointer the bytes given cut short is not read");

    const void* withNull[2] = {nullptr, &b};
    Check(FindHeldPointer(object, sizeof(object), withNull, 2) == 1,
          "a null candidate never matches the object's empty slots");

    return g_failures;
}
