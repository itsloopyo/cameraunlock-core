package com.cameraunlock.core;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.FileTime;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Predicate;

/**
 * The jar's module on Linux, where PosixHost answers its Win32 calls (docs/core-jar.md): the log,
 * CameraUnlock.ini and Defaults.ini, the hotkeys through a key source, and a tracker on a real
 * socket. Tests.java holds the same calls to Windows' answers; this runs where there is no
 * Windows, through `pixi run test-java-linux`, in a Linux container, as a user that is not root so
 * a file's permissions hold. Every check runs, and the tally is printed once at the end.
 */
public final class LinuxTests {
    private static final String CHILD = "defaults-child";
    private static int checks;
    private static int failures;
    /** The mod's log, which core's lines go to once it is open, and how much of it said() has read. */
    private static Path modLog;
    private static int read;

    private LinuxTests() {}

    public static void main(String[] arguments) throws Throwable {
        if (arguments.length == 2 && arguments[0].equals(CHILD)) {
            child(Path.of(arguments[1]));
            return;
        }
        Path scratch = Files.createTempDirectory("cameraunlock-linux-tests");
        check(!System.getProperty("os.name").startsWith("Windows"), "this runs on Linux: " + System.getProperty("os.name"));
        CameraUnlock core = CameraUnlock.loadBundled();
        check(core != null, "the module starts on Linux, where the JVM's captured call state is errno alone");
        logRotatesAndTakesLines(core, scratch);
        theWindowIsRefusedBeforeTheModule(core);
        Path file = configIsCreatedSavedAndReadAgain(core, scratch);
        hotkeysComeFromTheKeySource(core);
        aTrackerOnThePortReachesTheFrame(core);
        aReadOnlyFileIsNotSavedOver(core, file);
        defaultsIniIsFoundAsTheCSharpCoreFindsIt(scratch);
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

    private static String log(Path file) throws IOException {
        return Files.readString(file, StandardCharsets.UTF_8);
    }

    /** What the log has said since the last call. */
    private static String said() throws IOException {
        String all = log(modLog);
        String since = all.substring(read);
        read = all.length();
        return since;
    }

    private static void logRotatesAndTakesLines(CameraUnlock core, Path scratch) throws IOException {
        Path log = scratch.resolve("Mod.log");
        modLog = log;
        Files.writeString(log, "the session before\r\n");
        core.logOpen(log);
        core.logWrite("the host's own line, with a letter past ASCII: é");
        check(log(log).contains("the host's own line, with a letter past ASCII: é\r\n"), "a line reaches the log as UTF-8");
        check(Files.readString(scratch.resolve("Mod.prev.log")).equals("the session before\r\n"),
                "and the session before is kept as Mod.prev.log");
    }

    private static void theWindowIsRefusedBeforeTheModule(CameraUnlock core) {
        String refused = "";
        try {
            core.windowCenter(1);
        } catch (UnsupportedOperationException expected) {
            refused = expected.getMessage();
        }
        check(refused.contains("on Windows only"), "centring the window is refused in words: " + refused);
        check(core.aimModeLabel(CameraUnlock.AIM_STOCK_SIGHTS) != null, "and the module is still called after it");
    }

    private static Path configIsCreatedSavedAndReadAgain(CameraUnlock core, Path scratch) throws IOException {
        Path file = scratch.resolve("game").resolve("CameraUnlock.ini");
        Path defaults = scratch.resolve("user").resolve("CameraUnlock").resolve("Defaults.ini");
        Files.createDirectories(file.getParent());
        Files.createDirectories(defaults.getParent().getParent());
        core.configDescribe("Linux Test");
        for (String name : new String[] {"UdpPort", "EnableOnStartup", "LocalSmoothing", "RemoteSmoothing"}) {
            core.configConcept(name, 0, null, null);
        }
        int trees = core.configLocalInt("Content", "Trees", "How many trees.", CameraUnlock.ROW_WRITABLE | CameraUnlock.ROW_LIVE, 12, 0, 100);
        int key = core.configLocalHotkey("GraphicsKey", "Goes to the next graphics mode.", CameraUnlock.ROW_WRITABLE | CameraUnlock.ROW_LIVE,
                "F9", CameraUnlock.HOTKEY_LOCAL);
        check(trees == 0 && key == 1, "local rows are numbered as they are added");

        CameraUnlock.Config config = core.configLoad(file, defaults);
        check(config.status == CameraUnlock.LOAD_CREATED && log(file).contains("\r\nTrees=12\r\n"), "a first load creates the file");
        check(Files.isRegularFile(defaults), "and Defaults.ini, in a CameraUnlock folder it creates, where the load was told it is");
        check(!core.configChanged(), "the write times the module recorded are the ones the binding reads");
        core.configure(config.settings);

        Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-r-----"));
        check(core.configSaveInt(trees, 30) == CameraUnlock.SAVE_SAVED && log(file).contains("\r\nTrees=30\r\n"), "a writable row saves");
        check(PosixFilePermissions.toString(Files.getPosixFilePermissions(file)).equals("rw-r-----"),
                "and the file keeps the permissions it had, as Windows keeps a replaced file's attributes");
        try (var left = Files.list(file.getParent())) {
            check(left.count() == 1, "with no temporary file left beside it");
        }
        check(!core.configChanged(), "a save of the mod's own is not a change to read back");

        Files.writeString(file, log(file).replace("\r\nTrees=30\r\n", "\r\nTrees=7\r\n"), StandardCharsets.US_ASCII);
        Files.setLastModifiedTime(file, FileTime.fromMillis(System.currentTimeMillis() + 10_000));
        check(core.configChanged() && core.configReload() == CameraUnlock.RELOAD_APPLIED && core.configGetInt(trees) == 7,
                "a change another program makes is seen by its write time and read again");
        return file;
    }

    private static void aReadOnlyFileIsNotSavedOver(CameraUnlock core, Path file) throws IOException {
        Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("r--r--r--"));
        String before = log(file);
        said();
        int status = core.configSaveInt(0, 40);
        check(status != CameraUnlock.SAVE_SAVED && log(file).equals(before), "a file the player made read-only is not saved over: " + status);
        check(said().contains("the file is read-only"), "and the log says why, as it does on Windows");
        Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-r--r--"));
    }

