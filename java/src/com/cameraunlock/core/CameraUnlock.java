package com.cameraunlock.core;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemoryLayout.PathElement;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.StructLayout;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.VarHandle;
import java.net.URISyntaxException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;

/**
 * cameraunlock-core for a Java host: CameraUnlockCore.dll (docs/c-interface.md) through
 * java.lang.foreign, which is final from Java 22. One method for each function of
 * cpp/include/cameraunlock/c/cameraunlock.h, and one class for each struct.
 *
 * A call the library refuses throws an IllegalStateException whose message is the library's own
 * reason. frame, lean, viewFrame, viewLean and hotkeysTake allocate nothing.
 */
public final class CameraUnlock {
    /** The CAMERAUNLOCK_ABI this class was written against. */
    public static final int ABI = 5;
    /** The library's file, beside the jar or the class folder this class is in. */
    public static final String LIBRARY = "CameraUnlockCore.dll";

    public static final int TRACKING_ROTATION_AND_POSITION = 0, TRACKING_ROTATION_ONLY = 1, TRACKING_POSITION_ONLY = 2;
    public static final int AIM_SIGHTS_LOCKED = 0, AIM_FREE_LOOK_MARKER = 1, AIM_TRUE_FREE_LOOK = 2, AIM_STOCK_SIGHTS = 3;
    /** The views a host may run at once. View 0 is the session of start, stop, frame and lean. */
    public static final int VIEWS = 4;

    /** FrameInput.flags */
    public static final int FRAME_ACTIVE = 0x1, FRAME_LEAN = 0x2, FRAME_AIMING = 0x4, FRAME_RIG_AVAILABLE = 0x8,
            FRAME_CLOCK = 0x10;
    /** Frame.flags */
    public static final int STATE_LISTENING = 0x1, STATE_POSE = 0x2, STATE_FRESH = 0x4, STATE_REMOTE = 0x8,
            STATE_ROTATION = 0x10, STATE_LEAN = 0x20, STATE_LEAN_QUERY = 0x40, STATE_RELEASE_RIG = 0x80, STATE_LOG = 0x100;
    /** Lean.flags */
    public static final int LEAN_CONTACT = 0x1, LEAN_QUERY_FAILED = 0x2;

    public static final int ROW_WRITABLE = 0x1, ROW_PER_GAME = 0x2, ROW_LIVE = 0x4;
    public static final int LOAD_CANONICAL = 0, LOAD_MIGRATED = 1, LOAD_CREATED = 2, LOAD_DEFERRED = 3,
            LOAD_LEGACY_REFUSED = 4, LOAD_UNREADABLE = 5;
    public static final int SAVE_SAVED = 0, SAVE_NOT_SAVED = 1, SAVE_UNCERTAIN = 2;
    public static final int RELOAD_UNCHANGED = 0, RELOAD_APPLIED = 1, RELOAD_UNREADABLE = 3;
    /** Option.kind: a tick box, a slider of whole numbers, a slider, a list of words. */
    public static final int OPTION_BOOL = 0, OPTION_INT = 1, OPTION_FLOAT = 2, OPTION_ENUM = 3;
    /** Option.source: one row of the file, or the session's tracking mode or aim mode. */
    public static final int OPTION_ROW = 0, OPTION_TRACKING_MODE = 1, OPTION_AIM_MODE = 2;
    public static final int HOTKEY_TOGGLE = 0x1, HOTKEY_CYCLE_TRACKING_MODE = 0x2, HOTKEY_YAW_MODE = 0x4,
            HOTKEY_AIM_MODE = 0x8, HOTKEY_LOCAL = 0x100;
    /** How long a key of configLocalHotkeyHeld is down, in milliseconds, before it counts as held. */
    public static final int HOLD_MS = 400;
    /** How long after a key of configLocalHotkeyTaps is let go, in milliseconds, a second press still makes a double tap. */
    public static final int DOUBLE_TAP_MS = 300;

    private static final int ERROR = -1;
    private static final ValueLayout.OfInt INT = ValueLayout.JAVA_INT;
    private static final ValueLayout.OfLong LONG = ValueLayout.JAVA_LONG;
    private static final ValueLayout.OfFloat FLOAT = ValueLayout.JAVA_FLOAT;
    private static final ValueLayout.OfDouble DOUBLE = ValueLayout.JAVA_DOUBLE;
    private static final int OPTION_TEXT_ID = 0, OPTION_TEXT_SECTION = 1, OPTION_TEXT_LABEL = 2, OPTION_TEXT_COMMENT = 3,
            OPTION_TEXT_CHOICE = 16;
    private static final ValueLayout POINTER = ValueLayout.ADDRESS;

