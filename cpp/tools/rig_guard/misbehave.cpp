// RigMisbehave.exe: a stand-in for a game that takes the machine over, for the
// test of the guarded launch (powershell/tests/RigGuard.Tests.ps1). Never shipped.
//
//   RigMisbehave.<arch>.exe <seconds> <stats file> [child <exe>] [host <dll> <command file>]
//
// It does what the games that took the foreground, the mouse and the display did:
// makes its window two seconds in and a second one later, activates them every
// quarter of a second by every call there is, asks for another display mode every
// half second, goes exclusive fullscreen through Direct3D 9 and Direct3D 11, clips
// and moves the cursor, takes raw input in the background and the mouse
// exclusively through DirectInput, and starts a child that does the same.
//
// It is also a game that is windowed already: a third window, never given a
// Direct3D device, is made away from the corner and moved back out of it four
// times a second, by SetWindowPos, MoveWindow, SetWindowPlacement and
// DeferWindowPos in turn, and once while it is minimised. A tool window, an
// owned window, a child window and a message-only window are moved too, and
// those moves are theirs to make.
//
// It runs only with the rig shim in it: without, it would do all of that for real.
// It draws the whole time, loads the isolated-input host when given one, and at
// the end writes what it saw to the stats file, one key=value a line.

#include <Windows.h>

#include <d3d9.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <cstdio>
#include <string>

#include "inject.h"

