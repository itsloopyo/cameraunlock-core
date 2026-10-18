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
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import run.endive.runtime.HostFunction;
import run.endive.runtime.Memory;
import run.endive.wasm.types.FunctionType;
import run.endive.wasm.types.ValType;

/**
 * The imports of module "cameraunlock_win32" that the WebAssembly build of the C interface makes
 * (cpp/include/cameraunlock/os/win32_host.h): each is the same call into kernel32, user32, ws2_32,
 * shell32 or ole32 that CameraUnlockCore.dll makes, so core's file, window, key and socket code
 * behaves as it does in the DLL.
 *
 * The module's side of a call differs from Windows' in three ways, and each import converts:
 * pointers are offsets into the module's memory, which is copied to and from native memory;
 * wchar_t is 32 bits, so W text is UTF-32 there and UTF-16 here, and lengths count code points
 * there; and HANDLE, HWND, HMONITOR and SOCKET are 32 bits, which Windows keeps its values within,
 * so they are sign-extended here.
 *
 * Every import runs inside a call into the module, under WasmCore's lock, so the last error of the
 * one thread there is is kept here and answered by GetLastError and WSAGetLastError. The receive
 * threads keep their own.
 */
@SuppressWarnings("restricted")
final class Win32Host {
    private static final String MODULE = "cameraunlock_win32";
    private static final ValueLayout.OfInt INT = ValueLayout.JAVA_INT;
    private static final ValueLayout.OfLong LONG = ValueLayout.JAVA_LONG;
    private static final ValueLayout.OfShort SHORT = ValueLayout.JAVA_SHORT;
    private static final java.lang.foreign.AddressLayout POINTER = ValueLayout.ADDRESS;

    private static final int ERROR_INSUFFICIENT_BUFFER = 122;
    private static final int SOCKET_ERROR = -1;
    private static final int WSAEWOULDBLOCK = 10035;
    /** Wine's Unix code page, which defaults_location.cpp converts with. */
    private static final int UNIX_CODE_PAGE = 65010;
    /** WIN32_FIND_DATAW up to cFileName, the same in both: attributes, three FILETIMEs, sizes, two reserved. */
    private static final int FIND_DATA_HEAD = 44;
    private static final int FIND_DATA_NAME = 260, FIND_DATA_ALTERNATE = 14;
    /** fd_set on 64-bit Windows: u_int fd_count, padding, SOCKET fd_array[FD_SETSIZE]. */
    private static final int FD_SET_BYTES = 8 + 64 * 8;
    /** The receive buffer of UdpReceiver::ReceiverThread. */
    private static final int RECEIVE_BUFFER = 64;
    private static final int SOCKADDR_IN = 16;
    /** FOLDERID_RoamingAppData, as defaults_location.cpp has it. */
    private static final byte[] ROAMING_APP_DATA = {(byte) 0xDB, (byte) 0x85, (byte) 0xB6, 0x3E, (byte) 0xF9, 0x65, (byte) 0xF6, 0x4C,
            (byte) 0xA0, 0x3A, (byte) 0xE3, (byte) 0xEF, 0x65, 0x72, (byte) 0x9F, 0x3D};

    private static final StructLayout CAPTURE = Linker.Option.captureStateLayout();
    private static final VarHandle LAST_ERROR = CAPTURE.varHandle(PathElement.groupElement("GetLastError"));
    private static final VarHandle WSA_LAST_ERROR = CAPTURE.varHandle(PathElement.groupElement("WSAGetLastError"));

    private final WasmCore core;
    private final Linker linker = Linker.nativeLinker();
    private final SymbolLookup kernel32, user32, ws2_32;
    private final MemorySegment capture = Arena.global().allocate(CAPTURE);
    /** Eight bytes an import writes into and reads straight back, under the module's lock. */
    private final MemorySegment scratch = Arena.global().allocate(8);
    /**
     * The last error, read and written under the module's lock, so it is the error of the call the
     * module made last. Windows keeps one slot a thread for GetLastError and WSAGetLastError.
     */
    private int lastError;
    private final Map<Integer, Receiving> receiving = new ConcurrentHashMap<>();
    /** FindFirstFileW's handles, which are pointers past 32 bits, by the small number the module holds instead. */
    private final Map<Integer, MemorySegment> searches = new java.util.HashMap<>();
    private int nextSearch = 1;

    private final MethodHandle createFileW, readFile, writeFile, setFilePointerEx, flushFileBuffers, closeHandle,
            getFileInformationByHandle, getFileAttributesW, getFileAttributesExW, findFirstFileW, findClose,
            getFullPathNameW, replaceFileW, moveFileExW, deleteFileW, createDirectoryW, getACP, multiByteToWideChar,
            wideCharToMultiByte, compareStringOrdinal, queryPerformanceCounter, queryPerformanceFrequency, getLocalTime,
            getCurrentProcessId, sleep, formatMessageW, formatMessageA, getEnvironmentVariableW, getProcessHeap, heapFree;
    private final MethodHandle getAsyncKeyState, getForegroundWindow, getWindowThreadProcessId, isWindow, isIconic,
            isZoomed, getWindowLongW, getWindowRect, monitorFromWindow, getMonitorInfoW, setWindowPos;
    private final MethodHandle wsaStartup, wsaCleanup, socket, ioctlsocket, wsaIoctl, bind, closesocket, select, recvfrom;

