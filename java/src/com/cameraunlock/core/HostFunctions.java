package com.cameraunlock.core;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import run.endive.runtime.HostFunction;
import run.endive.runtime.Memory;
import run.endive.wasm.types.FunctionType;
import run.endive.wasm.types.ValType;

/**
 * What both hosts of module "cameraunlock_win32" need to answer its imports: the registration of
 * an import, and the module's text and buffers as it passes them. Win32Host answers on Windows,
 * PosixHost on Linux.
 */
final class HostFunctions {
    static final String MODULE = "cameraunlock_win32";

    private HostFunctions() {}

    interface Body {
        long apply(Memory memory, long[] arguments);
    }

    interface VoidBody {
        void apply(Memory memory, long[] arguments);
    }

    static HostFunction function(String name, int parameters, Body body) {
        List<ValType> types = new ArrayList<>();
        for (int i = 0; i < parameters; i++) types.add(ValType.I32);
        return new HostFunction(MODULE, name, FunctionType.of(types, List.of(ValType.I32)),
                (instance, arguments) -> new long[] {body.apply(instance.memory(), arguments)});
    }

    static HostFunction procedure(String name, int parameters, VoidBody body) {
        List<ValType> types = new ArrayList<>();
        for (int i = 0; i < parameters; i++) types.add(ValType.I32);
        return new HostFunction(MODULE, name, FunctionType.of(types, List.of()), (instance, arguments) -> {
            body.apply(instance.memory(), arguments);
            return null;
        });
    }

    /**
     * A buffer size the module passed, which Windows reads as a DWORD. Over 2 GiB it is negative
     * here, and on Windows the native buffer sized from it would be one byte that Windows then
     * writes past.
     */
    static int size(String function, long value) {
        int bytes = (int) value;
        if (bytes < 0) throw new IllegalArgumentException(function + ": a buffer of " + Integer.toUnsignedString(bytes) + " bytes");
        return bytes;
    }

    static void requireZero(String function, String what, long value) {
        if (value != 0) throw new UnsupportedOperationException(function + ": " + what + " is not supported in the WebAssembly build");
    }

    /** The module's NUL-terminated UTF-32, or `count` code points of it. Null for a NULL pointer. */
    static String wide(Memory memory, long at, int count) {
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
    static String text(Memory memory, long at, int count, String function) {
        if (at == 0) throw new IllegalArgumentException(function + ": the module passed NULL text");
        return wide(memory, at, count);
    }

    /** Writes `text` to the module as UTF-32, followed by a NUL when `terminate`. */
    static void writeWide(Memory memory, int at, String text, boolean terminate) {
        int[] points = text.codePoints().toArray();
        for (int i = 0; i < points.length; i++) memory.writeI32(at + 4 * i, points[i]);
        if (terminate) memory.writeI32(at + 4 * points.length, 0);
    }

    /** Writes `lines` to the module as UTF-8 and answers their length, which may be more than fits. */
    static int answer(Memory m, String lines, int at, int capacity) {
        byte[] bytes = lines.getBytes(StandardCharsets.UTF_8);
        if (bytes.length <= capacity) m.write(at, bytes);
        return bytes.length;
    }

    /**
     * One line of the probe's answer: the key, '=', and the value's bytes in hex, so a value may hold
     * any byte (a line break, an unpaired surrogate) and still arrive whole. defaults_location.cpp
     * reads it.
     */
    static void line(StringBuilder lines, String key, byte[] value) {
        lines.append(key).append('=').append(java.util.HexFormat.of().formatHex(value)).append('\n');
    }

    /** UTF-8 of `text` with an unpaired surrogate written as its own three bytes, which the module decodes back. */
    static byte[] wtf8(String text) {
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
}