namespace {

struct Seen {
    int activate = 0, activateApp = 0, setFocus = 0, killFocus = 0, deactivate = 0;
    int keyDownW = 0, rawInput = 0;
} g_seen;

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ACTIVATE: LOWORD(wParam) != WA_INACTIVE ? ++g_seen.activate : ++g_seen.deactivate; break;
        case WM_ACTIVATEAPP: if (wParam) ++g_seen.activateApp; break;
        case WM_SETFOCUS: ++g_seen.setFocus; break;
        case WM_KILLFOCUS: ++g_seen.killFocus; break;
        case WM_KEYDOWN: if (wParam == 'W') ++g_seen.keyDownW; break;
        case WM_INPUT: ++g_seen.rawInput; break;
        case WM_CLOSE: return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

using RealForeground_t = HWND (WINAPI*)();
RealForeground_t g_realForeground;

struct Calls {
    int rounds = 0, realForegroundMine = 0, believedForeground = 0, activeMine = 0, focusMine = 0;
    int modeAsked = 0, modeAnsweredDone = 0;
    BOOL setForeground = FALSE, clip = FALSE, cursor = FALSE;
} g_calls;

// Every way a program has of putting its window in front.
void Grab(HWND window) {
    ++g_calls.rounds;
    ShowWindow(window, SW_RESTORE);
    ShowWindow(window, SW_SHOW);
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(window);
    SwitchToThisWindow(window, TRUE);
    AllowSetForegroundWindow(ASFW_ANY);
    LockSetForegroundWindow(LSFW_UNLOCK);
    // The two classic ways round the foreground lock: joining the foreground thread's
    // input queue, and a synthetic Alt press.
    const DWORD front = GetWindowThreadProcessId(g_realForeground(), nullptr);
    AttachThreadInput(GetCurrentThreadId(), front, TRUE);
    keybd_event(VK_MENU, 0, 0, 0);
    keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    g_calls.setForeground = SetForegroundWindow(window);
    AttachThreadInput(GetCurrentThreadId(), front, FALSE);
    SetActiveWindow(window);
    SetFocus(window);
    FLASHWINFO flash{sizeof(flash), window, FLASHW_ALL | FLASHW_TIMERNOFG, 3, 0};
    FlashWindowEx(&flash);

    RECT rect;
    GetWindowRect(window, &rect);
    g_calls.clip = ClipCursor(&rect);
    g_calls.cursor = SetCursorPos((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);

    DWORD pid = 0;
    GetWindowThreadProcessId(g_realForeground(), &pid);
    if (pid == GetCurrentProcessId()) ++g_calls.realForegroundMine;
    if (GetForegroundWindow() == window) ++g_calls.believedForeground;
    if (GetActiveWindow() == window) ++g_calls.activeMine;
    if (GetFocus() == window) ++g_calls.focusMine;
}

void AskForMode() {
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    mode.dmPelsWidth = 1024;
    mode.dmPelsHeight = 768;
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    ++g_calls.modeAsked;
    if (ChangeDisplaySettingsExW(nullptr, &mode, nullptr, CDS_FULLSCREEN, nullptr) == DISP_CHANGE_SUCCESSFUL) ++g_calls.modeAnsweredDone;
    ++g_calls.modeAsked;
    if (ChangeDisplaySettingsW(&mode, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL) ++g_calls.modeAnsweredDone;
}

D3DPRESENT_PARAMETERS Fullscreen9(HWND window) {
    D3DPRESENT_PARAMETERS parameters{};
    parameters.BackBufferWidth = 640;
    parameters.BackBufferHeight = 480;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.Windowed = FALSE;
    parameters.FullScreen_RefreshRateInHz = 60;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    return parameters;
}

HWND MakeWindow(const wchar_t* title, int x, int y) {
    return CreateWindowExW(0, L"RigMisbehave", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, y, 480, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

// One placing call a round, each asking for a size of its own. `size` is left as what the window was last asked to be.
void MoveAway(HWND window, int round, SIZE& size) {
    switch (round % 5) {
        case 0: SetWindowPos(window, nullptr, 200, 200, 0, 0, SWP_NOSIZE | SWP_NOZORDER); break;
        case 1:
            size = {360, 220};
            MoveWindow(window, 200, 200, size.cx, size.cy, TRUE);
            break;
        case 2: {
            size = {340, 210};
            WINDOWPLACEMENT placement{};
            placement.length = sizeof(placement);
            placement.showCmd = SW_SHOWNORMAL;
            placement.rcNormalPosition = {200, 200, 200 + size.cx, 200 + size.cy};
            SetWindowPlacement(window, &placement);
            break;
        }
        case 3: {
            size = {350, 215};
            HDWP positions = BeginDeferWindowPos(1);
            positions = DeferWindowPos(positions, window, nullptr, 200, 200, size.cx, size.cy, SWP_NOZORDER);
            EndDeferWindowPos(positions);
            break;
        }
        case 4:
            size = {330, 205};
            SetWindowPos(window, nullptr, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER);
            break;
    }
}

void Pump() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 2;
    // Started by someone else than the guard, as Steam starts a game, it is given the shim a moment in.
    for (int waited = 0; waited < 3000 && !rig::IsShimmed(GetCurrentProcessId()); waited += 20) Sleep(20);
    if (!rig::IsShimmed(GetCurrentProcessId())) {
        fputws(L"RigMisbehave runs only with the rig shim in it: start it through the guard.\n", stderr);
        return 3;
    }
    const ULONGLONG seconds = _wcstoui64(argv[1], nullptr, 10);
    const std::wstring statsFile = argv[2];
    std::wstring childExe, hostDll, commandFile;
    for (int i = 3; i < argc; ++i) {
        if (!wcscmp(argv[i], L"child") && i + 1 < argc) childExe = argv[++i];
        else if (!wcscmp(argv[i], L"host") && i + 2 < argc) { hostDll = argv[++i]; commandFile = argv[++i]; }
        else return 2;
    }
    g_realForeground = reinterpret_cast<RealForeground_t>(GetProcAddress(LoadLibraryW(L"win32u.dll"), "NtUserGetForegroundWindow"));
    const ULONGLONG started = GetTickCount64();

    DWORD childPid = 0;
    bool childShimmed = false;
    HANDLE childProcess = nullptr;
    if (!childExe.empty()) {
        std::wstring commandLine = L"\"" + childExe + L"\" " + std::to_wstring(seconds > 10 ? seconds - 6 : seconds) + L" \"" + statsFile + L".child\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION made{};
        if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &made)) {
            childPid = made.dwProcessId;
            childShimmed = rig::IsShimmed(childPid);
            childProcess = made.hProcess;
            CloseHandle(made.hThread);
        }
    }

    Sleep(2000);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = &WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"RigMisbehave";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&windowClass);
    const int offset = childExe.empty() ? 320 : 0;
    const HWND window = MakeWindow(L"RigMisbehave Direct3D 9", 40 + offset, 40 + offset);
    if (!window) return 4;
    RECT made9{};
    GetWindowRect(window, &made9);
    const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    UpdateWindow(window);
    Grab(window);

    const HWND plain = CreateWindowExW(0, L"RigMisbehave", L"RigMisbehave windowed", WS_OVERLAPPEDWINDOW, 200 + offset, 200 + offset, 320, 200, nullptr, nullptr,
                                       GetModuleHandleW(nullptr), nullptr);
    const HWND tool = CreateWindowExW(WS_EX_TOOLWINDOW, L"RigMisbehave", L"RigMisbehave tool", WS_POPUP | WS_CAPTION, 8, 8, 160, 60, nullptr, nullptr,
                                      GetModuleHandleW(nullptr), nullptr);
    const HWND inner = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, 10, 10, 100, 40, plain, nullptr, GetModuleHandleW(nullptr), nullptr);
    const HWND owned = CreateWindowExW(0, L"RigMisbehave", L"RigMisbehave owned", WS_POPUP | WS_CAPTION, 8, 80, 160, 60, plain, nullptr,
                                       GetModuleHandleW(nullptr), nullptr);
    const HWND messageOnly = CreateWindowExW(0, L"RigMisbehave", L"", WS_POPUP, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!plain || !tool || !inner || !owned || !messageOnly) return 4;
    ShowWindow(plain, SW_SHOW);
    RECT plainShown{};
    GetWindowRect(plain, &plainShown);
    // Under everyone else's before they are shown: a tool window and an owned window are left where the game puts them.
    SetWindowPos(tool, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(tool, SW_SHOWNA);
    SetWindowPos(owned, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(owned, SW_SHOWNA);
    MoveWindow(messageOnly, 5, 5, 10, 10, FALSE);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int plainMoves = 0, plainAway = 0, plainForeground = 0;
    SIZE plainAsked{320, 200};
    RECT plainRestored{};

    RAWINPUTDEVICE raw[2] = {{1, 6, RIDEV_INPUTSINK, window}, {1, 2, RIDEV_INPUTSINK, window}};
    const BOOL rawRegistered = RegisterRawInputDevices(raw, 2, sizeof(RAWINPUTDEVICE));

    HRESULT mouseLevel = E_FAIL, mouseAcquire = E_FAIL;
    IDirectInput8W* directInput = nullptr;
    IDirectInputDevice8W* mouse = nullptr;
    if (SUCCEEDED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void**>(&directInput), nullptr)) &&
        SUCCEEDED(directInput->CreateDevice(GUID_SysMouse, &mouse, nullptr))) {
        mouse->SetDataFormat(&c_dfDIMouse2);
        mouseLevel = mouse->SetCooperativeLevel(window, DISCL_EXCLUSIVE | DISCL_BACKGROUND);
        mouseAcquire = mouse->Acquire();
    }

