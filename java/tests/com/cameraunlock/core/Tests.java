package com.cameraunlock.core;

import java.io.IOException;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.lang.reflect.Field;
import java.nio.charset.Charset;
import java.util.HexFormat;
import java.util.Map;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.lang.foreign.MemoryLayout.PathElement;
import java.lang.foreign.StructLayout;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.function.Supplier;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The Java binding and the boot class, through the libraries scripts/build-java.mjs puts beside
 * the class folders. Run from the repository's root: it reads docs/c-interface.md.
 *
 * Every check runs, and the tally is printed once at the end.
 */
@SuppressWarnings("restricted")
public final class Tests {
    private static int checks;
    private static int failures;

    private Tests() {}

    public static void main(String[] arguments) throws Throwable {
        Path scratch = Files.createTempDirectory("cameraunlock-java-tests");
        layoutsAreTheDocumentedOnes();
        libraryIsFoundBesideTheClasses();
        libraryOfAnotherAbiIsRefusedInWords();
        Testing testing = new Testing();
        refusalCarriesTheLibrarysReason(testing.core);
        defaultsAreLookedForInAppData(testing);
        windowsCallsAnswerAsWindowsDoes(testing, scratch);
        eachThreadIsGivenItsOwnReason(testing.core);
        poseArrivesAndTheLeanIsHeldOffAWall(testing);
        modesAreSetCycledAndNamed(testing.core);
        viewsEachHaveATrackerAndShareTheModes(testing);
        configIsDescribedRenderedLoadedSavedAndReadAgain(testing.core, scratch);
        hotkeysAndTheWindow(testing.core);
        aTrackerOnThePortReachesTheFrame(testing);
        if (Testing.BUNDLED) {
            aReceiveLoopThatFailsIsReopened(testing);
            aFloodOfLongDatagramsDoesNotHoldThePoseBack(testing);
            aModuleThatTrapsIsNotCalledAgain();
        }
        logGoesToItsFile(testing.core, scratch);
        bootLoadsTheAgentAndStartsTheGame(scratch);
        System.out.println(failures == 0 ? "all " + checks + " checks passed" : failures + " of " + checks + " checks FAILED");
        System.exit(failures == 0 ? 0 : 1);
    }

    private static void check(boolean held, String what) {
        checks++;
        if (!held) {
            failures++;
            System.out.println("  [FAIL] " + what);
        }
    }

    private static boolean near(float a, float b) {
        return Math.abs(a - b) <= 1e-4f;
    }

    private static String refusal(Runnable call) {
        try {
            call.run();
            return "";
        } catch (IllegalStateException refused) {
            return refused.getMessage();
        }
    }

    /** docs/c-interface.md's Layouts table, which cpp/tests/c_header.c holds to the header. */
    private static void layoutsAreTheDocumentedOnes() throws IOException {
        StructLayout[] structs = {CameraUnlock.SETTINGS, CameraUnlock.FRAME_INPUT, CameraUnlock.FRAME,
                CameraUnlock.OBSTRUCTION, CameraUnlock.LEAN, CameraUnlock.CONFIG, CameraUnlock.OPTION};
        String document = Files.readString(Path.of("docs", "c-interface.md"));
        for (StructLayout struct : structs) {
            String name = struct.name().orElseThrow();
            Matcher row = Pattern.compile("(?m)^\\| `" + name + "` \\| (\\d+) \\| (.*) \\|$").matcher(document);
            if (!row.find()) {
                check(false, "docs/c-interface.md has a layout row for " + name);
                continue;
            }
            check(struct.byteSize() == Long.parseLong(row.group(1)), name + " is the documented " + row.group(1) + " bytes");
            Matcher member = Pattern.compile("`(\\w+)` (\\d+)").matcher(row.group(2));
            int documented = 0;
            while (member.find()) {
                documented++;
                long offset;
                try {
                    offset = struct.byteOffset(PathElement.groupElement(member.group(1)));
                } catch (IllegalArgumentException absent) {
                    check(false, name + " has a member " + member.group(1));
                    continue;
                }
                check(offset == Long.parseLong(member.group(2)), name + "." + member.group(1) + " is at the documented " + member.group(2));
            }
            check(documented == struct.memberLayouts().size(), name + " declares the " + documented + " members the document lists");
        }
    }

    private static void defaultsAreLookedForInAppData(Testing testing) {
        String expected = System.getenv("APPDATA") + "\\CameraUnlock\\Defaults.ini";
        String found = testing.defaultsPath();
        check(found.equalsIgnoreCase(expected), "Defaults.ini is looked for in the roaming AppData folder: " + found);
    }

    private static void libraryIsFoundBesideTheClasses() {
        if (Testing.BUNDLED) {
            CameraUnlock core = CameraUnlock.loadBundled();
            check(Files.isRegularFile(core.library().resolve("com/cameraunlock/core/wasm/CoreModule.class")),
                    "the module a mod ships is named by the class folder its classes are in: " + core.library());
            check(core.settingsDefaults().dataFreshnessMs == 500, "and answers");
            check(CameraUnlock.loadBundled() == core, "and there is one in a process, as there is one DLL");
            return;
        }
        CameraUnlock core = CameraUnlock.load();
        check(core.library().equals(CameraUnlock.beside(CameraUnlock.class).resolve("CameraUnlockCore.dll")),
                "the library a mod ships is loaded from beside the class folder: " + core.library());
        check(core.settingsDefaults().dataFreshnessMs == 500, "and answers");
    }

    private static void libraryOfAnotherAbiIsRefusedInWords() {
        if (Testing.BUNDLED) {
            String reason = refusal(() -> CameraUnlock.bundled(com.cameraunlock.core.wasm.CoreOtherAbiModule::load,
                    com.cameraunlock.core.wasm.CoreOtherAbiModule::create, com.cameraunlock.core.wasm.CoreOtherAbiModule.class));
            Path classes = Path.of("java/build/test-classes").toAbsolutePath();
            check(reason.startsWith("The WebAssembly module in " + classes) && reason.contains("version " + (CameraUnlock.ABI - 1))
                            && reason.contains("written for version " + CameraUnlock.ABI) && reason.contains("the module is the older")
                            && reason.contains("come from two builds"),
                    "a module of the ABI before this one is refused at load, and the reason names the module, both versions, the"
                            + " older side and how they came apart: "
                            + reason);
            return;
        }
        Path older = CameraUnlock.beside(Tests.class).resolve("CameraUnlockCoreOtherAbi.dll");
        String reason = refusal(() -> CameraUnlock.load(older));
        check(reason.contains(older.toString()) && reason.contains("version " + (CameraUnlock.ABI - 1))
                        && reason.contains("written for version " + CameraUnlock.ABI) && reason.contains("the DLL is the older"),
                "a library of the ABI before this one is refused at load, and the reason names the file, both versions and the older side: "
                        + reason);
    }

