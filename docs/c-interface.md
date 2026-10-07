# The C interface

`CameraUnlockCore.dll` is cameraunlock-core for a host that is neither C++ nor C#: a Java agent,
a Lua or Python script, a Rust plugin. It holds the head tracking pipeline, the config owner, the
hotkey poller and the file log, behind plain C functions. A host in such a language loads the DLL
and writes no receiver, no interpolation, no smoothing, no lean clamp, no aim transition and no
config reader of its own, and no C++.

The declarations are `cpp/include/cameraunlock/c/cameraunlock.h`. The implementation is
`cpp/src/c/cameraunlock_c.cpp`, and `cpp/tests/c_interface_tests.cpp` drives every function
through the DLL with real OpenTrack datagrams and a config file in a scratch folder.

It is Windows only, as the config owner and the hotkey poller are.

## Building it

Core's CMake builds it when `CAMERAUNLOCK_BUILD_C` is on:

```text
cmake -S cameraunlock-core/cpp -B build/core -DCAMERAUNLOCK_BUILD_C=ON -DCAMERAUNLOCK_BUILD_TESTS=OFF
cmake --build build/core --config Release --target cameraunlock_c
```

The DLL is `build/core/Release/CameraUnlockCore.dll`. The C++ runtime is linked in. A mod ships it
beside its own code and loads it by full path.

## Rules that hold for every function

- Plain C, fixed-width types. Text in and out is UTF-8 and NUL terminated. Paths are full paths.
- Nothing thrown inside the library leaves it. A function that fails answers `CAMERAUNLOCK_ERROR`
  (-1), or `NULL` for `cameraunlock_aim_mode_label`. The reason is text: `cameraunlock_last_error`
  gives the calling thread's last one, and the same line goes to the log.
- The library never calls the host, and keeps no pointer the host passed.
- Every struct starts with `struct_size`, which the host sets to the size it was compiled with,
  in structs it fills and in structs the library fills. A size that is not the library's is
  refused, and the reason names both sizes.
- `cameraunlock_abi()` answers `CAMERAUNLOCK_ABI`. A host compares it with the number it was
  written against when it loads the DLL, and stops if they differ.
- Nothing joins a thread when the process ends. The receiver's and the poller's threads belong to
  objects that are never destroyed, so a host's process just ends. `cameraunlock_session_stop`
  joins the receiver's threads, for a host that stops tracking while it keeps running.

## Threads

| Calls | Thread |
|---|---|
| `cameraunlock_session_frame`, then `cameraunlock_session_lean` | One thread, the one that draws. The pair is one frame's work. |
| `cameraunlock_session_configure`, `_start`, `_stop` | Any. They wait for a frame in progress. |
| `cameraunlock_session_cycle_tracking_mode`, `_cycle_aim_mode`, `cameraunlock_hotkeys_take`, `_drop`, `cameraunlock_log_*`, `cameraunlock_last_error`, `cameraunlock_abi`, `cameraunlock_aim_mode_label` | Any, at any time. |
| `cameraunlock_config_*` | Any, one at a time between them. `_load`, `_reload`, `_save_*` and `_render` read or write a file: never from the thread that draws. |

A mode cycled on another thread is seen by the next frame.

## The frame

Once per rendered frame the host fills a `CameraUnlockFrameInput` and calls
`cameraunlock_session_frame`. It gets back, in one `CameraUnlockFrame`, the pose to apply and
what state the tracker is in.

What goes in:

| Member | Meaning |
|---|---|
| `flags` | `CAMERAUNLOCK_FRAME_ACTIVE` when tracking applies this frame. Clear in a menu, on a loading screen or with the master toggle off: the pipeline's state is dropped, and the next active frame starts at the tracker's pose. `_LEAN` when the host applies a positional lean. `_AIMING` while the sights are up. `_RIG_AVAILABLE` when the transform that carries the weapon can be moved. `_CLOCK` to have core measure time. |
| `now_ms` | A clock in milliseconds that never goes back. The aim transitions run on it: one that went back would hold a transition where it is. |
| `delta_seconds` | The frame time. With `_CLOCK`, the longest frame taken whole, and core measures the frame time and the clock itself. |
| `tan_half_fov`, `tan_half_fov_base` | The tangent of half the field of view drawn now, and of half the game's un-zoomed one, measured the same way up the picture. Equal when nothing is zoomed. |
| `forward_stop` | With the sights up, how far along the aim a lean may take the eye. Infinity for no stop. |
| `aim_forward` | The way the clean camera looks, unit length, in the host's world axes. |
| `tracker_to_world` | Three rows of three. The lean in the host's world axes is this matrix times the tracker's (x, y, z). It carries the host's axis signs and the way the body faces, which are the host's to know. |

