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
  gives the calling thread's last one, and the same line goes to the log, once a second at most
  while one thread's call goes on failing for one reason, so a frame that fails every time does
  not write a line a frame.
- The library never calls the host, and keeps no pointer the host passed.
- Every struct starts with `struct_size`, which the host sets to the size it was compiled with,
  in structs it fills and in structs the library fills. A size that is not the library's is
  refused, and the reason names both sizes.
- `cameraunlock_abi()` answers `CAMERAUNLOCK_ABI`. A host compares it with the number it was
  written against when it loads the DLL, before it looks up any other function, and stops if they
  differ. The number is raised when a function is added, when a function's meaning changes and
  when a struct's layout changes, so the lower of the two numbers is the older side.
- `cameraunlock_struct_size(which)` answers the size the library has for a struct. A binding that
  declares the structs in its own language compares each with its declaration at load.
- Nothing joins a thread when the process ends. The receiver's and the poller's threads belong to
  objects that are never destroyed, so a host's process just ends. `cameraunlock_session_stop`
  joins the receiver's threads, for a host that stops tracking while it keeps running.

## Threads

| Calls | Thread |
|---|---|
| `cameraunlock_session_frame`, then `cameraunlock_session_lean` | One thread, the one that draws. The pair is one frame's work. |
| `cameraunlock_view_frame`, then `cameraunlock_view_lean`, for each view | The same thread, one view after another. The pair is one view's work for the frame. |
| `cameraunlock_session_configure`, `_start`, `_stop`, `cameraunlock_view_start`, `_stop` | Any. They wait for a frame in progress. |
| `cameraunlock_session_cycle_tracking_mode`, `_cycle_aim_mode`, `_set_aim_mode`, `cameraunlock_hotkeys_take`, `_drop`, `cameraunlock_log_*`, `cameraunlock_last_error`, `cameraunlock_abi`, `cameraunlock_struct_size`, `cameraunlock_aim_mode_label` | Any, at any time. |
| `cameraunlock_config_*` | Any, one at a time between them. `_load`, `_reload`, `_save_*`, `_option_save` and `_render` read or write a file: never from the thread that draws. |

A mode cycled on another thread is seen by the next frame, of every view.

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
`cameraunlock_session_configure` puts one on the session, whole, between one frame and the next:
the tracking mode, the aim mode, the two
smoothing values, the five position limits, the lean clamp's margin and release, whether the clamp
runs, `data_freshness_ms` and `light_multiplier`. A host with a config file passes the `settings`
member of the `CameraUnlockConfig` the load filled, as it is.

`cameraunlock_session_cycle_tracking_mode` and `cameraunlock_session_cycle_aim_mode` go to the
next mode and answer it. `cameraunlock_aim_mode_label` is the line every mod shows for an aim mode.
`cameraunlock_session_set_aim_mode` puts the session in one mode, for a game whose cycle is not
core's four: where the game draws a reticle of its own whenever the sights are up, free look
without a marker is free look with one, and the host steps over it.

## The receiver

`cameraunlock_session_start(port)` listens for OpenTrack datagrams. A port that cannot be bound is
not an error: the system's own reason goes to the log, the bind is tried again every 500 ms, and
`CAMERAUNLOCK_STATE_LISTENING` says when it holds. Starting a started session is an error. The log
has one of two lines for every start, `Listening for OpenTrack datagrams on UDP port <n>` or
`Failed to bind UDP port <n>: <the system's reason>`, and `Bound UDP port <n> after <s>s of
waiting` when a later try holds. A host's troubleshooting text can name them.

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

## Views

A game that draws more than one first person view in a frame, as split screen does, gives each
its own view: `CAMERAUNLOCK_VIEWS` of them, numbered from 0. A view is one camera and one tracker.
It has a receiver on a port of its own, and its own interpolation, smoothing state, lean clamp,
aim transitions, frame clock and waiting lean, so one player's head, wall and sights are never
another's.

| Function | Is, for the view named |
|---|---|
| `cameraunlock_view_start(view, port)` | `cameraunlock_session_start` |
| `cameraunlock_view_stop(view)` | `cameraunlock_session_stop` |
| `cameraunlock_view_frame(view, input, out)` | `cameraunlock_session_frame` |
| `cameraunlock_view_lean(view, obstruction, out)` | `cameraunlock_session_lean` |