    // Each struct of the header, once, with the header's member names. Nothing below reads or
    // writes a member except through its name here.
    static final StructLayout SETTINGS = MemoryLayout.structLayout(
            INT.withName("struct_size"), INT.withName("tracking_mode"), INT.withName("aim_mode"),
            INT.withName("data_freshness_ms"), INT.withName("collision_enabled"), FLOAT.withName("local_smoothing"),
            FLOAT.withName("remote_smoothing"), FLOAT.withName("limit_x"), FLOAT.withName("limit_y"),
            FLOAT.withName("limit_y_down"), FLOAT.withName("limit_z"), FLOAT.withName("limit_z_back"),
            FLOAT.withName("collision_margin"), FLOAT.withName("collision_release_smoothing"),
            FLOAT.withName("light_multiplier")).withName("CameraUnlockSettings");
    static final StructLayout FRAME_INPUT = MemoryLayout.structLayout(
            INT.withName("struct_size"), INT.withName("flags"), LONG.withName("now_ms"), FLOAT.withName("delta_seconds"),
            FLOAT.withName("tan_half_fov"), FLOAT.withName("tan_half_fov_base"), FLOAT.withName("forward_stop"),
            MemoryLayout.sequenceLayout(3, FLOAT).withName("aim_forward"),
            MemoryLayout.sequenceLayout(9, FLOAT).withName("tracker_to_world")).withName("CameraUnlockFrameInput");
    static final StructLayout FRAME = MemoryLayout.structLayout(
            INT.withName("struct_size"), INT.withName("flags"), INT.withName("tracking_mode"), INT.withName("aim_mode"),
            FLOAT.withName("yaw"), FLOAT.withName("pitch"), FLOAT.withName("roll"), FLOAT.withName("light_yaw"),
            FLOAT.withName("light_pitch"), FLOAT.withName("light_roll"), FLOAT.withName("head_yaw"),
            FLOAT.withName("head_pitch"), FLOAT.withName("head_roll"), FLOAT.withName("head_x"), FLOAT.withName("head_y"),
            FLOAT.withName("head_z"), FLOAT.withName("pose_share"), FLOAT.withName("zoom_factor"),
            FLOAT.withName("delta_seconds"), MemoryLayout.sequenceLayout(3, FLOAT).withName("query_direction"),
            FLOAT.withName("query_reach")).withName("CameraUnlockFrame");
    static final StructLayout OBSTRUCTION = MemoryLayout.structLayout(
            INT.withName("struct_size"), INT.withName("queried"), INT.withName("blocked"), FLOAT.withName("distance"))
            .withName("CameraUnlockObstruction");
    static final StructLayout LEAN = MemoryLayout.structLayout(
            INT.withName("struct_size"), INT.withName("flags"), MemoryLayout.sequenceLayout(3, FLOAT).withName("camera"),
            MemoryLayout.sequenceLayout(3, FLOAT).withName("rig"), FLOAT.withName("asked"), FLOAT.withName("given"))
            .withName("CameraUnlockLean");
    static final StructLayout CONFIG = MemoryLayout.structLayout(
            INT.withName("struct_size"), INT.withName("udp_port"), INT.withName("enable_on_startup"),
            INT.withName("world_space_yaw"), SETTINGS.withName("settings")).withName("CameraUnlockConfig");
    static final StructLayout OPTION = MemoryLayout.structLayout(
            INT.withName("struct_size"), INT.withName("kind"), INT.withName("source"), INT.withName("choices"),
            DOUBLE.withName("min"), DOUBLE.withName("max"), DOUBLE.withName("step")).withName("CameraUnlockOption");

    /** The header's CAMERAUNLOCK_STRUCT_* numbers, in order. */
    private static final StructLayout[] STRUCTS = {SETTINGS, FRAME_INPUT, FRAME, OBSTRUCTION, LEAN, CONFIG, OPTION};

    private static VarHandle member(StructLayout struct, String name) {
        return struct.varHandle(PathElement.groupElement(name));
    }

    private static VarHandle element(StructLayout struct, String name) {
        return struct.varHandle(PathElement.groupElement(name), PathElement.sequenceElement());
    }

    private static MemorySegment sized(StructLayout struct) {
        MemorySegment segment = Arena.global().allocate(struct);
        member(struct, "struct_size").set(segment, 0L, (int) struct.byteSize());
        return segment;
    }

    /** What the session runs on: CameraUnlockSettings. */
    public static final class Settings {
        public int trackingMode, aimMode, dataFreshnessMs;
        public boolean collisionEnabled;
        public float localSmoothing, remoteSmoothing, limitX, limitY, limitYDown, limitZ, limitZBack, collisionMargin,
                collisionReleaseSmoothing, lightMultiplier;

        private static final VarHandle TRACKING_MODE = member(SETTINGS, "tracking_mode"), AIM_MODE = member(SETTINGS, "aim_mode"),
                DATA_FRESHNESS_MS = member(SETTINGS, "data_freshness_ms"), COLLISION_ENABLED = member(SETTINGS, "collision_enabled"),
                LOCAL_SMOOTHING = member(SETTINGS, "local_smoothing"), REMOTE_SMOOTHING = member(SETTINGS, "remote_smoothing"),
                LIMIT_X = member(SETTINGS, "limit_x"), LIMIT_Y = member(SETTINGS, "limit_y"),
                LIMIT_Y_DOWN = member(SETTINGS, "limit_y_down"), LIMIT_Z = member(SETTINGS, "limit_z"),
                LIMIT_Z_BACK = member(SETTINGS, "limit_z_back"), COLLISION_MARGIN = member(SETTINGS, "collision_margin"),
                COLLISION_RELEASE_SMOOTHING = member(SETTINGS, "collision_release_smoothing"),
                LIGHT_MULTIPLIER = member(SETTINGS, "light_multiplier");

        private void read(MemorySegment from) {
            trackingMode = (int) TRACKING_MODE.get(from, 0L);
            aimMode = (int) AIM_MODE.get(from, 0L);
            dataFreshnessMs = (int) DATA_FRESHNESS_MS.get(from, 0L);
            collisionEnabled = (int) COLLISION_ENABLED.get(from, 0L) != 0;
            localSmoothing = (float) LOCAL_SMOOTHING.get(from, 0L);
            remoteSmoothing = (float) REMOTE_SMOOTHING.get(from, 0L);
            limitX = (float) LIMIT_X.get(from, 0L);
            limitY = (float) LIMIT_Y.get(from, 0L);
            limitYDown = (float) LIMIT_Y_DOWN.get(from, 0L);
            limitZ = (float) LIMIT_Z.get(from, 0L);
            limitZBack = (float) LIMIT_Z_BACK.get(from, 0L);
            collisionMargin = (float) COLLISION_MARGIN.get(from, 0L);
            collisionReleaseSmoothing = (float) COLLISION_RELEASE_SMOOTHING.get(from, 0L);
            lightMultiplier = (float) LIGHT_MULTIPLIER.get(from, 0L);
        }

