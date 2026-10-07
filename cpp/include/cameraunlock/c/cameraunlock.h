/* cameraunlock-core's head tracking pipeline, config owner, hotkeys and log for a host that is
 * neither C++ nor C#. docs/c-interface.md is the contract; this header is its declarations.
 *
 * Every function is plain C, catches whatever is thrown inside it, and answers
 * CAMERAUNLOCK_ERROR (-1) for a failure, with the reason in cameraunlock_last_error and in the
 * log. Text in and out is UTF-8, NUL terminated. No function calls back into the host, and none
 * keeps a pointer the host passed.
 */
#ifndef CAMERAUNLOCK_C_CAMERAUNLOCK_H
#define CAMERAUNLOCK_C_CAMERAUNLOCK_H

#include <stdint.h>

#if defined(_WIN32) && defined(CAMERAUNLOCK_C_EXPORTS)
#define CAMERAUNLOCK_C __declspec(dllexport)
#else
#define CAMERAUNLOCK_C
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Raised when a function's meaning or a struct's layout changes. */
#define CAMERAUNLOCK_ABI 1

#define CAMERAUNLOCK_OK 0
#define CAMERAUNLOCK_ERROR (-1)

/* The CAMERAUNLOCK_ABI this library was built as. A host compares it with its own at load. */
CAMERAUNLOCK_C int32_t cameraunlock_abi(void);

/* The reason for the last CAMERAUNLOCK_ERROR on the calling thread. Answers its length in bytes
 * and copies it, with its NUL, when `capacity` holds length + 1. */
CAMERAUNLOCK_C int32_t cameraunlock_last_error(char* buffer, int32_t capacity);

/* ---- Log ------------------------------------------------------------------------------------ */

/* Opens core's file log at `path`, a full path. The file a previous run left is kept beside it as
 * <name>.prev.log. From here every line core has for the log, and every cameraunlock_log_write,
 * goes to this file, each flushed as it is written. */
CAMERAUNLOCK_C int32_t cameraunlock_log_open(const char* path);
/* One line for the file log. An error while no file is open. */
CAMERAUNLOCK_C int32_t cameraunlock_log_write(const char* line);
/* For a host with a log of its own, which never calls cameraunlock_log_open: the lines core has
 * for it, each ending in '\n'. Answers their length in bytes. They are copied to `buffer` and
 * forgotten only when `capacity` holds them all. CAMERAUNLOCK_STATE_LOG says when there are any. */
CAMERAUNLOCK_C int32_t cameraunlock_log_take(char* buffer, int32_t capacity);

/* ---- Session -------------------------------------------------------------------------------- */

#define CAMERAUNLOCK_TRACKING_ROTATION_AND_POSITION 0
#define CAMERAUNLOCK_TRACKING_ROTATION_ONLY 1
#define CAMERAUNLOCK_TRACKING_POSITION_ONLY 2

#define CAMERAUNLOCK_AIM_SIGHTS_LOCKED 0
#define CAMERAUNLOCK_AIM_FREE_LOOK_MARKER 1
#define CAMERAUNLOCK_AIM_TRUE_FREE_LOOK 2
#define CAMERAUNLOCK_AIM_STOCK_SIGHTS 3

/* What the session runs on. Every member is four bytes. */
typedef struct CameraUnlockSettings {
    uint32_t struct_size;
    int32_t tracking_mode;
    int32_t aim_mode;
    /* A tracker is quiet once this long has passed without a packet the receiver took. */
    int32_t data_freshness_ms;
    int32_t collision_enabled;
    float local_smoothing;
    float remote_smoothing;
    /* Metres, in the tracker's axes: x either way, y up and down, z forward (the tracker's
     * negative z) and back. */
    float limit_x;
    float limit_y;
    float limit_y_down;
    float limit_z;
    float limit_z_back;
    /* How far off a surface the lean clamp holds the eye, in the host's world units. */
    float collision_margin;
    float collision_release_smoothing;
    float light_multiplier;
} CameraUnlockSettings;

/* Fills `out` with core's defaults. The host sets out->struct_size first, here and in every
 * call that takes a struct. */
CAMERAUNLOCK_C int32_t cameraunlock_settings_defaults(CameraUnlockSettings* out);
/* Puts the settings on the session, the tracking mode and the aim mode included. */
CAMERAUNLOCK_C int32_t cameraunlock_session_configure(const CameraUnlockSettings* settings);

/* Starts listening for OpenTrack datagrams on the port. A port that cannot be bound is not an
 * error: the reason the system gave goes to the log and the bind is tried again every 500 ms
 * until it succeeds. An error when the session is already started. */
CAMERAUNLOCK_C int32_t cameraunlock_session_start(int32_t udp_port);
/* Stops listening and joins the receiver's threads. Not for process exit: a host that is
 * ending just ends, and nothing here joins a thread then. */
