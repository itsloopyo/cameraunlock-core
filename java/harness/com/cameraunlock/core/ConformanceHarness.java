package com.cameraunlock.core;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.HexFormat;
import java.util.Map;

/**
 * Conformance harness for the Java binding to the C interface (docs/c-interface.md).
 *
 * Speaks the line protocol scripts/pipeline-vectors/run-vectors.mjs drives, and reaches the
 * pipeline only through CameraUnlock, as a Java mod does: a datagram in, one frame, the pose read
 * out of CameraUnlock.Frame. What the vectors hold here is the binding: the layouts of
 * CameraUnlockSettings, CameraUnlockFrameInput and CameraUnlockFrame as Java declares them, the
 * units, and the order of calls.
 *
 * It runs the vectors the C harness (harness/c/main.cpp) runs and skips the ones it skips, for the
 * same reasons: the interface has one entry to the pipeline, the frame. pixi.toml's vectors-java
 * names each skipped vector.
 */
public final class ConformanceHarness {
    private final Testing testing = new Testing();
    private final CameraUnlock core = testing.core;
    private final PrintStream out = new PrintStream(System.out, false, StandardCharsets.US_ASCII);
    private final CameraUnlock.FrameInput input = new CameraUnlock.FrameInput();
    private final CameraUnlock.Frame frame = new CameraUnlock.Frame();
    private final Map<String, Double> config = new HashMap<>();

    private String unit = "";
    private String vector = "";
    private boolean skipping;
    private boolean remote;
    private double nowMs;

    public static void main(String[] arguments) throws IOException {
        new ConformanceHarness().run();
    }

    private void run() throws IOException {
        BufferedReader in = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.US_ASCII));
        for (String line = in.readLine(); line != null; line = in.readLine()) {
            if (line.isEmpty()) {
                continue;
            }
            String[] words = line.trim().split(" +");
            switch (words[0]) {
                case "unit" -> {
                    unit = words[1];
                    vector = words[2];
                    config.clear();
                    skipping = false;
                }
                case "cfg" -> config.put(words[1], Double.parseDouble(words[2]));
                case "begin" -> begin();
                case "q" -> {
                    if (!skipping) {
                        packet(words[1]);
                    }
                }
                case "p" -> {
                    if (!skipping) {
                        testing.deliver(HexFormat.of().parseHex(words[1]), remote);
                        step(Float.parseFloat(words[2]));
                    }
                }
                case "f" -> {
                    if (!skipping) {
                        step(Float.parseFloat(words[1]));
                    }
                }
                case "s", "e" -> {
                    if (!skipping) {
                        throw new IOException("a step for a unit this harness skips");
                    }
                }
                case "end" -> { }
                case "bye" -> {
                    out.flush();
                    return;
                }
                default -> throw new IOException("unknown command: " + words[0]);
            }
        }
        out.flush();
    }

    private void skip(String reason) {
        out.println("skip " + reason);
        out.flush();
        skipping = true;
    }

    /** The session as a new process has it, with the vector's settings over core's defaults. */
    private boolean configure() {
        testing.reset();
        CameraUnlock.Settings settings = core.settingsDefaults();
        remote = false;
        for (Map.Entry<String, Double> entry : config.entrySet()) {
            float value = entry.getValue().floatValue();
            switch (entry.getKey()) {
                case "local_smoothing" -> settings.localSmoothing = value;
                case "remote_smoothing" -> settings.remoteSmoothing = value;
                case "is_remote" -> remote = value != 0f;
                case "limit_x" -> settings.limitX = value;
                case "limit_y" -> settings.limitY = value;
                case "limit_y_down" -> settings.limitYDown = value;
                case "limit_z" -> settings.limitZ = value;
                case "limit_z_back" -> settings.limitZBack = value;
                default -> {
                    skip("the C interface has no setting for cfg key " + entry.getKey());
                    return false;
                }
            }
        }
        core.configure(settings);
        nowMs = 0.0;
        return true;
    }

    private void begin() {
        if (unit.equals("packet")) {
            if (vector.equals("wire-position-is-centimetres") || vector.equals("hcam-trailer-parses")
                    || vector.equals("packet-accepts-a-longer-datagram-without-inventing-a-trailer")
                    || vector.equals("hcam-trailer-version-is-forward-compatible")) {
                skip("the C interface does not say whether a datagram carried a trailer, which this vector asserts");
                return;
            }
        } else if (vector.equals("hcam-trailer-does-not-recenter") || vector.equals("no-center-captured-on-connect-rotation")) {
            skip("the receiver holds a jump to a pose that then repeats bit for bit, and this vector's stream is one");
            return;
        } else if (!unit.equals("session_rot") && !unit.equals("session_pos")) {
            skip("the C interface runs the whole pipeline in one call and does not expose unit " + unit);
            return;
        }
        if (configure()) {
            out.println("ok");
            out.flush();
        }
    }

    private void oneFrame(float delta) {
        input.flags = CameraUnlock.FRAME_ACTIVE;
        nowMs += delta * 1000.0;
        input.nowMs = (long) nowMs;
        input.deltaSeconds = delta;
        input.tanHalfFov = 1f;
        input.tanHalfFovBase = 1f;
        input.forwardStop = Float.POSITIVE_INFINITY;
        core.frame(input, frame);
    }

    /**
     * Each datagram meets a session that has seen nothing. The interface has one verdict on a
     * datagram, whether it gave a pose, so ok_rotation and ok_position repeat it.
     */
    private void packet(String hex) {
        configure();
        testing.deliver(HexFormat.of().parseHex(hex), false);
        oneFrame(0f);
        int ok = (frame.flags & CameraUnlock.STATE_POSE) != 0 ? 1 : 0;
        emit(ok, frame.headYaw, frame.headPitch, frame.headRoll, frame.headX, frame.headY, frame.headZ, 0, 0, ok, ok);
    }

    private void step(float delta) {
        oneFrame(delta);
        if (unit.equals("session_rot")) {
            emit(frame.headYaw, frame.headPitch, frame.headRoll);
        } else {
            emit(frame.headX, frame.headY, frame.headZ);
        }
    }

    private void emit(double... values) {
        StringBuilder line = new StringBuilder();
        for (double value : values) {
            if (!line.isEmpty()) {
                line.append(' ');
            }
            line.append(value);
        }
        out.println(line);
        out.flush();
    }
}
