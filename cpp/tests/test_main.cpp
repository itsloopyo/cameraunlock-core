#include <iostream>

int RunAdsTests();
int RunProtocolTests();
int RunReceiverTests();
int RunMathTests();
int RunUtilTests();
int RunSessionTests();
int RunSmoothingTests();
int RunUnrealMathTests();
int RunProjectionTests();
int RunReMathTests();
int RunInterpolatorTests();
int RunProbeSelectionTests();
int RunFileLogTests();
int RunHotkeyTests();
int RunOsTests();
int RunValueGuardTests();
int RunConfigSchemaTests();
int RunSafeMemoryTests();
#ifdef CAMERAUNLOCK_TESTS_REFRAMEWORK
int RunPluginConfigMigrationTests();
#endif

// Simple test runner - expand with a proper framework if needed
int main() {
    std::cout << "CameraUnlock Core Tests\n";
    std::cout << "=====================\n";

    int failures = 0;
    failures += RunAdsTests();
    failures += RunProtocolTests();
    failures += RunReceiverTests();
    failures += RunMathTests();
    failures += RunUtilTests();
    failures += RunSessionTests();
    failures += RunSmoothingTests();
    failures += RunUnrealMathTests();
    failures += RunProjectionTests();
    failures += RunReMathTests();
    failures += RunInterpolatorTests();
    failures += RunProbeSelectionTests();
    failures += RunFileLogTests();
    failures += RunHotkeyTests();
    failures += RunOsTests();
    failures += RunValueGuardTests();
    failures += RunConfigSchemaTests();
    failures += RunSafeMemoryTests();
#ifdef CAMERAUNLOCK_TESTS_REFRAMEWORK
    failures += RunPluginConfigMigrationTests();
#endif

    if (failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << failures << " test(s) FAILED\n";
    return 1;
}