What comes out:

| Member | Meaning |
|---|---|
| `flags` | `CAMERAUNLOCK_STATE_LISTENING`: the port is bound. `_POSE`: tracking applies and the tracker has sent a pose. `_FRESH`: a packet arrived within `data_freshness_ms`. `_REMOTE`: the sender is not on loopback. `_ROTATION`: yaw, pitch and roll are to be applied. `_LEAN` and `_LEAN_QUERY`: see below. `_RELEASE_RIG`: the rig carried a share of the lean last frame and no longer does. `_LOG`: `cameraunlock_log_take` has lines. |
| `yaw`, `pitch`, `roll` | Degrees, in the tracker's senses. What the view turns by: yaw and pitch scaled by the zoom and by stock sights, roll as the tracker gave it. |
| `light_yaw`, `light_pitch`, `light_roll` | The same three times `light_multiplier`, for a carried light. |
| `head_yaw` to `head_z` | The tracker's pose after interpolation, smoothing and the position limits, before the zoom and stock sights. Degrees, and metres in the tracker's axes. For a trace. |
| `pose_share` | What of yaw, pitch and the lean reaches the view in stock sights: 1 at the hip, 0 with the sights up. |
| `zoom_factor`, `delta_seconds` | The zoom factor and the frame time used. |
| `tracking_mode`, `aim_mode` | The modes this frame ran in. |

The pose is in the tracker's axes and signs. Converting it to the engine's is the host's one piece
of pipeline code: negative z is the forward lean, and the doctrine's "Tracker axis signs" says
where to start for yaw, roll and x.

A tracker that goes quiet keeps `_POSE` and loses `_FRESH`: the view holds the last pose.

## The lean

No function takes a callback, so the world query the lean clamp needs stays in the host and its
answer is handed in. When a frame's flags carry `CAMERAUNLOCK_STATE_LEAN`, the host calls
`cameraunlock_session_lean` once before the next frame:

1. If the flags also carry `CAMERAUNLOCK_STATE_LEAN_QUERY`, the host measures its world from the
   clean eye along `query_direction` for `query_reach`, and fills a `CameraUnlockObstruction`:
   `queried` 1, `blocked` 1 and the `distance` to the surface, or `blocked` 0 for a clear path.
   `queried` 0 means the query could not run: the lean passes unclamped and
   `CAMERAUNLOCK_LEAN_QUERY_FAILED` is set, for the host to log.
2. `cameraunlock_session_lean` answers a `CameraUnlockLean`: `camera`, the share that moves the
   view alone, and `rig`, the share that moves the weapon and the round's start point with the
   eye, both in the host's world axes. The eye is at the clean eye plus both. `asked` and `given`
   are the lean's length before and after the world, and `CAMERAUNLOCK_LEAN_CONTACT` says the
   eye is held short.

Core holds the eye `collision_margin` short of the surface, tightens at once and opens again
slowly, and hands the lean across the aim to the rig as the sights come up. A host whose query
sweeps a shape of its own reports the distance at which the eye must stop plus the margin.

A frame that asked for a lean and never got the call is an error at the next frame.

## Settings

`cameraunlock_settings_defaults` fills a `CameraUnlockSettings` with core's defaults, and
`cameraunlock_session_configure` puts one on the session: the tracking mode, the aim mode, the two
smoothing values, the five position limits, the lean clamp's margin and release, whether the clamp
runs, `data_freshness_ms` and `light_multiplier`. A host with a config file passes the `settings`
member of the `CameraUnlockConfig` the load filled, as it is.

