package com.cameraunlock.core;

import java.lang.foreign.AddressLayout;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.MemoryLayout;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import run.endive.runtime.ByteArrayMemory;
import run.endive.runtime.ExportFunction;
import run.endive.runtime.Instance;
import run.endive.runtime.Machine;
import run.endive.runtime.Memory;
import run.endive.runtime.Store;
import run.endive.wasi.WasiOptions;
import run.endive.wasi.WasiPreview1;
import run.endive.wasm.WasmModule;
import run.endive.wasm.types.Value;

/**
 * The C interface as a WebAssembly module run by Endive, behind CameraUnlock's handles
 * (docs/core-jar.md).
 *
 * A module has one memory and runs on one thread at a time, so every call into it, from the
 * binding, from the receive threads and from the tick, holds this object's lock. A handle copies
 * each MemorySegment argument into the module's memory before the call and back out after it, so
 * the binding's segments work here as they do with the DLL. The module has no threads of its own:
 * a thread here calls cameraunlock_host_tick every 16 ms, which polls the hotkeys and steps the
 * receivers' supervisors, and Win32Host receives on the sockets.
 *
 * With no threads in the module its one thread_local is one string for the whole JVM, so the reason
 * of a failed call is read in the same call and kept for the thread that made it, and
 * cameraunlock_last_error answers from there.
 *
 * Anything that leaves the module other than through its own return (a trap, or an exception from
 * an import) has skipped the C++ code that would release its mutexes, so the module is not called
 * again: every later call, and the tick and the receive threads, throw with the first cause.
 */
final class WasmCore implements CameraUnlock.Backend {
    /** The hotkey poller's interval in CameraUnlockCore.dll, HotkeyPoller::Start's default. */
    static final long TICK_MS = 16;

    private static final String LAST_ERROR = "cameraunlock_last_error";
    private static final MethodHandle CALL, LAST_ERROR_HERE;

    static {
        try {
            MethodHandles.Lookup lookup = MethodHandles.lookup();
            CALL = lookup.findVirtual(WasmCore.class, "call",
                    MethodType.methodType(Object.class, String.class, ExportFunction.class, FunctionDescriptor.class, Object[].class));
            LAST_ERROR_HERE = lookup.findVirtual(WasmCore.class, "lastErrorHere",
                    MethodType.methodType(int.class, MemorySegment.class, int.class));
        } catch (ReflectiveOperationException failure) {
            throw new ExceptionInInitializerError(failure);
        }
    }

    private final Instance instance;
    private final Win32Host host;
    private final Memory memory;
    // Looked up once the binding has checked the module's ABI: a module of another one need not have them.
    private ExportFunction malloc, free, lastError, tick, received, receiveFailed;
    /** Text the module answers with a pointer to, copied out once per pointer: it is the module's own static text. */
    private final Map<Long, MemorySegment> texts = new HashMap<>();
    /** The reason of the calling thread's last failed call, in UTF-8. */
    private final ThreadLocal<byte[]> reason = ThreadLocal.withInitial(() -> new byte[0]);
    /** Why the module is no longer called, or null. */
    private volatile Throwable dead;

    WasmCore(WasmModule module, Function<Instance, Machine> machine) {
        host = new Win32Host(this);
        WasiPreview1 wasi = WasiPreview1.builder()
                .withOptions(WasiOptions.builder().withStdout(System.out).withStderr(System.err).build()).build();
        Store store = new Store().addFunction(wasi.toHostFunctions()).addFunction(host.functions());
        instance = store.instantiate("cameraunlock", imports -> Instance.builder(module).withImportValues(imports)
                .withMachineFactory(machine).withMemoryFactory(ByteArrayMemory::new).build());
        memory = instance.memory();
        // A reactor module's constructors run in its _initialize, which the instance does not call.
        synchronized (this) {
            enter(() -> instance.export("_initialize").apply());
        }
    }

    @Override
    public synchronized void started() {
        malloc = instance.export("malloc");
        free = instance.export("free");
        tick = instance.export("cameraunlock_host_tick");
        received = instance.export("cameraunlock_host_received");
        receiveFailed = instance.export("cameraunlock_host_receive_failed");
        Thread ticker = new Thread(this::tickForever, "cameraunlock-core tick");
        ticker.setDaemon(true);
        ticker.start();
    }

