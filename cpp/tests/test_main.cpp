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
int RunFrozenIniHelperTests();
int RunWin32ProfileSemanticsTests();
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
int RunHeadTrackingConfigTableTests();
int RunDefaultsIniTests();
int RunDefaultsLocationTests();
int RunPreferencesFixtureTests();
int RunCheckedFileWriterInterruptChild(const char* step);
int RunConfigOwnerTests();
int RunConfigOwnerInterruptChild(const char* step);
int RunGetAsyncKeyStateProbe();
int RunDefaultsIniProbe(int argc, char** argv);
int RunLegacyImportTests();
int RunIniMutationsTests();
int RunCanonicalConfigExampleTests();
#ifdef CAMERAUNLOCK_TESTS_REFRAMEWORK
int RunPluginConfigMigrationTests();
int RunPluginConfigCanonicalTests();
#endif

// Simple test runner - expand with a proper framework if needed
int main(int argc, char** argv) {
    // A test that has to die partway through a checked write runs as a child process:
    // this executable, started again with these arguments.
    if (argc == 3 && std::string(argv[1]) == "--checked-write-interrupt") {
        return RunCheckedFileWriterInterruptChild(argv[2]);
    }
    if (argc == 3 && std::string(argv[1]) == "--config-owner-interrupt") {
        return RunConfigOwnerInterruptChild(argv[2]);
    }
    if (argc == 2 && std::string(argv[1]) == "--probe-getasynckeystate") {
        return RunGetAsyncKeyStateProbe();
    }
    if (argc >= 2 && std::string(argv[1]) == "--probe-defaults-ini") {
        return RunDefaultsIniProbe(argc, argv);
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
    failures += RunFrozenIniHelperTests();
    failures += RunWin32ProfileSemanticsTests();
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
    failures += RunHeadTrackingConfigTableTests();
    failures += RunDefaultsIniTests();
    failures += RunDefaultsLocationTests();
    failures += RunPreferencesFixtureTests();
    failures += RunLegacyImportTests();
    failures += RunIniMutationsTests();
    failures += RunConfigOwnerTests();
    failures += RunCanonicalConfigExampleTests();
#ifdef CAMERAUNLOCK_TESTS_REFRAMEWORK
    failures += RunPluginConfigMigrationTests();
    failures += RunPluginConfigCanonicalTests();
#endif

    if (failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << failures << " test(s) FAILED\n";
    return 1;
}
