package com.cameraunlock.core;

import java.io.IOException;
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
public final class Tests {
    private static int checks;
    private static int failures;

    private Tests() {}

    public static void main(String[] arguments) throws Exception {
        Path scratch = Files.createTempDirectory("cameraunlock-java-tests");
        layoutsAreTheDocumentedOnes();
        libraryIsFoundBesideTheClasses();
        libraryOfAnotherAbiIsRefusedInWords();
        Testing testing = new Testing();
        refusalCarriesTheLibrarysReason(testing.core);
        poseArrivesAndTheLeanIsHeldOffAWall(testing);
        modesAreSetCycledAndNamed(testing.core);
        viewsEachHaveATrackerAndShareTheModes(testing);
        configIsDescribedRenderedLoadedSavedAndReadAgain(testing.core, scratch);
        hotkeysAndTheWindow(testing.core);
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

    private static void libraryIsFoundBesideTheClasses() {
        CameraUnlock core = CameraUnlock.load();
        check(core.library().equals(CameraUnlock.beside(CameraUnlock.class).resolve("CameraUnlockCore.dll")),
                "the library a mod ships is loaded from beside the class folder: " + core.library());
        check(core.settingsDefaults().dataFreshnessMs == 500, "and answers");
    }

    private static void libraryOfAnotherAbiIsRefusedInWords() {
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
        String twice = refusal(() -> core.configLocalHotkeyTaps("TurnKey", "Tapped, tapped twice or held.", 0, "Home",
                CameraUnlock.HOTKEY_LOCAL << 3, CameraUnlock.HOTKEY_LOCAL << 2, CameraUnlock.HOTKEY_LOCAL << 5));
        check(twice.contains("cameraunlock_config_local_hotkey_taps") && twice.contains("double_bit is another row's"),
                "a key row that tells a double tap as well cannot answer another row's bit for it: " + twice);
        check(core.configLocalHotkeyTaps("TurnKey", "Tapped, tapped twice or held.", 0, "Home", CameraUnlock.HOTKEY_LOCAL << 3,
                        CameraUnlock.HOTKEY_LOCAL << 4, CameraUnlock.HOTKEY_LOCAL << 5) == 6 && CameraUnlock.DOUBLE_TAP_MS == 300,
                "and with three bits of its own is a row numbered like the others");

        core.configRender(rendered);
        String fresh = Files.readString(rendered, StandardCharsets.US_ASCII);
        check(fresh.contains("[CameraUnlock]") && fresh.contains("\r\nFieldOfView=65.0\r\n") && fresh.contains("\r\nQuality=Low\r\n")
                        && fresh.contains("\r\nGraphicsKey=F9\r\n") && fresh.contains("\r\nSeatKey=Delete\r\n") && fresh.contains("\r\nTurnKey=Home\r\n") && fresh.contains("\r\nCollisionMargin=0.12\r\n")
                        && fresh.contains("; How far the view is held off a wall, in map tiles.\r\n") && fresh.contains("\r\nLocalSmoothing=default\r\n"),
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

    private static void hotkeysAndTheWindow(CameraUnlock core) {
        core.hotkeysDrop();
        check(core.hotkeysTake() == 0, "with no key pressed nothing is taken");
        check(!core.windowCenter(0L), "a window that is none is left alone");
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

    private static void bootLoadsTheAgentAndStartsTheGame(Path scratch) throws IOException, InterruptedException {
        // A space in the game folder's path, as an installed game's usually has.
        Path game = Files.createDirectories(scratch.resolve("Game Folder"));
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