    @SuppressWarnings("restricted")
    Win32Host(WasmCore core) {
        this.core = core;
        kernel32 = SymbolLookup.libraryLookup("kernel32", Arena.global());
        user32 = SymbolLookup.libraryLookup("user32", Arena.global());
        ws2_32 = SymbolLookup.libraryLookup("ws2_32", Arena.global());

        createFileW = win(kernel32, "CreateFileW", FunctionDescriptor.of(POINTER, POINTER, INT, INT, POINTER, INT, INT, POINTER));
        readFile = win(kernel32, "ReadFile", FunctionDescriptor.of(INT, POINTER, POINTER, INT, POINTER, POINTER));
        writeFile = win(kernel32, "WriteFile", FunctionDescriptor.of(INT, POINTER, POINTER, INT, POINTER, POINTER));
        setFilePointerEx = win(kernel32, "SetFilePointerEx", FunctionDescriptor.of(INT, POINTER, LONG, POINTER, INT));
        flushFileBuffers = win(kernel32, "FlushFileBuffers", FunctionDescriptor.of(INT, POINTER));
        closeHandle = win(kernel32, "CloseHandle", FunctionDescriptor.of(INT, POINTER));
        getFileInformationByHandle = win(kernel32, "GetFileInformationByHandle", FunctionDescriptor.of(INT, POINTER, POINTER));
        getFileAttributesW = win(kernel32, "GetFileAttributesW", FunctionDescriptor.of(INT, POINTER));
        getFileAttributesExW = win(kernel32, "GetFileAttributesExW", FunctionDescriptor.of(INT, POINTER, INT, POINTER));
        findFirstFileW = win(kernel32, "FindFirstFileW", FunctionDescriptor.of(POINTER, POINTER, POINTER));
        findClose = win(kernel32, "FindClose", FunctionDescriptor.of(INT, POINTER));
        getFullPathNameW = win(kernel32, "GetFullPathNameW", FunctionDescriptor.of(INT, POINTER, INT, POINTER, POINTER));
        replaceFileW = win(kernel32, "ReplaceFileW", FunctionDescriptor.of(INT, POINTER, POINTER, POINTER, INT, POINTER, POINTER));
        moveFileExW = win(kernel32, "MoveFileExW", FunctionDescriptor.of(INT, POINTER, POINTER, INT));
        deleteFileW = win(kernel32, "DeleteFileW", FunctionDescriptor.of(INT, POINTER));
        createDirectoryW = win(kernel32, "CreateDirectoryW", FunctionDescriptor.of(INT, POINTER, POINTER));
        getACP = win(kernel32, "GetACP", FunctionDescriptor.of(INT));
        multiByteToWideChar = win(kernel32, "MultiByteToWideChar", FunctionDescriptor.of(INT, INT, INT, POINTER, INT, POINTER, INT));
        wideCharToMultiByte = win(kernel32, "WideCharToMultiByte",
                FunctionDescriptor.of(INT, INT, INT, POINTER, INT, POINTER, INT, POINTER, POINTER));
        compareStringOrdinal = win(kernel32, "CompareStringOrdinal", FunctionDescriptor.of(INT, POINTER, INT, POINTER, INT, INT));
        queryPerformanceCounter = win(kernel32, "QueryPerformanceCounter", FunctionDescriptor.of(INT, POINTER));
        queryPerformanceFrequency = win(kernel32, "QueryPerformanceFrequency", FunctionDescriptor.of(INT, POINTER));
        getLocalTime = win(kernel32, "GetLocalTime", FunctionDescriptor.ofVoid(POINTER));
        getCurrentProcessId = win(kernel32, "GetCurrentProcessId", FunctionDescriptor.of(INT));
        sleep = win(kernel32, "Sleep", FunctionDescriptor.ofVoid(INT));
        formatMessageW = win(kernel32, "FormatMessageW", FunctionDescriptor.of(INT, INT, POINTER, INT, INT, POINTER, INT, POINTER));
        formatMessageA = win(kernel32, "FormatMessageA", FunctionDescriptor.of(INT, INT, POINTER, INT, INT, POINTER, INT, POINTER));
        getEnvironmentVariableW = win(kernel32, "GetEnvironmentVariableW", FunctionDescriptor.of(INT, POINTER, POINTER, INT));
        getProcessHeap = win(kernel32, "GetProcessHeap", FunctionDescriptor.of(POINTER));
        heapFree = win(kernel32, "HeapFree", FunctionDescriptor.of(INT, POINTER, INT, POINTER));

        getAsyncKeyState = win(user32, "GetAsyncKeyState", FunctionDescriptor.of(SHORT, INT));
        getForegroundWindow = win(user32, "GetForegroundWindow", FunctionDescriptor.of(POINTER));
        getWindowThreadProcessId = win(user32, "GetWindowThreadProcessId", FunctionDescriptor.of(INT, POINTER, POINTER));
        isWindow = win(user32, "IsWindow", FunctionDescriptor.of(INT, POINTER));
        isIconic = win(user32, "IsIconic", FunctionDescriptor.of(INT, POINTER));
        isZoomed = win(user32, "IsZoomed", FunctionDescriptor.of(INT, POINTER));
        getWindowLongW = win(user32, "GetWindowLongW", FunctionDescriptor.of(INT, POINTER, INT));
        getWindowRect = win(user32, "GetWindowRect", FunctionDescriptor.of(INT, POINTER, POINTER));
        monitorFromWindow = win(user32, "MonitorFromWindow", FunctionDescriptor.of(POINTER, POINTER, INT));
        getMonitorInfoW = win(user32, "GetMonitorInfoW", FunctionDescriptor.of(INT, POINTER, POINTER));
        setWindowPos = win(user32, "SetWindowPos", FunctionDescriptor.of(INT, POINTER, POINTER, INT, INT, INT, INT, INT));

        wsaStartup = winsock("WSAStartup", FunctionDescriptor.of(INT, SHORT, POINTER));
        wsaCleanup = winsock("WSACleanup", FunctionDescriptor.of(INT));
        socket = winsock("socket", FunctionDescriptor.of(LONG, INT, INT, INT));
        ioctlsocket = winsock("ioctlsocket", FunctionDescriptor.of(INT, LONG, INT, POINTER));
        wsaIoctl = winsock("WSAIoctl", FunctionDescriptor.of(INT, LONG, INT, POINTER, INT, POINTER, INT, POINTER, POINTER, POINTER));
        bind = winsock("bind", FunctionDescriptor.of(INT, LONG, POINTER, INT));
        closesocket = winsock("closesocket", FunctionDescriptor.of(INT, LONG));
        select = winsock("select", FunctionDescriptor.of(INT, INT, POINTER, POINTER, POINTER, POINTER));
        recvfrom = winsock("recvfrom", FunctionDescriptor.of(INT, LONG, POINTER, INT, INT, POINTER, POINTER));
    }