    private static void refusalCarriesTheLibrarysReason(CameraUnlock core) {
        CameraUnlock.Settings settings = core.settingsDefaults();
        check(settings.localSmoothing == 0f && near(settings.remoteSmoothing, 0.15f) && near(settings.limitX, 0.30f)
                        && near(settings.limitY, 0.20f) && near(settings.limitYDown, 0.20f) && near(settings.limitZ, 0.40f)
                        && near(settings.limitZBack, 0.10f) && near(settings.collisionMargin, 0.10f)
                        && near(settings.collisionReleaseSmoothing, 0.9f) && near(settings.lightMultiplier, 1.5f)
                        && settings.trackingMode == CameraUnlock.TRACKING_ROTATION_AND_POSITION
                        && settings.aimMode == CameraUnlock.AIM_SIGHTS_LOCKED,
                "the defaults are core's");
        settings.trackingMode = 7;
        String reason = refusal(() -> core.configure(settings));
        check(reason.contains("cameraunlock_session_configure") && reason.contains("tracking_mode"),
                "a call the library refuses throws its reason: " + reason);
    }

    /** x, y and z in the wire's centimetres, the angles in degrees. */
    private static byte[] datagram(double x, double y, double z, double yaw, double pitch, double roll) {
        ByteBuffer wire = ByteBuffer.allocate(48).order(ByteOrder.LITTLE_ENDIAN);
        for (double value : new double[] {x, y, z, yaw, pitch, roll}) {
            wire.putDouble(value);
        }
        return wire.array();
    }

    private static CameraUnlock.FrameInput leaning() {
        CameraUnlock.FrameInput input = new CameraUnlock.FrameInput();
        input.flags = CameraUnlock.FRAME_ACTIVE | CameraUnlock.FRAME_LEAN;
        input.deltaSeconds = 1f / 60f;
        input.tanHalfFov = 0.5f;
        input.tanHalfFovBase = 0.5f;
        input.forwardStop = Float.POSITIVE_INFINITY;
        input.aimForward[2] = 1f;
        // The tracker's x to the host's -y, its y to the host's z and its z to the host's x.
        input.trackerToWorld[2] = 1f;
        input.trackerToWorld[3] = -1f;
        input.trackerToWorld[7] = 1f;
        return input;
    }

    private static void poseArrivesAndTheLeanIsHeldOffAWall(Testing testing) {
        CameraUnlock core = testing.core;
        testing.reset();
        CameraUnlock.Settings settings = core.settingsDefaults();
        settings.collisionEnabled = true;
        settings.collisionMargin = 0.05f;
        settings.lightMultiplier = 2f;
        core.configure(settings);

        CameraUnlock.FrameInput input = leaning();
        CameraUnlock.Frame frame = new CameraUnlock.Frame();
        core.frame(input, frame);
        check((frame.flags & CameraUnlock.STATE_POSE) == 0, "there is no pose before a datagram");

        testing.deliver(datagram(20, 5, -10, 30, -6, 4), false);
        input.nowMs += 16;
        core.frame(input, frame);
        check((frame.flags & (CameraUnlock.STATE_POSE | CameraUnlock.STATE_ROTATION | CameraUnlock.STATE_LEAN | CameraUnlock.STATE_LEAN_QUERY))
                        == (CameraUnlock.STATE_POSE | CameraUnlock.STATE_ROTATION | CameraUnlock.STATE_LEAN | CameraUnlock.STATE_LEAN_QUERY),
                "a datagram gives a pose, a rotation and a lean to measure the world for");
        check((frame.flags & CameraUnlock.STATE_REMOTE) == 0, "from loopback, which is local");
        check(near(frame.headYaw, 30f) && near(frame.headPitch, -6f) && near(frame.headRoll, 4f), "the head's angles in degrees");
        check(near(frame.headX, 0.20f) && near(frame.headY, 0.05f) && near(frame.headZ, -0.10f), "the wire's centimetres as metres");
        check(near(frame.yaw, 30f) && near(frame.pitch, -6f) && near(frame.roll, 4f) && near(frame.poseShare, 1f) && near(frame.zoomFactor, 1f),
                "unzoomed at the hip the view gets the head's own angles");
        check(near(frame.lightYaw, 60f) && near(frame.lightPitch, -12f), "the light's angles are the view's by the multiplier");
        float length = (float) Math.sqrt(0.20 * 0.20 + 0.05 * 0.05 + 0.10 * 0.10);
        check(near(frame.queryDirection[0], -0.10f / length) && near(frame.queryDirection[1], -0.20f / length)
                        && near(frame.queryDirection[2], 0.05f / length) && near(frame.queryReach, length + 0.05f),
                "the query is along the lean in the host's axes, for its length and the margin");

        String forgotten = refusal(() -> core.frame(input, frame));
        check(forgotten.contains("cameraunlock_session_lean"), "a frame whose lean was never finished is said so at the next: " + forgotten);

        CameraUnlock.Obstruction found = new CameraUnlock.Obstruction();
        CameraUnlock.Lean lean = new CameraUnlock.Lean();
        core.frame(input, frame);
        core.lean(found, lean);
        check((lean.flags & CameraUnlock.LEAN_QUERY_FAILED) != 0 && near(lean.given, length),
                "a query that could not run passes the lean and says so");

        found.queried = true;
        found.blocked = true;
        found.distance = 0.15f;
        core.frame(input, frame);
        core.lean(found, lean);
        check(lean.flags == CameraUnlock.LEAN_CONTACT && near(lean.asked, length) && near(lean.given, 0.10f),
                "a wall holds the eye the margin short of it");
        check(near(lean.camera[0], frame.queryDirection[0] * 0.10f) && near(lean.camera[1], frame.queryDirection[1] * 0.10f)
                        && near(lean.camera[2], frame.queryDirection[2] * 0.10f) && lean.rig[0] == 0f && lean.rig[1] == 0f && lean.rig[2] == 0f,
                "and at the hip the view has all of what is left");

        input.flags = 0;
        core.frame(input, frame);
        check((frame.flags & (CameraUnlock.STATE_POSE | CameraUnlock.STATE_LEAN)) == 0 && frame.yaw == 0f, "an inactive frame carries no pose");
    }

    private static void modesAreSetCycledAndNamed(CameraUnlock core) {
        check(core.cycleTrackingMode() == CameraUnlock.TRACKING_ROTATION_ONLY && core.cycleTrackingMode() == CameraUnlock.TRACKING_POSITION_ONLY
                        && core.cycleTrackingMode() == CameraUnlock.TRACKING_ROTATION_AND_POSITION,
                "the tracking mode cycles in core's order");
        check(core.cycleAimMode() == CameraUnlock.AIM_FREE_LOOK_MARKER, "the aim mode cycles");
        core.setAimMode(CameraUnlock.AIM_STOCK_SIGHTS);
        CameraUnlock.Frame frame = new CameraUnlock.Frame();
        core.frame(new CameraUnlock.FrameInput(), frame);
        check(frame.aimMode == CameraUnlock.AIM_STOCK_SIGHTS && frame.trackingMode == CameraUnlock.TRACKING_ROTATION_AND_POSITION,
                "and is set outright, and the frame says which modes it ran in");
        check(core.aimModeLabel(CameraUnlock.AIM_STOCK_SIGHTS).equals("Aim mode: stock sights"), "an aim mode's label is core's");
        check(refusal(() -> core.aimModeLabel(9)).contains("aim_mode"), "and a mode that is none has no label");
        core.setAimMode(CameraUnlock.AIM_SIGHTS_LOCKED);
    }