`cameraunlock_session_cycle_tracking_mode` and `cameraunlock_session_cycle_aim_mode` go to the
next mode and answer it. `cameraunlock_aim_mode_label` is the line every mod shows for an aim mode.

## The receiver

`cameraunlock_session_start(port)` listens for OpenTrack datagrams. A port that cannot be bound is
not an error: the system's own reason goes to the log, the bind is tried again every 500 ms, and
`CAMERAUNLOCK_STATE_LISTENING` says when it holds. Starting a started session is an error.

What a host tells its users about the tracker:

- It sends the OpenTrack UDP protocol to the port in `[Network] UdpPort`, over IPv4. Core listens
  on IPv4 only, so a tracker pointed at `::1` or another IPv6 address reaches nothing.
- A tracker sending to `127.0.0.1` (any `127.x.x.x`) is local and gets `LocalSmoothing`. One
  sending to any other address is remote and gets `RemoteSmoothing`, this machine's own network
  address included.
- One tracker at a time. The receiver follows the first sender it hears and ignores a second
  until the first has been silent for two seconds, and the log names both.
- A datagram is refused whole when a value is not a number, an angle is past 360 degrees or a
  position past 100 m.

## The config

A host describes its `CameraUnlock.ini` at run time, then loads it:

1. `cameraunlock_config_describe(display_name)`.
2. `cameraunlock_config_concept(name, flags, comment, default_text)` for each row of the fleet's
   vocabulary, by its name in `data/config-schema.json`. `CAMERAUNLOCK_ROW_WRITABLE` marks a row a
   save may change. `default_text` gives a concept that is not global (`CollisionMargin`,
   `CollisionChannel`) the game's own default, and with `CAMERAUNLOCK_ROW_PER_GAME` a global one,
   which needs the owner-approved `per_game` entry `docs/canonical-config.md` describes.
3. `cameraunlock_config_local_bool`, `_int`, `_float`, `_enum` and `_hotkey` for the game's own
   rows. Each answers the row's number. `CAMERAUNLOCK_ROW_LIVE` marks a row
   `cameraunlock_config_reload` reads again while the game runs.
4. `cameraunlock_config_load(path, defaults_path, out)`. `defaults_path` is `NULL` in a mod, for
   the player's own Defaults.ini, and a scratch file in every test.

Every rule the config owner and the table enforce on a C++ table holds for a described one,
because the description builds the same table: a refused row is an error at the call that added
it, with the table's own reason.

`cameraunlock_config_render(path)` writes the file a first start creates. A mod's build calls it
after the same description to produce its committed `config/CameraUnlock.ini`, which the README's
config block is generated from. The description therefore lives in one place in the host, called
by both the mod and its build.

`cameraunlock_config_get_int` and `_get_float` read a local row. `cameraunlock_config_save_int`
and `_save_float` write one. `cameraunlock_config_save_tracking_mode` and `_save_aim_mode` write
the session's modes, and `_save_world_space_yaw` the yaw mode, which the host holds.

## Hotkeys

`cameraunlock_hotkeys_start` puts the loaded file's key lists on core's poller: the fleet's four
where the description has their rows, and every local hotkey row with the bit it was described
with. `cameraunlock_hotkeys_take` answers the actions whose keys went down since the last call, as
bits, and forgets them. `cameraunlock_hotkeys_drop` forgets them unanswered: a host calls it when
it starts acting on keys again, so a key pressed in a menu does not fire on the first frame of
play.

## The log

`cameraunlock_log_open(path)` opens core's file log, which keeps the previous run's file as
`<name>.prev.log` and flushes each line. From then on core's lines and the host's
(`cameraunlock_log_write`) go to that one file. A host that keeps a log of its own never opens it
and drains core's lines with `cameraunlock_log_take` when a frame's flags carry
`CAMERAUNLOCK_STATE_LOG`.

## The game's window

`cameraunlock_window_center(window)` is `os::CenterWindowInWorkArea` for a host: it centres a
windowed, bordered game window in the work area of the monitor it is on, keeps no latch and never
activates the window, so a host calls it after every placement the game makes. It answers 1 when
the window is at the centred origin on return and 0 when it was left alone, and the reason for
either a move or a refusal goes to the log.

