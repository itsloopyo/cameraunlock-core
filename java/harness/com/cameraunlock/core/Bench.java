package com.cameraunlock.core;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

/**
 * What the binding costs, on the DLL or with -Dcameraunlock.bundled=true on the module:
 * `pixi run bench-java` runs both, and docs/core-jar.md quotes them.
 *
 * - load: CameraUnlock.load or loadBundled, first in a fresh JVM.
 * - frame: one session frame on the testing build, a datagram delivered every fourth.
 * - save: one configSaveWorldSpaceYaw, and the longest a frame on another thread waited while
 *   saves ran, beside the same while the main thread only waited.
 */
final class Bench {
    private static final int WARM = 20_000, FRAMES = 100_000, SAVES = 20;

    private Bench() {}

    public static void main(String[] arguments) throws Exception {
        long loading = System.nanoTime();
        CameraUnlock shipped = Testing.BUNDLED ? CameraUnlock.loadBundled() : CameraUnlock.load();
        double loadMs = (System.nanoTime() - loading) / 1e6;
        shipped.settingsDefaults();

        Testing testing = new Testing();
        CameraUnlock core = testing.core;
        testing.reset();
        core.configure(core.settingsDefaults());
        CameraUnlock.FrameInput input = new CameraUnlock.FrameInput();
        input.flags = CameraUnlock.FRAME_ACTIVE;
        input.deltaSeconds = 1f / 60f;
        input.tanHalfFov = 0.5f;
        input.tanHalfFovBase = 0.5f;
        input.forwardStop = Float.POSITIVE_INFINITY;
        input.aimForward[2] = 1f;
        CameraUnlock.Frame frame = new CameraUnlock.Frame();
        long[] each = new long[FRAMES];
        for (int i = 0; i < WARM + FRAMES; i++) {
            if (i % 4 == 0) testing.deliver(datagram(i % 7, 0, 0, i % 30, 0, 0), false);
            input.nowMs += 16;
            long before = System.nanoTime();
            core.frame(input, frame);
            if (i >= WARM) each[i - WARM] = System.nanoTime() - before;
        }
        Arrays.sort(each);
        double mean = Arrays.stream(each).average().orElseThrow() / 1e3;
        double p99 = each[FRAMES * 99 / 100] / 1e3;

        Path scratch = Files.createTempDirectory("cameraunlock-bench");
        core.configDescribe("Bench");
        core.configConcept("WorldSpaceYaw", CameraUnlock.ROW_WRITABLE, null, null);
        core.configLoad(scratch.resolve("CameraUnlock.ini"), scratch.resolve("Defaults.ini"));
        long[] saves = new long[SAVES];
        // Frames with no datagram on another thread, while the main thread only waits and while it saves.
        long idle = longestFrame(core, () -> {
            for (int i = 0; i < SAVES; i++) Thread.sleep(27);
        });
        long saving = longestFrame(core, () -> {
            for (int i = 0; i < SAVES; i++) {
                long before = System.nanoTime();
                core.configSaveWorldSpaceYaw(i % 2 == 0);
                saves[i] = System.nanoTime() - before;
                Thread.sleep(20);
            }
        });
        Arrays.sort(saves);

        System.out.printf("%s: load %.0f ms, frame %.2f us mean and %.1f us at the 99th percentile, a save %.1f ms (median)"
                        + " and %.1f ms at most, the longest frame on another thread %.1f ms while saves ran and %.1f ms"
                        + " while none did%n",
                Testing.BUNDLED ? "module" : "DLL", loadMs, mean, p99, saves[SAVES / 2] / 1e6, saves[SAVES - 1] / 1e6, saving / 1e6,
                idle / 1e6);
    }

    private interface Work {
        void run() throws InterruptedException;
    }

    /** The longest single frame a thread of its own waited for while `work` ran on this one. */
    private static long longestFrame(CameraUnlock core, Work work) throws InterruptedException {
        long[] longest = new long[1];
        boolean[] running = {true};
        Throwable[] failed = new Throwable[1];
        Thread frames = new Thread(() -> {
            try {
                framesUntilStopped(core, longest, running);
            } catch (Throwable failure) {
                failed[0] = failure;
            }
        });
        frames.start();
        try {
            work.run();
        } finally {
            synchronized (running) {
                running[0] = false;
            }
            frames.join();
        }
        if (failed[0] != null) throw new IllegalStateException("the frame thread failed", failed[0]);
        return longest[0];
    }

    /** Frames until `running` says stop, the longest one kept in `longest`. */
    private static void framesUntilStopped(CameraUnlock core, long[] longest, boolean[] running) {
        CameraUnlock.FrameInput input = new CameraUnlock.FrameInput();
        input.flags = CameraUnlock.FRAME_ACTIVE;
        input.deltaSeconds = 1f / 60f;
        input.tanHalfFov = 0.5f;
        input.tanHalfFovBase = 0.5f;
        input.forwardStop = Float.POSITIVE_INFINITY;
        input.aimForward[2] = 1f;
        CameraUnlock.Frame out = new CameraUnlock.Frame();
        while (true) {
            synchronized (running) {
                if (!running[0]) return;
            }
            input.nowMs += 16;
            long before = System.nanoTime();
            core.frame(input, out);
            longest[0] = Math.max(longest[0], System.nanoTime() - before);
        }
    }

    private static byte[] datagram(double x, double y, double z, double yaw, double pitch, double roll) {
        ByteBuffer wire = ByteBuffer.allocate(48).order(ByteOrder.LITTLE_ENDIAN);
        for (double value : new double[] {x, y, z, yaw, pitch, roll}) {
            wire.putDouble(value);
        }
        return wire.array();
    }
}