CAMERAUNLOCK_C int32_t cameraunlock_session_stop(void);

/* The mode after the current one. Each answers the new mode. */
CAMERAUNLOCK_C int32_t cameraunlock_session_cycle_tracking_mode(void);
CAMERAUNLOCK_C int32_t cameraunlock_session_cycle_aim_mode(void);
/* The line a mod shows and logs for an aim mode, the same in every mod. A static string. */
CAMERAUNLOCK_C const char* cameraunlock_aim_mode_label(int32_t aim_mode);

/* CameraUnlockFrameInput.flags */
/* Tracking applies this frame. Clear in a menu, a loading screen or with the master toggle off:
 * the pipeline's state is then dropped, so the next frame it applies starts at the tracker's
 * pose. */
#define CAMERAUNLOCK_FRAME_ACTIVE 0x1u
/* The host applies a positional lean this frame, and calls cameraunlock_session_lean for it. */
#define CAMERAUNLOCK_FRAME_LEAN 0x2u
/* The sights are up this frame. */
#define CAMERAUNLOCK_FRAME_AIMING 0x4u
/* The transform that carries the weapon can be moved with the eye this frame. */
#define CAMERAUNLOCK_FRAME_RIG_AVAILABLE 0x8u
/* Core measures the frame time and the clock itself: delta_seconds is then the longest frame
 * taken whole (a longer one counts as that long), and now_ms is not read. */
#define CAMERAUNLOCK_FRAME_CLOCK 0x10u

typedef struct CameraUnlockFrameInput {
    uint32_t struct_size;
    uint32_t flags;
    /* A clock that never goes back, in milliseconds. The aim transitions run on it. */
    uint64_t now_ms;
    float delta_seconds;
    /* The tangent of half the field of view being drawn now and of half the game's un-zoomed
     * one, both measured the same way up the picture. Equal when nothing is zoomed. */
    float tan_half_fov;
    float tan_half_fov_base;
    /* With the sights up, how far along the aim the lean may take the eye. Infinity for no
     * stop. */
    float forward_stop;
    /* The way the clean camera looks, unit length, in the host's world axes. */
    float aim_forward[3];
    /* Takes the tracker's position to a lean in the host's world axes: three rows of three,
     * lean = M * (x, y, z). The host's axis signs and the body's facing are both in it. */
    float tracker_to_world[9];
} CameraUnlockFrameInput;

/* CameraUnlockFrame.flags */
#define CAMERAUNLOCK_STATE_LISTENING 0x1u /* the port is bound */
#define CAMERAUNLOCK_STATE_POSE 0x2u      /* tracking applies and the tracker has sent a pose */
#define CAMERAUNLOCK_STATE_FRESH 0x4u     /* a packet arrived within data_freshness_ms */
#define CAMERAUNLOCK_STATE_REMOTE 0x8u    /* the tracker is not on this machine's loopback */
#define CAMERAUNLOCK_STATE_ROTATION 0x10u /* yaw, pitch and roll are to be applied */
/* A lean is to be applied: call cameraunlock_session_lean before the next frame. */
#define CAMERAUNLOCK_STATE_LEAN 0x20u
/* Measure the world along query_direction for query_reach first, and hand the answer in. */
#define CAMERAUNLOCK_STATE_LEAN_QUERY 0x40u
/* The rig carried a share of the lean last frame and no longer does: put it back. */
#define CAMERAUNLOCK_STATE_RELEASE_RIG 0x80u
#define CAMERAUNLOCK_STATE_LOG 0x100u     /* cameraunlock_log_take has lines */

typedef struct CameraUnlockFrame {
    uint32_t struct_size;
    uint32_t flags;
    int32_t tracking_mode;
    int32_t aim_mode;
    /* What the view turns by, in degrees and the tracker's senses: yaw and pitch scaled by the
     * zoom and by stock sights, roll as the tracker gave it. */
    float yaw;
    float pitch;
    float roll;
    /* The same three scaled by light_multiplier, for a carried light. */
    float light_yaw;
    float light_pitch;
    float light_roll;
    /* The tracker's pose after interpolation, smoothing and the position limits: degrees, and
     * metres in the tracker's axes. For a trace. */
    float head_yaw;
    float head_pitch;
    float head_roll;
    float head_x;
    float head_y;
    float head_z;
    /* What of yaw, pitch and the lean reaches the view in stock sights: 1 at the hip. */
    float pose_share;
    float zoom_factor;
    /* The frame time used, in seconds. */
    float delta_seconds;
    /* With CAMERAUNLOCK_STATE_LEAN_QUERY: a unit vector in the host's world axes, and how far
     * along it from the clean eye to look for a surface. */
    float query_direction[3];
    float query_reach;
} CameraUnlockFrame;