    private static void hotkeysComeFromTheKeySource(CameraUnlock core) throws InterruptedException {
        String refused = "";
        try {
            core.hotkeysStart();
        } catch (IllegalStateException expected) {
            refused = expected.getMessage();
        }
        check(refused.contains("no key source"), "the hotkeys are refused before a key source is given, not stopped in the tick: " + refused);
        AtomicInteger held = new AtomicInteger();
        AtomicInteger asked = new AtomicInteger();
        core.keySource(virtualKey -> {
            asked.incrementAndGet();
            return virtualKey == held.get();
        });
        core.hotkeysStart();
        check(askedPast(asked, 0), "the tick asks the key source");
        held.set(0x78);
        check(askedPast(asked, asked.get() + 2), "while F9 is held");
        held.set(0);
        check(askedPast(asked, asked.get() + 2), "and once it is let go");
        int taken = core.hotkeysTake();
        check((taken & CameraUnlock.HOTKEY_LOCAL) != 0, "F9 held and let go fires its row: " + Integer.toHexString(taken));
        check(core.hotkeysTake() == 0, "once");
    }

    /** Whether the tick asks the key source more than `past` times within two seconds: it runs on a thread of its own. */
    private static boolean askedPast(AtomicInteger asked, int past) throws InterruptedException {
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(2);
        while (asked.get() <= past && System.nanoTime() < end) {
            Thread.sleep(5);
        }
        return asked.get() > past;
    }

    /** x, y and z in the wire's centimetres, the angles in degrees. */
    private static byte[] datagram(double x, double y, double z, double yaw, double pitch, double roll) {
        ByteBuffer wire = ByteBuffer.allocate(48).order(ByteOrder.LITTLE_ENDIAN);
        for (double value : new double[] {x, y, z, yaw, pitch, roll}) {
            wire.putDouble(value);
        }
        return wire.array();
    }

    private static int freePort() throws IOException {
        try (DatagramSocket probe = new DatagramSocket(0, InetAddress.getLoopbackAddress())) {
            return probe.getLocalPort();
        }
    }