        private void write(MemorySegment to) {
            TRACKING_MODE.set(to, 0L, trackingMode);
            AIM_MODE.set(to, 0L, aimMode);
            DATA_FRESHNESS_MS.set(to, 0L, dataFreshnessMs);
            COLLISION_ENABLED.set(to, 0L, collisionEnabled ? 1 : 0);
            LOCAL_SMOOTHING.set(to, 0L, localSmoothing);
            REMOTE_SMOOTHING.set(to, 0L, remoteSmoothing);
            LIMIT_X.set(to, 0L, limitX);
            LIMIT_Y.set(to, 0L, limitY);
            LIMIT_Y_DOWN.set(to, 0L, limitYDown);
            LIMIT_Z.set(to, 0L, limitZ);
            LIMIT_Z_BACK.set(to, 0L, limitZBack);
            COLLISION_MARGIN.set(to, 0L, collisionMargin);
            COLLISION_RELEASE_SMOOTHING.set(to, 0L, collisionReleaseSmoothing);
            LIGHT_MULTIPLIER.set(to, 0L, lightMultiplier);
        }
    }

    /** The fleet's rows as the file gave them, and what the load found: CameraUnlockConfig. */
    public static final class Config {
        /** A LOAD_*. */
        public int status;
        public int udpPort;
        public boolean enableOnStartup, worldSpaceYaw;
        public final Settings settings = new Settings();

        private static final VarHandle UDP_PORT = member(CONFIG, "udp_port"),
                ENABLE_ON_STARTUP = member(CONFIG, "enable_on_startup"), WORLD_SPACE_YAW = member(CONFIG, "world_space_yaw");
        private static final long SETTINGS_AT = CONFIG.byteOffset(PathElement.groupElement("settings"));
    }

    /**
     * One setting a game's own options screen can show: CameraUnlockOption and its texts. Its
     * value is one double whatever it holds: a bool as 0 or 1, an int, a float, an enum as its
     * word's place in choices.
     */
    public static final class Option {
        /** An OPTION_BOOL, OPTION_INT, OPTION_FLOAT or OPTION_ENUM. */
        public int kind;
        /** An OPTION_ROW, OPTION_TRACKING_MODE or OPTION_AIM_MODE. */
        public int source;
        /** The row's key, or "TrackingMode" or "AimMode". */
        public String id;
        public String section;
        /** The id as words: "Field of view". */
        public String label;
        /** The file's comment above the row, its lines parted by a line feed. */
        public String comment;
        /** An enum's words as a screen shows them. Empty for every other kind. */
        public String[] choices;
        /** The lowest and highest value, both allowed, and how far one notch of a slider moves it. */
        public double min, max, step;

        private static final VarHandle KIND = member(OPTION, "kind"), SOURCE = member(OPTION, "source"),
                CHOICES = member(OPTION, "choices"), MIN = member(OPTION, "min"), MAX = member(OPTION, "max"),
                STEP = member(OPTION, "step");
    }

    /** What a frame is given: CameraUnlockFrameInput. */
    public static final class FrameInput {
        public int flags;
        public long nowMs;
        public float deltaSeconds, tanHalfFov, tanHalfFovBase, forwardStop;
        public final float[] aimForward = new float[3];
        public final float[] trackerToWorld = new float[9];

        private static final VarHandle FLAGS = member(FRAME_INPUT, "flags"), NOW_MS = member(FRAME_INPUT, "now_ms"),
                DELTA_SECONDS = member(FRAME_INPUT, "delta_seconds"), TAN_HALF_FOV = member(FRAME_INPUT, "tan_half_fov"),
                TAN_HALF_FOV_BASE = member(FRAME_INPUT, "tan_half_fov_base"), FORWARD_STOP = member(FRAME_INPUT, "forward_stop"),
                AIM_FORWARD = element(FRAME_INPUT, "aim_forward"), TRACKER_TO_WORLD = element(FRAME_INPUT, "tracker_to_world");

        private void write(MemorySegment to) {
            FLAGS.set(to, 0L, flags);
            NOW_MS.set(to, 0L, nowMs);
            DELTA_SECONDS.set(to, 0L, deltaSeconds);
            TAN_HALF_FOV.set(to, 0L, tanHalfFov);
            TAN_HALF_FOV_BASE.set(to, 0L, tanHalfFovBase);
            FORWARD_STOP.set(to, 0L, forwardStop);
            for (int i = 0; i < aimForward.length; i++) {
                AIM_FORWARD.set(to, 0L, (long) i, aimForward[i]);
            }
            for (int i = 0; i < trackerToWorld.length; i++) {
                TRACKER_TO_WORLD.set(to, 0L, (long) i, trackerToWorld[i]);
            }
        }
    }

    /** What a frame answers: CameraUnlockFrame. */
    public static final class Frame {
        public int flags, trackingMode, aimMode;
        public float yaw, pitch, roll, lightYaw, lightPitch, lightRoll, headYaw, headPitch, headRoll, headX, headY, headZ,
                poseShare, zoomFactor, deltaSeconds, queryReach;
        public final float[] queryDirection = new float[3];