/* Once per rendered frame. */
CAMERAUNLOCK_C int32_t cameraunlock_session_frame(const CameraUnlockFrameInput* input, CameraUnlockFrame* out);

/* What the host's world query found along query_direction. */
typedef struct CameraUnlockObstruction {
    uint32_t struct_size;
    /* 0 when the query could not be run at all. The lean then passes unclamped and
     * CAMERAUNLOCK_LEAN_QUERY_FAILED is set. */
    int32_t queried;
    int32_t blocked;
    /* With blocked: from the clean eye to the surface along query_direction, in the host's
     * world units. Core holds the eye collision_margin short of it. */
    float distance;
} CameraUnlockObstruction;

#define CAMERAUNLOCK_LEAN_CONTACT 0x1u      /* the eye is held short of where the tracker asked */
#define CAMERAUNLOCK_LEAN_QUERY_FAILED 0x2u

typedef struct CameraUnlockLean {
    uint32_t struct_size;
    uint32_t flags;
    /* The lean in the host's world axes, split between what moves the view alone and what moves
     * the rig, the weapon and the round's start point with the eye. The eye is at clean +
     * camera + rig. */
    float camera[3];
    float rig[3];
    /* The length of the lean asked for and of the lean the world left room for. */
    float asked;
    float given;
} CameraUnlockLean;

/* Once after each frame whose flags carry CAMERAUNLOCK_STATE_LEAN. `obstruction` is read when
 * they also carry CAMERAUNLOCK_STATE_LEAN_QUERY, and may be NULL otherwise. */
CAMERAUNLOCK_C int32_t cameraunlock_session_lean(const CameraUnlockObstruction* obstruction, CameraUnlockLean* out);

/* ---- Config --------------------------------------------------------------------------------- */

/* Row flags. */
#define CAMERAUNLOCK_ROW_WRITABLE 0x1u /* a save may change the row */
#define CAMERAUNLOCK_ROW_PER_GAME 0x2u /* a global concept this game keeps its own default of */
/* A local row read again by cameraunlock_config_reload while the game runs. */
#define CAMERAUNLOCK_ROW_LIVE 0x4u

/* Starts the description of the game's CameraUnlock.ini. `display_name` is the game's name as
 * data/games.json spells it. The rows follow, in the order the file lists them within a section,
 * and the description ends at cameraunlock_config_load or cameraunlock_config_render. */
CAMERAUNLOCK_C int32_t cameraunlock_config_describe(const char* display_name);
/* A row of the fleet's vocabulary, by its name in data/config-schema.json ("LocalSmoothing").
 * `comment` replaces the schema's comment above the key, NULL to keep it. `default_text` is the
 * game's own default as the file would hold it, NULL for the schema's: allowed on a concept that
 * is not global, and on a global one with CAMERAUNLOCK_ROW_PER_GAME. */
CAMERAUNLOCK_C int32_t cameraunlock_config_concept(const char* name, uint32_t flags, const char* comment,
                                                   const char* default_text);
/* A row of the game's own. Each answers the row's number, counted from 0 in the order added,
 * which cameraunlock_config_get_* and cameraunlock_config_save_* take. */
CAMERAUNLOCK_C int32_t cameraunlock_config_local_bool(const char* section, const char* key, const char* comment,
                                                      uint32_t flags, int32_t default_value);
CAMERAUNLOCK_C int32_t cameraunlock_config_local_int(const char* section, const char* key, const char* comment,
                                                     uint32_t flags, int32_t default_value, int32_t min,
                                                     int32_t max);
CAMERAUNLOCK_C int32_t cameraunlock_config_local_float(const char* section, const char* key, const char* comment,
                                                       uint32_t flags, float default_value, float min, float max);
/* `tokens` is the words the file holds, PascalCase, separated by commas. The value is a word's
 * place in that list, counted from 0. */
CAMERAUNLOCK_C int32_t cameraunlock_config_local_enum(const char* section, const char* key, const char* comment,
                                                      uint32_t flags, const char* tokens, int32_t default_index);
/* A key list of the game's own under [Hotkeys]. `hotkey_bit` is the bit cameraunlock_hotkeys_take
 * answers with when one of its keys goes down: one bit, CAMERAUNLOCK_HOTKEY_LOCAL or above. */
CAMERAUNLOCK_C int32_t cameraunlock_config_local_hotkey(const char* key, const char* comment, uint32_t flags,
                                                        const char* default_keys, int32_t hotkey_bit);

/* The file the description renders for a first start, written to `path`: the committed
 * config/CameraUnlock.ini a mod's build keeps in step with its rows. */
CAMERAUNLOCK_C int32_t cameraunlock_config_render(const char* path);