    private MethodHandle win(SymbolLookup library, String name, FunctionDescriptor descriptor) {
        return linker.downcallHandle(library.find(name).orElseThrow(() -> new IllegalStateException("Windows has no " + name)),
                descriptor, Linker.Option.captureCallState("GetLastError"));
    }

    private final java.util.Set<MethodHandle> winsockFunctions = java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<>());

    private MethodHandle winsock(String name, FunctionDescriptor descriptor) {
        MethodHandle function = linker.downcallHandle(ws2_32.find(name).orElseThrow(() -> new IllegalStateException("ws2_32 has no " + name)),
                descriptor, Linker.Option.captureCallState("WSAGetLastError"));
        winsockFunctions.add(function);
        return function;
    }

    // ---- Calls --------------------------------------------------------------------------------------

    /** A captured call: the result, with the thread's last error kept for GetLastError. */
    private Object call(MethodHandle function, Object... arguments) {
        Object[] all = new Object[arguments.length + 1];
        all[0] = capture;
        System.arraycopy(arguments, 0, all, 1, arguments.length);
        try {
            Object result = function.invokeWithArguments(all);
            lastError = (int) (winsockFunctions.contains(function) ? WSA_LAST_ERROR : LAST_ERROR).get(capture, 0L);
            return result;
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
    }

    private int callInt(MethodHandle function, Object... arguments) {
        return (int) call(function, arguments);
    }

    /** A handle of the module's, as Windows has it. */
    private static MemorySegment handle(long value) {
        return MemorySegment.ofAddress((int) value);
    }

    /** A handle of Windows', as the module has it: 32 bits, which sign-extend back to the same value. */
    private static long handle(MemorySegment value) {
        long address = value.address();
        if ((long) (int) address != address) {
            throw new IllegalStateException("a Windows handle, 0x" + Long.toHexString(address)
                    + ", does not fit the 32 bits the WebAssembly module keeps it in");
        }
        return (int) address;
    }

    /** NUL-terminated native UTF-16 from `offset`, unit by unit, so an unpaired surrogate is kept as Windows has it. */
    private static String utf16z(MemorySegment text, long offset, int most) {
        StringBuilder units = new StringBuilder();
        for (int i = 0; i < most; i++) {
            char unit = text.get(ValueLayout.JAVA_CHAR_UNALIGNED, offset + 2L * i);
            if (unit == 0) break;
            units.append(unit);
        }
        return units.toString();
    }

    private static MemorySegment nullable(Arena arena, Memory memory, long at, int bytes) {
        return at == 0 ? MemorySegment.NULL : copyIn(arena, memory, (int) at, bytes);
    }

    private static MemorySegment copyIn(Arena arena, Memory memory, int at, int bytes) {
        MemorySegment segment = arena.allocate(Math.max(bytes, 1));
        MemorySegment.copy(memory.readBytes(at, bytes), 0, segment, ValueLayout.JAVA_BYTE, 0, bytes);
        return segment;
    }

    private static void copyOut(Memory memory, MemorySegment segment, int at, int bytes) {
        if (at != 0) memory.write(at, segment.asSlice(0, bytes).toArray(ValueLayout.JAVA_BYTE));
    }

    private static void requireZero(String function, String what, long value) {
        if (value != 0) throw new UnsupportedOperationException(function + ": " + what + " is not supported in the WebAssembly build");
    }

    // ---- Text ---------------------------------------------------------------------------------------

    /** The module's NUL-terminated UTF-32, or `count` code points of it. Null for a NULL pointer. */
    private static String wide(Memory memory, long at, int count) {
        if (at == 0) return null;
        StringBuilder text = new StringBuilder();
        for (int i = 0; count < 0 || i < count; i++) {
            int point = memory.readInt((int) at + 4 * i);
            if (count < 0 && point == 0) break;
            text.appendCodePoint(point);
        }
        return text.toString();
    }

    /** The module's text for a parameter Windows needs: a NULL one is a bug of the caller's. */
    private static String text(Memory memory, long at, int count, String function) {
        if (at == 0) throw new IllegalArgumentException(function + ": the module passed NULL text");
        return wide(memory, at, count);
    }

    private static MemorySegment utf16(Arena arena, String text) {
        return text == null ? MemorySegment.NULL : arena.allocateFrom(text, StandardCharsets.UTF_16LE);
    }

    /** `units` UTF-16 code units of native text. */
    private static String utf16(MemorySegment text, int units) {
        char[] chars = new char[units];
        MemorySegment.copy(text, ValueLayout.JAVA_CHAR_UNALIGNED, 0, chars, 0, units);
        return new String(chars);
    }

    /** Writes `text` to the module as UTF-32, followed by a NUL when `terminate`. */
    private static void writeWide(Memory memory, int at, String text, boolean terminate) {
        int[] points = text.codePoints().toArray();
        for (int i = 0; i < points.length; i++) memory.writeI32(at + 4 * i, points[i]);
        if (terminate) memory.writeI32(at + 4 * points.length, 0);
    }

    // ---- The imports --------------------------------------------------------------------------------

    private interface Body {
        long apply(Memory memory, long[] arguments);
    }

    private interface VoidBody {
        void apply(Memory memory, long[] arguments);
    }

    private static HostFunction function(String name, int parameters, Body body) {
        List<ValType> types = new ArrayList<>();
        for (int i = 0; i < parameters; i++) types.add(ValType.I32);
        return new HostFunction(MODULE, name, FunctionType.of(types, List.of(ValType.I32)),
                (instance, arguments) -> new long[] {body.apply(instance.memory(), arguments)});
    }

    private static HostFunction procedure(String name, int parameters, VoidBody body) {
        List<ValType> types = new ArrayList<>();
        for (int i = 0; i < parameters; i++) types.add(ValType.I32);
        return new HostFunction(MODULE, name, FunctionType.of(types, List.of()), (instance, arguments) -> {
            body.apply(instance.memory(), arguments);
            return null;
        });
    }

    HostFunction[] functions() {
        return new HostFunction[] {
            function("GetLastError", 0, (m, a) -> lastError),
            function("WSAGetLastError", 0, (m, a) -> lastError),

            function("CreateFileW", 7, (m, a) -> {
                requireZero("CreateFileW", "security attributes", a[3]);
                requireZero("CreateFileW", "a template file", a[6]);
                try (Arena arena = Arena.ofConfined()) {
                    return handle((MemorySegment) call(createFileW, utf16(arena, wide(m, a[0], -1)), (int) a[1], (int) a[2],
                            MemorySegment.NULL, (int) a[4], (int) a[5], MemorySegment.NULL));
                }
            }),
            function("ReadFile", 5, (m, a) -> {
                requireZero("ReadFile", "an overlapped read", a[4]);
                try (Arena arena = Arena.ofConfined()) {
                    int size = (int) a[2];
                    MemorySegment buffer = arena.allocate(Math.max(size, 1)), read = arena.allocate(INT);
                    int result = callInt(readFile, handle(a[0]), buffer, size, read, MemorySegment.NULL);
                    int got = read.get(INT, 0);
                    copyOut(m, buffer, (int) a[1], got);
                    if (a[3] != 0) m.writeI32((int) a[3], got);
                    return result;
                }
            }),
            function("WriteFile", 5, (m, a) -> {
                requireZero("WriteFile", "an overlapped write", a[4]);
                try (Arena arena = Arena.ofConfined()) {
                    int size = (int) a[2];
                    MemorySegment written = arena.allocate(INT);
                    int result = callInt(writeFile, handle(a[0]), copyIn(arena, m, (int) a[1], size), size, written, MemorySegment.NULL);
                    if (a[3] != 0) m.writeI32((int) a[3], written.get(INT, 0));
                    return result;
                }
            }),
            function("SetFilePointerEx", 4, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    MemorySegment position = arena.allocate(LONG);
                    int result = callInt(setFilePointerEx, handle(a[0]), m.readLong((int) a[1]), position, (int) a[3]);
                    if (a[2] != 0) m.writeLong((int) a[2], position.get(LONG, 0));
                    return result;
                }
            }),
            function("FlushFileBuffers", 1, (m, a) -> callInt(flushFileBuffers, handle(a[0]))),
            function("CloseHandle", 1, (m, a) -> callInt(closeHandle, handle(a[0]))),
            function("GetFileInformationByHandle", 2, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    MemorySegment information = arena.allocate(52);
                    int result = callInt(getFileInformationByHandle, handle(a[0]), information);
                    copyOut(m, information, (int) a[1], 52);
                    return result;
                }
            }),
            function("GetFileAttributesW", 1, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    return callInt(getFileAttributesW, utf16(arena, wide(m, a[0], -1)));
                }
            }),
            function("GetFileAttributesExW", 3, (m, a) -> {
                requireZero("GetFileAttributesExW", "a level other than GetFileExInfoStandard", a[1]);
                try (Arena arena = Arena.ofConfined()) {
                    MemorySegment data = arena.allocate(36);
                    int result = callInt(getFileAttributesExW, utf16(arena, wide(m, a[0], -1)), 0, data);
                    copyOut(m, data, (int) a[2], 36);
                    return result;
                }
            }),
            function("FindFirstFileW", 2, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    MemorySegment found = arena.allocate(FIND_DATA_HEAD + 2 * (FIND_DATA_NAME + FIND_DATA_ALTERNATE));
                    MemorySegment search = (MemorySegment) call(findFirstFileW, utf16(arena, text(m, a[0], -1, "FindFirstFileW")), found);
                    if (search.address() == -1L) return -1;
                    int at = (int) a[1];
                    copyOut(m, found, at, FIND_DATA_HEAD);
                    writeWide(m, at + FIND_DATA_HEAD, utf16z(found, FIND_DATA_HEAD, FIND_DATA_NAME), true);
                    writeWide(m, at + FIND_DATA_HEAD + 4 * FIND_DATA_NAME,
                            utf16z(found, FIND_DATA_HEAD + 2L * FIND_DATA_NAME, FIND_DATA_ALTERNATE), true);
                    int token = nextSearch++;
                    searches.put(token, search);
                    return token;
                }
            }),
            function("FindClose", 1, (m, a) -> {
                MemorySegment search = searches.remove((int) a[0]);
                if (search == null) {
                    throw new IllegalStateException("FindClose of " + a[0] + ", which FindFirstFileW did not answer or is closed");
                }
                return callInt(findClose, search);
            }),
            function("GetFullPathNameW", 4, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    int capacity = (int) a[1];
                    MemorySegment buffer = capacity == 0 ? MemorySegment.NULL : arena.allocate(2L * capacity);
                    MemorySegment part = arena.allocate(POINTER);
                    int units = callInt(getFullPathNameW, utf16(arena, wide(m, a[0], -1)), capacity, buffer,
                            a[3] == 0 ? MemorySegment.NULL : part);
                    if (units == 0 || units >= capacity) return units;
                    String full = utf16(buffer, units);
                    writeWide(m, (int) a[2], full, true);
                    if (a[3] != 0) {
                        long into = part.get(POINTER, 0).address();
                        int partAt = into == 0 ? 0
                                : (int) a[2] + 4 * full.codePointCount(0, (int) ((into - buffer.address()) / 2));
                        m.writeI32((int) a[3], partAt);
                    }
                    return full.codePointCount(0, full.length());
                }
            }),
            function("ReplaceFileW", 6, (m, a) -> {
                requireZero("ReplaceFileW", "excluding", a[4]);
                requireZero("ReplaceFileW", "a reserved pointer", a[5]);
                try (Arena arena = Arena.ofConfined()) {
                    return callInt(replaceFileW, utf16(arena, wide(m, a[0], -1)), utf16(arena, wide(m, a[1], -1)),
                            utf16(arena, wide(m, a[2], -1)), (int) a[3], MemorySegment.NULL, MemorySegment.NULL);
                }
            }),
            function("MoveFileExW", 3, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    return callInt(moveFileExW, utf16(arena, wide(m, a[0], -1)), utf16(arena, wide(m, a[1], -1)), (int) a[2]);
                }
            }),
            function("DeleteFileW", 1, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    return callInt(deleteFileW, utf16(arena, wide(m, a[0], -1)));
                }
            }),
            function("CreateDirectoryW", 2, (m, a) -> {
                requireZero("CreateDirectoryW", "security attributes", a[1]);
                try (Arena arena = Arena.ofConfined()) {
                    return callInt(createDirectoryW, utf16(arena, wide(m, a[0], -1)), MemorySegment.NULL);
                }
            }),

            function("GetACP", 0, (m, a) -> callInt(getACP)),
            function("MultiByteToWideChar", 6, (m, a) -> multiByteToWideChar(m, (int) a[0], (int) a[1], (int) a[2], (int) a[3],
                    (int) a[4], (int) a[5])),
            function("WideCharToMultiByte", 8, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    int count = (int) a[3];
                    // A length of -1 counts the terminating NUL, which the conversion then carries.
                    String text = count < 0 ? text(m, a[2], -1, "WideCharToMultiByte") + '\0'
                            : text(m, a[2], count, "WideCharToMultiByte");
                    MemorySegment wideText = arena.allocate(2L * Math.max(text.length(), 1));
                    MemorySegment.copy(text.toCharArray(), 0, wideText, ValueLayout.JAVA_CHAR_UNALIGNED, 0, text.length());
                    int capacity = (int) a[5];
                    MemorySegment out = capacity == 0 ? MemorySegment.NULL : arena.allocate(capacity);
                    MemorySegment usedDefault = arena.allocate(INT);
                    MemorySegment defaultChar = a[6] == 0 ? MemorySegment.NULL
                            : arena.allocateFrom(m.readCString((int) a[6], StandardCharsets.ISO_8859_1), StandardCharsets.ISO_8859_1);
                    int bytes = callInt(wideCharToMultiByte, (int) a[0], (int) a[1], wideText, text.length(), out, capacity,
                            defaultChar, a[7] == 0 ? MemorySegment.NULL : usedDefault);
                    if (capacity != 0 && bytes > 0) copyOut(m, out, (int) a[4], bytes);
                    if (a[7] != 0) m.writeI32((int) a[7], usedDefault.get(INT, 0));
                    return bytes;
                }
            }),
            function("CompareStringOrdinal", 5, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    String first = text(m, a[0], (int) a[1], "CompareStringOrdinal");
                    String second = text(m, a[2], (int) a[3], "CompareStringOrdinal");
                    return callInt(compareStringOrdinal, utf16(arena, first), first.length(), utf16(arena, second), second.length(),
                            (int) a[4]);
                }
            }),

            function("QueryPerformanceCounter", 1, (m, a) -> largeInteger(m, queryPerformanceCounter, (int) a[0])),
            function("QueryPerformanceFrequency", 1, (m, a) -> largeInteger(m, queryPerformanceFrequency, (int) a[0])),
            procedure("GetLocalTime", 1, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    MemorySegment time = arena.allocate(16);
                    call(getLocalTime, time);
                    copyOut(m, time, (int) a[0], 16);
                }
            }),
            function("GetCurrentProcessId", 0, (m, a) -> {
                try {
                    return (int) getCurrentProcessId.invokeExact(capture);
                } catch (Throwable failure) {
                    throw rethrown(failure);
                }
            }),
            procedure("Sleep", 1, (m, a) -> call(sleep, (int) a[0])),
            function("FormatMessageW", 7, (m, a) -> {
                requireZero("FormatMessageW", "a message source", a[1]);
                requireZero("FormatMessageW", "inserts", a[6]);
                try (Arena arena = Arena.ofConfined()) {
                    int capacity = (int) a[5];
                    MemorySegment buffer = arena.allocate(2L * Math.max(capacity, 1));
                    int units = callInt(formatMessageW, (int) a[0], MemorySegment.NULL, (int) a[2], (int) a[3], buffer, capacity,
                            MemorySegment.NULL);
                    if (units == 0) return 0;
                    String message = utf16(buffer, units);
                    writeWide(m, (int) a[4], message, true);
                    return message.codePointCount(0, message.length());
                }
            }),
            function("FormatMessageA", 7, (m, a) -> {
                requireZero("FormatMessageA", "a message source", a[1]);
                requireZero("FormatMessageA", "inserts", a[6]);
                try (Arena arena = Arena.ofConfined()) {
                    int capacity = (int) a[5];
                    MemorySegment buffer = arena.allocate(Math.max(capacity, 1));
                    int bytes = callInt(formatMessageA, (int) a[0], MemorySegment.NULL, (int) a[2], (int) a[3], buffer, capacity,
                            MemorySegment.NULL);
                    if (bytes > 0) copyOut(m, buffer, (int) a[4], Math.min(bytes + 1, capacity));
                    return bytes;
                }
            }),

            // The hotkey poller asks these of every key every 16 ms: typed calls, with no arena. None of
            // them is read for its last error unless it failed, and Windows leaves it alone otherwise.
            function("GetAsyncKeyState", 1, (m, a) -> {
                try {
                    return (short) getAsyncKeyState.invokeExact(capture, (int) a[0]);
                } catch (Throwable failure) {
                    throw rethrown(failure);
                }
            }),
            function("GetForegroundWindow", 0, (m, a) -> {
                try {
                    return handle((MemorySegment) getForegroundWindow.invokeExact(capture));
                } catch (Throwable failure) {
                    throw rethrown(failure);
                }
            }),
            function("GetWindowThreadProcessId", 2, (m, a) -> {
                try {
                    // A local, not a ?: in the call: invokeExact types a conditional argument as Object.
                    MemorySegment process = a[1] == 0 ? MemorySegment.NULL : scratch;
                    int thread = (int) getWindowThreadProcessId.invokeExact(capture, handle(a[0]), process);
                    if (thread == 0) lastError = (int) LAST_ERROR.get(capture, 0L);
                    if (a[1] != 0) m.writeI32((int) a[1], scratch.get(INT, 0));
                    return thread;
                } catch (Throwable failure) {
                    throw rethrown(failure);
                }
            }),
            function("IsWindow", 1, (m, a) -> callInt(isWindow, handle(a[0]))),
            function("IsIconic", 1, (m, a) -> callInt(isIconic, handle(a[0]))),
            function("IsZoomed", 1, (m, a) -> callInt(isZoomed, handle(a[0]))),
            function("GetWindowLongW", 2, (m, a) -> callInt(getWindowLongW, handle(a[0]), (int) a[1])),
            function("GetWindowRect", 2, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    MemorySegment rect = arena.allocate(16);
                    int result = callInt(getWindowRect, handle(a[0]), rect);
                    copyOut(m, rect, (int) a[1], 16);
                    return result;
                }
            }),
            function("MonitorFromWindow", 2, (m, a) -> handle((MemorySegment) call(monitorFromWindow, handle(a[0]), (int) a[1]))),
            function("GetMonitorInfoW", 2, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    int size = m.readInt((int) a[1]);
                    MemorySegment info = copyIn(arena, m, (int) a[1], size);
                    int result = callInt(getMonitorInfoW, handle(a[0]), info);
                    copyOut(m, info, (int) a[1], size);
                    return result;
                }
            }),
            function("SetWindowPos", 7, (m, a) -> callInt(setWindowPos, handle(a[0]), handle(a[1]), (int) a[2], (int) a[3],
                    (int) a[4], (int) a[5], (int) a[6])),

            function("WSAStartup", 2, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    return callInt(wsaStartup, (short) a[0], arena.allocate(408));
                }
            }),
            function("WSACleanup", 0, (m, a) -> callInt(wsaCleanup)),
            function("socket", 3, (m, a) -> {
                long made = (long) call(socket, (int) a[0], (int) a[1], (int) a[2]);
                // The module hands it back sign-extended, as it does a handle; INVALID_SOCKET is -1.
                if ((long) (int) made != made) {
                    throw new IllegalStateException("a socket, 0x" + Long.toHexString(made)
                            + ", does not fit the 32 bits the WebAssembly module keeps it in");
                }
                return (int) made;
            }),
            function("ioctlsocket", 3, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    MemorySegment argument = copyIn(arena, m, (int) a[2], 4);
                    int result = callInt(ioctlsocket, (long) (int) a[0], (int) a[1], argument);
                    copyOut(m, argument, (int) a[2], 4);
                    return result;
                }
            }),
            function("WSAIoctl", 9, (m, a) -> {
                requireZero("WSAIoctl", "an overlapped call", a[7]);
                requireZero("WSAIoctl", "a completion routine", a[8]);
                try (Arena arena = Arena.ofConfined()) {
                    int inSize = (int) a[3], outSize = (int) a[5];
                    MemorySegment out = a[4] == 0 ? MemorySegment.NULL : arena.allocate(Math.max(outSize, 1));
                    MemorySegment returned = arena.allocate(INT);
                    int result = callInt(wsaIoctl, (long) (int) a[0], (int) a[1], nullable(arena, m, a[2], inSize), inSize, out,
                            outSize, returned, MemorySegment.NULL, MemorySegment.NULL);
                    if (a[4] != 0) copyOut(m, out, (int) a[4], outSize);
                    if (a[6] != 0) m.writeI32((int) a[6], returned.get(INT, 0));
                    return result;
                }
            }),
            function("bind", 3, (m, a) -> {
                try (Arena arena = Arena.ofConfined()) {
                    return callInt(bind, (long) (int) a[0], copyIn(arena, m, (int) a[1], (int) a[2]), (int) a[2]);
                }
            }),
            function("closesocket", 1, (m, a) -> callInt(closesocket, (long) (int) a[0])),
            procedure("ReceiveOn", 2, (m, a) -> receiveOn((int) a[0], (int) a[1])),
            procedure("StopReceiving", 1, (m, a) -> {
                Receiving stopped = receiving.remove((int) a[0]);
                if (stopped != null) stopped.stopped = true;
            }),

            function("ProbeDefaults", 2, (m, a) -> answer(m, probeDefaults(), (int) a[0], (int) a[1])),
            function("WineDosFileName", 3, (m, a) -> answer(m, wineDosFileName(text(m, a[0], -1, "WineDosFileName")), (int) a[1], (int) a[2])),
        };
    }

    private int multiByteToWideChar(Memory m, int codePage, int flags, int text, int count, int out, int capacity) {
        try (Arena arena = Arena.ofConfined()) {
            int bytes = count;
            if (count < 0) {
                bytes = 0;
                while (m.read(text + bytes) != 0) bytes++;
                bytes++;
            }
            MemorySegment input = copyIn(arena, m, text, bytes);
            int units = callInt(multiByteToWideChar, codePage, flags, input, bytes, MemorySegment.NULL, 0);
            if (units == 0) return 0;
            MemorySegment converted = arena.allocate(2L * units);
            units = callInt(multiByteToWideChar, codePage, flags, input, bytes, converted, units);
            if (units == 0) return 0;
            String wideText = utf16(converted, units);
            int points = wideText.codePointCount(0, wideText.length());
            if (capacity == 0) return points;
            if (capacity < points) {
                lastError = ERROR_INSUFFICIENT_BUFFER;
                return 0;
            }
            writeWide(m, out, wideText, false);
            return points;
        }
    }

    /** QueryPerformanceCounter and its frequency, which the frame clock asks for every frame: typed, with no arena. */
    private int largeInteger(Memory m, MethodHandle function, int at) {
        try {
            int result = (int) function.invokeExact(capture, scratch);
            if (result == 0) lastError = (int) LAST_ERROR.get(capture, 0L);
            m.writeLong(at, scratch.get(LONG, 0));
            return result;
        } catch (Throwable failure) {
            throw rethrown(failure);
        }
    }

    private static RuntimeException rethrown(Throwable failure) {
        if (failure instanceof RuntimeException runtime) return runtime;
        if (failure instanceof Error error) throw error;
        return new IllegalStateException(failure);
    }

    /** Writes `lines` to the module as UTF-8 and answers their length, which may be more than fits. */
    private static int answer(Memory m, String lines, int at, int capacity) {
        byte[] bytes = lines.getBytes(StandardCharsets.UTF_8);
        if (bytes.length <= capacity) m.write(at, bytes);
        return bytes.length;
    }

    // ---- Defaults.ini -------------------------------------------------------------------------------

    /**
     * One line of the probe's answer: the key, '=', and the value's bytes in hex, so a value may hold
     * any byte (a line break, an unpaired surrogate) and still arrive whole. defaults_location.cpp
     * reads it.
     */
    private static void line(StringBuilder lines, String key, byte[] value) {
        lines.append(key).append('=').append(java.util.HexFormat.of().formatHex(value)).append('\n');
    }

    /** UTF-8 of `text` with an unpaired surrogate written as its own three bytes, which the module decodes back. */
    private static byte[] wtf8(String text) {
        java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
        text.codePoints().forEach(point -> {
            if (point < 0x80) {
                bytes.write(point);
            } else if (point < 0x800) {
                bytes.write(0xC0 | point >> 6);
                bytes.write(0x80 | point & 0x3F);
            } else if (point < 0x10000) {
                bytes.write(0xE0 | point >> 12);
                bytes.write(0x80 | point >> 6 & 0x3F);
                bytes.write(0x80 | point & 0x3F);
            } else {
                bytes.write(0xF0 | point >> 18);
                bytes.write(0x80 | point >> 12 & 0x3F);
                bytes.write(0x80 | point >> 6 & 0x3F);
                bytes.write(0x80 | point & 0x3F);
            }
        });
        return bytes.toByteArray();
    }

    /** defaults_location.cpp's ProbeDefaults on Windows, as lines of key=hex. */
    private String probeDefaults() {
        StringBuilder lines = new StringBuilder();
        String folder = knownFolder();
        if (!folder.isEmpty()) line(lines, "known_folder", wtf8(folder));
        SymbolLookup ntdll = SymbolLookup.libraryLookup("ntdll", Arena.global());
        Optional<MemorySegment> version = ntdll.find("wine_get_version");
        if (version.isEmpty()) {
            line(lines, "platform", "windows".getBytes(StandardCharsets.US_ASCII));
            Optional<MemorySegment> package_ = kernel32.find("GetCurrentPackageFullName");
            if (package_.isPresent()) {
                try (Arena arena = Arena.ofConfined()) {
                    MemorySegment length = arena.allocate(INT);
                    int result = (int) invoke(linker.downcallHandle(package_.get(), FunctionDescriptor.of(INT, POINTER, POINTER)),
                            length, MemorySegment.NULL);
                    line(lines, "package_result", Integer.toString(result).getBytes(StandardCharsets.US_ASCII));
                }
            }
            return lines.toString();
        }
        line(lines, "platform", "wine".getBytes(StandardCharsets.US_ASCII));
        MemorySegment wineVersion = (MemorySegment) invoke(linker.downcallHandle(version.get(), FunctionDescriptor.of(POINTER)));
        line(lines, "wine_version", cString(wineVersion));
        Optional<MemorySegment> hostVersion = ntdll.find("wine_get_host_version");
        if (hostVersion.isPresent()) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment system = arena.allocate(POINTER), release = arena.allocate(POINTER);
                invoke(linker.downcallHandle(hostVersion.get(), FunctionDescriptor.ofVoid(POINTER, POINTER)), system, release);
                line(lines, "host_system", cString(system.get(POINTER, 0)));
            }
        }
        for (String[] variable : new String[][] {{"wine_home_dir", "WINEHOMEDIR"},
                {"wine_host_xdg_config_home", "WINE_HOST_XDG_CONFIG_HOME"}, {"xdg_config_home", "XDG_CONFIG_HOME"}}) {
            String value = environmentVariable(variable[1]);
            if (!value.isEmpty()) line(lines, variable[0], wtf8(value));
        }
        return lines.toString();
    }

    /** defaults_location.cpp's UnixBytes and DosFileName, as lines of key=hex. */
    private String wineDosFileName(String unixFolder) {
        StringBuilder lines = new StringBuilder();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment text = arena.allocate(2L * Math.max(unixFolder.length(), 1));
            MemorySegment.copy(unixFolder.toCharArray(), 0, text, ValueLayout.JAVA_CHAR_UNALIGNED, 0, unixFolder.length());
            int length = callInt(wideCharToMultiByte, UNIX_CODE_PAGE, 0, text, unixFolder.length(), MemorySegment.NULL, 0,
                    MemorySegment.NULL, MemorySegment.NULL);
            MemorySegment bytes = length <= 0 ? MemorySegment.NULL : arena.allocate(length + 1L);
            if (length <= 0 || callInt(wideCharToMultiByte, UNIX_CODE_PAGE, 0, text, unixFolder.length(), bytes, length,
                    MemorySegment.NULL, MemorySegment.NULL) != length) {
                line(lines, "code_page_failed", "1".getBytes(StandardCharsets.US_ASCII));
                return lines.toString();
            }
            Optional<MemorySegment> convert = kernel32.find("wine_get_dos_file_name");
            if (convert.isEmpty()) return "";
            MemorySegment dos = (MemorySegment) invoke(linker.downcallHandle(convert.get(), FunctionDescriptor.of(POINTER, POINTER)),
                    bytes);
            if (dos.equals(MemorySegment.NULL)) return "";
            String name = utf16z(dos.reinterpret(Long.MAX_VALUE), 0, Integer.MAX_VALUE);
            call(heapFree, call(getProcessHeap), 0, dos);
            if (!name.isEmpty()) line(lines, "dos_file_name", wtf8(name));
            return lines.toString();
        }
    }

    /** The roaming AppData known folder, empty when shell32 or ole32 does not have the function or it fails. */
    private String knownFolder() {
        Optional<MemorySegment> get = SymbolLookup.libraryLookup("shell32", Arena.global()).find("SHGetKnownFolderPath");
        Optional<MemorySegment> taskMemFree = SymbolLookup.libraryLookup("ole32", Arena.global()).find("CoTaskMemFree");
        if (get.isEmpty() || taskMemFree.isEmpty()) return "";
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment id = arena.allocateFrom(ValueLayout.JAVA_BYTE, ROAMING_APP_DATA);
            MemorySegment path = arena.allocate(POINTER);
            int result = (int) invoke(linker.downcallHandle(get.get(), FunctionDescriptor.of(INT, POINTER, INT, POINTER, POINTER)),
                    id, 0, MemorySegment.NULL, path);
            MemorySegment text = path.get(POINTER, 0);
            String folder = result >= 0 && !text.equals(MemorySegment.NULL) ? utf16z(text.reinterpret(Long.MAX_VALUE), 0, Integer.MAX_VALUE) : "";
            invoke(linker.downcallHandle(taskMemFree.get(), FunctionDescriptor.ofVoid(POINTER)), text);
            return folder;
        }
    }

    private String environmentVariable(String name) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment key = utf16(arena, name);
            int size = callInt(getEnvironmentVariableW, key, MemorySegment.NULL, 0);
            if (size == 0) return "";
            MemorySegment value = arena.allocate(2L * size);
            int length = callInt(getEnvironmentVariableW, key, value, size);
            if (length >= size) throw new IllegalStateException("GetEnvironmentVariableW: the variable changed while it was read");
            return utf16(value, length);
        }
    }

    /** A C string's bytes as they are, as the DLL keeps them. */
    private static byte[] cString(MemorySegment text) {
        if (text.equals(MemorySegment.NULL)) return new byte[0];
        MemorySegment all = text.reinterpret(Long.MAX_VALUE);
        long length = 0;
        while (all.get(ValueLayout.JAVA_BYTE, length) != 0) length++;
        return all.asSlice(0, length).toArray(ValueLayout.JAVA_BYTE);
    }

    private static Object invoke(MethodHandle function, Object... arguments) {
        try {
            return function.invokeWithArguments(arguments);
        } catch (RuntimeException | Error failure) {
            throw failure;
        } catch (Throwable failure) {
            throw new IllegalStateException(failure);
        }
    }

    // ---- Receiving ----------------------------------------------------------------------------------

    /** kSupervisorTickMs in udp_receiver.cpp: how long a wait for a datagram lasts, so a stop is noticed. */
    private static final int WAIT_MICROSECONDS = 100_000;

    /** One receive thread: StopReceiving sets `stopped`, and the thread delivers nothing after it. */
    private static final class Receiving {
        final long socket;
        volatile boolean stopped;

        Receiving(long socket) {
            this.socket = socket;
        }
    }

    /**
     * UdpReceiver::ReceiverThread's wait and recvfrom, on a thread of its own: select with a
     * timeout of WAIT_MICROSECONDS in place of the DLL's event, so StopReceiving is noticed, and
     * every other recvfrom result handed to the receiver at `receiver`.
     */
    private void receiveOn(int socketValue, int receiver) {
        Receiving mine = new Receiving(socketValue);
        Receiving before = receiving.put(receiver, mine);
        if (before != null) before.stopped = true;
        long socketHandle = socketValue;
        Thread thread = new Thread(() -> receiveLoop(socketHandle, receiver, mine), "cameraunlock-core receive");
        thread.setDaemon(true);
        thread.start();
    }

    private void receiveLoop(long socketHandle, int receiver, Receiving mine) {
        String why = null;
        Throwable thrown = null;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment threadCapture = arena.allocate(CAPTURE);
            MemorySegment readable = arena.allocate(FD_SET_BYTES), timeout = arena.allocate(8);
            MemorySegment buffer = arena.allocate(RECEIVE_BUFFER), sender = arena.allocate(SOCKADDR_IN), senderSize = arena.allocate(INT);
            while (!mine.stopped && !core.dead()) {
                readable.set(INT, 0, 1);
                readable.set(LONG, 8, socketHandle);
                timeout.set(INT, 0, 0);
                timeout.set(INT, 4, WAIT_MICROSECONDS);
                int ready = (int) invoke(select, threadCapture, 0, readable, MemorySegment.NULL, MemorySegment.NULL, timeout);
                if (mine.stopped) return;
                if (ready == SOCKET_ERROR) {
                    why = "select failed with " + (int) WSA_LAST_ERROR.get(threadCapture, 0L);
                    break;
                }
                if (ready == 0) continue;
                senderSize.set(INT, 0, SOCKADDR_IN);
                int bytes;
                // Under the lock StopReceiving and closesocket are called under, so the socket read is
                // this thread's and not a new one given the same handle value. It does not block: the
                // socket is non-blocking and select said it is readable.
                synchronized (core) {
                    if (mine.stopped) return;
                    bytes = (int) invoke(recvfrom, threadCapture, socketHandle, buffer, RECEIVE_BUFFER, 0, sender, senderSize);
                    int error = bytes == SOCKET_ERROR ? (int) WSA_LAST_ERROR.get(threadCapture, 0L) : 0;
                    if (bytes == SOCKET_ERROR && error == WSAEWOULDBLOCK) continue;
                    core.received(receiver, buffer.toArray(ValueLayout.JAVA_BYTE), bytes, error, sender.toArray(ValueLayout.JAVA_BYTE));
                }
                // A socket that fails every read would otherwise take the module's lock in a loop.
                if (bytes == SOCKET_ERROR) Thread.sleep(WasmCore.TICK_MS);
            }
            if (mine.stopped || core.dead()) return;
        } catch (Throwable failure) {
            // A module that stopped has said why already, from the call that stopped it.
            if (core.dead()) return;
            why = "the receive thread failed: " + failure;
            thrown = failure;
        }
        failed(receiver, mine, why);
        if (thrown instanceof Error error) throw error;
    }

    /** The receive loop gives up, and the receiver's supervisor reopens the socket, unless it was stopped or the module is. */
    private void failed(int receiver, Receiving mine, String what) {
        synchronized (core) {
            if (!mine.stopped && !core.dead()) core.receiveFailed(receiver, what);
        }
    }
}