    private static void viewsEachHaveATrackerAndShareTheModes(Testing testing) {
        CameraUnlock core = testing.core;
        testing.reset();
        CameraUnlock.Settings settings = core.settingsDefaults();
        settings.collisionEnabled = false;
        core.configure(settings);

        testing.deliver(0, datagram(20, 0, 0, 30, 0, 0), false);
        testing.deliver(1, datagram(-10, 0, 0, -15, 0, 0), false);
        CameraUnlock.FrameInput input = leaning();
        CameraUnlock.Frame one = new CameraUnlock.Frame();
        CameraUnlock.Frame two = new CameraUnlock.Frame();
        CameraUnlock.Frame three = new CameraUnlock.Frame();
        core.viewFrame(0, input, one);
        core.viewFrame(1, input, two);
        core.viewFrame(2, input, three);
        check(near(one.headYaw, 30f) && near(one.headX, 0.20f) && near(two.headYaw, -15f) && near(two.headX, -0.10f),
                "two views in one frame each have the pose of their own tracker");
        check((three.flags & CameraUnlock.STATE_POSE) == 0, "and a view no datagram reached has none");

        CameraUnlock.Obstruction found = new CameraUnlock.Obstruction();
        CameraUnlock.Lean first = new CameraUnlock.Lean();
        CameraUnlock.Lean second = new CameraUnlock.Lean();
        core.viewLean(1, found, second);
        core.lean(found, first);
        check(near(first.camera[1], -0.20f) && near(second.camera[1], 0.10f) && near(first.given, 0.20f) && near(second.given, 0.10f),
                "each view has a lean of its own, and the session's is view 0's");

        core.viewFrame(1, input, two);
        String forgotten = refusal(() -> core.viewFrame(1, input, two));
        check(forgotten.contains("view 1") && forgotten.contains("cameraunlock_view_lean"),
                "a view whose lean was never finished is said so by name at its next frame: " + forgotten);

        check(core.cycleTrackingMode() == CameraUnlock.TRACKING_ROTATION_ONLY, "the tracking mode is cycled once");
        core.setAimMode(CameraUnlock.AIM_TRUE_FREE_LOOK);
        core.viewFrame(0, input, one);
        core.viewFrame(1, input, two);
        check(one.trackingMode == CameraUnlock.TRACKING_ROTATION_ONLY && two.trackingMode == CameraUnlock.TRACKING_ROTATION_ONLY
                        && one.aimMode == CameraUnlock.AIM_TRUE_FREE_LOOK && two.aimMode == CameraUnlock.AIM_TRUE_FREE_LOOK,
                "and both views run in it, and in the aim mode set once");
        core.cycleTrackingMode();
        core.cycleTrackingMode();
        core.setAimMode(CameraUnlock.AIM_SIGHTS_LOCKED);

        String none = refusal(() -> core.viewStart(CameraUnlock.VIEWS, 4243));
        check(none.contains("cameraunlock_view_start") && none.contains("there is no view " + CameraUnlock.VIEWS),
                "a view past the last is refused by its number: " + none);
        check(refusal(() -> core.viewStart(1, -1)).contains("udp_port is outside 1 to 65535"), "and a port that is none");
        core.viewStop(1);
        core.logTake();
        refusal(() -> testing.deliver(CameraUnlock.VIEWS, datagram(0, 0, 0, 0, 0, 0), false));
        String undelivered = core.logTake();
        check(undelivered.contains("cameraunlock_testing_deliver_view: there is no view " + CameraUnlock.VIEWS),
                "a datagram for a view that is none is refused under the name of the function called: " + undelivered);
        check(refusal(() -> core.viewStop(-1)).contains("there is no view -1"), "stopping a stopped view is not an error, and a view that is none is");
    }

