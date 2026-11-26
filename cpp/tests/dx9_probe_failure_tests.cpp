#include <d3d9.h>
#include <Windows.h>
#include <cstdio>
#include <string>

struct FakeComObject { void** table; };
FakeComObject factory{};
void* factoryTable[17]{};

ULONG __stdcall ReleaseFactory(IDirect3D9*) { return 0; }
HRESULT __stdcall CreateLost(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                             D3DPRESENT_PARAMETERS*, IDirect3DDevice9**) {
    return D3DERR_DEVICELOST;
}
IDirect3D9* WINAPI CreateFactory(UINT) {
    factoryTable[2] = reinterpret_cast<void*>(&ReleaseFactory);
    factoryTable[16] = reinterpret_cast<void*>(&CreateLost);
    factory.table = factoryTable;
    return reinterpret_cast<IDirect3D9*>(&factory);
}

#define Direct3DCreate9 CreateFactory
#define CAMERAUNLOCK_DX9_OVERLAY_IMPLEMENTATION
#include "cameraunlock/rendering/dx9_overlay.h"
namespace rndr = cameraunlock::rendering;

std::string logged;
int checks = 0;
int failures = 0;

void Check(bool ok, const char* what) {
    ++checks;
    if (ok) return;
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what);
}

int main() {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    rndr::SetDX9OverlayLogger([](const char* line) {
        std::puts(line);
        logged += line;
        logged += '\n';
    });
    rndr::DX9Overlay overlay;
    Check(overlay.Install(), "a probe that finds the device lost still arms the CreateDevice hook");
    Check(!overlay.PresentHooked(), "a probe that finds the device lost hooks no Present");
    Check(logged.find("probe device creation failed (HRESULT 0x88760868)") != std::string::npos,
          "the log names the probe's HRESULT");
    overlay.Remove();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
