// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
package com.cameraunlock.core;

/** A mod's bridge, as the bytecode patcher's tests have one: the class patched game code calls. */
public final class PatchBridge {
    public interface Hooks {
        boolean call(int site, Object a, Object b);
    }

    public static volatile Hooks hooks;

    private PatchBridge() {}

    public static boolean call(int site, Object a, Object b) {
        Hooks h = hooks;
        return h != null && h.call(site, a, b);
    }
}