## Testing without a socket

`CameraUnlockCoreTesting.dll` (CMake target `cameraunlock_c_testing`, never shipped) is the same
library with two more functions, declared in `cameraunlock/c/testing/cameraunlock_testing.h`:
`cameraunlock_testing_deliver` hands a session that is not started one datagram as if it had just
arrived, and `cameraunlock_testing_reset` gives a fresh session. They are for core's vectors
harness and a binding's own. A mod has no use for them: a tracker's poses reach it as datagrams.

## Layouts

Every member is four bytes except `now_ms`, and no struct has padding. `cpp/tests/c_header.c`
holds these to the header, compiled as C.

| Struct | Bytes | Offsets |
|---|---|---|
| `CameraUnlockSettings` | 60 | `struct_size` 0, `tracking_mode` 4, `aim_mode` 8, `data_freshness_ms` 12, `collision_enabled` 16, `local_smoothing` 20, `remote_smoothing` 24, `limit_x` 28, `limit_y` 32, `limit_y_down` 36, `limit_z` 40, `limit_z_back` 44, `collision_margin` 48, `collision_release_smoothing` 52, `light_multiplier` 56 |
| `CameraUnlockFrameInput` | 80 | `struct_size` 0, `flags` 4, `now_ms` 8 (eight bytes), `delta_seconds` 16, `tan_half_fov` 20, `tan_half_fov_base` 24, `forward_stop` 28, `aim_forward` 32, `tracker_to_world` 44 |
| `CameraUnlockFrame` | 92 | `struct_size` 0, `flags` 4, `tracking_mode` 8, `aim_mode` 12, `yaw` 16, `pitch` 20, `roll` 24, `light_yaw` 28, `light_pitch` 32, `light_roll` 36, `head_yaw` 40, `head_pitch` 44, `head_roll` 48, `head_x` 52, `head_y` 56, `head_z` 60, `pose_share` 64, `zoom_factor` 68, `delta_seconds` 72, `query_direction` 76, `query_reach` 88 |
| `CameraUnlockObstruction` | 16 | `struct_size` 0, `queried` 4, `blocked` 8, `distance` 12 |
| `CameraUnlockLean` | 40 | `struct_size` 0, `flags` 4, `camera` 8, `rig` 20, `asked` 32, `given` 36 |
| `CameraUnlockConfig` | 76 | `struct_size` 0, `udp_port` 4, `enable_on_startup` 8, `world_space_yaw` 12, `settings` 16 |

A binding declares each struct once as a named layout in its own language and reads members by
name. The table is here to check that declaration against, not to copy numbers out of.

## What a new host writes

1. Load `CameraUnlockCore.dll` by full path and compare `cameraunlock_abi()`.
2. Describe the config, load it, hand `settings` to `cameraunlock_session_configure`, start the
   session on `udp_port`, start the hotkeys.
3. Each frame: one `cameraunlock_session_frame`, the engine boundary conversion of the pose, and
   where it leans, its world query and `cameraunlock_session_lean`.
4. On a thread of its own: `cameraunlock_config_reload` about once a second, and the saves.
5. A build step that runs the same description and `cameraunlock_config_render`.

## What it does not do

- It has one entry to the pipeline, the frame. So of the pipeline conformance vectors
  (`pixi run vectors-c`, harness in `scripts/pipeline-vectors/harness/c/`) it runs the ones that
  go datagram in, pose out, and the harness skips the rest by name with a reason: those that
  drive one interpolator or processor on its own, those that assert whether a datagram carried
  a trailer, and two whose stream drops to zero and then repeats, which the receiver's
  lost-tracker gate holds.
- No legacy import: a described config has no legacy file. A mod that published builds before the
  canonical format keeps its C++ table and import.
- No string, list or color local row, no `Engine()` row, and no concept row read again by
  `cameraunlock_config_reload`.
- `os::CenterGameWindowOnce`, the one-shot that also raises the window, is not exported.
- No camera cut signal for the lean clamp. An inactive frame resets it.
