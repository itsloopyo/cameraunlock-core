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

    final CameraUnlock core = CameraUnlock.load(CameraUnlock.beside(Testing.class).resolve(LIBRARY));
    private final MethodHandle reset = core.bind("cameraunlock_testing_reset", FunctionDescriptor.of(ValueLayout.JAVA_INT));
    private final MethodHandle deliver = core.bind("cameraunlock_testing_deliver",
            FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));
    private final MethodHandle deliverView = core.bind("cameraunlock_testing_deliver_view", FunctionDescriptor.of(
            ValueLayout.JAVA_INT, ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.JAVA_INT));

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

    private static void must(int status) {
        if (status == -1) {
            throw new IllegalStateException("a cameraunlock_testing call failed, and the log has the reason");
        }
    }
}