    /** Frames until `until` holds or two seconds pass: what the receiver does runs on threads of its own. */
    private static CameraUnlock.Frame frameUntil(CameraUnlock core, CameraUnlock.FrameInput input, Runnable each,
            Predicate<CameraUnlock.Frame> until) throws InterruptedException {
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

    private static void aTrackerOnThePortReachesTheFrame(CameraUnlock core) throws IOException, InterruptedException {
        said();
        CameraUnlock.FrameInput input = new CameraUnlock.FrameInput();
        input.flags = CameraUnlock.FRAME_ACTIVE;
        input.deltaSeconds = 1f / 60f;
        input.tanHalfFov = 0.5f;
        input.tanHalfFovBase = 0.5f;
        input.forwardStop = Float.POSITIVE_INFINITY;
        input.aimForward[2] = 1f;
        input.trackerToWorld[2] = 1f;
        input.trackerToWorld[3] = -1f;
        input.trackerToWorld[7] = 1f;
        int port = freePort();
        core.start(port);
        try (DatagramSocket tracker = new DatagramSocket(0, InetAddress.getLoopbackAddress())) {
            byte[] pose = datagram(0, 0, 0, 25, -5, 3);
            byte[] tooLong = new byte[100];
            List<byte[]> sends = new ArrayList<>(List.of(tooLong));
            Runnable send = () -> {
                try {
                    byte[] next = sends.isEmpty() ? pose : sends.remove(0);
                    tracker.send(new DatagramPacket(next, next.length, InetAddress.getLoopbackAddress(), port));
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
            String said = said();
            check(said.contains("recvfrom failed with WSA error 10040"), "a datagram longer than a pose is refused as Windows refuses it: " + said);
            check(said.contains("First UDP packet received: 48 bytes from 127.0.0.1"), "and the first pose is in the log");
        }
        core.stop();
        said();
        core.start(port);
        String again = said();
        check(again.contains("Listening for OpenTrack datagrams on UDP port " + port) && !again.contains("Failed to bind"),
                "a start straight after a stop binds the same port again: " + again);
        core.stop();

        int held = freePort();
        try (DatagramSocket other = new DatagramSocket(held, InetAddress.getLoopbackAddress())) {
            check(other.getLocalPort() == held, "another socket holds the port");
            core.start(held);
            String said = said();
            check(said.contains("Failed to bind UDP port " + held + ": bind failed with error 10048"),
                    "a port another socket holds is not listened on, and the log says why: " + said);
        }
        CameraUnlock.Frame bound = frameUntil(core, input, () -> {}, f -> (f.flags & CameraUnlock.STATE_LISTENING) != 0);
        check((bound.flags & CameraUnlock.STATE_LISTENING) != 0 && said().contains("Bound UDP port " + held + " after"),
                "once the port is let go the receiver binds it, and says so");
        core.stop();
        try (DatagramSocket after = new DatagramSocket(held, InetAddress.getLoopbackAddress())) {
            check(after.getLocalPort() == held, "a stop lets the port go");
        }
    }

    /** A JVM of its own for each home, since Defaults.ini is looked for once, at the load. */
    private static void defaultsIniIsFoundAsTheCSharpCoreFindsIt(Path scratch) throws IOException, InterruptedException {
        Path empty = Files.createDirectories(scratch.resolve("home-empty"));
        String none = runChild(scratch.resolve("child-none"), empty, null);
        check(none.contains("Defaults.ini: no file at ~/.config/CameraUnlock/Defaults.ini or ~/Library/Application Support/CameraUnlock/Defaults.ini;"
                        + " on this system the mod reads Defaults.ini but does not create it."),
                "with no Defaults.ini the log names where it is looked for: " + none);
        check(!Files.exists(empty.resolve(".config")), "and nothing is created in the home");

        Path home = scratch.resolve("home-with");
        Path defaults = Files.createDirectories(home.resolve(".config").resolve("CameraUnlock")).resolve("Defaults.ini");
        Files.writeString(defaults, "[CameraUnlock]\r\nConfigFormat=1\r\n[Smoothing]\r\nRemoteSmoothing=0.4\r\n");
        String read = runChild(scratch.resolve("child-read"), home, null);
        check(read.contains("remote=0.4") && read.contains("Defaults.ini: ~/.config/CameraUnlock/Defaults.ini (read)"),
                "~/.config/CameraUnlock/Defaults.ini is read: " + read);

        Path xdg = scratch.resolve("xdg");
        Files.createDirectories(xdg.resolve("CameraUnlock"));
        Files.writeString(xdg.resolve("CameraUnlock").resolve("Defaults.ini"), "[CameraUnlock]\r\nConfigFormat=1\r\n[Smoothing]\r\nRemoteSmoothing=0.6\r\n");
        String fromXdg = runChild(scratch.resolve("child-xdg"), empty, xdg);
        check(fromXdg.contains("remote=0.6"), "and $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini before it: " + fromXdg);
    }

    private static String runChild(Path folder, Path home, Path xdg) throws IOException, InterruptedException {
        Files.createDirectories(folder);
        ProcessBuilder builder = new ProcessBuilder(Path.of(System.getProperty("java.home"), "bin", "java").toString(),
                "--enable-native-access=ALL-UNNAMED", "-cp", System.getProperty("java.class.path"), LinuxTests.class.getName(), CHILD,
                folder.toString()).redirectErrorStream(true);
        builder.environment().put("HOME", home.toString());
        if (xdg == null) {
            builder.environment().remove("XDG_CONFIG_HOME");
        } else {
            builder.environment().put("XDG_CONFIG_HOME", xdg.toString());
        }
        Process process = builder.start();
        String output = new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        int exit = process.waitFor();
        check(exit == 0, "the child ran: " + output);
        Path childLog = folder.resolve("Mod.log");
        return output + (Files.exists(childLog) ? log(childLog) : "");
    }

    private static void child(Path folder) throws IOException {
        CameraUnlock core = CameraUnlock.loadBundled();
        core.logOpen(folder.resolve("Mod.log"));
        core.configDescribe("Linux Test");
        core.configConcept("RemoteSmoothing", 0, null, null);
        CameraUnlock.Config config = core.configLoad(folder.resolve("CameraUnlock.ini"), null);
        System.out.println("remote=" + config.settings.remoteSmoothing);
    }
}