    IDirect3D9* d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    IDirect3DDevice9* device9 = nullptr;
    D3DPRESENT_PARAMETERS parameters9 = Fullscreen9(window);
    const HRESULT create9 = d3d9 ? d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters9, &device9)
                                 : E_FAIL;

    HWND window11 = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device11 = nullptr;
    ID3D11DeviceContext* context11 = nullptr;
    ID3D11RenderTargetView* target11 = nullptr;
    HRESULT create11 = E_FAIL, setFullscreen = E_FAIL, resizeTarget = E_FAIL;
    BOOL saysFullscreen = FALSE;
    HWND host = nullptr;
    int hostResult = -1;

    ULONGLONG grabMs = 0, present9Ms = 0, present11Ms = 0, reset9Ms = 0;
    const auto since = [](ULONGLONG from) { return GetTickCount64() - from; };
    int frames9 = 0, frames11 = 0, reset9Asked = 0, reset9Done = 0, wHeldFrames = 0;
    ULONGLONG nextGrab = 0, nextMode = 0, nextReset = 0;
    bool madeSecond = false, startedHost = false;
    while (GetTickCount64() - started < seconds * 1000) {
        Pump();
        const ULONGLONG now = GetTickCount64() - started;

        if (!madeSecond && now > 4000) {
            madeSecond = true;
            window11 = MakeWindow(L"RigMisbehave Direct3D 11", 560 + offset, 40 + offset);
            DXGI_SWAP_CHAIN_DESC description{};
            description.BufferDesc.Width = 640;
            description.BufferDesc.Height = 480;
            description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            description.BufferDesc.RefreshRate = {60, 1};
            description.SampleDesc.Count = 1;
            description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            description.BufferCount = 1;
            description.OutputWindow = window11;
            description.Windowed = FALSE;
            description.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
            if (childExe.empty()) {
                // The child goes through a factory of its own, the parent through Direct3D 11's.
                IDXGIFactory1* factory = nullptr;
                create11 = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device11, nullptr, &context11);
                if (SUCCEEDED(create11)) create11 = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
                if (SUCCEEDED(create11)) create11 = factory->CreateSwapChain(device11, &description, &swapChain);
            } else {
                create11 = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &description, &swapChain,
                                                         &device11, nullptr, &context11);
            }
            if (SUCCEEDED(create11)) {
                setFullscreen = swapChain->SetFullscreenState(TRUE, nullptr);
                swapChain->GetFullscreenState(&saysFullscreen, nullptr);
                DXGI_MODE_DESC mode = description.BufferDesc;
                mode.Width = 1024;
                mode.Height = 768;
                resizeTarget = swapChain->ResizeTarget(&mode);
                ID3D11Texture2D* back = nullptr;
                if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
                    device11->CreateRenderTargetView(back, nullptr, &target11);
                    back->Release();
                }
            }
        }

        if (!startedHost && !hostDll.empty() && now > 6000) {
            startedHost = true;
            if (const HMODULE module = LoadLibraryW(hostDll.c_str())) {
                using Start_t = int (*)(const wchar_t*);
                if (const auto start = reinterpret_cast<Start_t>(GetProcAddress(module, "CameraUnlockStartIsolatedInput"))) hostResult = start(commandFile.c_str());
            }
            host = window;
        }

        if (now >= nextGrab) {
            nextGrab = now + 250;
            const ULONGLONG from = GetTickCount64();
            Grab(window);
            if (window11) Grab(window11);
            if (plainMoves == 7) {
                ShowWindow(plain, SW_MINIMIZE);
                SetWindowPos(plain, nullptr, 200, 200, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                ShowWindow(plain, SW_RESTORE);
                GetWindowRect(plain, &plainRestored);
            }
            MoveAway(plain, plainMoves++, plainAsked);
            RECT at{};
            GetWindowRect(plain, &at);
            if (at.right != work.right || at.bottom != work.bottom || at.right - at.left != plainAsked.cx || at.bottom - at.top != plainAsked.cy) ++plainAway;
            if (g_realForeground() == plain) ++plainForeground;
            SetWindowPos(tool, nullptr, 16, 16, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(owned, nullptr, 16, 90, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowPos(inner, nullptr, 20, 20, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            grabMs += since(from);
        }
        if (now >= nextMode) {
            nextMode = now + 500;
            AskForMode();
        }
        if (device9 && now >= nextReset) {
            nextReset = now + 2000;
            D3DPRESENT_PARAMETERS again = Fullscreen9(window);
            ++reset9Asked;
            const ULONGLONG from = GetTickCount64();
            if (SUCCEEDED(device9->Reset(&again))) ++reset9Done;
            reset9Ms += since(from);
        }

        if (device9) {
            device9->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(static_cast<int>(now / 8) & 255, 40, 90), 1.0f, 0);
            device9->BeginScene();
            device9->EndScene();
            const ULONGLONG from = GetTickCount64();
            if (SUCCEEDED(device9->Present(nullptr, nullptr, nullptr, nullptr))) ++frames9;
            present9Ms += since(from);
        }
        if (target11) {
            const float colour[4] = {0.1f, static_cast<float>((now / 8) & 255) / 255.0f, 0.3f, 1.0f};
            context11->ClearRenderTargetView(target11, colour);
            const ULONGLONG from = GetTickCount64();
            if (SUCCEEDED(swapChain->Present(0, 0))) ++frames11;
            present11Ms += since(from);
        }
        if (host && (GetAsyncKeyState('W') & 0x8000)) ++wHeldFrames;
        Sleep(8);
    }

    RECT rect9{}, rect11{}, rectPlain{}, rectTool{}, rectInner{}, rectOwned{}, rectMessageOnly{};
    GetWindowRect(owned, &rectOwned);
    GetWindowRect(messageOnly, &rectMessageOnly);
    GetWindowRect(window, &rect9);
    GetWindowRect(plain, &rectPlain);
    GetWindowRect(tool, &rectTool);
    GetWindowRect(inner, &rectInner);
    MapWindowPoints(nullptr, plain, reinterpret_cast<POINT*>(&rectInner), 2);
    if (window11) GetWindowRect(window11, &rect11);
    DWORD childExit = STILL_ACTIVE;
    if (childProcess) {
        WaitForSingleObject(childProcess, 15000);
        GetExitCodeProcess(childProcess, &childExit);
    }
    FILE* stats = nullptr;
    if (_wfopen_s(&stats, statsFile.c_str(), L"w") != 0 || !stats) return 5;
    fprintf(stats, "pid=%lu\nchildPid=%lu\nchildShimmed=%d\nchildExit=%lu\n", GetCurrentProcessId(), childPid, childShimmed ? 1 : 0, childExit);
    fprintf(stats, "noActivateStyle=%d\ntopmost=%d\n", (exStyle & WS_EX_NOACTIVATE) ? 1 : 0, (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) ? 1 : 0);
    fprintf(stats, "grabRounds=%d\nrealForegroundMine=%d\nbelievedForeground=%d\nactiveMine=%d\nfocusMine=%d\nsetForegroundAnswered=%d\n", g_calls.rounds,
            g_calls.realForegroundMine, g_calls.believedForeground, g_calls.activeMine, g_calls.focusMine, g_calls.setForeground);
    fprintf(stats, "wmActivate=%d\nwmActivateApp=%d\nwmSetFocus=%d\nwmKillFocus=%d\nwmDeactivate=%d\n", g_seen.activate, g_seen.activateApp, g_seen.setFocus,
            g_seen.killFocus, g_seen.deactivate);
    fprintf(stats, "modeAsked=%d\nmodeAnsweredDone=%d\nclipAnswered=%d\ncursorAnswered=%d\n", g_calls.modeAsked, g_calls.modeAnsweredDone, g_calls.clip,
            g_calls.cursor);
    fprintf(stats, "rawRegistered=%d\nrawInputMessages=%d\nmouseLevel=0x%08lX\nmouseAcquire=0x%08lX\n", rawRegistered, g_seen.rawInput,
            static_cast<unsigned long>(mouseLevel), static_cast<unsigned long>(mouseAcquire));
    fprintf(stats, "create9=0x%08lX\nansweredWindowed9=%d\nframes9=%d\nreset9Asked=%d\nreset9Done=%d\nwindow9=%ldx%ld\n", static_cast<unsigned long>(create9),
            parameters9.Windowed, frames9, reset9Asked, reset9Done, rect9.right - rect9.left, rect9.bottom - rect9.top);
    fprintf(stats, "create11=0x%08lX\nsetFullscreen=0x%08lX\nsaysFullscreen=%d\nresizeTarget=0x%08lX\nframes11=%d\nwindow11=%ldx%ld\n",
            static_cast<unsigned long>(create11), static_cast<unsigned long>(setFullscreen), saysFullscreen, static_cast<unsigned long>(resizeTarget), frames11,
            rect11.right - rect11.left, rect11.bottom - rect11.top);
    const auto place = [stats](const char* name, const RECT& rect) { fprintf(stats, "%s=%ld,%ld,%ld,%ld\n", name, rect.left, rect.top, rect.right, rect.bottom); };
    place("work", work);
    place("made9", made9);
    place("plainShown", plainShown);
    place("plain", rectPlain);
    place("window9At", rect9);
    place("window11At", rect11);
    place("plainRestored", plainRestored);
    fprintf(stats, "plainAsked=%ldx%ld\n", plainAsked.cx, plainAsked.cy);
    place("tool", rectTool);
    place("owned", rectOwned);
    place("messageOnly", rectMessageOnly);
    place("inner", rectInner);
    fprintf(stats, "plainMoves=%d\nplainAway=%d\nplainForeground=%d\n", plainMoves, plainAway, plainForeground);
    fprintf(stats, "grabMs=%llu\npresent9Ms=%llu\npresent11Ms=%llu\nreset9Ms=%llu\n", grabMs, present9Ms, present11Ms, reset9Ms);
    fprintf(stats, "hostResult=%d\nkeyDownW=%d\nwHeldFrames=%d\n", hostResult, g_seen.keyDownW, wHeldFrames);
    fclose(stats);
    return 0;
}