- View 0 is the session. Each `cameraunlock_session_*` function of the four is its
  `cameraunlock_view_*` one with 0, so a host with one view calls what it always did, and one that
  adds a second player starts view 1 beside it.
- The settings and the two modes are not a view's. `cameraunlock_session_configure`,
  `_cycle_tracking_mode`, `_cycle_aim_mode` and `_set_aim_mode` reach every view, one started
  later included, and every view's frame answers the same `tracking_mode` and `aim_mode`.
  `cameraunlock_config_save_tracking_mode` and `_save_aim_mode` save that one pair.
- A view outside 0 to `CAMERAUNLOCK_VIEWS` - 1 is an error, and the reason names the number.
- Two started views cannot listen on one port: two receivers there would split one tracker's
  datagrams between two players. The second start is an error that names both views and the port.
  A stopped view's port is free again.
- A view that was never started draws as a session that was never started does: its frame
  succeeds, with no `_LISTENING` and no `_POSE`.
- A frame and its lean are a pair in each view. A view whose frame asked for a lean and never got
  the call has the error at its own next frame, and the other views go on.
- A line in the log about a view past 0 ends in ` (view <n>)`, the receiver's own lines included:
  `Listening for OpenTrack datagrams on UDP port 4243 (view 1)`. View 0's lines are the
  session's, unmarked.

Which tracker belongs to which player is the host's to say: it starts each view on a port, and
tells its users which port is whose.

## The config

A host describes its `CameraUnlock.ini` at run time, then loads it:

1. `cameraunlock_config_describe(display_name)`.
2. `cameraunlock_config_concept(name, flags, comment, default_text)` for each row of the fleet's
   vocabulary, by its name in `data/config-schema.json`. `CAMERAUNLOCK_ROW_WRITABLE` marks a row a
   save may change. `default_text` gives a concept that is not global (`CollisionMargin`,
   `CollisionChannel`) the game's own default, and with `CAMERAUNLOCK_ROW_PER_GAME` a global one,
   which needs the owner-approved `per_game` entry `docs/canonical-config.md` describes.
3. `cameraunlock_config_local_bool`, `_int`, `_float`, `_enum`, `_hotkey`, `_hotkey_held` and
   `_hotkey_taps` for the game's own rows. Each answers the row's number. `CAMERAUNLOCK_ROW_LIVE` marks a row
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

## Options

A host whose game has an options screen a mod can add to fills it from the loaded file, with no
list of its own to keep in step with the rows:

1. `cameraunlock_config_option_count()`, then for each option `cameraunlock_config_option(option,
   out)` and `cameraunlock_config_option_text(option, which, buffer, capacity)` for its id, its
   section, its label, the file's comment and each word of an enum.
2. One control per option by its `kind`: a tick box for `CAMERAUNLOCK_OPTION_BOOL`, a slider from
   `min` to `max` in notches of `step` for `_INT` and `_FLOAT`, a list of its `choices` words for
   `_ENUM`. The label names it and the comment is its tooltip.
3. `cameraunlock_config_option_get(option, out)` for what the control shows, read again each
   time the screen opens: a hotkey or a hand edit of the file may have changed it since.
4. `cameraunlock_config_option_save(option, value)` when the player applies a change, then the
   host applies the value to its own running state, as after any save.