    private static void configIsDescribedRenderedLoadedSavedAndReadAgain(CameraUnlock core, Path scratch) throws IOException {
        Path file = scratch.resolve("game").resolve("CameraUnlock.ini");
        Path defaults = scratch.resolve("user").resolve("Defaults.ini");
        Path rendered = scratch.resolve("rendered.ini");
        Files.createDirectories(file.getParent());
        Files.createDirectories(defaults.getParent());

        core.configDescribe("Test Game");
        for (String name : new String[] {"UdpPort", "EnableOnStartup", "DataFreshnessMs", "LocalSmoothing", "RemoteSmoothing",
                "PositionLimitX", "PositionLimitY", "PositionLimitYDown", "PositionLimitZ", "PositionLimitZBack", "CollisionEnabled",
                "CollisionReleaseSmoothing", "ToggleKey", "LightMultiplier"}) {
            core.configConcept(name, 0, null, null);
        }
        for (String name : new String[] {"WorldSpaceYaw", "RotationEnabled", "PositionEnabled", "TrueFreeLook", "FreeLookMarker", "StockSights"}) {
            core.configConcept(name, CameraUnlock.ROW_WRITABLE, null, null);
        }
        core.configConcept("CollisionMargin", 0, "How far the view is held off a wall, in map tiles.", "0.12");
        check(refusal(() -> core.configConcept("FieldOfVision", 0, null, null)).contains("not a canonical concept"),
                "a name that is no concept is refused in the library's words");

        int fov = core.configLocalFloat("General", "FieldOfView", "Field of view in degrees.", CameraUnlock.ROW_LIVE, 65f, 40f, 110f);
        int quality = core.configLocalEnum("General", "Quality", "The graphics mode.", CameraUnlock.ROW_WRITABLE, "Automatic,Low,Medium,High", 1);
        int arms = core.configLocalBool("Content", "OwnArms", "The mod's own arms.", 0, true);
        int trees = core.configLocalInt("Content", "Trees", "How many trees.", 0, 12, 0, 100);
        int key = core.configLocalHotkey("GraphicsKey", "Goes to the next graphics mode.", 0, "F9", CameraUnlock.HOTKEY_LOCAL);
        check(fov == 0 && quality == 1 && arms == 2 && trees == 3 && key == 4, "local rows are numbered as they are added");
        String taken = refusal(() -> core.configLocalHotkeyHeld("SeatKey", "Tapped for the first player, held for the second.", 0,
                "Delete", CameraUnlock.HOTKEY_LOCAL << 1, CameraUnlock.HOTKEY_LOCAL));
        check(taken.contains("cameraunlock_config_local_hotkey_held") && taken.contains("held_bit is another row's"),
                "a key row that tells a tap from a hold cannot answer another row's bit held: " + taken);
        check(core.configLocalHotkeyHeld("SeatKey", "Tapped for the first player, held for the second.", 0, "Delete",
                        CameraUnlock.HOTKEY_LOCAL << 1, CameraUnlock.HOTKEY_LOCAL << 2) == 5 && CameraUnlock.HOLD_MS == 400,
                "and with two bits of its own is a row numbered like the others");
        String twice = refusal(() -> core.configLocalHotkeyTaps("TurnKey", "Tapped once to three times or held.", 0, "Home",
                new int[] {CameraUnlock.HOTKEY_LOCAL << 3, CameraUnlock.HOTKEY_LOCAL << 2, CameraUnlock.HOTKEY_LOCAL << 6},
                CameraUnlock.HOTKEY_LOCAL << 5));
        check(twice.contains("cameraunlock_config_local_hotkey_taps") && twice.contains("a bit of tap_bits is another row's"),
                "a key row that counts taps cannot answer another row's bit for one of them: " + twice);
        String once = refusal(() -> core.configLocalHotkeyTaps("TurnKey", "Tapped or held.", 0, "Home",
                new int[] {CameraUnlock.HOTKEY_LOCAL << 3}, CameraUnlock.HOTKEY_LOCAL << 5));
        check(once.contains("taps is not 2 to CAMERAUNLOCK_MOST_TAPS"), "nor count one tap alone: " + once);
        check(core.configLocalHotkeyTaps("TurnKey", "Tapped once to three times or held.", 0, "Home",
                        new int[] {CameraUnlock.HOTKEY_LOCAL << 3, CameraUnlock.HOTKEY_LOCAL << 4, CameraUnlock.HOTKEY_LOCAL << 6},
                        CameraUnlock.HOTKEY_LOCAL << 5) == 6 && CameraUnlock.TAP_GAP_MS == 300,
                "and with bits of its own is a row numbered like the others");
        int microphone = core.configLocalText("Voice", "Microphone", "Which microphone.", CameraUnlock.ROW_WRITABLE, "Default");
        check(microphone == 7, "a text row is numbered like the others");
        check(refusal(() -> core.configLocalText("Voice", "Speaker", "Which speaker.", 0, "Default ")).contains("cameraunlock_config_local_text"),
                "and one with a default the file cannot hold as it is is refused");

        core.configRender(rendered);
        String fresh = Files.readString(rendered, StandardCharsets.US_ASCII);
        check(fresh.contains("[CameraUnlock]") && fresh.contains("\r\nFieldOfView=65.0\r\n") && fresh.contains("\r\nQuality=Low\r\n")
                        && fresh.contains("\r\nGraphicsKey=F9\r\n") && fresh.contains("\r\nSeatKey=Delete\r\n") && fresh.contains("\r\nTurnKey=Home\r\n") && fresh.contains("\r\nCollisionMargin=0.12\r\n")
                        && fresh.contains("; How far the view is held off a wall, in map tiles.\r\n") && fresh.contains("\r\nLocalSmoothing=default\r\n")
                        && fresh.contains("\r\nMicrophone=Default\r\n"),
                "the render holds the stamp, the local rows, the game's own default and comment, and the global concepts as default");

        CameraUnlock.Config config = core.configLoad(file, defaults);
        check(config.status == CameraUnlock.LOAD_CREATED && Arrays.equals(Files.readAllBytes(file), Files.readAllBytes(rendered)),
                "a first load creates the file the render wrote");
        check(Files.exists(defaults), "and Defaults.ini where the load was told it is");
        check(config.udpPort == 4242 && config.enableOnStartup && config.worldSpaceYaw
                        && config.settings.trackingMode == CameraUnlock.TRACKING_ROTATION_AND_POSITION && config.settings.collisionEnabled
                        && near(config.settings.collisionMargin, 0.12f) && near(config.settings.remoteSmoothing, 0.15f)
                        && near(config.settings.lightMultiplier, 1.5f) && config.settings.dataFreshnessMs == 500,
                "the fleet's rows come back, the settings nested in them read by name");
        core.configure(config.settings);

        check(core.configGetFloat(fov) == 65f && core.configGetInt(quality) == 1 && core.configGetInt(arms) == 1 && core.configGetInt(trees) == 12,
                "local rows read back: a float, an enum as its word's place, a bool as 1, an int");
        check(core.configSaveInt(quality, 3) == CameraUnlock.SAVE_SAVED && Files.readString(file).contains("\r\nQuality=High\r\n"),
                "a writable row saves as its word");
        check(refusal(() -> core.configSaveInt(arms, 0)).contains("Writable"), "a row not marked writable does not");
        check(refusal(() -> core.configSaveFloat(quality, 1f)).contains("not a float row"), "nor a float into an enum row");
        check(core.configSaveRows(new int[] {quality}, new double[] {2}) == CameraUnlock.SAVE_SAVED
                        && Files.readString(file).contains("\r\nQuality=Medium\r\n") && core.configGetInt(quality) == 2,
                "rows saved together are written and read back");
        byte[] before = Files.readAllBytes(file);
        String together = refusal(() -> core.configSaveRows(new int[] {quality, arms}, new double[] {1, 0}));
        check(together.contains("cameraunlock_config_save_rows") && together.contains("Writable")
                        && Arrays.equals(Files.readAllBytes(file), before) && core.configGetInt(quality) == 2,
                "a row among them that may not be saved refuses them all, with nothing written: " + together);
        String uneven = "";
        try {
            core.configSaveRows(new int[] {quality}, new double[] {1, 2});
        } catch (IllegalArgumentException refused) {
            uneven = refused.getMessage();
        }
        check(uneven.equals("1 rows and 2 values"), "and as many values as rows are given: " + uneven);
        check(core.configSaveRows(new int[] {quality}, new double[] {3}) == CameraUnlock.SAVE_SAVED, "the row saves again");
        check(core.configGetText(microphone).equals("Default"), "a text row reads back");
        String named = "Microphone (USB Audio Device) é";
        check(core.configSaveText(microphone, named) == CameraUnlock.SAVE_SAVED
                        && Files.readString(file, StandardCharsets.UTF_8).contains("\r\nMicrophone=" + named + "\r\n")
                        && core.configGetText(microphone).equals(named),
                "and saves as UTF-8, reading back as saved");
        check(refusal(() -> core.configSaveText(microphone, "two\nlines")).contains("cameraunlock_config_save_text")
                        && refusal(() -> core.configGetText(fov)).contains("not a text row"),
                "text the file cannot hold is refused, and a float row is not read as text");
        // The checks after this one read the file as ASCII.
        core.configSaveText(microphone, "Default");

        core.cycleTrackingMode();
        core.setAimMode(CameraUnlock.AIM_FREE_LOOK_MARKER);
        check(core.configSaveTrackingMode() == CameraUnlock.SAVE_SAVED && core.configSaveAimMode() == CameraUnlock.SAVE_SAVED
                        && core.configSaveWorldSpaceYaw(false) == CameraUnlock.SAVE_SAVED,
                "the session's modes and the yaw mode save");
        String saved = Files.readString(file, StandardCharsets.US_ASCII);
        check(saved.contains("\r\nRotationEnabled=true\r\n") && saved.contains("\r\nPositionEnabled=false\r\n")
                        && saved.contains("\r\nTrueFreeLook=true\r\n") && saved.contains("\r\nFreeLookMarker=true\r\n")
                        && saved.contains("\r\nWorldSpaceYaw=false\r\n"),
                "as the rows core's encoders give");

        CameraUnlock.Option yaw = core.configOption(0), tracking = core.configOption(1), aim = core.configOption(2),
                graphics = core.configOption(3);
        check(core.configOptionCount() == 4 && yaw.id.equals("WorldSpaceYaw") && yaw.kind == CameraUnlock.OPTION_BOOL
                        && yaw.source == CameraUnlock.OPTION_ROW && yaw.choices.length == 0 && yaw.max == 1 && !yaw.comment.isEmpty(),
                "the options are the writable rows a control holds: a bool row");
        check(tracking.id.equals("TrackingMode") && tracking.source == CameraUnlock.OPTION_TRACKING_MODE
                        && tracking.label.equals("Tracking mode") && tracking.section.equals("General")
                        && Arrays.equals(tracking.choices, new String[] {"Rotation and position", "Rotation only", "Position only"})
                        && aim.id.equals("AimMode") && aim.source == CameraUnlock.OPTION_AIM_MODE && aim.choices.length == 4,
                "each mode as one option, with its modes as words");
        check(graphics.id.equals("Quality") && graphics.kind == CameraUnlock.OPTION_ENUM && graphics.label.equals("Quality")
                        && graphics.section.equals("General") && graphics.comment.equals("The graphics mode.")
                        && Arrays.equals(graphics.choices, new String[] {"Automatic", "Low", "Medium", "High"})
                        && graphics.min == 0 && graphics.max == 3 && graphics.step == 1,
                "and an enum row with its words, range and comment");
        check(core.configOptionGet(0) == 0 && core.configOptionGet(1) == CameraUnlock.TRACKING_ROTATION_ONLY
                        && core.configOptionGet(2) == CameraUnlock.AIM_FREE_LOOK_MARKER && core.configOptionGet(3) == 3,
                "each reads as one number, as it was last saved");
        check(core.configOptionSave(3, 2) == CameraUnlock.SAVE_SAVED && core.configOptionSave(0, 1) == CameraUnlock.SAVE_SAVED
                        && core.configOptionSave(1, CameraUnlock.TRACKING_POSITION_ONLY) == CameraUnlock.SAVE_SAVED
                        && core.configOptionSave(2, CameraUnlock.AIM_STOCK_SIGHTS) == CameraUnlock.SAVE_SAVED,
                "and saves");
        String paged = Files.readString(file, StandardCharsets.US_ASCII);
        check(paged.contains("\r\nQuality=Medium\r\n") && paged.contains("\r\nWorldSpaceYaw=true\r\n")
                        && paged.contains("\r\nRotationEnabled=false\r\n") && paged.contains("\r\nPositionEnabled=true\r\n")
                        && paged.contains("\r\nTrueFreeLook=false\r\n") && paged.contains("\r\nStockSights=true\r\n"),
                "as its row, or every row of its mode");
        check(core.configGetInt(quality) == 2 && core.configOptionGet(1) == CameraUnlock.TRACKING_POSITION_ONLY
                        && core.cycleAimMode() == CameraUnlock.AIM_SIGHTS_LOCKED && core.configOptionGet(2) == CameraUnlock.AIM_SIGHTS_LOCKED,
                "a saved mode is the session's, and a mode the session cycled is what its option reads");
        check(refusal(() -> core.configOptionSave(3, 7)).contains("none of the row's words")
                        && refusal(() -> core.configOptionSave(1, 3)).contains("not a tracking mode")
                        && refusal(() -> core.configOption(4)).contains("no option 4") && core.configOptionGet(3) == 2,
                "a value an option does not hold, and an option that is not there, are refused in the library's words");
        core.configOptionSave(1, CameraUnlock.TRACKING_ROTATION_ONLY);

        check(core.configReload() == CameraUnlock.RELOAD_UNCHANGED, "a file nobody touched is not read again");
        Files.writeString(file, saved.replace("FieldOfView=65.0", "FieldOfView=90").replace("Trees=12", "Trees=40"), StandardCharsets.US_ASCII);
        Files.setLastModifiedTime(file, java.nio.file.attribute.FileTime.fromMillis(System.currentTimeMillis() + 5000));
        check(core.configReload() == CameraUnlock.RELOAD_APPLIED, "a file the player saved is");
        check(core.configGetFloat(fov) == 90f && core.configGetInt(trees) == 12,
                "a live row holds the new value and a row not marked live what the load gave it");
        core.setAimMode(CameraUnlock.AIM_SIGHTS_LOCKED);
        core.cycleTrackingMode();
        core.cycleTrackingMode();
    }

