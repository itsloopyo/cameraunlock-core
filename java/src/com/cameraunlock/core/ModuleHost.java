package com.cameraunlock.core;

import java.util.function.IntPredicate;
import run.endive.runtime.HostFunction;

/**
 * What answers the module's imports of "cameraunlock_win32": Win32Host on Windows, PosixHost on
 * Linux (docs/core-jar.md). The defaults are Windows', where core reads the keys and the files
 * itself.
 */
interface ModuleHost {
    HostFunction[] functions();

    /** The write times configChanged compares, read as the module's GetFileAttributesExW reads them. */
    default ConfigWatch.Times writeTimes() {
        return ConfigWatch::lastWriteTime;
    }

    /** Where the hotkeys' key state comes from, on a system where core cannot read it itself. */
    default void keySource(IntPredicate virtualKeyDown) {
        throw new UnsupportedOperationException("core reads the keys itself on Windows: there is no key source to give it");
    }

    /** Throws where the hotkeys would ask for keys nothing can answer, before the module's tick would. */
    default void requireKeySource() {}

    /** Throws where the game's window cannot be reached, before a call into the module would. */
    default void requireWindowCalls() {}
}