    @Override
    public MethodHandle bind(String name, FunctionDescriptor descriptor) {
        ExportFunction function = instance.export(name);
        MethodHandle handle = name.equals(LAST_ERROR)
                ? LAST_ERROR_HERE.bindTo(this)
                : MethodHandles.insertArguments(CALL, 0, this, name, function, descriptor)
                        .asCollector(Object[].class, descriptor.argumentLayouts().size());
        return handle.asType(descriptor.toMethodType());
    }

    @Override
    public String kind() {
        return "module";
    }

    @Override
    public String describe(Path library) {
        return "The WebAssembly module in " + library;
    }

    @Override
    public String apart() {
        return "The module's classes and the Java code beside them come from two builds of cameraunlock-core";
    }

    /** Throws when the module is no longer called. */
    private void requireAlive() {
        Throwable cause = dead;
        if (cause != null) {
            throw new IllegalStateException("cameraunlock-core's module stopped working and is no longer called: " + cause, cause);
        }
    }

    private interface Entry {
        long[] apply();
    }

    /** A call into the module; anything that leaves it other than by its return stops it for good. Under the lock. */
    private long[] enter(Entry entry) {
        requireAlive();
        try {
            return entry.apply();
        } catch (Throwable failure) {
            dead = failure;
            throw new IllegalStateException("cameraunlock-core's module stopped working and is no longer called: " + failure, failure);
        }
    }

    /** One call of an export, its arguments as the descriptor declares them. */
    @SuppressWarnings("unused")
    private synchronized Object call(String name, ExportFunction function, FunctionDescriptor descriptor, Object[] arguments) {
        requireAlive();
        List<MemoryLayout> layouts = descriptor.argumentLayouts();
        long[] values = new long[arguments.length];
        int[] copies = new int[arguments.length];
        long[] results;
        try {
            for (int i = 0; i < arguments.length; i++) {
                MemoryLayout layout = layouts.get(i);
                if (layout instanceof AddressLayout) {
                    MemorySegment segment = (MemorySegment) arguments[i];
                    if (!segment.equals(MemorySegment.NULL)) {
                        copies[i] = copyIn(segment);
                        values[i] = copies[i];
                    }
                } else {
                    values[i] = value(layout, arguments[i]);
                }
            }
            results = enter(() -> function.apply(values));
            for (int i = 0; i < arguments.length; i++) {
                if (copies[i] != 0) {
                    MemorySegment segment = (MemorySegment) arguments[i];
                    MemorySegment.copy(memory.readBytes(copies[i], (int) segment.byteSize()), 0, segment, ValueLayout.JAVA_BYTE, 0,
                            (int) segment.byteSize());
                }
            }
        } finally {
            if (dead == null) {
                for (int copy : copies) {
                    if (copy != 0) release(copy);
                }
            }
        }
        Object result = descriptor.returnLayout().map(layout -> result(layout, results[0])).orElse(null);
        if ((result instanceof Integer status && status == CameraUnlock.ERROR) || MemorySegment.NULL.equals(result)) {
            keepReason();
        }
        return result;
    }

    /** cameraunlock_last_error's own text, read in the call that failed, for the thread that made it. Under the lock. */
    private void keepReason() {
        if (lastError == null) lastError = instance.export(LAST_ERROR);
        int length = (int) enter(() -> lastError.apply(0, 0))[0];
        int buffer = allocate(length + 1);
        enter(() -> lastError.apply(buffer, length + 1));
        reason.set(memory.readBytes(buffer, length));
        release(buffer);
    }

    /** cameraunlock_last_error, answered from the calling thread's kept reason. */
    @SuppressWarnings("unused")
    private int lastErrorHere(MemorySegment buffer, int capacity) {
        byte[] text = reason.get();
        if (!buffer.equals(MemorySegment.NULL) && capacity >= text.length + 1) {
            MemorySegment.copy(text, 0, buffer, ValueLayout.JAVA_BYTE, 0, text.length);
            buffer.set(ValueLayout.JAVA_BYTE, text.length, (byte) 0);
        }
        return text.length;
    }