    private static void hotkeysAndTheWindow(CameraUnlock core) throws Throwable {
        core.hotkeysStart();
        core.hotkeysDrop();
        check(core.hotkeysTake() == 0, "with no key pressed nothing is taken");
        if (Testing.BUNDLED) hotkeysFireFromTheTick(core);
        check(!core.windowCenter(0L), "a window that is none is left alone");
        aWindowIsCentredInItsWorkArea(core);
    }

    // ---- Windows, through the calls core makes ------------------------------------------------------

    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup USER32 = SymbolLookup.libraryLookup("user32", Arena.global());
    private static final SymbolLookup KERNEL32 = SymbolLookup.libraryLookup("kernel32", Arena.global());

    private static MethodHandle windows(SymbolLookup library, String name, FunctionDescriptor descriptor) {
        return LINKER.downcallHandle(library.find(name).orElseThrow(), descriptor);
    }

    /** What Windows calls error `error`, as FormatMessageW gives it with no inserts, trailing breaks dropped. */
    private static String windowsMessage(int error) throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buffer = arena.allocate(2L * 1024);
            int units = (int) windows(KERNEL32, "FormatMessageW", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                    ValueLayout.ADDRESS)).invokeExact(0x1000 | 0x200, MemorySegment.NULL, error, 0x400, buffer, 1024, MemorySegment.NULL);
            String message = new String(buffer.asSlice(0, 2L * units).toArray(ValueLayout.JAVA_CHAR_UNALIGNED));
            return message.replaceAll("[\\r\\n ]+$", "");
        }
    }

    /**
     * The calls of the config owner, the file writer and the clock that nothing else here reaches,
     * each against what Windows or Java says: the DLL makes them of Windows, the module of Win32Host.
     */
    private static void windowsCallsAnswerAsWindowsDoes(Testing testing, Path scratch) throws Throwable {
        Path folder = Files.createDirectories(scratch.resolve("win32"));
        String name = "Gr\u00fc\u00dfe \u540d\u524d \ud83d\ude00.txt";
        Path named = Files.writeString(folder.resolve(name), "0123456789");
        check(testing.win32("find", named.toString()).equals(name), "FindFirstFileW finds a file by its name, and gives it whole");
        check(testing.win32("find", folder + "\\*.txt").equals(name), "and by a pattern");
        check(testing.win32("compare", "gr\u00fc\u00dfe\nGR\u00dc\u00dfE").equals("2") && testing.win32("compare", "a\nb").equals("1")
                        && testing.win32("compare", "b\nA").equals("3"),
                "CompareStringOrdinal orders two texts as Windows does, case aside");
        String encoding = System.getProperty("native.encoding");
        String codePage = testing.win32("code_page", "");
        Charset ansi = Charset.forName(encoding);
        check(codePage.equals("65001") ? ansi.equals(StandardCharsets.UTF_8)
                        : java.util.stream.Stream.concat(java.util.stream.Stream.of(ansi.name()), ansi.aliases().stream())
                                .anyMatch(alias -> alias.replaceAll("\\D", "").equals(codePage)),
                "GetACP is the code page Java was started on: " + codePage + " and " + encoding);
        String narrowed = testing.win32("narrow", "caf\u00e9 1");
        check(narrowed.equals(HexFormat.of().formatHex("caf\u00e9 1".getBytes(Charset.forName(encoding)))),
                "WideCharToMultiByte writes the ANSI code page's bytes: " + narrowed);
        check(testing.win32("read_from", "4\n" + named).equals("456789"), "SetFilePointerEx moves where ReadFile reads from");
        String text = testing.win32("error_text", "2");
        check(text.equals("Windows error 2: " + windowsMessage(2)), "FormatMessageW gives Windows' own text for an error: " + text);
        check(testing.win32("delete", named.toString()).equals("1") && !Files.exists(named), "DeleteFileW deletes the file");
        long slept = Long.parseLong(testing.win32("sleep", "30"));
        check(slept >= 25 && slept < 2000, "Sleep waits as long as it is asked, by QueryPerformanceCounter: " + slept + " ms");
    }

    /** The reason of a refused call is the one the calling thread was refused for, though others are refused at once. */
    private static void eachThreadIsGivenItsOwnReason(CameraUnlock core) throws InterruptedException {
        List<String> wrong = java.util.Collections.synchronizedList(new ArrayList<>());
        Thread views = new Thread(() -> {
            for (int i = 0; i < 300; i++) {
                String reason = refusal(() -> core.viewStop(-1));
                if (!reason.contains("there is no view -1")) wrong.add(reason);
            }
        });
        Thread labels = new Thread(() -> {
            for (int i = 0; i < 300; i++) {
                String reason = refusal(() -> core.aimModeLabel(9));
                if (!reason.contains("aim_mode")) wrong.add(reason);
            }
        });
        views.start();
        labels.start();
        views.join();
        labels.join();
        check(wrong.isEmpty(), "two threads refused at once are each given their own reason: " + wrong.stream().limit(3).toList());
    }

    private static Object field(Object owner, String name) throws ReflectiveOperationException {
        Field field = owner.getClass().getDeclaredField(name);
        field.setAccessible(true);
        return field.get(owner);
    }

    private static void setField(Object owner, String name, Object value) throws ReflectiveOperationException {
        Field field = owner.getClass().getDeclaredField(name);
        field.setAccessible(true);
        field.set(owner, value);
    }

    /** A receive loop whose socket fails ends, and the receiver's supervisor opens the port again. */
    private static void aReceiveLoopThatFailsIsReopened(Testing testing) throws Throwable {
        CameraUnlock core = testing.core;
        testing.reset();
        core.configure(core.settingsDefaults());
        CameraUnlock.FrameInput input = leaning();
        input.flags = CameraUnlock.FRAME_ACTIVE;
        int port = freePort();
        core.start(port);
        core.logTake();
        Object module = field(core, "backend");
        @SuppressWarnings("unchecked")
        Map<Integer, Object> receiving = (Map<Integer, Object>) field(field(module, "host"), "receiving");
        long socket = (long) field(receiving.values().iterator().next(), "socket");
        MethodHandle closesocket = windows(SymbolLookup.libraryLookup("ws2_32", Arena.global()), "closesocket",
                FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.JAVA_LONG));
        synchronized (module) {
            check((int) closesocket.invokeExact(socket) == 0, "the receive loop's socket is closed under it");
        }
        try {
            StringBuilder log = new StringBuilder();
            frameUntil(core, input, () -> {}, f -> log.append(core.logTake()).toString().contains("socket re-established"));
            check(log.toString().contains("Receive thread failed on UDP port " + port + " - socket re-established"),
                    "the receiver says its loop failed and the socket is open again: " + log);
            // Made only now: the receiver closes the old socket's value as it reopens, and a socket of
            // this test's made before then could have been given that value.
            try (DatagramSocket tracker = new DatagramSocket(0, InetAddress.getLoopbackAddress())) {
                byte[] pose = datagram(0, 0, 0, -20, 4, 2);
                CameraUnlock.Frame frame = frameUntil(core, input, () -> {
                    try {
                        tracker.send(new DatagramPacket(pose, pose.length, InetAddress.getLoopbackAddress(), port));
                    } catch (IOException failure) {
                        throw new IllegalStateException(failure);
                    }
                }, f -> (f.flags & CameraUnlock.STATE_POSE) != 0 && near(f.headYaw, -20f));
                check(near(frame.headYaw, -20f), "and a tracker reaches the frame through it: yaw " + frame.headYaw);
            }
        } finally {
            core.stop();
        }
    }

    /** Datagrams too long for a pose are dropped one by one, and a pose sent after a burst of them still arrives at once. */
    private static void aFloodOfLongDatagramsDoesNotHoldThePoseBack(Testing testing) throws Throwable {
        CameraUnlock core = testing.core;
        testing.reset();
        core.configure(core.settingsDefaults());
        CameraUnlock.FrameInput input = leaning();
        input.flags = CameraUnlock.FRAME_ACTIVE;
        int port = freePort();
        core.start(port);
        try (DatagramSocket tracker = new DatagramSocket(0, InetAddress.getLoopbackAddress())) {
            byte[] tooLong = new byte[200];
            for (int sent = 0; sent < 300; sent++) {
                tracker.send(new DatagramPacket(tooLong, tooLong.length, InetAddress.getLoopbackAddress(), port));
            }
            byte[] pose = datagram(0, 0, 0, 25, 0, 0);
            long start = System.nanoTime();
            CameraUnlock.Frame frame = frameUntil(core, input, () -> {
                try {
                    tracker.send(new DatagramPacket(pose, pose.length, InetAddress.getLoopbackAddress(), port));
                } catch (IOException failure) {
                    throw new IllegalStateException(failure);
                }
            }, f -> (f.flags & CameraUnlock.STATE_POSE) != 0 && near(f.headYaw, 25f));
            long took = TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - start);
            check(near(frame.headYaw, 25f) && took < 1000, "a pose behind 300 datagrams too long for one arrives in " + took + " ms, yaw " + frame.headYaw);
        } finally {
            core.stop();
            core.logTake();
        }
    }

    /** A module that traps is called no more: every later call says it stopped and why, and its tick thread ends. */
    private static void aModuleThatTrapsIsNotCalledAgain() throws Throwable {
        CameraUnlock dying = CameraUnlock.bundled(com.cameraunlock.core.wasm.CoreTestingModule::load,
                com.cameraunlock.core.wasm.CoreTestingModule::create, com.cameraunlock.core.wasm.CoreTestingModule.class);
        Supplier<Long> tickers = () -> Thread.getAllStackTraces().keySet().stream()
                .filter(thread -> thread.getName().equals("cameraunlock-core tick")).count();
        long before = tickers.get();
        MethodHandle trap = dying.bind("cameraunlock_testing_trap", FunctionDescriptor.of(ValueLayout.JAVA_INT));
        String first = refusal(() -> {
            try {
                int ignored = (int) trap.invokeExact();
            } catch (RuntimeException | Error failure) {
                throw failure;
            } catch (Throwable failure) {
                throw new IllegalStateException(failure);
            }
        });
        check(first.contains("stopped working and is no longer called"), "a trap stops the module and says so: " + first);
        String later = refusal(() -> dying.settingsDefaults());
        check(later.contains("no longer called") && later.contains(first.substring(first.indexOf(": ") + 2)),
                "and every later call says so with the first cause: " + later);
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
        while (tickers.get() >= before && System.nanoTime() < end) Thread.sleep(16);
        check(tickers.get() == before - 1, "and its tick thread ends");
    }

    private static volatile int heldKey;
    /** How many times the tick has asked for F9 while it was held, and while it was not. */
    private static final java.util.concurrent.atomic.AtomicInteger seenDown = new java.util.concurrent.atomic.AtomicInteger(),
            seenUp = new java.util.concurrent.atomic.AtomicInteger();

    @SuppressWarnings("unused")
    private static short keyState(MemorySegment capture, int key) {
        boolean down = key == heldKey;
        if (key == 0x78) (down ? seenDown : seenUp).incrementAndGet();
        return (short) (down ? 0x8000 : 0);
    }

    /** Waits until `count` is past `from`, or two seconds pass. */
    private static boolean polledPast(java.util.concurrent.atomic.AtomicInteger count, int from) throws InterruptedException {
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
        while (count.get() <= from && System.nanoTime() < end) Thread.sleep(4);
        return count.get() > from;
    }

    @SuppressWarnings("unused")
    private static MemorySegment ourWindow(MemorySegment capture) {
        return MemorySegment.ofAddress(0x10);
    }

    @SuppressWarnings("unused")
    private static int ourProcess(MemorySegment capture, MemorySegment window, MemorySegment process) {
        if (!process.equals(MemorySegment.NULL)) process.set(ValueLayout.JAVA_INT, 0, (int) ProcessHandle.current().pid());
        return 1;
    }

    /**
     * A key held while the game's window is in front is a press the tick finds: Win32Host's key
     * state and foreground window answer as the test says, under the module's lock, since a test
     * presses no real key.
     */
    private static void hotkeysFireFromTheTick(CameraUnlock core) throws Throwable {
        Object module = field(core, "backend");
        Object host = field(module, "host");
        MethodHandles.Lookup lookup = MethodHandles.lookup();
        Map<String, MethodHandle> fakes = Map.of(
                "getAsyncKeyState", lookup.findStatic(Tests.class, "keyState",
                        MethodType.methodType(short.class, MemorySegment.class, int.class)),
                "getForegroundWindow", lookup.findStatic(Tests.class, "ourWindow",
                        MethodType.methodType(MemorySegment.class, MemorySegment.class)),
                "getWindowThreadProcessId", lookup.findStatic(Tests.class, "ourProcess",
                        MethodType.methodType(int.class, MemorySegment.class, MemorySegment.class, MemorySegment.class)));
        Map<String, Object> real = new java.util.HashMap<>();
        synchronized (module) {
            for (Map.Entry<String, MethodHandle> fake : fakes.entrySet()) {
                real.put(fake.getKey(), field(host, fake.getKey()));
                setField(host, fake.getKey(), fake.getValue());
            }
        }
        try {
            int down = seenDown.get();
            heldKey = 0x78;
            check(polledPast(seenDown, down), "the tick asks for F9 while it is held");
            int up = seenUp.get();
            heldKey = 0;
            check(polledPast(seenUp, up), "and once it is let go");
            int taken = core.hotkeysTake();
            check((taken & CameraUnlock.HOTKEY_LOCAL) != 0, "F9 held and let go in the game's window fires its row: " + Integer.toHexString(taken));
            check(core.hotkeysTake() == 0, "and is taken once");
        } finally {
            heldKey = 0;
            synchronized (module) {
                for (Map.Entry<String, Object> one : real.entrySet()) setField(host, one.getKey(), one.getValue());
            }
        }
    }

    /** A window of this thread, hidden, with a caption: core moves it to the middle of its monitor's work area. */
    private static void aWindowIsCentredInItsWorkArea(CameraUnlock core) throws Throwable {
        MethodHandle create = windows(USER32, "CreateWindowExW", FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.JAVA_INT,
                ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT,
                ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
        MethodHandle destroy = windows(USER32, "DestroyWindow", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS));
        MethodHandle rectangle = windows(USER32, "GetWindowRect", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
        MethodHandle monitor = windows(USER32, "MonitorFromWindow", FunctionDescriptor.of(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
                ValueLayout.JAVA_INT));
        MethodHandle monitorInfo = windows(USER32, "GetMonitorInfoW", FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
                ValueLayout.ADDRESS));
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment className = arena.allocateFrom("STATIC\0", StandardCharsets.UTF_16LE);
            MemorySegment title = arena.allocateFrom("\0", StandardCharsets.UTF_16LE);
            int overlapped = 0x00CF0000, popup = 0x80000000;
            MemorySegment window = (MemorySegment) create.invokeExact(0, className, title, overlapped, 7, 9, 320, 200,
                    MemorySegment.NULL, MemorySegment.NULL, MemorySegment.NULL, MemorySegment.NULL);
            MemorySegment borderless = (MemorySegment) create.invokeExact(0, className, title, popup, 7, 9, 320, 200,
                    MemorySegment.NULL, MemorySegment.NULL, MemorySegment.NULL, MemorySegment.NULL);
            check(!window.equals(MemorySegment.NULL) && !borderless.equals(MemorySegment.NULL), "two hidden windows are made");
            try {
                core.logTake();
                check(core.windowCenter(window.address()), "a window with a caption is centred");
                MemorySegment rect = arena.allocate(16), info = arena.allocate(40);
                info.set(ValueLayout.JAVA_INT, 0, 40);
                check((int) rectangle.invokeExact(window, rect) != 0
                        && (int) monitorInfo.invokeExact((MemorySegment) monitor.invokeExact(window, 2), info) != 0, "and asked where it is");
                int left = rect.get(ValueLayout.JAVA_INT, 0), top = rect.get(ValueLayout.JAVA_INT, 4);
                int width = rect.get(ValueLayout.JAVA_INT, 8) - left, height = rect.get(ValueLayout.JAVA_INT, 12) - top;
                int workLeft = info.get(ValueLayout.JAVA_INT, 20), workTop = info.get(ValueLayout.JAVA_INT, 24);
                int workWidth = info.get(ValueLayout.JAVA_INT, 28) - workLeft, workHeight = info.get(ValueLayout.JAVA_INT, 32) - workTop;
                check(width == 320 && height == 200 && left == workLeft + (workWidth - width) / 2 && top == workTop + (workHeight - height) / 2,
                        "it is in the middle of its monitor's work area: " + width + "x" + height + " at " + left + "," + top);
                check(core.logTake().contains("window: centered 320x200"), "and the log says where it went");
                check(!core.windowCenter(borderless.address()) && core.logTake().contains("borderless"),
                        "a window with no caption is left where it is, and the log says why");
            } finally {
                int ignored = (int) destroy.invokeExact(window);
                ignored = (int) destroy.invokeExact(borderless);
            }
        }
    }

    /** A port no socket of this machine holds now. */
    private static int freePort() throws IOException {
        try (DatagramSocket probe = new DatagramSocket(0, InetAddress.getLoopbackAddress())) {
            return probe.getLocalPort();
        }
    }

    /** Frames until `until` holds or two seconds pass: what the receiver does runs on threads of its own. */
    private static CameraUnlock.Frame frameUntil(CameraUnlock core, CameraUnlock.FrameInput input, Runnable each,
            java.util.function.Predicate<CameraUnlock.Frame> until) throws InterruptedException {
        CameraUnlock.Frame frame = new CameraUnlock.Frame();
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
        do {
            each.run();
            Thread.sleep(16);
            input.nowMs += 16;
            core.frame(input, frame);
        } while (!until.test(frame) && System.nanoTime() < end);
        return frame;
    }

    /** The receiver on a real socket: what a tracker sends reaches the frame, and a port held by another is waited for. */
    private static void aTrackerOnThePortReachesTheFrame(Testing testing) throws IOException, InterruptedException {
        CameraUnlock core = testing.core;
        testing.reset();
        core.configure(core.settingsDefaults());
        core.logTake();
        CameraUnlock.FrameInput input = leaning();
        input.flags = CameraUnlock.FRAME_ACTIVE;
        int port = freePort();
        core.start(port);
        check(core.logTake().contains("Listening for OpenTrack datagrams on UDP port " + port), "a start says it listens");
        try (DatagramSocket tracker = new DatagramSocket(0, InetAddress.getLoopbackAddress())) {
            byte[] pose = datagram(0, 0, 0, 25, -5, 3);
            Runnable send = () -> {
                try {
                    tracker.send(new DatagramPacket(pose, pose.length, InetAddress.getLoopbackAddress(), port));
                } catch (IOException failure) {
                    throw new IllegalStateException(failure);
                }
            };
            CameraUnlock.Frame frame = frameUntil(core, input, send, f -> (f.flags & CameraUnlock.STATE_POSE) != 0 && near(f.headYaw, 25f));
            check((frame.flags & (CameraUnlock.STATE_LISTENING | CameraUnlock.STATE_POSE | CameraUnlock.STATE_FRESH))
                            == (CameraUnlock.STATE_LISTENING | CameraUnlock.STATE_POSE | CameraUnlock.STATE_FRESH)
                            && near(frame.headYaw, 25f) && near(frame.headPitch, -5f) && near(frame.headRoll, 3f),
                    "a tracker sending to the port reaches the frame: " + Integer.toHexString(frame.flags) + " yaw " + frame.headYaw);
            check((frame.flags & CameraUnlock.STATE_REMOTE) == 0, "from loopback, which is local");
            check(core.logTake().contains("First UDP packet received: 48 bytes from 127.0.0.1"), "and the first datagram is in the log");
        }
        core.stop();

        int held = freePort();
        try (DatagramSocket other = new DatagramSocket(held, InetAddress.getLoopbackAddress())) {
            check(other.getLocalPort() == held, "another socket holds the port");
            core.start(held);
            CameraUnlock.Frame frame = new CameraUnlock.Frame();
            input.nowMs += 16;
            core.frame(input, frame);
            check((frame.flags & CameraUnlock.STATE_LISTENING) == 0 && core.logTake().contains("Failed to bind UDP port " + held),
                    "a port another socket holds is not listened on, and the log says why");
        }
        CameraUnlock.Frame bound = frameUntil(core, input, () -> {}, f -> (f.flags & CameraUnlock.STATE_LISTENING) != 0);
        check((bound.flags & CameraUnlock.STATE_LISTENING) != 0 && core.logTake().contains("Bound UDP port " + held + " after"),
                "once the port is let go the receiver binds it, and says so");
        core.stop();
        try (DatagramSocket after = new DatagramSocket(held, InetAddress.getLoopbackAddress())) {
            check(after.getLocalPort() == held, "a stop lets the port go");
        }
    }

    private static void logGoesToItsFile(CameraUnlock core, Path scratch) throws IOException {
        refusal(() -> core.start(-1));
        check(core.logTake().contains("udp_port is outside 1 to 65535"), "until the file log is open, core's lines are there to take");
        check(core.logTake().isEmpty(), "once");
        check(refusal(() -> core.logWrite("too early")).contains("cameraunlock_log_open"), "and a line for the file is refused");
        core.logTake();
        Path log = scratch.resolve("Mod.log");
        core.logOpen(log);
        core.logWrite("the host's own line, with a letter past ASCII: é");
        refusal(() -> core.start(-1));
        String text = Files.readString(log, StandardCharsets.UTF_8);
        check(text.contains("the host's own line, with a letter past ASCII: é\r\n"), "the host's line is in the file, as UTF-8");
        check(text.contains("udp_port is outside 1 to 65535"), "and core's lines from then on");
    }

    private record Ran(int exit, String output) {}

    /**
     * The 8.3 short form of a folder (GAMEFO~1), as a CI runner's TEMP gives it, or the folder
     * itself on a volume that keeps no short names.
     */
    @SuppressWarnings("restricted")
    private static Path shortForm(Path folder) throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            MethodHandle getShortPathName = Linker.nativeLinker().downcallHandle(
                    SymbolLookup.libraryLookup("kernel32", arena).find("GetShortPathNameW").orElseThrow(),
                    FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
            MemorySegment longName = arena.allocateFrom(folder.toString(), StandardCharsets.UTF_16LE);
            MemorySegment shortName = arena.allocate(2 * 32768);
            int length = (int) getShortPathName.invokeExact(longName, shortName, 32768);
            if (length == 0 || length >= 32768) throw new IOException("GetShortPathNameW failed on " + folder);
            return Path.of(shortName.getString(0, StandardCharsets.UTF_16LE));
        }
    }

    /** Runs the boot class as the site JVM config has a game's launcher run it, from the game folder. */
    private static Ran boot(Path game, String agent, boolean namesTheGame) throws IOException, InterruptedException {
        List<String> command = new ArrayList<>(List.of(Path.of(System.getProperty("java.home"), "bin", "java").toString(),
                "-XX:+EnableDynamicAgentLoading", "-Dfixture.agent=" + agent, "-cp", "Fixture.jar"));
        if (namesTheGame) {
            command.add("-Dcameraunlock.mainClass=fixture/Game");
        }
        command.addAll(List.of("com.cameraunlock.core.agent.Boot", "first", "second"));
        Process process = new ProcessBuilder(command).directory(game.toFile()).redirectErrorStream(true).start();
        String output = new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        if (!process.waitFor(60, TimeUnit.SECONDS)) {
            process.destroyForcibly();
            throw new IOException("the boot class did not finish");
        }
        return new Ran(process.exitValue(), output);
    }

    private static void bootLoadsTheAgentAndStartsTheGame(Path scratch) throws Throwable {
        // A space in the game folder's path, as an installed game's usually has, and the game
        // started from its short form, which the boot class has to match to the jar's long one.
        Path game = shortForm(Files.createDirectories(scratch.resolve("Game Folder")));
        Files.copy(CameraUnlock.beside(Tests.class).resolve("boot").resolve("Fixture.jar"), game.resolve("Fixture.jar"));

        Ran ran = boot(game, "starts", true);
        Supplier<String> said = () -> ": exit " + ran.exit() + ", " + ran.output();
        check(ran.exit() == 0 && ran.output().contains("agent started") && ran.output().contains("game ran with first,second"),
                "the boot class loads its jar as an agent, then runs the game's main with the arguments" + said.get());
        check(ran.output().indexOf("agent started") < ran.output().indexOf("game ran"), "the agent before the game");

        Ran failed = boot(game, "fails", true);
        check(failed.exit() == 0 && failed.output().contains("game ran with first,second"),
                "an agent that starts and then fails leaves the game to run: exit " + failed.exit() + ", " + failed.output());

        Ran silent = boot(game, "silent", true);
        check(silent.exit() != 0 && silent.output().contains("did not start Fixture.jar as a Java agent") && !silent.output().contains("game ran"),
                "an agent that never says it started stops the boot: exit " + silent.exit() + ", " + silent.output());

        Ran unnamed = boot(game, "starts", false);
        check(unnamed.exit() != 0 && unnamed.output().contains("-Dcameraunlock.mainClass"),
                "a JVM that was not told the game's main class says which argument is missing: exit " + unnamed.exit() + ", " + unnamed.output());
    }
}
