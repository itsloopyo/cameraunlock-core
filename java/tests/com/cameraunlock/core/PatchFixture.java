// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
package com.cameraunlock.core;

import java.util.ArrayList;
import java.util.List;

/** Stands in for a game's class in the bytecode patcher's tests. */
public class PatchFixture {
    public final List<String> trace = new ArrayList<>();

    public void update(int frames) {
        trace.add("before");
        align(frames);
        if (frames > 1) {
            trace.add("many");
            return;
        }
        trace.add("after");
    }

    private void align(float frames) {
        trace.add("align " + frames);
    }

    public int count() {
        return trace.size();
    }

    public void draw(Object graphics) {
        trace.add("draw " + graphics);
        if (graphics == null) {
            return;
        }
        trace.add("drawn");
    }
}
