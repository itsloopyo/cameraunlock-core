// cameraunlock/dev/isolated_input.h as a DLL, for a mod with no native code of
// its own to compile the header into. A C# mod's dev build loads it through
// CameraUnlock.Core.Dev.IsolatedInput.StartIfAsked, which calls the one export.
//
// Nothing happens when the DLL is loaded. The start is an export because the
// caller has to give the command file's path and has to be told when the
// detours did not go in, and DllMain can do neither.

#include "cameraunlock/logging/file_log.h"

#define CAMERAUNLOCK_ISOLATED_INPUT_IMPLEMENTATION
#include "cameraunlock/dev/isolated_input.h"

#include <filesystem>
#include <mutex>

namespace {

// CameraUnlock.Core.Dev.IsolatedInput turns these into its exception messages.
enum StartResult : int {
    kStarted = 0,
    kMinHookFailed = 1,
    kDetoursFailed = 2,
    kAlreadyStarted = 3,
};

std::mutex g_mutex;
bool g_started = false;

}  // namespace

// Starts isolated input on `commandFile` and logs to
// CameraUnlockIsolatedInput.log in the same folder.
extern "C" __declspec(dllexport) int CameraUnlockStartIsolatedInput(const wchar_t* commandFile) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_started) return kAlreadyStarted;

    const std::filesystem::path folder = std::filesystem::path(commandFile).parent_path();
    cameraunlock::logging::Open((folder / L"CameraUnlockIsolatedInput.log").wstring());

    const MH_STATUS initialised = MH_Initialize();
    if (initialised != MH_OK) {
        cameraunlock::logging::Line("isolated input host: MinHook did not initialise: %s", MH_StatusToString(initialised));
        return kMinHookFailed;
    }
    if (!cameraunlock::dev::StartIsolatedInput(commandFile,
                                               [](const char* text) { cameraunlock::logging::Line("%s", text); })) {
        cameraunlock::logging::Line("isolated input host: a detour could not be installed, so none is left in");
        MH_Uninitialize();
        return kDetoursFailed;
    }
    g_started = true;
    return kStarted;
}