    private static long value(MemoryLayout layout, Object argument) {
        if (layout instanceof ValueLayout.OfInt) return (int) argument;
        if (layout instanceof ValueLayout.OfLong) return (long) argument;
        if (layout instanceof ValueLayout.OfFloat) return Value.floatToLong((float) argument);
        if (layout instanceof ValueLayout.OfDouble) return Value.doubleToLong((double) argument);
        throw new IllegalArgumentException("no WebAssembly value for an argument laid out as " + layout);
    }

    private Object result(MemoryLayout layout, long result) {
        if (layout instanceof ValueLayout.OfInt) return (int) result;
        if (layout instanceof AddressLayout) {
            int at = (int) result;
            if (at == 0) return MemorySegment.NULL;
            return texts.computeIfAbsent((long) at, ignored -> Arena.global().allocateFrom(memory.readCString(at,
                    StandardCharsets.UTF_8), StandardCharsets.UTF_8));
        }
        throw new IllegalArgumentException("no Java value for a result laid out as " + layout);
    }

    /** The module's address of a copy of `segment`, which `release` frees. Under the lock. */
    int copyIn(MemorySegment segment) {
        int at = allocate((int) segment.byteSize());
        memory.write(at, segment.toArray(ValueLayout.JAVA_BYTE));
        return at;
    }

    /** `size` bytes of the module's memory, which `release` frees. Under the lock. */
    int allocate(int size) {
        if (malloc == null) {
            malloc = instance.export("malloc");
            free = instance.export("free");
        }
        int at = (int) enter(() -> malloc.apply(Math.max(size, 1)))[0];
        if (at == 0) throw new OutOfMemoryError("the WebAssembly module could not allocate " + size + " bytes");
        return at;
    }

    void release(int at) {
        enter(() -> free.apply(at));
    }

    Memory memory() {
        return memory;
    }

    /** True when the module is no longer called, after which the receive threads end. */
    boolean dead() {
        return dead != null;
    }

    /** Stops the module for good with `cause`, from a thread of the host's that cannot go on. Under the lock. */
    void fail(Throwable cause) {
        if (dead == null) dead = cause;
    }

    /** What one recvfrom gave, for the receiver at `receiver` (cameraunlock_host_received). Takes the lock. */
    synchronized void received(int receiver, byte[] datagram, int bytes, int error, byte[] sender) {
        requireAlive();
        int buffer = allocate(datagram.length);
        long status;
        try {
            int from = allocate(sender.length);
            try {
                memory.write(buffer, datagram);
                memory.write(from, sender);
                status = enter(() -> received.apply(receiver, buffer, bytes, error, from))[0];
            } finally {
                if (dead == null) release(from);
            }
        } finally {
            if (dead == null) release(buffer);
        }
        if (status != 0) throw new IllegalStateException("cameraunlock_host_received failed, and the log has the reason");
    }

    /** The receive loop of the receiver at `receiver` gave up (cameraunlock_host_receive_failed). Takes the lock. */
    synchronized void receiveFailed(int receiver, String what) {
        requireAlive();
        byte[] text = (what + '\0').getBytes(StandardCharsets.UTF_8);
        int at = allocate(text.length);
        memory.write(at, text);
        long status = enter(() -> receiveFailed.apply(receiver, at))[0];
        release(at);
        if (status != 0) throw new IllegalStateException("cameraunlock_host_receive_failed failed, and the log has the reason");
    }

    private void tickForever() {
        try {
            while (true) {
                synchronized (this) {
                    if (enter(() -> tick.apply())[0] != 0) {
                        throw new IllegalStateException("cameraunlock_host_tick failed, and the log has the reason");
                    }
                }
                Thread.sleep(TICK_MS);
            }
        } catch (Throwable failure) {
            // The hotkeys and the receivers' retries stop with this thread, so the module stops too,
            // and the host's next call says why. A module another call stopped has said so already.
            synchronized (this) {
                if (dead != null) return;
                fail(failure);
            }
            if (failure instanceof Error error) throw error;
            throw new IllegalStateException(failure);
        }
    }
}