        private static final VarHandle FLAGS = member(FRAME, "flags"), TRACKING_MODE = member(FRAME, "tracking_mode"),
                AIM_MODE = member(FRAME, "aim_mode"), YAW = member(FRAME, "yaw"), PITCH = member(FRAME, "pitch"),
                ROLL = member(FRAME, "roll"), LIGHT_YAW = member(FRAME, "light_yaw"), LIGHT_PITCH = member(FRAME, "light_pitch"),
                LIGHT_ROLL = member(FRAME, "light_roll"), HEAD_YAW = member(FRAME, "head_yaw"),
                HEAD_PITCH = member(FRAME, "head_pitch"), HEAD_ROLL = member(FRAME, "head_roll"), HEAD_X = member(FRAME, "head_x"),
                HEAD_Y = member(FRAME, "head_y"), HEAD_Z = member(FRAME, "head_z"), POSE_SHARE = member(FRAME, "pose_share"),
                ZOOM_FACTOR = member(FRAME, "zoom_factor"), DELTA_SECONDS = member(FRAME, "delta_seconds"),
                QUERY_DIRECTION = element(FRAME, "query_direction"), QUERY_REACH = member(FRAME, "query_reach");

        private void read(MemorySegment from) {
            flags = (int) FLAGS.get(from, 0L);
            trackingMode = (int) TRACKING_MODE.get(from, 0L);
            aimMode = (int) AIM_MODE.get(from, 0L);
            yaw = (float) YAW.get(from, 0L);
            pitch = (float) PITCH.get(from, 0L);
            roll = (float) ROLL.get(from, 0L);
            lightYaw = (float) LIGHT_YAW.get(from, 0L);
            lightPitch = (float) LIGHT_PITCH.get(from, 0L);
            lightRoll = (float) LIGHT_ROLL.get(from, 0L);
            headYaw = (float) HEAD_YAW.get(from, 0L);
            headPitch = (float) HEAD_PITCH.get(from, 0L);
            headRoll = (float) HEAD_ROLL.get(from, 0L);
            headX = (float) HEAD_X.get(from, 0L);
            headY = (float) HEAD_Y.get(from, 0L);
            headZ = (float) HEAD_Z.get(from, 0L);
            poseShare = (float) POSE_SHARE.get(from, 0L);
            zoomFactor = (float) ZOOM_FACTOR.get(from, 0L);
            deltaSeconds = (float) DELTA_SECONDS.get(from, 0L);
            for (int i = 0; i < queryDirection.length; i++) {
                queryDirection[i] = (float) QUERY_DIRECTION.get(from, 0L, (long) i);
            }
            queryReach = (float) QUERY_REACH.get(from, 0L);
        }
    }

    /** What the host's world query found along Frame.queryDirection: CameraUnlockObstruction. */
    public static final class Obstruction {
        /** False when the query could not be run at all. */
        public boolean queried;
        public boolean blocked;
        public float distance;

        private static final VarHandle QUERIED = member(OBSTRUCTION, "queried"), BLOCKED = member(OBSTRUCTION, "blocked"),
                DISTANCE = member(OBSTRUCTION, "distance");

        private void write(MemorySegment to) {
            QUERIED.set(to, 0L, queried ? 1 : 0);
            BLOCKED.set(to, 0L, blocked ? 1 : 0);
            DISTANCE.set(to, 0L, distance);
        }
    }

    /** The lean, split between the view and the rig: CameraUnlockLean. */
    public static final class Lean {
        public int flags;
        public final float[] camera = new float[3];
        public final float[] rig = new float[3];
        public float asked, given;

        private static final VarHandle FLAGS = member(LEAN, "flags"), CAMERA = element(LEAN, "camera"), RIG = element(LEAN, "rig"),
                ASKED = member(LEAN, "asked"), GIVEN = member(LEAN, "given");

        private void read(MemorySegment from) {
            flags = (int) FLAGS.get(from, 0L);
            for (int i = 0; i < 3; i++) {
                camera[i] = (float) CAMERA.get(from, 0L, (long) i);
                rig[i] = (float) RIG.get(from, 0L, (long) i);
            }
            asked = (float) ASKED.get(from, 0L);
            given = (float) GIVEN.get(from, 0L);
        }
    }

    private final Path library;
    private final SymbolLookup lookup;
    private final Linker linker = Linker.nativeLinker();

    private final MethodHandle lastError, logOpen, logWrite, logTake, settingsDefaults, sessionConfigure, sessionStart,
            sessionStop, cycleTrackingMode, cycleAimMode, setAimMode, aimModeLabel, sessionFrame, sessionLean, viewStart,
            viewStop, viewFrame, viewLean, configDescribe,
            configConcept, configLocalBool, configLocalInt, configLocalFloat, configLocalEnum, configLocalHotkey, configLocalHotkeyHeld,
            configLocalHotkeyTaps, configRender,
            configLoad, configReload, configGetInt, configGetFloat, configSaveInt, configSaveFloat, configSaveTrackingMode,
            configSaveAimMode, configSaveWorldSpaceYaw, configOptionCount, configOption, configOptionText, configOptionGet,
            configOptionSave, hotkeysStart, hotkeysTake, hotkeysDrop, windowCenter;

    private final MemorySegment frameInput = sized(FRAME_INPUT), frameOut = sized(FRAME), obstruction = sized(OBSTRUCTION),
            leanOut = sized(LEAN);

    /**
     * Loads CameraUnlockCore.dll from beside the jar, or the class folder, this class is in, and
     * from nowhere else.
     */
    public static CameraUnlock load() {
        return load(beside(CameraUnlock.class).resolve(LIBRARY));
    }

