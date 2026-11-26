#include <iostream>
#include <string>

int RunAdsTests();
int RunProtocolTests();
int RunReceiverTests();
int RunPoseJumpGateTests();
int RunMathTests();
int RunUtilTests();
int RunSessionTests();
int RunSmoothingTests();
int RunUnrealMathTests();
int RunProjectionTests();
int RunReMathTests();
int RunInterpolatorTests();
int RunAccelaFilterTests();
int RunProbeSelectionTests();
int RunFileLogTests();
int RunHotkeyTests();
int RunHotkeyPollerSwapTests();
int RunOsTests();
int RunValueGuardTests();
int RunFrozenIniHelperTests();
int RunWin32ProfileSemanticsTests();
int RunConfigSchemaTests();
int RunSafeMemoryTests();
int RunMemoryViewTests();
int RunPeImageTests();
int RunMsvcRttiTests();
int RunLeanClampTests();
int RunHeldPointerTests();
#ifdef CAMERAUNLOCK_TESTS_DISCOVERY
int RunRttiVtableTests();
#endif
#ifdef CAMERAUNLOCK_TESTS_HOOKS
int RunHookManagerTests();
int RunX86DecodeTests();
#endif
int RunInputScriptTests();
int RunDirectInputStateTests();
int RunGameInputStateTests();
#ifdef CAMERAUNLOCK_TESTS_GAME_LINK
int RunGameLinkTests();
#endif
#ifdef CAMERAUNLOCK_TESTS_HEAP_EXE
int RunHeapExeTests();
#endif
int RunLeanLineSweepTests();
int RunRigLeanTests();
int RunZoomCompensationTests();
int RunQuatHelpersTests();
int RunHeadBonesTests();
int RunFollowTurnTests();
int RunFovAxisTests();
int RunViewmodelTests();
int RunDeferredRigLeanTests();
int RunClockGateTests();
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
    failures += RunPoseJumpGateTests();
    failures += RunMathTests();
    failures += RunUtilTests();
    failures += RunSessionTests();
    failures += RunSmoothingTests();
    failures += RunUnrealMathTests();
    failures += RunProjectionTests();
    failures += RunReMathTests();
    failures += RunInterpolatorTests();
    failures += RunAccelaFilterTests();
    failures += RunProbeSelectionTests();
    failures += RunFileLogTests();
    failures += RunHotkeyTests();
    failures += RunHotkeyPollerSwapTests();
    failures += RunOsTests();
    failures += RunValueGuardTests();
    failures += RunFrozenIniHelperTests();
    failures += RunWin32ProfileSemanticsTests();
    failures += RunConfigSchemaTests();
    failures += RunSafeMemoryTests();
    failures += RunMemoryViewTests();
    failures += RunPeImageTests();
    failures += RunMsvcRttiTests();
    failures += RunLeanClampTests();
    failures += RunHeldPointerTests();
    failures += RunInputScriptTests();
    failures += RunDirectInputStateTests();
    failures += RunGameInputStateTests();
#ifdef CAMERAUNLOCK_TESTS_GAME_LINK
    failures += RunGameLinkTests();
#endif
#ifdef CAMERAUNLOCK_TESTS_HEAP_EXE
    failures += RunHeapExeTests();
#endif
    failures += RunLeanLineSweepTests();
    failures += RunRigLeanTests();
    failures += RunZoomCompensationTests();
    failures += RunQuatHelpersTests();
    failures += RunHeadBonesTests();
    failures += RunFollowTurnTests();
    failures += RunFovAxisTests();
    failures += RunViewmodelTests();
    failures += RunDeferredRigLeanTests();
    failures += RunClockGateTests();
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
#ifdef CAMERAUNLOCK_TESTS_DISCOVERY
    failures += RunRttiVtableTests();
#endif
#ifdef CAMERAUNLOCK_TESTS_HOOKS
    failures += RunHookManagerTests();
    failures += RunX86DecodeTests();
#endif
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