/* ConfigLoadStatus, ConfigSaveStatus and ConfigReloadStatus, as core numbers them. */
#define CAMERAUNLOCK_LOAD_CANONICAL 0
#define CAMERAUNLOCK_LOAD_MIGRATED 1
#define CAMERAUNLOCK_LOAD_CREATED 2
#define CAMERAUNLOCK_LOAD_DEFERRED 3
#define CAMERAUNLOCK_LOAD_LEGACY_REFUSED 4
#define CAMERAUNLOCK_LOAD_UNREADABLE 5
#define CAMERAUNLOCK_SAVE_SAVED 0
#define CAMERAUNLOCK_SAVE_NOT_SAVED 1
#define CAMERAUNLOCK_SAVE_UNCERTAIN 2
#define CAMERAUNLOCK_RELOAD_UNCHANGED 0
#define CAMERAUNLOCK_RELOAD_APPLIED 1
#define CAMERAUNLOCK_RELOAD_UNREADABLE 3

/* The fleet's rows as the file gave them. */
typedef struct CameraUnlockConfig {
    uint32_t struct_size;
    int32_t udp_port;
    int32_t enable_on_startup;
    int32_t world_space_yaw;
    CameraUnlockSettings settings;
} CameraUnlockConfig;

/* Reads the game's CameraUnlock.ini at `path`, a full path, creating it where there is none.
 * `defaults_path` is NULL for the player's own Defaults.ini, and a full path to a scratch file in
 * every test. Answers a CAMERAUNLOCK_LOAD_*. Not from a per-frame path. */
CAMERAUNLOCK_C int32_t cameraunlock_config_load(const char* path, const char* defaults_path, CameraUnlockConfig* out);
/* Reads the file again when it or Defaults.ini has changed since it was last read or written.
 * Answers a CAMERAUNLOCK_RELOAD_*. On CAMERAUNLOCK_RELOAD_APPLIED the local rows flagged
 * CAMERAUNLOCK_ROW_LIVE hold what the file holds now; every other row keeps what the load gave
 * it. For a thread that is not drawing a frame, once a second or so. */
CAMERAUNLOCK_C int32_t cameraunlock_config_reload(void);

/* A local row's value: a bool as 0 or 1, an int, an enum as its word's place. */
CAMERAUNLOCK_C int32_t cameraunlock_config_get_int(int32_t row, int32_t* out);
CAMERAUNLOCK_C int32_t cameraunlock_config_get_float(int32_t row, float* out);

/* Each writes rows of the file and answers a CAMERAUNLOCK_SAVE_*. Synchronous: from a thread
 * that is not drawing a frame. The host has already applied the value to its running state. */
CAMERAUNLOCK_C int32_t cameraunlock_config_save_int(int32_t row, int32_t value);
CAMERAUNLOCK_C int32_t cameraunlock_config_save_float(int32_t row, float value);
/* The session's tracking mode as RotationEnabled and PositionEnabled, and its aim mode as
 * TrueFreeLook, FreeLookMarker and StockSights. */
CAMERAUNLOCK_C int32_t cameraunlock_config_save_tracking_mode(void);
CAMERAUNLOCK_C int32_t cameraunlock_config_save_aim_mode(void);
CAMERAUNLOCK_C int32_t cameraunlock_config_save_world_space_yaw(int32_t world_space_yaw);

/* ---- Hotkeys -------------------------------------------------------------------------------- */

#define CAMERAUNLOCK_HOTKEY_TOGGLE 0x1
#define CAMERAUNLOCK_HOTKEY_CYCLE_TRACKING_MODE 0x2
#define CAMERAUNLOCK_HOTKEY_YAW_MODE 0x4
#define CAMERAUNLOCK_HOTKEY_AIM_MODE 0x8
/* The lowest bit a row of the game's own may take. */
#define CAMERAUNLOCK_HOTKEY_LOCAL 0x100

/* Puts the loaded file's key lists on core's poller, the fleet's four where the description has
 * their rows and each local one, and starts its thread. Once per process. */
CAMERAUNLOCK_C int32_t cameraunlock_hotkeys_start(void);
/* The actions whose keys went down since the last take or drop, as bits, forgotten as they are
 * answered. */
CAMERAUNLOCK_C int32_t cameraunlock_hotkeys_take(void);
/* Forgets them without answering: for presses made where the host does not act on them. */
CAMERAUNLOCK_C void cameraunlock_hotkeys_drop(void);

/* ---- The game's window ---------------------------------------------------------------------- */

/* Centres a windowed, bordered game window in the work area of the monitor it is on
 * (os::CenterWindowInWorkArea). `window` is the HWND. Answers 1 when the window is at the centred
 * origin on return and 0 when it was left alone, with the reason in the log. It keeps no latch and
 * never activates the window, so a host calls it after every placement the game makes. */
CAMERAUNLOCK_C int32_t cameraunlock_window_center(uint64_t window);

#ifdef __cplusplus
}
#endif

#endif
