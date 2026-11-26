package com.cameraunlock.core;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;

/**
 * CameraUnlockCoreTesting.dll (cameraunlock/c/testing/cameraunlock_testing.h), loaded from beside
 * this class's folder: the library a mod ships with three more functions, which hand a session or
 * one of its views a datagram without a socket. For the vectors harness and this tree's tests. A mod has no use for it.
 */
final class Testing {
    static final String LIBRARY = "CameraUnlockCoreTesting.dll";

    /** -Dcameraunlock.bundled=true runs the tests and the vectors on the testing build compiled to WebAssembly. */
    static final boolean BUNDLED = Boolean.getBoolean("cameraunlock.bundled");

    final CameraUnlock core = BUNDLED
            ? CameraUnlock.bundled(com.cameraunlock.core.wasm.CoreTestingModule::load, com.cameraunlock.core.wasm.CoreTestingModule::create,
                    com.cameraunlock.core.wasm.CoreTestingModule.class)
            : CameraUnlock.load(CameraUnlock.beside(Testing.class).resolve(LIBRARY));
    private final MethodHandle reset = core.bind("cameraunlock_testing_reset", FunctionDescriptor.of(ValueLayout.JAVA_INT));
    private final MethodHandle deliver = core.bind("cameraunlock_testing_deliver",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private final MethodHandle deliverView = core.bind("cameraunlock_testing_deliver_view", FunctionDescriptor.of(
            ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private final MethodHandle defaultsPath = core.bind("cameraunlock_testing_defaults_path",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));
    private final MethodHandle win32 = core.bind("cameraunlock_testing_win32", FunctionDescriptor.of(ValueLayout.JAVA_INT,
            ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT));

    /** A session as a new process has it, in every view. */
    void reset() {
        try {
            must((int) reset.invokeExact());
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
    }

    /** One datagram as if it had just arrived, from loopback or from another machine. */
    void deliver(byte[] bytes, boolean fromAnotherMachine) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment datagram = arena.allocateFrom(ValueLayout.JAVA_BYTE, bytes);
            must((int) deliver.invokeExact(datagram, bytes.length, fromAnotherMachine ? 1 : 0));
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
    }

    /** The same to one view. */
    void deliver(int view, byte[] bytes, boolean fromAnotherMachine) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment datagram = arena.allocateFrom(ValueLayout.JAVA_BYTE, bytes);
            must((int) deliverView.invokeExact(view, datagram, bytes.length, fromAnotherMachine ? 1 : 0));
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
    }

    /** Where the config owner looks for the player's Defaults.ini, or why it looks nowhere. */
    String defaultsPath() {
        try (Arena arena = Arena.ofConfined()) {
            int length = (int) defaultsPath.invokeExact(MemorySegment.NULL, 0);
            must(length);
            MemorySegment text = arena.allocate(length + 1L);
            must((int) defaultsPath.invokeExact(text, length + 1));
            return text.getString(0);
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
    }

    /** One of core's Windows calls, made as core makes it: cameraunlock_testing_win32, made once. */
    String win32(String call, String argument) {
        try (Arena arena = Arena.ofConfined()) {
            int most = 4096;
            MemorySegment text = arena.allocate(most);
            int length = (int) win32.invokeExact(arena.allocateFrom(call), arena.allocateFrom(argument), text, most);
            must(length);
            if (length >= most) throw new IllegalStateException(call + " answered " + length + " bytes, more than " + most);
            return text.getString(0);
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
    }

    private static void must(int status) {
        if (status == -1) {
            throw new IllegalStateException("a cameraunlock_testing call failed, and the log has the reason");
        }
    }
}