The options are `ConfigTable::Options()` of the described table
([canonical-config.md](canonical-config.md#options-for-a-games-own-options-screen)): each row
marked `CAMERAUNLOCK_ROW_WRITABLE` that holds a bool, an int, a float or an enum, in the order of
the file. `RotationEnabled` and `PositionEnabled` are the one option `TrackingMode`, and
`TrueFreeLook`, `FreeLookMarker` and `StockSights` the one option `AimMode`: their `source` says
so, their value is a `CAMERAUNLOCK_TRACKING_*` or a `CAMERAUNLOCK_AIM_*`, a get answers the
session's mode, and a save puts the mode on the session before it writes the rows. A key list is
no option, and nor is a row that is not writable: marking a row writable is how a host puts it on
the screen.

A value is one `double` whatever the option holds: a bool as 0 or 1, an int, a float, an enum as
its word's place. A value the option does not hold is an error, with nothing written and nothing
put on the session.

A game without the whole aim cycle leaves a word out of its list and maps places itself:
`CAMERAUNLOCK_OPTION_AIM_MODE` tells it which option that is.

## Hotkeys

`cameraunlock_hotkeys_start` puts the loaded file's key lists on core's poller: the fleet's four
where the description has their rows, and every local hotkey row with the bit it was described
with. `cameraunlock_hotkeys_take` answers the actions whose keys went down since the last call, as
bits, and forgets them. `cameraunlock_hotkeys_drop` forgets them unanswered: a host calls it when
it starts acting on keys again, so a key pressed in a menu does not fire on the first frame of
play.

A key that does one thing tapped and another held is a row of
`cameraunlock_config_local_hotkey_held(key, comment, flags, default_keys, hotkey_bit, held_bit)`.
It is a key list under `[Hotkeys]` like a row of `cameraunlock_config_local_hotkey`, in the file
and in every rule, with two bits where that has one:

- `hotkey_bit` is answered when a key of the list is let go less than `CAMERAUNLOCK_HOLD_MS`
  (400 ms) after it went down. A tap answers at the release, not as the key goes down.
- `held_bit` is answered once, when the key has been down that long and is still down. Nothing is
  answered when it is then let go.
- Each is one bit at `CAMERAUNLOCK_HOTKEY_LOCAL` or above, the two differ, and neither is another
  row's bit, tapped or held.
- A chord is judged as its key goes down: `Ctrl+Shift+J` with Ctrl let go before J is still that
  chord's tap or hold, and a bare key pressed with Ctrl and Shift both held is neither.
- As with every hotkey, nothing is answered while the game is not in the foreground, and a press
  that began there answers neither bit, however it ends.
- `cameraunlock_hotkeys_drop` also ends every such press that is down as it is called: that press
  answers neither bit, when it is let go or when it has been held, and the next press is a press
  like any other. So a key that went down in a menu, where the host drops, does not answer in
  play.
- The poller looks at the keys every 16 ms. When more than 100 ms pass between two looks, a key
  down at both may have been let go and pressed again unseen, so it is timed as a press begun at
  the second look.

A key that does one thing tapped, another tapped twice and a third held is a row of
`cameraunlock_config_local_hotkey_taps(key, comment, flags, default_keys, hotkey_bit, double_bit,
held_bit)`, with three bits:

- `hotkey_bit` is answered for a tap, a press let go before `CAMERAUNLOCK_HOLD_MS`, once
  `CAMERAUNLOCK_DOUBLE_TAP_MS` (300 ms) have passed since it was let go with no second press. A
  tap is late by that much, which is what telling it from a double tap costs.
- `double_bit` is answered as a second press goes down within that time. That press answers
  nothing else, however long it is held and when it is let go, and the first tap's bit is never
  answered. The press after it starts afresh.
- `held_bit` is answered once, when a first press has been down `CAMERAUNLOCK_HOLD_MS` and is
  still down. Nothing is answered when it is let go, and a press right after it starts afresh: a
  hold and a tap make no double tap.
- Each is one bit at `CAMERAUNLOCK_HOTKEY_LOCAL` or above, the three differ, and none is a bit of
  another row of any kind.
- A chord is judged as each press goes down. A press that is not the chord takes no part: it is
  no tap, no hold and no second press, and a tap waiting for its time is left waiting and is
  answered when the time is up, as if that press had not been made.
- The foreground rule is the same: a press begun while the game is not in the foreground takes no
  part either, a tap let go there is no tap, and a tap whose time runs out there is dropped, not
  answered late.
- `cameraunlock_hotkeys_drop` forgets a tap that is waiting, as well as ending every press that
  is down.
- After more than 100 ms between two looks at the keys, a waiting tap is dropped and a key still
  down is a first press begun at the second look.

A background test taps, double taps and holds such a key through isolated input's `tap <key> <ms>`
and `wait <ms>` ([isolated-input.md](isolated-input.md), The command file).

## The log

`cameraunlock_log_open(path)` opens core's file log, which keeps the previous run's file as
`<name>.prev.log` and flushes each line. A file that cannot be created is an error. From then on
core's lines and the host's (`cameraunlock_log_write`) go to that one file. An entry may be of any
length and hold line breaks, as a stack trace does. A host that keeps a log of its own never opens it
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
library with three more functions, declared in `cameraunlock/c/testing/cameraunlock_testing.h`:
`cameraunlock_testing_deliver` hands a session that is not started one datagram as if it had just
arrived, `cameraunlock_testing_deliver_view` does the same for one view, and
`cameraunlock_testing_reset` gives a fresh session, in every view. They are for core's vectors
harness and a binding's own. A mod has no use for them: a tracker's poses reach it as datagrams.

## Layouts

Every member is four bytes except `now_ms` and the three doubles of `CameraUnlockOption`, and no
struct has padding. `cpp/tests/c_header.c`
holds these to the header, compiled as C.

| Struct | Bytes | Offsets |
|---|---|---|
| `CameraUnlockSettings` | 60 | `struct_size` 0, `tracking_mode` 4, `aim_mode` 8, `data_freshness_ms` 12, `collision_enabled` 16, `local_smoothing` 20, `remote_smoothing` 24, `limit_x` 28, `limit_y` 32, `limit_y_down` 36, `limit_z` 40, `limit_z_back` 44, `collision_margin` 48, `collision_release_smoothing` 52, `light_multiplier` 56 |
| `CameraUnlockFrameInput` | 80 | `struct_size` 0, `flags` 4, `now_ms` 8 (eight bytes), `delta_seconds` 16, `tan_half_fov` 20, `tan_half_fov_base` 24, `forward_stop` 28, `aim_forward` 32, `tracker_to_world` 44 |
| `CameraUnlockFrame` | 92 | `struct_size` 0, `flags` 4, `tracking_mode` 8, `aim_mode` 12, `yaw` 16, `pitch` 20, `roll` 24, `light_yaw` 28, `light_pitch` 32, `light_roll` 36, `head_yaw` 40, `head_pitch` 44, `head_roll` 48, `head_x` 52, `head_y` 56, `head_z` 60, `pose_share` 64, `zoom_factor` 68, `delta_seconds` 72, `query_direction` 76, `query_reach` 88 |
| `CameraUnlockObstruction` | 16 | `struct_size` 0, `queried` 4, `blocked` 8, `distance` 12 |
| `CameraUnlockLean` | 40 | `struct_size` 0, `flags` 4, `camera` 8, `rig` 20, `asked` 32, `given` 36 |
| `CameraUnlockConfig` | 76 | `struct_size` 0, `udp_port` 4, `enable_on_startup` 8, `world_space_yaw` 12, `settings` 16 |
| `CameraUnlockOption` | 40 | `struct_size` 0, `kind` 4, `source` 8, `choices` 12, `min` 16 (eight bytes), `max` 24 (eight bytes), `step` 32 (eight bytes) |

A binding declares each struct once as a named layout in its own language and reads members by
name. The table is here to check that declaration against, not to copy numbers out of.

## The Java binding

`java/src/com/cameraunlock/core/CameraUnlock.java` is this interface for a Java host, through
`java.lang.foreign`: one method for each function and one class with public fields for each
struct. A mod compiles it into its own jar from the submodule's source.
[java-agent-mod.md](java-agent-mod.md) is the page for a mod that uses it.

- Java 22 or later. `pixi run build-java` compiles it for 22.
- `CameraUnlock.load()` loads `CameraUnlockCore.dll` from the folder its jar or class folder is in,
  and from nowhere else.
- At load it compares `cameraunlock_abi()` with the version it was written for, then each
  `cameraunlock_struct_size` with its own declaration. A difference throws, and the message names
  the file, both versions and the older side.
- Each struct is declared once, as a layout with the header's member names, and every member is
  read and written through its name. `pixi run test-java` holds those layouts to the table above.
- A call that answers `CAMERAUNLOCK_ERROR` throws an `IllegalStateException` whose message is
  `cameraunlock_last_error`.
- `frame`, `lean`, `viewFrame`, `viewLean`, `hotkeysTake` and `hotkeysDrop` allocate nothing.
- It needs no upcall, as the interface has no callback.
- The JVM is started with `--enable-native-access=ALL-UNNAMED`. Without it Java 25 prints a warning
  at the first call and says a later release will refuse such calls.

`pixi run vectors-java` runs the pipeline vectors through it
(`java/harness/com/cameraunlock/core/ConformanceHarness.java`), the same five the C harness runs.

## What a new host writes

1. Load `CameraUnlockCore.dll` by full path and compare `cameraunlock_abi()`.
2. Describe the config, load it, hand `settings` to `cameraunlock_session_configure`, start the
   session on `udp_port`, start the hotkeys.
3. Each frame: one `cameraunlock_session_frame`, the engine boundary conversion of the pose, and
   where it leans, its world query and `cameraunlock_session_lean`. With more than one first
   person view, the same per view through `cameraunlock_view_frame` and `_lean`.
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
