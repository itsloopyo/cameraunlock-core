package com.cameraunlock.core;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemoryLayout.PathElement;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.StructLayout;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.VarHandle;
import java.nio.charset.StandardCharsets;
import java.util.OptionalLong;

/**
 * The look configReload takes at the two files before it calls the library, so a poll that finds
 * nothing changed answers RELOAD_UNCHANGED without the library. Under loadBundled every call into
 * the module holds one lock, and a write time Windows is slow to give (a virus scanner, a busy
 * disk) held every other thread's call, the frame's included, for as long as it took.
 *
 * The decision is cameraunlock_config_reload's own: the file has changed when its write time, or
 * Defaults.ini's, differs from the one the library recorded (cameraunlock_config_watched). Only an
 * answer the library would read the same way is used here; on any other the library is called.
 */
final class ConfigWatch {
    /** A file the library watches, and the write time it recorded for it. An empty path is a Defaults.ini the load did not find. */
    record Watched(String path, long recorded) {}

    /** The last write time of a file as the library would read it, or empty when that is not certain. */
    interface Times {
        OptionalLong of(String path);
    }

    private static final int ERROR_FILE_NOT_FOUND = 2, ERROR_PATH_NOT_FOUND = 3, ERROR_NOT_READY = 21;
    /** ftLastWriteTime in WIN32_FILE_ATTRIBUTE_DATA, and the struct's size. */
    private static final long LAST_WRITE_AT = 20, ATTRIBUTE_DATA = 36;
    private static final StructLayout CAPTURE = Linker.Option.captureStateLayout();
    private static final VarHandle LAST_ERROR = CAPTURE.varHandle(PathElement.groupElement("GetLastError"));
    private static final MethodHandle GET_FILE_ATTRIBUTES_EX_W = getFileAttributesExW();

    @SuppressWarnings("restricted")
    private static MethodHandle getFileAttributesExW() {
        SymbolLookup kernel32 = SymbolLookup.libraryLookup("kernel32", Arena.global());
        return Linker.nativeLinker().downcallHandle(kernel32.find("GetFileAttributesExW").orElseThrow(),
                FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS),
                Linker.Option.captureCallState("GetLastError"));
    }

    private ConfigWatch() {}

    /** True when cameraunlock_config_reload would find nothing changed. */
    static boolean unchanged(Watched file, Watched defaults, Times times) {
        return same(file, times) && same(defaults, times);
    }

    private static boolean same(Watched watched, Times times) {
        // The library reads a Defaults.ini it did not find as time 0.
        OptionalLong now = watched.path().isEmpty() ? OptionalLong.of(0) : times.of(watched.path());
        return now.isPresent() && now.getAsLong() == watched.recorded();
    }

    /**
     * GetFileAttributesExW's last write time as a FILETIME count, 0 for a missing file as the
     * library counts one. Empty for any other failure, which the library may read another way
     * (it then asks FindFirstFileW), and for a path with an unpaired surrogate, which the DLL and
     * the module hand Windows differently.
     */
    static OptionalLong lastWriteTime(String path) {
        if (path.codePoints().anyMatch(ConfigWatch::lone)) {
            return OptionalLong.empty();
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment data = arena.allocate(ATTRIBUTE_DATA);
            MemorySegment capture = arena.allocate(CAPTURE);
            int found = (int) GET_FILE_ATTRIBUTES_EX_W.invokeExact(capture, arena.allocateFrom(path, StandardCharsets.UTF_16LE), 0, data);
            if (found != 0) {
                return OptionalLong.of(data.get(ValueLayout.JAVA_LONG_UNALIGNED, LAST_WRITE_AT));
            }
            int error = (int) LAST_ERROR.get(capture, 0L);
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_NOT_READY
                    ? OptionalLong.of(0) : OptionalLong.empty();
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
    }

    /** codePoints() yields an unpaired surrogate as itself. */
    private static boolean lone(int point) {
        return point >= Character.MIN_SURROGATE && point <= Character.MAX_SURROGATE;
    }
}
