# Core as a jar

`CameraUnlockCore.dll` is core for a host that can load a DLL. Some cannot ship one: a Steam
Workshop item for Project Zomboid is uploaded by the game's own uploader, which refuses a file
whose name ends in `.exe`, `.dll`, `.bat`, `.app`, `.dylib`, `.sh`, `.so` or `.zip` anywhere under
the item's `Contents/` (`zombie.core.znet.SteamWorkshopItem.validateFileTypes`). The comparison is
case-sensitive, and a name ending in `pyramid.zip`, in any case, is let through. For a host on a
JVM, core is also a jar with no native file in it. It is the same C++, the same C interface and the same Java
binding, so nothing is written twice.

## What is in it

`node scripts/build-core-jar.mjs <out.jar>` writes one jar:

| Part | From |
|---|---|
| The binding and the boot class | `java/src` |
| The C interface as JVM classes, `com.cameraunlock.core.wasm.CoreModule` | `cpp/` compiled to WebAssembly, then to Java bytecode by Endive's build-time compiler, which also copies code of its own into them (`CoreModuleMachineShaded`) |
| The classes those run on | Endive 1.1.0: `runtime`, `wasm`, `wasi`, `log`, `annotations` |
| The notices that travel with them | `META-INF/licenses/`, from `legal/` |

`CameraUnlock.loadBundled()` starts it. Every method of `CameraUnlock` behaves as it does with
`CameraUnlock.load()` and the DLL, and `pixi run test-java` and `pixi run vectors-java` run each
check on both.

A mod compiles against the jar and puts its whole content in its own jar. A loader that takes one
jar per mod (ZombieBuddy) needs that, and so does the boot class a site config names
([java-agent-mod.md](java-agent-mod.md)): `Boot` loads the jar it is in as the agent, so it has to
be in the mod's jar.

## How it is built

`scripts/build-wasm.mjs` compiles the C interface's sources with wasi-sdk's clang for
`wasm32-wasip1`, as a reactor module, with standard WebAssembly exception handling
(`-fwasm-exceptions -mllvm -wasm-use-legacy-eh=false`), the one kind Endive runs. Endive's
build-time compiler turns the module into Java bytecode, a class per group of functions, and
stops the build if a function does not fit one JVM method rather than leave it to its
interpreter. It writes three variants under `cpp/build-wasm/`: `core`, which ships, `testing`,
with the entry points of `cameraunlock_testing.h`, and `other-abi`, for the test of a module of
the ABI before this one.

Every source of the C interface is compiled with `-Werror -Wall` here, where the MSVC build holds
only the C interface's own targets to `/W4 /WX`. That is deliberate: clang sees warnings MSVC does
not, it found a dead constant the DLL build let through, and `pixi run check` builds the module, so
a warning in any of core's C++ that the jar carries stops the check rather than reaching a mod.

The toolchain is pinned by version and SHA-256 in `scripts/wasm-toolchain.mjs` (wasi-sdk 34,
Endive 1.1.0), fetched once into `%LOCALAPPDATA%\cameraunlock-core\toolchain` and checked before
use. It runs on Windows, the same build a developer's `pixi run` and CI's run.

## How the module reaches Windows

The module has no Windows headers. Core's Win32 code includes `cameraunlock/os/win32.h`, which on
Windows is `<windows.h>` and in the WebAssembly build (`CAMERAUNLOCK_WIN32_HOST`) is
`cameraunlock/os/win32_host.h`: the types, constants and functions core calls, each function an
import of module `cameraunlock_win32`. `java/src/com/cameraunlock/core/Win32Host.java` implements
each import with the same call into kernel32, user32 or ws2_32 through `java.lang.foreign`, so the
config owner's atomic replace, its file times, the hotkeys and the window centring behave as in the
DLL. Defaults.ini's probe also calls shell32 (`SHGetKnownFolderPath`), ole32 (`CoTaskMemFree`) and,
where it has them, Wine's functions in ntdll and kernel32. Windows' own DLLs are all it calls, and nothing native
ships.

The two sides differ in three ways, and Win32Host converts at each call:

- `wchar_t` is 32 bits in the module, so a W function's text is UTF-32 there and lengths count
  code points.
- Pointers are 32 bits, and so are `HANDLE`, `HWND`, `HMONITOR` and `SOCKET` in the module. The
  host sign-extends each to 64 bits on the way to Windows, and checks each value Windows hands
  back before it cuts it to 32: each has to sign-extend back to the same value (an invalid handle
  or `INVALID_SOCKET` is -1, which does), and anything else throws
  `IllegalStateException`. `FindFirstFileW`'s handles are pointers past 32 bits, so the module
  holds a number the host gives it in their place.
- A struct passed by value is passed by pointer (`SetFilePointerEx`'s `LARGE_INTEGER`).

`win32_host.h` declares only what core uses. A new Win32 call in core's Windows code does not
compile for WebAssembly until it is declared there. A declared import Win32Host does not register
would link, and fail only when the module is instantiated, so `pixi run build-java` runs
`scripts/check-win32-imports.mjs` first: it reads the modules' imports of `cameraunlock_win32` and
fails on one Win32Host does not register with the same signature, and on a function Win32Host
registers that no module imports.

Defaults.ini's probe is not one call each. It finds its functions by name with `GetProcAddress`,
which code in WebAssembly cannot call through, so `ProbeDefaults` and `WineDosFileName` are imports
Win32Host answers with the same lookups. The file log writes through `CreateFileW` and `WriteFile`
like the DLL.

## Threads

The module runs on one thread at a time and has none of its own. `java/src/com/cameraunlock/core/WasmCore.java`
holds one lock around every call into it, and does what the DLL's threads do:

- A thread calls `cameraunlock_host_tick` every 16 ms, the hotkey poller's interval: one look at
  the keys, and one step of each receiver's supervisor (the bind retried every 500 ms while it
  fails, the socket reopened after its receive loop gives up).
- For each socket a receiver opens, a thread waits with `select` (100 ms, so a stop is noticed),
  `recvfrom`s, and hands each result to `cameraunlock_host_received`. `UdpReceiver` has the steps
  both builds share: `SupervisorStep`, `Received`, `ReceiveLoopFailed`.

`cameraunlock/c/host/cameraunlock_host.h` declares those three entry points. A DLL host never
calls them.

## When the module stops

A trap, or an exception thrown by an import, leaves the module past the C++ code that would have
let go of its mutexes. So after one the module is not called again: every later call throws
`IllegalStateException` with the first cause, and the tick and the receive threads end. A tick that
answers `CAMERAUNLOCK_ERROR` stops it the same way, where the DLL's poller would log and carry on,
because the hotkeys and the receivers' retries would otherwise stop with nothing said.

The reason of a failed call is read inside that call and kept for the thread that made it, so
`cameraunlock_last_error` answers each thread with its own, as the DLL's thread_local does.

## Cost

Measured on the testing module against the testing DLL, one frame with a datagram every fourth:
about 4 microseconds against 0.3, once warm. Every call allocates its arguments' copies, so
`frame`, `lean`, `viewFrame`, `viewLean`, `hotkeysTake` and `hotkeysDrop` allocate here, where
with the DLL they do not.

## What it does not do

- It is Windows only, as the DLL is: the imports are Win32.
- No legacy import and the rest of what [c-interface.md](c-interface.md) lists under "What it
  does not do".
- The window finding and the one-shot raise (`os::FindGameWindow`, `os::CenterGameWindowOnce`)
  are not in it.