    /** The folder a class's jar or class folder is in. */
    static Path beside(Class<?> type) {
        try {
            return Path.of(type.getProtectionDomain().getCodeSource().getLocation().toURI()).getParent();
        } catch (URISyntaxException failure) {
            throw new IllegalStateException(failure);
        }
    }

    /** For this tree's own tests, which load the testing build of the library by its own name. */
    @SuppressWarnings("restricted")
    static CameraUnlock load(Path library) {
        return new CameraUnlock(library, SymbolLookup.libraryLookup(library, Arena.global()));
    }

    private CameraUnlock(Path library, SymbolLookup lookup) {
        this.library = library;
        this.lookup = lookup;
        // Before anything else is looked up: a library of another ABI may not have it.
        int abi = status(bind("cameraunlock_abi", FunctionDescriptor.of(INT)));
        if (abi != ABI) {
            throw new IllegalStateException(library + " is version " + abi + " of cameraunlock-core's C interface and the"
                    + " Java code beside it was written for version " + ABI + ": the " + (abi < ABI ? "DLL" : "Java code")
                    + " is the older of the two. Both come from one build of the mod, so one of them was replaced"
                    + " without the other");
        }
        lastError = bind("cameraunlock_last_error", FunctionDescriptor.of(INT, POINTER, INT));
        MethodHandle structSize = bind("cameraunlock_struct_size", FunctionDescriptor.of(INT, INT));
        for (int which = 0; which < STRUCTS.length; which++) {
            int theirs = check(status(structSize, which));
            if (theirs != STRUCTS[which].byteSize()) {
                throw new IllegalStateException(library + " has " + STRUCTS[which].name().orElseThrow() + " at " + theirs
                        + " bytes and the Java code beside it at " + STRUCTS[which].byteSize() + ", though both say they are"
                        + " version " + ABI + " of cameraunlock-core's C interface: they are from different builds");
            }
        }

        logOpen = bind("cameraunlock_log_open", FunctionDescriptor.of(INT, POINTER));
        logWrite = bind("cameraunlock_log_write", FunctionDescriptor.of(INT, POINTER));
        logTake = bind("cameraunlock_log_take", FunctionDescriptor.of(INT, POINTER, INT));
        settingsDefaults = bind("cameraunlock_settings_defaults", FunctionDescriptor.of(INT, POINTER));
        sessionConfigure = bind("cameraunlock_session_configure", FunctionDescriptor.of(INT, POINTER));
        sessionStart = bind("cameraunlock_session_start", FunctionDescriptor.of(INT, INT));
        sessionStop = bind("cameraunlock_session_stop", FunctionDescriptor.of(INT));
        cycleTrackingMode = bind("cameraunlock_session_cycle_tracking_mode", FunctionDescriptor.of(INT));
        cycleAimMode = bind("cameraunlock_session_cycle_aim_mode", FunctionDescriptor.of(INT));
        setAimMode = bind("cameraunlock_session_set_aim_mode", FunctionDescriptor.of(INT, INT));
        aimModeLabel = bind("cameraunlock_aim_mode_label", FunctionDescriptor.of(POINTER, INT));
        sessionFrame = bind("cameraunlock_session_frame", FunctionDescriptor.of(INT, POINTER, POINTER));
        sessionLean = bind("cameraunlock_session_lean", FunctionDescriptor.of(INT, POINTER, POINTER));
        viewStart = bind("cameraunlock_view_start", FunctionDescriptor.of(INT, INT, INT));
        viewStop = bind("cameraunlock_view_stop", FunctionDescriptor.of(INT, INT));
        viewFrame = bind("cameraunlock_view_frame", FunctionDescriptor.of(INT, INT, POINTER, POINTER));
        viewLean = bind("cameraunlock_view_lean", FunctionDescriptor.of(INT, INT, POINTER, POINTER));
        configDescribe = bind("cameraunlock_config_describe", FunctionDescriptor.of(INT, POINTER));
        configConcept = bind("cameraunlock_config_concept", FunctionDescriptor.of(INT, POINTER, INT, POINTER, POINTER));
        configLocalBool = bind("cameraunlock_config_local_bool", FunctionDescriptor.of(INT, POINTER, POINTER, POINTER, INT, INT));
        configLocalInt = bind("cameraunlock_config_local_int",
                FunctionDescriptor.of(INT, POINTER, POINTER, POINTER, INT, INT, INT, INT));
        configLocalFloat = bind("cameraunlock_config_local_float",
                FunctionDescriptor.of(INT, POINTER, POINTER, POINTER, INT, FLOAT, FLOAT, FLOAT));
        configLocalEnum = bind("cameraunlock_config_local_enum",
                FunctionDescriptor.of(INT, POINTER, POINTER, POINTER, INT, POINTER, INT));
        configLocalHotkey = bind("cameraunlock_config_local_hotkey", FunctionDescriptor.of(INT, POINTER, POINTER, INT, POINTER, INT));
        configLocalHotkeyHeld = bind("cameraunlock_config_local_hotkey_held",
                FunctionDescriptor.of(INT, POINTER, POINTER, INT, POINTER, INT, INT));
        configLocalHotkeyTaps = bind("cameraunlock_config_local_hotkey_taps",
                FunctionDescriptor.of(INT, POINTER, POINTER, INT, POINTER, INT, INT, INT));
        configRender = bind("cameraunlock_config_render", FunctionDescriptor.of(INT, POINTER));
        configLoad = bind("cameraunlock_config_load", FunctionDescriptor.of(INT, POINTER, POINTER, POINTER));
        configReload = bind("cameraunlock_config_reload", FunctionDescriptor.of(INT));
        configGetInt = bind("cameraunlock_config_get_int", FunctionDescriptor.of(INT, INT, POINTER));
        configGetFloat = bind("cameraunlock_config_get_float", FunctionDescriptor.of(INT, INT, POINTER));
        configSaveInt = bind("cameraunlock_config_save_int", FunctionDescriptor.of(INT, INT, INT));
        configSaveFloat = bind("cameraunlock_config_save_float", FunctionDescriptor.of(INT, INT, FLOAT));
        configSaveTrackingMode = bind("cameraunlock_config_save_tracking_mode", FunctionDescriptor.of(INT));
        configSaveAimMode = bind("cameraunlock_config_save_aim_mode", FunctionDescriptor.of(INT));
        configSaveWorldSpaceYaw = bind("cameraunlock_config_save_world_space_yaw", FunctionDescriptor.of(INT, INT));
        configOptionCount = bind("cameraunlock_config_option_count", FunctionDescriptor.of(INT));
        configOption = bind("cameraunlock_config_option", FunctionDescriptor.of(INT, INT, POINTER));
        configOptionText = bind("cameraunlock_config_option_text", FunctionDescriptor.of(INT, INT, INT, POINTER, INT));
        configOptionGet = bind("cameraunlock_config_option_get", FunctionDescriptor.of(INT, INT, POINTER));
        configOptionSave = bind("cameraunlock_config_option_save", FunctionDescriptor.of(INT, INT, DOUBLE));
        hotkeysStart = bind("cameraunlock_hotkeys_start", FunctionDescriptor.of(INT));
        hotkeysTake = bind("cameraunlock_hotkeys_take", FunctionDescriptor.of(INT));
        hotkeysDrop = bind("cameraunlock_hotkeys_drop", FunctionDescriptor.ofVoid());
        windowCenter = bind("cameraunlock_window_center", FunctionDescriptor.of(INT, LONG));
    }

