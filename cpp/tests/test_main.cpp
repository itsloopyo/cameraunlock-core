#include <iostream>
#include <string>

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
int RunLeanClampTests();
int RunZoomCompensationTests();
int RunTrackingModeTests();
int RunIniEditorTests();
int RunCheckedFileWriterTests();
int RunCanonicalIniTests();
int RunKeyBindingsTests();
int RunValueCodecsTests();
int RunConfigTableTests();
int RunCheckedFileWriterInterruptChild(const char* step);
#ifdef CAMERAUNLOCK_TESTS_REFRAMEWORK
int RunPluginConfigMigrationTests();
#endif

// Simple test runner - expand with a proper framework if needed
int main(int argc, char** argv) {
    // A test that has to die partway through a checked write runs as a child process:
    // this executable, started again with these arguments.
    if (argc == 3 && std::string(argv[1]) == "--checked-write-interrupt") {
        return RunCheckedFileWriterInterruptChild(argv[2]);
    }

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
    failures += RunLeanClampTests();
    failures += RunZoomCompensationTests();
    failures += RunTrackingModeTests();
    failures += RunIniEditorTests();
    failures += RunCheckedFileWriterTests();
    failures += RunCanonicalIniTests();
    failures += RunKeyBindingsTests();
    failures += RunValueCodecsTests();
    failures += RunConfigTableTests();
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