    @SuppressWarnings("restricted")
    MethodHandle bind(String name, FunctionDescriptor descriptor) {
        return linker.downcallHandle(lookup.find(name).orElseThrow(() ->
                new IllegalStateException(library + " has no function " + name)), descriptor);
    }

    /** The file this instance is bound to. */
    public Path library() {
        return library;
    }

    // ---- Log ---------------------------------------------------------------------------------

    /** Opens core's file log. From here core's lines and logWrite's go to this file. */
    public void logOpen(Path file) {
        try (Arena arena = Arena.ofConfined()) {
            check(status(logOpen, text(arena, file.toAbsolutePath().toString())));
        }
    }

    public void logWrite(String line) {
        try (Arena arena = Arena.ofConfined()) {
            check(status(logWrite, text(arena, line)));
        }
    }

    /** For a host with a log of its own: the lines core has for it, each ending in a newline. */
    public String logTake() {
        int length = status(logTake, MemorySegment.NULL, 0);
        if (length == 0) {
            return "";
        }
        try (Arena arena = Arena.ofConfined()) {
            // A line added between the two calls leaves this one too small, and nothing is taken.
            MemorySegment buffer = arena.allocate(length);
            return status(logTake, buffer, length) == length
                    ? new String(buffer.toArray(ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8) : "";
        }
    }

    // ---- Session -----------------------------------------------------------------------------

    public Settings settingsDefaults() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = stamped(arena, SETTINGS);
            check(status(settingsDefaults, out));
            Settings settings = new Settings();
            settings.read(out);
            return settings;
        }
    }

    public void configure(Settings settings) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment in = stamped(arena, SETTINGS);
            settings.write(in);
            check(status(sessionConfigure, in));
        }
    }

    public void start(int udpPort) {
        check(status(sessionStart, udpPort));
    }

    public void stop() {
        check(status(sessionStop));
    }

    /** The mode after the current one, which is answered. */
    public int cycleTrackingMode() {
        return check(status(cycleTrackingMode));
    }

    public int cycleAimMode() {
        return check(status(cycleAimMode));
    }

    public void setAimMode(int aimMode) {
        check(status(setAimMode, aimMode));
    }

    @SuppressWarnings("restricted")
    public String aimModeLabel(int aimMode) {
        MemorySegment label;
        try {
            label = (MemorySegment) aimModeLabel.invokeExact(aimMode);
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
        if (label.equals(MemorySegment.NULL)) {
            throw new IllegalStateException(lastError());
        }
        return label.reinterpret(Long.MAX_VALUE).getString(0);
    }

    /** Once per rendered frame, on the thread that draws. */
    public void frame(FrameInput input, Frame out) {
        input.write(frameInput);
        int status;
        try {
            status = (int) sessionFrame.invokeExact(frameInput, frameOut);
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
        check(status);
        out.read(frameOut);
    }

    /**
     * Once after each frame whose flags carry STATE_LEAN, on the same thread. The obstruction is
     * read when they also carry STATE_LEAN_QUERY.
     */
    public void lean(Obstruction found, Lean out) {
        found.write(obstruction);
        int status;
        try {
            status = (int) sessionLean.invokeExact(obstruction, leanOut);
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
        check(status);
        out.read(leanOut);
    }

    // ---- Views -------------------------------------------------------------------------------

    /**
     * Starts one view, 0 to VIEWS - 1, listening on a port of its own: a camera with its own
     * tracker, for a game that draws more than one first person view in a frame. The settings and
     * the two modes are every view's. A port another started view listens on is refused.
     */
    public void viewStart(int view, int udpPort) {
        check(status(viewStart, view, udpPort));
    }

    public void viewStop(int view) {
        check(status(viewStop, view));
    }

    /** frame for one view, on the thread that draws. It allocates nothing. */
    public void viewFrame(int view, FrameInput input, Frame out) {
        input.write(frameInput);
        int status;
        try {
            status = (int) viewFrame.invokeExact(view, frameInput, frameOut);
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
        check(status);
        out.read(frameOut);
    }

    /** lean for one view: once after each of its frames whose flags carry STATE_LEAN, on the same thread. */
    public void viewLean(int view, Obstruction found, Lean out) {
        found.write(obstruction);
        int status;
        try {
            status = (int) viewLean.invokeExact(view, obstruction, leanOut);
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
        check(status);
        out.read(leanOut);
    }

    // ---- Config ------------------------------------------------------------------------------

    /** Starts the description of the game's CameraUnlock.ini. The rows follow, then load or render. */
    public void configDescribe(String displayName) {
        try (Arena arena = Arena.ofConfined()) {
            check(status(configDescribe, text(arena, displayName)));
        }
    }

    /**
     * A row of the fleet's vocabulary, by its name in data/config-schema.json.
     *
     * @param comment     replaces the schema's comment above the key, or null to keep it
     * @param defaultText the game's own default as the file would hold it, or null for the schema's
     */
    public void configConcept(String name, int flags, String comment, String defaultText) {
        try (Arena arena = Arena.ofConfined()) {
            check(status(configConcept, text(arena, name), flags, optional(arena, comment), optional(arena, defaultText)));
        }
    }

    /** Each configLocal answers the row's number, which configGet and configSave take. */
    public int configLocalBool(String section, String key, String comment, int flags, boolean defaultValue) {
        try (Arena arena = Arena.ofConfined()) {
            return check(status(configLocalBool, text(arena, section), text(arena, key), text(arena, comment), flags,
                    defaultValue ? 1 : 0));
        }
    }

    public int configLocalInt(String section, String key, String comment, int flags, int defaultValue, int min, int max) {
        try (Arena arena = Arena.ofConfined()) {
            return check(status(configLocalInt, text(arena, section), text(arena, key), text(arena, comment), flags,
                    defaultValue, min, max));
        }
    }

    public int configLocalFloat(String section, String key, String comment, int flags, float defaultValue, float min,
            float max) {
        try (Arena arena = Arena.ofConfined()) {
            return check(status(configLocalFloat, text(arena, section), text(arena, key), text(arena, comment), flags,
                    defaultValue, min, max));
        }
    }

    /** @param tokens the words the file holds, PascalCase, separated by commas */
    public int configLocalEnum(String section, String key, String comment, int flags, String tokens, int defaultIndex) {
        try (Arena arena = Arena.ofConfined()) {
            return check(status(configLocalEnum, text(arena, section), text(arena, key), text(arena, comment), flags,
                    text(arena, tokens), defaultIndex));
        }
    }

    /** @param hotkeyBit the bit hotkeysTake answers with: one bit, HOTKEY_LOCAL or above */
    public int configLocalHotkey(String key, String comment, int flags, String defaultKeys, int hotkeyBit) {
        try (Arena arena = Arena.ofConfined()) {
            return check(status(configLocalHotkey, text(arena, key), text(arena, comment), flags, text(arena, defaultKeys),
                    hotkeyBit));
        }
    }

    /**
     * A key list whose keys do one thing tapped and another held.
     *
     * @param hotkeyBit the bit hotkeysTake answers with when a key is let go within HOLD_MS of going down
     * @param heldBit   the bit it answers with once a key has been down that long. Each is one bit,
     *                  HOTKEY_LOCAL or above, and no other row's
     */
    public int configLocalHotkeyHeld(String key, String comment, int flags, String defaultKeys, int hotkeyBit, int heldBit) {
        try (Arena arena = Arena.ofConfined()) {
            return check(status(configLocalHotkeyHeld, text(arena, key), text(arena, comment), flags,
                    text(arena, defaultKeys), hotkeyBit, heldBit));
        }
    }

    /**
     * A key list whose keys do one thing tapped, another tapped twice and a third held.
     *
     * @param hotkeyBit the bit hotkeysTake answers with once DOUBLE_TAP_MS have passed since a tap was let go
     *                  with no second press
     * @param doubleBit the bit it answers with as a second press goes down within that time
     * @param heldBit   the bit it answers with once a first press has been down HOLD_MS. Each is one bit,
     *                  HOTKEY_LOCAL or above, and no other row's
     */
    public int configLocalHotkeyTaps(String key, String comment, int flags, String defaultKeys, int hotkeyBit, int doubleBit,
            int heldBit) {
        try (Arena arena = Arena.ofConfined()) {
            return check(status(configLocalHotkeyTaps, text(arena, key), text(arena, comment), flags,
                    text(arena, defaultKeys), hotkeyBit, doubleBit, heldBit));
        }
    }

    /** Writes the file the description renders for a first start. */
    public void configRender(Path file) {
        try (Arena arena = Arena.ofConfined()) {
            check(status(configRender, text(arena, file.toAbsolutePath().toString())));
        }
    }

    /**
     * Reads the game's CameraUnlock.ini, creating it where there is none.
     *
     * @param defaults null in a mod, for the player's own Defaults.ini, and a scratch file in every test
     */
    public Config configLoad(Path file, Path defaults) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = stamped(arena, CONFIG);
            int status = check(status(configLoad, text(arena, file.toAbsolutePath().toString()),
                    defaults == null ? MemorySegment.NULL : text(arena, defaults.toAbsolutePath().toString()), out));
            Config config = new Config();
            config.status = status;
            config.udpPort = (int) Config.UDP_PORT.get(out, 0L);
            config.enableOnStartup = (int) Config.ENABLE_ON_STARTUP.get(out, 0L) != 0;
            config.worldSpaceYaw = (int) Config.WORLD_SPACE_YAW.get(out, 0L) != 0;
            config.settings.read(out.asSlice(Config.SETTINGS_AT, SETTINGS));
            return config;
        }
    }

    /** Answers a RELOAD_*. Not from the thread that draws. */
    public int configReload() {
        return check(status(configReload));
    }

    /** A local row's value: a bool as 0 or 1, an int, an enum as its word's place. */
    public int configGetInt(int row) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(INT);
            check(status(configGetInt, row, out));
            return out.get(INT, 0);
        }
    }

    public float configGetFloat(int row) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(FLOAT);
            check(status(configGetFloat, row, out));
            return out.get(FLOAT, 0);
        }
    }

    /** Each configSave answers a SAVE_*. Not from the thread that draws. */
    public int configSaveInt(int row, int value) {
        return check(status(configSaveInt, row, value));
    }

    public int configSaveFloat(int row, float value) {
        return check(status(configSaveFloat, row, value));
    }

    public int configSaveTrackingMode() {
        return check(status(configSaveTrackingMode));
    }

    public int configSaveAimMode() {
        return check(status(configSaveAimMode));
    }

    public int configSaveWorldSpaceYaw(boolean worldSpaceYaw) {
        return check(status(configSaveWorldSpaceYaw, worldSpaceYaw ? 1 : 0));
    }

    // ---- Options -----------------------------------------------------------------------------

    /**
     * How many settings of the loaded file a game's own options screen can show: its writable
     * bool, int, float and enum rows, with the tracking mode and the aim mode as one each.
     */
    public int configOptionCount() {
        return check(status(configOptionCount));
    }

    /** The option at a place from 0 to configOptionCount() - 1. */
    public Option configOption(int option) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = stamped(arena, OPTION);
            check(status(configOption, option, out));
            Option described = new Option();
            described.kind = (int) Option.KIND.get(out, 0L);
            described.source = (int) Option.SOURCE.get(out, 0L);
            described.min = (double) Option.MIN.get(out, 0L);
            described.max = (double) Option.MAX.get(out, 0L);
            described.step = (double) Option.STEP.get(out, 0L);
            described.id = optionText(arena, option, OPTION_TEXT_ID);
            described.section = optionText(arena, option, OPTION_TEXT_SECTION);
            described.label = optionText(arena, option, OPTION_TEXT_LABEL);
            described.comment = optionText(arena, option, OPTION_TEXT_COMMENT);
            described.choices = new String[(int) Option.CHOICES.get(out, 0L)];
            for (int choice = 0; choice < described.choices.length; choice++) {
                described.choices[choice] = optionText(arena, option, OPTION_TEXT_CHOICE + choice);
            }
            return described;
        }
    }

    private String optionText(Arena arena, int option, int which) {
        int length = check(status(configOptionText, option, which, MemorySegment.NULL, 0));
        MemorySegment buffer = arena.allocate(length + 1L);
        check(status(configOptionText, option, which, buffer, length + 1));
        return buffer.getString(0);
    }

    /** An option's value now. The two modes are the session's, so a mode a hotkey cycled reads as it is. */
    public double configOptionGet(int option) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment out = arena.allocate(DOUBLE);
            check(status(configOptionGet, option, out));
            return out.get(DOUBLE, 0);
        }
    }

    /**
     * Writes an option's rows and answers a SAVE_*. A mode is put on the session first. Every
     * other option the host applies to its own running state. Not from the thread that draws.
     */
    public int configOptionSave(int option, double value) {
        return check(status(configOptionSave, option, value));
    }

    // ---- Hotkeys -----------------------------------------------------------------------------

    public void hotkeysStart() {
        check(status(hotkeysStart));
    }

    /**
     * The actions whose keys went down since the last take or drop, as HOTKEY_* bits. A row of
     * configLocalHotkeyHeld answers when its key is let go or has been held, not as it goes down, and one of
     * configLocalHotkeyTaps as that method says.
     */
    public int hotkeysTake() {
        try {
            return (int) hotkeysTake.invokeExact();
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
    }

    public void hotkeysDrop() {
        try {
            hotkeysDrop.invokeExact();
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
    }

    // ---- The game's window -------------------------------------------------------------------

    /** @return true when the window is at the centred origin on return, false when it was left alone */
    public boolean windowCenter(long window) {
        return check(status(windowCenter, window)) == 1;
    }

    // ------------------------------------------------------------------------------------------

    private String lastError() {
        int length = status(lastError, MemorySegment.NULL, 0);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buffer = arena.allocate(length + 1L);
            status(lastError, buffer, length + 1);
            return buffer.getString(0);
        }
    }

    private int check(int status) {
        if (status == ERROR) {
            throw new IllegalStateException(lastError());
        }
        return status;
    }

    private static MemorySegment stamped(Arena arena, StructLayout struct) {
        MemorySegment segment = arena.allocate(struct);
        member(struct, "struct_size").set(segment, 0L, (int) struct.byteSize());
        return segment;
    }

    private static MemorySegment text(Arena arena, String text) {
        return arena.allocateFrom(text);
    }

    private static MemorySegment optional(Arena arena, String text) {
        return text == null ? MemorySegment.NULL : arena.allocateFrom(text);
    }

    private static int status(MethodHandle function, Object... arguments) {
        try {
            return (int) function.invokeWithArguments(arguments);
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
    }
}
