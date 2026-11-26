package com.cameraunlock.core;

import static com.cameraunlock.core.HostFunctions.answer;
import static com.cameraunlock.core.HostFunctions.function;
import static com.cameraunlock.core.HostFunctions.line;
import static com.cameraunlock.core.HostFunctions.procedure;
import static com.cameraunlock.core.HostFunctions.requireZero;
import static com.cameraunlock.core.HostFunctions.size;
import static com.cameraunlock.core.HostFunctions.text;
import static com.cameraunlock.core.HostFunctions.writeWide;
import static com.cameraunlock.core.HostFunctions.wtf8;

import java.io.IOException;
import java.net.BindException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.StandardProtocolFamily;
import java.nio.ByteBuffer;
import java.nio.channels.ClosedChannelException;
import java.nio.channels.ClosedSelectorException;
import java.nio.channels.DatagramChannel;
import java.nio.channels.FileChannel;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.AccessDeniedException;
import java.nio.file.FileAlreadyExistsException;
import java.nio.file.FileSystemException;
import java.nio.file.Files;
import java.nio.file.InvalidPathException;
import java.nio.file.NoSuchFileException;
import java.nio.file.OpenOption;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.nio.file.attribute.FileTime;
import java.nio.file.attribute.PosixFilePermission;
import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.OptionalLong;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.IntPredicate;
import run.endive.runtime.HostFunction;
import run.endive.runtime.Memory;

/**
 * The imports of module "cameraunlock_win32" answered on Linux (docs/core-jar.md). The module is the
 * one Win32Host serves on Windows: its config owner, checked writer, file log, receiver and hotkey
 * poller call Win32 functions, and each is answered here with Java's own files, sockets and clocks,
 * under the contract the C++ reads: the same results, the same Win32 error codes it branches on, the
 * same layouts. Nothing here calls native code, so nothing here depends on the C library's layout.
 *
 * Where Linux cannot give what Windows does, the difference is the one stated at the call: there
 * are no share modes, so nothing here fails with ERROR_SHARING_VIOLATION, and a file's identity is
 * read by its path just after the open. A failure with no Win32 code is given an application-defined
 * one (bit 29 set, as Win32 reserves for them) whose FormatMessage text is what Java said, so its
 * reason reaches the log whole.
 *
 * Every import runs inside a call into the module, under WasmCore's lock, so one last error serves
 * GetLastError and WSAGetLastError, as in Win32Host. The receive threads keep their own.
 */
final class PosixHost implements ModuleHost {
    private static final int ERROR_FILE_NOT_FOUND = 2, ERROR_PATH_NOT_FOUND = 3, ERROR_ACCESS_DENIED = 5,
            ERROR_FILE_EXISTS = 80, ERROR_INVALID_PARAMETER = 87, ERROR_INSUFFICIENT_BUFFER = 122, ERROR_INVALID_NAME = 123,
            ERROR_NEGATIVE_SEEK = 131, ERROR_ALREADY_EXISTS = 183, ERROR_MR_MID_NOT_FOUND = 317, ERROR_INVALID_FLAGS = 1004,
            ERROR_NO_UNICODE_TRANSLATION = 1113;
    private static final int WSAEADDRINUSE = 10048, WSAEMSGSIZE = 10040;
    /** Win32's mark of an application-defined error code. */
    private static final int APPLICATION_ERROR = 0x20000000;
    private static final int SOCKET_ERROR = -1, INVALID_HANDLE = -1;
    private static final long GENERIC_READ = 0x80000000L, GENERIC_WRITE = 0x40000000L;
    private static final int CREATE_NEW = 1, CREATE_ALWAYS = 2, OPEN_EXISTING = 3;
    private static final int FILE_ATTRIBUTE_READONLY = 0x1, FILE_ATTRIBUTE_DIRECTORY = 0x10, FILE_ATTRIBUTE_NORMAL = 0x80;
    private static final int MOVEFILE_REPLACE_EXISTING = 0x1, MOVEFILE_WRITE_THROUGH = 0x8;
    private static final int CP_ACP = 0, CP_UTF8 = 65001, MB_ERR_INVALID_CHARS = 0x8, WC_ERR_INVALID_CHARS = 0x80;
    private static final int FORMAT_MESSAGE_IGNORE_INSERTS = 0x200, FORMAT_MESSAGE_FROM_SYSTEM = 0x1000;
    private static final int AF_INET = 2, SOCK_DGRAM = 2, IPPROTO_UDP = 17, FIONBIO = 0x8004667E, SIO_UDP_CONNRESET = 0x9800000C;
    /** 100 ns intervals from 1601 to 1970, which a FILETIME counts from. */
    private static final long FILETIME_1970 = 116_444_736_000_000_000L;
    /** WIN32_FIND_DATAW up to cFileName, and its two names in UTF-32 as the module lays it out (win32_host.h). */
    private static final int FIND_DATA_HEAD = 44, FIND_DATA_NAME = 260;
    /** The receive buffer of UdpReceiver::ReceiverThread. */
    private static final int RECEIVE_BUFFER = 64;
    /** kSupervisorTickMs in udp_receiver.cpp: how long a wait for a datagram lasts, so a stop is noticed. */
    private static final int WAIT_MS = 100;
    /** What GetForegroundWindow answers: the window whose keys the key source reports, which is the game's. */
    private static final int GAME_WINDOW = 1;

    /** What FormatMessage says for each Win32 code this host gives. */
    private static final Map<Integer, String> MESSAGES = Map.ofEntries(
            Map.entry(ERROR_FILE_NOT_FOUND, "The system cannot find the file specified."),
            Map.entry(ERROR_PATH_NOT_FOUND, "The system cannot find the path specified."),
            Map.entry(ERROR_ACCESS_DENIED, "Access is denied."),
            Map.entry(ERROR_FILE_EXISTS, "The file exists."),
            Map.entry(ERROR_INVALID_PARAMETER, "The parameter is incorrect."),
            Map.entry(ERROR_INSUFFICIENT_BUFFER, "The data area passed to a system call is too small."),
            Map.entry(ERROR_INVALID_NAME, "The filename, directory name, or volume label syntax is incorrect."),
            Map.entry(ERROR_NEGATIVE_SEEK, "An attempt was made to move the file pointer before the beginning of the file."),
            Map.entry(ERROR_ALREADY_EXISTS, "Cannot create a file when that file already exists."),
            Map.entry(ERROR_INVALID_FLAGS, "Invalid flags."),
            Map.entry(ERROR_NO_UNICODE_TRANSLATION, "No mapping for the Unicode character exists in the target multi-byte code page."),
            Map.entry(WSAEMSGSIZE, "A message sent on a datagram socket was larger than the internal message buffer or some other"
                    + " network limit, or the buffer used to receive a datagram into was smaller than the datagram itself."),
            Map.entry(WSAEADDRINUSE, "Only one usage of each socket address (protocol/network address/port) is normally permitted."));

    private final WasmCore core;
    /** The last error of the call the module made last; see the class comment. */
    private int lastError;
    /** The texts of the application-defined codes, by code: one per distinct text, so the list stays as short as Java's reasons. */
    private final List<String> applicationErrors = new ArrayList<>();
    private final Map<Integer, Object> handles = new HashMap<>();
    private int nextHandle = 0x100;
    private final Map<Integer, Receiving> receiving = new ConcurrentHashMap<>();
    private volatile IntPredicate keySource;

    PosixHost(WasmCore core) {
        this.core = core;
    }

    /** An open file: its channel, the path it was opened by, and its identity as read just after the open. */
    private record OpenFile(FileChannel channel, Path path, long device, long inode) {}

    /** A FindFirstFileW answer, held until FindClose. */
    private record Search() {}

    /** A socket, and the Selector its receive thread waits on while there is one. */
    private static final class Socket {
        final DatagramChannel channel;
        volatile Selector waiting;

        Socket(DatagramChannel channel) {
            this.channel = channel;
        }

        DatagramChannel channel() {
            return channel;
        }
    }

    // ---- Errors -------------------------------------------------------------------------------------

    /** The Win32 code for a failure, which the C++ branches on and FormatMessage explains. */
    private int code(Exception failure) {
        // A name Java cannot turn into the bytes of this locale's encoding (sun.jnu.encoding, ASCII under LANG=C).
        if (failure instanceof InvalidPathException invalid) {
            return applicationError(invalid.getMessage() + ", in the file name encoding " + System.getProperty("sun.jnu.encoding"));
        }
        if (failure instanceof NoSuchFileException missing) {
            Path parent = missing.getFile() == null ? null : Path.of(missing.getFile()).getParent();
            return parent == null || Files.isDirectory(parent) ? ERROR_FILE_NOT_FOUND : ERROR_PATH_NOT_FOUND;
        }
        if (failure instanceof AccessDeniedException) return ERROR_ACCESS_DENIED;
        if (failure instanceof FileAlreadyExistsException) return ERROR_FILE_EXISTS;
        String reason = failure instanceof FileSystemException system && system.getReason() != null ? system.getReason()
                : failure.getMessage() != null ? failure.getMessage() : failure.getClass().getName();
        // ENOTDIR, a part of the path that is a file, is a path Windows does not find; EPERM is a refusal.
        if (reason.equals("Not a directory")) return ERROR_PATH_NOT_FOUND;
        if (reason.equals("Operation not permitted")) return ERROR_ACCESS_DENIED;
        return applicationError(reason);
    }

    /** Whether something is at `path`, where a failure to look (EACCES) is the failure and not an absence. */
    private static boolean present(Path path) throws IOException {
        try {
            Files.readAttributes(path, BasicFileAttributes.class);
            return true;
        } catch (NoSuchFileException missing) {
            return false;
        }
    }

    private static boolean folder(Path path) throws IOException {
        try {
            return Files.readAttributes(path, BasicFileAttributes.class).isDirectory();
        } catch (NoSuchFileException missing) {
            return false;
        }
    }

    private static long[] identity(Path path) throws IOException {
        Map<String, Object> found = Files.readAttributes(path, "unix:dev,ino");
        return new long[] {(long) found.get("dev"), (long) found.get("ino")};
    }

    private int applicationError(String text) {
        int at = applicationErrors.indexOf(text);
        if (at < 0) {
            at = applicationErrors.size();
            applicationErrors.add(text);
        }
        return APPLICATION_ERROR | at;
    }

    /** The text FormatMessage gives for `code`, or null for one it does not know, as Windows gives none. */
    private String message(int code) {
        if ((code & APPLICATION_ERROR) != 0) {
            int at = code & ~APPLICATION_ERROR;
            return at < applicationErrors.size() ? applicationErrors.get(at) : null;
        }
        return MESSAGES.get(code);
    }

    private int fail(Exception failure) {
        lastError = code(failure);
        return 0;
    }

    // ---- Handles ------------------------------------------------------------------------------------

    private int open(Object what) {
        int handle = nextHandle;
        nextHandle += 4;
        handles.put(handle, what);
        return handle;
    }

    private <T> T handle(long value, Class<T> type, String function) {
        Object held = handles.get((int) value);
        if (!type.isInstance(held)) {
            throw new IllegalStateException(function + " of " + (int) value + ", which is not an open " + type.getSimpleName());
        }
        return type.cast(held);
    }

    // ---- Files --------------------------------------------------------------------------------------

    private static Path path(Memory m, long at, String function) {
        return Path.of(text(m, at, -1, function));
    }

    /** A time as a FILETIME counts it: 100 ns intervals since 1601. */
    static long fileTime(FileTime time) {
        java.time.Instant instant = time.toInstant();
        return FILETIME_1970 + instant.getEpochSecond() * 10_000_000L + instant.getNano() / 100;
    }

    private static int attributes(Path path, BasicFileAttributes found) {
        if (found.isDirectory()) return FILE_ATTRIBUTE_DIRECTORY;
        return Files.isWritable(path) ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_READONLY;
    }

    /**
     * A file this user may not write, which Windows reports as FILE_ATTRIBUTE_READONLY and will not
     * rename another file over. Linux would, since a rename asks only the folder, so the replace and
     * the move refuse it here as Windows does.
     */
    private static boolean readOnly(Path path) {
        return Files.isRegularFile(path) && !Files.isWritable(path);
    }

    /**
     * A file's last write time as the module's GetFileAttributesExW reads it, 0 for a missing file as
     * the library counts one, and empty for any other failure or a path with an unpaired surrogate,
     * which the library may read another way (ConfigWatch).
     */
    static OptionalLong lastWriteTime(String path) {
        if (path.codePoints().anyMatch(point -> point >= Character.MIN_SURROGATE && point <= Character.MAX_SURROGATE)) {
            return OptionalLong.empty();
        }
        try {
            return OptionalLong.of(fileTime(Files.getLastModifiedTime(Path.of(path))));
        } catch (NoSuchFileException missing) {
            return OptionalLong.of(0);
        } catch (IOException | InvalidPathException other) {
            return OptionalLong.empty();
        }
    }

    /** Writes the attributes, the three times and the size at `at`, as WIN32_FILE_ATTRIBUTE_DATA and the head of WIN32_FIND_DATAW have them. */
    private static void writeAttributeData(Memory m, int at, Path path, BasicFileAttributes found) {
        m.writeI32(at, attributes(path, found));
        m.writeLong(at + 4, fileTime(found.creationTime()));
        m.writeLong(at + 12, fileTime(found.lastAccessTime()));
        m.writeLong(at + 20, fileTime(found.lastModifiedTime()));
        m.writeI32(at + 28, (int) (found.size() >>> 32));
        m.writeI32(at + 32, (int) found.size());
    }

    private long createFile(Memory m, long[] a) {
        requireZero("CreateFileW", "security attributes", a[3]);
        requireZero("CreateFileW", "a template file", a[6]);
        long access = a[1] & 0xFFFFFFFFL;
        if (access == 0 || (access & ~(GENERIC_READ | GENERIC_WRITE)) != 0) {
            throw new UnsupportedOperationException("CreateFileW: access 0x" + Long.toHexString(access) + " is not GENERIC_READ or GENERIC_WRITE");
        }
        int disposition = (int) a[4];
        if ((int) a[5] != FILE_ATTRIBUTE_NORMAL) {
            throw new UnsupportedOperationException("CreateFileW: flags 0x" + Integer.toHexString((int) a[5]) + " are not FILE_ATTRIBUTE_NORMAL");
        }
        // The share mode (a[2]) has nothing to set: POSIX locks are advisory, and no open is refused for another's.
        Set<OpenOption> options = new HashSet<>();
        if ((access & GENERIC_READ) != 0) options.add(StandardOpenOption.READ);
        if ((access & GENERIC_WRITE) != 0) options.add(StandardOpenOption.WRITE);
        switch (disposition) {
            case CREATE_NEW -> options.add(StandardOpenOption.CREATE_NEW);
            case CREATE_ALWAYS -> {
                options.add(StandardOpenOption.CREATE);
                options.add(StandardOpenOption.TRUNCATE_EXISTING);
            }
            case OPEN_EXISTING -> { }
            default -> throw new UnsupportedOperationException("CreateFileW: disposition " + disposition + " is not one core uses");
        }
        try {
            Path path = path(m, a[0], "CreateFileW");
            // Windows opens no directory without FILE_FLAG_BACKUP_SEMANTICS, where Linux would open it to read.
            if (folder(path)) {
                lastError = ERROR_ACCESS_DENIED;
                return INVALID_HANDLE;
            }
            boolean existed = disposition == CREATE_ALWAYS && present(path);
            // The identity is read by the path, so it is read on both sides of the open: the same file
            // both times is the file that was opened, as GetFileInformationByHandle reads it on Windows.
            long[] before = disposition == OPEN_EXISTING ? identity(path) : null;
            FileChannel channel = FileChannel.open(path, options);
            long[] after;
            try {
                after = identity(path);
            } catch (IOException | RuntimeException failure) {
                channel.close();
                throw failure;
            }
            if (before != null && !java.util.Arrays.equals(before, after)) {
                channel.close();
                lastError = applicationError("the file was replaced while it was being opened");
                return INVALID_HANDLE;
            }
            if (disposition == CREATE_ALWAYS) lastError = existed ? ERROR_ALREADY_EXISTS : 0;
            return open(new OpenFile(channel, path, after[0], after[1]));
        } catch (IOException | InvalidPathException failure) {
            fail(failure);
            return INVALID_HANDLE;
        }
    }

    private long readFile(Memory m, long[] a) {
        requireZero("ReadFile", "an overlapped read", a[4]);
        OpenFile file = handle(a[0], OpenFile.class, "ReadFile");
        ByteBuffer buffer = ByteBuffer.allocate(size("ReadFile", a[2]));
        int got;
        try {
            got = Math.max(file.channel().read(buffer), 0);
        } catch (IOException failure) {
            if (a[3] != 0) m.writeI32((int) a[3], 0);
            return fail(failure);
        }
        m.write((int) a[1], buffer.array(), 0, got);
        if (a[3] != 0) m.writeI32((int) a[3], got);
        return 1;
    }

    private long writeFile(Memory m, long[] a) {
        requireZero("WriteFile", "an overlapped write", a[4]);
        OpenFile file = handle(a[0], OpenFile.class, "WriteFile");
        ByteBuffer buffer = ByteBuffer.wrap(m.readBytes((int) a[1], size("WriteFile", a[2])));
        try {
            while (buffer.hasRemaining()) file.channel().write(buffer);
        } catch (IOException failure) {
            if (a[3] != 0) m.writeI32((int) a[3], buffer.position());
            return fail(failure);
        }
        if (a[3] != 0) m.writeI32((int) a[3], buffer.position());
        return 1;
    }

    private long setFilePointer(Memory m, long[] a) {
        FileChannel channel = handle(a[0], OpenFile.class, "SetFilePointerEx").channel();
        long distance = m.readLong((int) a[1]);
        try {
            long from = switch ((int) a[3]) {
                case 0 -> 0;
                case 1 -> channel.position();
                case 2 -> channel.size();
                default -> throw new UnsupportedOperationException("SetFilePointerEx: method " + a[3]);
            };
            long position = from + distance;
            if (position < 0) {
                lastError = ERROR_NEGATIVE_SEEK;
                return 0;
            }
            channel.position(position);
            if (a[2] != 0) m.writeLong((int) a[2], position);
            return 1;
        } catch (IOException failure) {
            return fail(failure);
        }
    }

    private long fileInformation(Memory m, long[] a) {
        OpenFile file = handle(a[0], OpenFile.class, "GetFileInformationByHandle");
        int at = (int) a[1];
        try {
            BasicFileAttributes found = Files.readAttributes(file.path(), BasicFileAttributes.class);
            int links = (int) Files.getAttribute(file.path(), "unix:nlink");
            long size = file.channel().size();
            m.writeI32(at, attributes(file.path(), found));
            m.writeLong(at + 4, fileTime(found.creationTime()));
            m.writeLong(at + 12, fileTime(found.lastAccessTime()));
            m.writeLong(at + 20, fileTime(found.lastModifiedTime()));
            m.writeI32(at + 28, (int) file.device());
            m.writeI32(at + 32, (int) (size >>> 32));
            m.writeI32(at + 36, (int) size);
            m.writeI32(at + 40, links);
            m.writeI32(at + 44, (int) (file.inode() >>> 32));
            m.writeI32(at + 48, (int) file.inode());
            return 1;
        } catch (IOException failure) {
            return fail(failure);
        }
    }

    private long fileAttributes(Memory m, long[] a) {
        try {
            Path path = path(m, a[0], "GetFileAttributesW");
            return attributes(path, Files.readAttributes(path, BasicFileAttributes.class));
        } catch (IOException | InvalidPathException failure) {
            fail(failure);
            return -1;
        }
    }

    private long fileAttributesEx(Memory m, long[] a) {
        requireZero("GetFileAttributesExW", "a level other than GetFileExInfoStandard", a[1]);
        try {
            Path path = path(m, a[0], "GetFileAttributesExW");
            writeAttributeData(m, (int) a[2], path, Files.readAttributes(path, BasicFileAttributes.class));
            return 1;
        } catch (IOException | InvalidPathException failure) {
            return fail(failure);
        }
    }

    /** One file, named exactly: config_owner.cpp asks it only for a name with no wildcard in it. */
    private long findFirstFile(Memory m, long[] a) {
        String name = text(m, a[0], -1, "FindFirstFileW");
        String last = name.substring(name.lastIndexOf('/') + 1);
        if (last.indexOf('*') >= 0 || last.indexOf('?') >= 0) {
            throw new UnsupportedOperationException("FindFirstFileW: a wildcard, in " + name + ", is not supported on Linux");
        }
        try {
            Path path = Path.of(name);
            BasicFileAttributes found = Files.readAttributes(path, BasicFileAttributes.class);
            int at = (int) a[1];
            writeAttributeData(m, at, path, found);
            m.writeI32(at + 36, 0);
            m.writeI32(at + 40, 0);
            String file = path.getFileName() == null ? name : path.getFileName().toString();
            if (file.codePointCount(0, file.length()) >= FIND_DATA_NAME) {
                throw new IllegalStateException("FindFirstFileW: " + file + " is longer than WIN32_FIND_DATAW holds");
            }
            writeWide(m, at + FIND_DATA_HEAD, file, true);
            writeWide(m, at + FIND_DATA_HEAD + 4 * FIND_DATA_NAME, "", true);
            return open(new Search());
        } catch (IOException | InvalidPathException failure) {
            fail(failure);
            return INVALID_HANDLE;
        }
    }

    private long findClose(long[] a) {
        handle(a[0], Search.class, "FindClose");
        handles.remove((int) a[0]);
        return 1;
    }

    private long fullPathName(Memory m, long[] a) {
        String full;
        try {
            String given = text(m, a[0], -1, "GetFullPathNameW");
            full = Path.of(given).toAbsolutePath().normalize().toString();
            // Windows keeps a trailing separator, which tells the checked writer the path names a folder.
            if (given.endsWith("/") && !full.endsWith("/")) full += "/";
        } catch (InvalidPathException failure) {
            return fail(failure);
        }
        int points = full.codePointCount(0, full.length());
        // Too small: the size it needs, its NUL included, as Windows answers.
        if ((int) a[1] <= points) return points + 1;
        writeWide(m, (int) a[2], full, true);
        if (a[3] != 0) {
            int slash = full.lastIndexOf('/');
            boolean named = slash < full.length() - 1;
            m.writeI32((int) a[3], named ? (int) a[2] + 4 * full.codePointCount(0, slash + 1) : 0);
        }
        return points;
    }

    /**
     * ReplaceFileW with no backup: the replacement takes the replaced file's name in one rename, and
     * its permissions first, as Windows carries the replaced file's attributes over. They are set
     * only where they differ, so a file system with fixed permissions (FAT, exFAT) is not asked to
     * change them.
     */
    private long replaceFile(Memory m, long[] a) {
        requireZero("ReplaceFileW", "a backup file", a[2]);
        requireZero("ReplaceFileW", "flags", a[3]);
        requireZero("ReplaceFileW", "excluding", a[4]);
        requireZero("ReplaceFileW", "a reserved pointer", a[5]);
        try {
            Path replaced = path(m, a[0], "ReplaceFileW"), replacement = path(m, a[1], "ReplaceFileW");
            if (readOnly(replaced)) {
                lastError = ERROR_ACCESS_DENIED;
                return 0;
            }
            Set<PosixFilePermission> permissions = Files.getPosixFilePermissions(replaced);
            if (!permissions.equals(Files.getPosixFilePermissions(replacement))) {
                Files.setPosixFilePermissions(replacement, permissions);
            }
            Files.move(replacement, replaced, StandardCopyOption.ATOMIC_MOVE);
            return 1;
        } catch (IOException | InvalidPathException failure) {
            return fail(failure);
        }
    }

    private long moveFile(Memory m, long[] a) {
        int flags = (int) a[2];
        if ((flags & ~(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) != 0) {
            throw new UnsupportedOperationException("MoveFileExW: flags 0x" + Integer.toHexString(flags));
        }
        try {
            Path from = path(m, a[0], "MoveFileExW"), to = path(m, a[1], "MoveFileExW");
            if (!present(from)) {
                lastError = folder(from.toAbsolutePath().getParent()) ? ERROR_FILE_NOT_FOUND : ERROR_PATH_NOT_FOUND;
                return 0;
            }
            Path into = to.toAbsolutePath().getParent();
            if (!folder(into)) {
                lastError = ERROR_PATH_NOT_FOUND;
                return 0;
            }
            if ((flags & MOVEFILE_REPLACE_EXISTING) != 0) {
                if (readOnly(to)) {
                    lastError = ERROR_ACCESS_DENIED;
                    return 0;
                }
                Files.move(from, to, StandardCopyOption.ATOMIC_MOVE);
            } else {
                // A link fails on an existing name in the same step that makes it, where a rename after a
                // look would replace a file another program made between the two. The checked writer
                // counts on that refusal. A file system with no links (FAT, exFAT) fails the move.
                try {
                    Files.createLink(to, from);
                } catch (UnsupportedOperationException noLinks) {
                    lastError = applicationError("the file system of " + into + " has no hard links, which a move that keeps an existing file needs on Linux");
                    return 0;
                }
                Files.delete(from);
            }
            if ((flags & MOVEFILE_WRITE_THROUGH) != 0) {
                try (FileChannel folder = FileChannel.open(into, StandardOpenOption.READ)) {
                    folder.force(true);
                }
            }
            return 1;
        } catch (FileAlreadyExistsException exists) {
            lastError = ERROR_ALREADY_EXISTS;
            return 0;
        } catch (IOException | InvalidPathException failure) {
            return fail(failure);
        }
    }

    private long deleteFile(Memory m, long[] a) {
        try {
            Path path = path(m, a[0], "DeleteFileW");
            if (folder(path)) {
                lastError = ERROR_ACCESS_DENIED;
                return 0;
            }
            Files.delete(path);
            return 1;
        } catch (IOException | InvalidPathException failure) {
            return fail(failure);
        }
    }

    private long createDirectory(Memory m, long[] a) {
        requireZero("CreateDirectoryW", "security attributes", a[1]);
        try {
            Path path = path(m, a[0], "CreateDirectoryW");
            Path parent = path.toAbsolutePath().getParent();
            if (parent != null && !folder(parent)) {
                lastError = ERROR_PATH_NOT_FOUND;
                return 0;
            }
            Files.createDirectory(path);
            return 1;
        } catch (FileAlreadyExistsException exists) {
            lastError = ERROR_ALREADY_EXISTS;
            return 0;
        } catch (IOException | InvalidPathException failure) {
            return fail(failure);
        }
    }

    // ---- Text ---------------------------------------------------------------------------------------

    /** The ANSI code page is UTF-8 on Linux, so CP_ACP and CP_UTF8 are one here. */
    private static void requireUtf8(String function, int codePage) {
        if (codePage != CP_ACP && codePage != CP_UTF8) {
            throw new UnsupportedOperationException(function + ": code page " + codePage + " on Linux, where the ANSI code page is UTF-8");
        }
    }

    private long multiByteToWideChar(Memory m, long[] a) {
        requireUtf8("MultiByteToWideChar", (int) a[0]);
        int flags = (int) a[1];
        if ((flags & ~MB_ERR_INVALID_CHARS) != 0) {
            lastError = ERROR_INVALID_FLAGS;
            return 0;
        }
        int text = (int) a[2], count = (int) a[3], out = (int) a[4], capacity = (int) a[5];
        if (count == 0) {
            lastError = ERROR_INVALID_PARAMETER;
            return 0;
        }
        int bytes = count;
        if (count < 0) {
            bytes = 0;
            while (m.read(text + bytes) != 0) bytes++;
            bytes++;
        }
        CodingErrorAction bad = (flags & MB_ERR_INVALID_CHARS) != 0 ? CodingErrorAction.REPORT : CodingErrorAction.REPLACE;
        String decoded;
        try {
            decoded = StandardCharsets.UTF_8.newDecoder().onMalformedInput(bad).onUnmappableCharacter(bad)
                    .decode(ByteBuffer.wrap(m.readBytes(text, bytes))).toString();
        } catch (CharacterCodingException invalid) {
            lastError = ERROR_NO_UNICODE_TRANSLATION;
            return 0;
        }
        int points = decoded.codePointCount(0, decoded.length());
        if (capacity == 0) return points;
        if (capacity < points) {
            lastError = ERROR_INSUFFICIENT_BUFFER;
            return 0;
        }
        writeWide(m, out, decoded, false);
        return points;
    }

    /** UTF-8 of the module's code points; an unpaired surrogate is U+FFFD as Windows writes it, or a failure where asked. */
    private static byte[] utf8(String text, boolean refuseSurrogates) {
        java.io.ByteArrayOutputStream bytes = new java.io.ByteArrayOutputStream();
        int[] points = text.codePoints().toArray();
        for (int point : points) {
            if (point >= Character.MIN_SURROGATE && point <= Character.MAX_SURROGATE) {
                if (refuseSurrogates) return null;
                point = 0xFFFD;
            }
            bytes.writeBytes(new String(Character.toChars(point)).getBytes(StandardCharsets.UTF_8));
        }
        return bytes.toByteArray();
    }

    private long wideCharToMultiByte(Memory m, long[] a) {
        requireUtf8("WideCharToMultiByte", (int) a[0]);
        int flags = (int) a[1];
        // As Windows: UTF-8 takes no flag but WC_ERR_INVALID_CHARS, and no default character.
        if ((flags & ~WC_ERR_INVALID_CHARS) != 0) {
            lastError = ERROR_INVALID_FLAGS;
            return 0;
        }
        if (a[6] != 0 || a[7] != 0 || (int) a[3] == 0) {
            lastError = ERROR_INVALID_PARAMETER;
            return 0;
        }
        int count = (int) a[3];
        String text = count < 0 ? text(m, a[2], -1, "WideCharToMultiByte") + '\0' : text(m, a[2], count, "WideCharToMultiByte");
        byte[] bytes = utf8(text, (flags & WC_ERR_INVALID_CHARS) != 0);
        if (bytes == null) {
            lastError = ERROR_NO_UNICODE_TRANSLATION;
            return 0;
        }
        int capacity = (int) a[5];
        if (capacity == 0) return bytes.length;
        if (capacity < bytes.length) {
            lastError = ERROR_INSUFFICIENT_BUFFER;
            return 0;
        }
        m.write((int) a[4], bytes);
        return bytes.length;
    }

    /** CompareStringOrdinal over UTF-16 code units, as Windows compares; with ignore case, each unit's simple upper case. */
    private long compareStringOrdinal(Memory m, long[] a) {
        String first = text(m, a[0], (int) a[1], "CompareStringOrdinal");
        String second = text(m, a[2], (int) a[3], "CompareStringOrdinal");
        boolean ignoreCase = a[4] != 0;
        for (int i = 0; i < Math.min(first.length(), second.length()); i++) {
            char x = first.charAt(i), y = second.charAt(i);
            if (ignoreCase) {
                x = Character.toUpperCase(x);
                y = Character.toUpperCase(y);
            }
            if (x != y) return x < y ? 1 : 3;
        }
        return Integer.compare(first.length(), second.length()) + 2;
    }

    private long formatMessage(Memory m, long[] a, boolean wideText) {
        String function = wideText ? "FormatMessageW" : "FormatMessageA";
        requireZero(function, "a message source", a[1]);
        requireZero(function, "inserts", a[6]);
        if (((int) a[0] & ~FORMAT_MESSAGE_IGNORE_INSERTS) != FORMAT_MESSAGE_FROM_SYSTEM) {
            throw new UnsupportedOperationException(function + ": flags 0x" + Integer.toHexString((int) a[0]));
        }
        String message = message((int) a[2]);
        if (message == null) {
            lastError = ERROR_MR_MID_NOT_FOUND;
            return 0;
        }
        message += "\r\n";
        int capacity = size(function, a[5]);
        if (wideText) {
            int points = message.codePointCount(0, message.length());
            if (capacity < points + 1) {
                lastError = ERROR_INSUFFICIENT_BUFFER;
                return 0;
            }
            writeWide(m, (int) a[4], message, true);
            return points;
        }
        byte[] bytes = message.getBytes(StandardCharsets.UTF_8);
        if (capacity < bytes.length + 1) {
            lastError = ERROR_INSUFFICIENT_BUFFER;
            return 0;
        }
        m.write((int) a[4], bytes);
        m.writeByte((int) a[4] + bytes.length, (byte) 0);
        return bytes.length;
    }

    // ---- Sockets ------------------------------------------------------------------------------------

    private long socket(long[] a) {
        if ((int) a[0] != AF_INET || (int) a[1] != SOCK_DGRAM || (int) a[2] != IPPROTO_UDP) {
            throw new UnsupportedOperationException("socket(" + a[0] + ", " + a[1] + ", " + a[2] + "): only UDP over IPv4");
        }
        try {
            return open(new Socket(DatagramChannel.open(StandardProtocolFamily.INET)));
        } catch (IOException failure) {
            lastError = code(failure);
            return INVALID_HANDLE;
        }
    }

    private long ioctlsocket(Memory m, long[] a) {
        if ((int) a[1] != FIONBIO) throw new UnsupportedOperationException("ioctlsocket: command 0x" + Integer.toHexString((int) a[1]));
        try {
            handle(a[0], Socket.class, "ioctlsocket").channel().configureBlocking(m.readInt((int) a[2]) == 0);
            return 0;
        } catch (IOException failure) {
            lastError = code(failure);
            return SOCKET_ERROR;
        }
    }

    /**
     * SIO_UDP_CONNRESET only, which turns off Windows' report of an ICMP port unreachable on the
     * next receive. Linux reports one to an unconnected UDP socket only where IP_RECVERR asks for
     * it, and this socket never connects, so there is nothing to turn off.
     */
    private long wsaIoctl(Memory m, long[] a) {
        requireZero("WSAIoctl", "an overlapped call", a[7]);
        requireZero("WSAIoctl", "a completion routine", a[8]);
        if ((int) a[1] != SIO_UDP_CONNRESET) throw new UnsupportedOperationException("WSAIoctl: code 0x" + Integer.toHexString((int) a[1]));
        handle(a[0], Socket.class, "WSAIoctl");
        if (a[6] != 0) m.writeI32((int) a[6], 0);
        return 0;
    }

    private long bind(Memory m, long[] a) {
        DatagramChannel channel = handle(a[0], Socket.class, "bind").channel();
        int at = (int) a[1];
        if ((int) a[2] != 16 || m.readShort(at) != AF_INET) {
            throw new UnsupportedOperationException("bind: an address that is not a sockaddr_in");
        }
        int port = (m.read(at + 2) & 0xFF) << 8 | m.read(at + 3) & 0xFF;
        try {
            channel.bind(new InetSocketAddress(InetAddress.getByAddress(m.readBytes(at + 4, 4)), port));
            return 0;
        } catch (BindException taken) {
            // EADDRINUSE, EADDRNOTAVAIL and EACCES all come as a BindException, told apart only by the
            // message, which is glibc's strerror text or musl's.
            String why = taken.getMessage();
            lastError = "Address already in use".equals(why) || "Address in use".equals(why) ? WSAEADDRINUSE : code(taken);
            return SOCKET_ERROR;
        } catch (IOException failure) {
            lastError = code(failure);
            return SOCKET_ERROR;
        }
    }

    /**
     * A channel registered with a Selector keeps its port until the Selector lets the registration
     * go, which a receive thread's next select does, up to WAIT_MS later. Windows lets the port go in
     * closesocket, and a start right after a stop binds it again, so the registration is let go here.
     */
    private long closesocket(long[] a) {
        Socket socket = handle(a[0], Socket.class, "closesocket");
        handles.remove((int) a[0]);
        try {
            socket.channel().close();
            Selector waiting = socket.waiting;
            // A close waits out a select in progress and lets the registration go before it returns.
            if (waiting != null) waiting.close();
            return 0;
        } catch (IOException failure) {
            lastError = code(failure);
            return SOCKET_ERROR;
        }
    }

    /** One receive thread: StopReceiving sets `stopped`, and the thread delivers nothing after it. */
    private static final class Receiving {
        volatile boolean stopped;
    }

    /**
     * UdpReceiver::ReceiverThread's wait and recvfrom, on a thread of its own: a Selector with a
     * WAIT_MS timeout, so StopReceiving is noticed, and each datagram handed to the receiver at
     * `receiver`. Java cuts a datagram longer than the buffer down without a word, so it is read
     * whole and one over RECEIVE_BUFFER is handed over as Windows reports it, as WSAEMSGSIZE.
     */
    private void receiveOn(int socketValue, int receiver) {
        Socket socket = handle(socketValue, Socket.class, "ReceiveOn");
        Receiving mine = new Receiving();
        Receiving before = receiving.put(receiver, mine);
        if (before != null) before.stopped = true;
        Selector selector;
        try {
            selector = Selector.open();
            socket.channel().register(selector, SelectionKey.OP_READ);
        } catch (IOException failure) {
            throw new IllegalStateException("ReceiveOn could not wait on the socket", failure);
        }
        socket.waiting = selector;
        Thread thread = new Thread(() -> receiveLoop(socket.channel(), selector, receiver, mine), "cameraunlock-core receive");
        thread.setDaemon(true);
        thread.start();
    }

    private void receiveLoop(DatagramChannel channel, Selector waiting, int receiver, Receiving mine) {
        String why = null;
        Throwable thrown = null;
        try (Selector selector = waiting) {
            ByteBuffer buffer = ByteBuffer.allocate(65536);
            byte[] datagram = new byte[RECEIVE_BUFFER], from = new byte[16];
            while (!mine.stopped && !core.dead()) {
                int ready = selector.select(WAIT_MS);
                if (mine.stopped) return;
                // A closed channel's registration is gone, and a select would wait on nothing from then on.
                if (!channel.isOpen()) throw new ClosedChannelException();
                if (ready == 0) continue;
                selector.selectedKeys().clear();
                int bytes, error = 0;
                synchronized (core) {
                    if (mine.stopped) return;
                    buffer.clear();
                    InetSocketAddress sender;
                    try {
                        sender = (InetSocketAddress) channel.receive(buffer);
                    } catch (ClosedChannelException closed) {
                        throw closed;
                    } catch (IOException failure) {
                        sender = null;
                        error = code(failure);
                    }
                    if (sender == null && error == 0) continue;
                    if (error != 0) {
                        bytes = SOCKET_ERROR;
                    } else if (buffer.position() > RECEIVE_BUFFER) {
                        bytes = SOCKET_ERROR;
                        error = WSAEMSGSIZE;
                    } else {
                        bytes = buffer.position();
                    }
                    java.util.Arrays.fill(datagram, (byte) 0);
                    System.arraycopy(buffer.array(), 0, datagram, 0, Math.min(buffer.position(), RECEIVE_BUFFER));
                    java.util.Arrays.fill(from, (byte) 0);
                    if (sender != null) {
                        from[0] = AF_INET;
                        from[2] = (byte) (sender.getPort() >> 8);
                        from[3] = (byte) sender.getPort();
                        System.arraycopy(sender.getAddress().getAddress(), 0, from, 4, 4);
                    }
                    core.received(receiver, datagram, bytes, error, from);
                }
                // As Win32Host: a socket that fails every read would otherwise take the module's lock in
                // a loop, and a datagram too long is one dropped, not a failing socket.
                if (bytes == SOCKET_ERROR && error != WSAEMSGSIZE) Thread.sleep(WasmCore.TICK_MS);
            }
            if (mine.stopped || core.dead()) return;
        } catch (ClosedChannelException | ClosedSelectorException closed) {
            if (mine.stopped || core.dead()) return;
            why = "the socket was closed while it was being received on";
        } catch (Throwable failure) {
            if (core.dead()) return;
            why = "the receive thread failed: " + failure;
            thrown = failure;
        }
        synchronized (core) {
            if (!mine.stopped && !core.dead()) core.receiveFailed(receiver, why);
        }
        if (thrown instanceof Error error) throw error;
    }

    // ---- Defaults.ini -------------------------------------------------------------------------------

    /** defaults_location.cpp's probe for a native runtime, as the C# probe reads it there: HOME and XDG_CONFIG_HOME. */
    private static String probeDefaults() {
        StringBuilder lines = new StringBuilder();
        line(lines, "platform", "native".getBytes(StandardCharsets.US_ASCII));
        for (String[] variable : new String[][] {{"home", "HOME"}, {"xdg_config_home", "XDG_CONFIG_HOME"}}) {
            String value = System.getenv(variable[1]);
            if (value != null && !value.isEmpty()) line(lines, variable[0], wtf8(value));
        }
        return lines.toString();
    }

    // ---- ModuleHost ---------------------------------------------------------------------------------

    @Override
    public ConfigWatch.Times writeTimes() {
        return PosixHost::lastWriteTime;
    }

    @Override
    public void keySource(IntPredicate virtualKeyDown) {
        if (virtualKeyDown == null) throw new IllegalArgumentException("the key source is null");
        keySource = virtualKeyDown;
    }

    @Override
    public void requireKeySource() {
        if (keySource == null) {
            throw new IllegalStateException("the hotkeys were started on " + System.getProperty("os.name")
                    + " with no key source: core cannot read the keys there, and the mod gives them with CameraUnlock.keySource");
        }
    }

    @Override
    public void requireWindowCalls() {
        throw new UnsupportedOperationException("cameraunlock-core reaches the game's window on Windows only, and this is "
                + System.getProperty("os.name"));
    }

    private static UnsupportedOperationException noWindows(String function) {
        return new UnsupportedOperationException(function + ": there are no Windows windows on " + System.getProperty("os.name"));
    }

    @Override
    public HostFunction[] functions() {
        return new HostFunction[] {
            function("PosixPaths", 0, (m, a) -> 1),
            function("GetLastError", 0, (m, a) -> lastError),
            function("WSAGetLastError", 0, (m, a) -> lastError),
            procedure("SetLastError", 1, (m, a) -> lastError = (int) a[0]),

            function("CreateFileW", 7, this::createFile),
            function("ReadFile", 5, this::readFile),
            function("WriteFile", 5, this::writeFile),
            function("SetFilePointerEx", 4, this::setFilePointer),
            function("FlushFileBuffers", 1, (m, a) -> {
                try {
                    handle(a[0], OpenFile.class, "FlushFileBuffers").channel().force(true);
                    return 1;
                } catch (IOException failure) {
                    return fail(failure);
                }
            }),
            function("CloseHandle", 1, (m, a) -> {
                FileChannel channel = handle(a[0], OpenFile.class, "CloseHandle").channel();
                handles.remove((int) a[0]);
                try {
                    channel.close();
                    return 1;
                } catch (IOException failure) {
                    return fail(failure);
                }
            }),
            function("GetFileInformationByHandle", 2, this::fileInformation),
            function("GetFileAttributesW", 1, this::fileAttributes),
            function("GetFileAttributesExW", 3, this::fileAttributesEx),
            function("FindFirstFileW", 2, this::findFirstFile),
            function("FindClose", 1, (m, a) -> findClose(a)),
            function("GetFullPathNameW", 4, this::fullPathName),
            function("ReplaceFileW", 6, this::replaceFile),
            function("MoveFileExW", 3, this::moveFile),
            function("DeleteFileW", 1, this::deleteFile),
            function("CreateDirectoryW", 2, this::createDirectory),

            function("GetACP", 0, (m, a) -> CP_UTF8),
            function("MultiByteToWideChar", 6, this::multiByteToWideChar),
            function("WideCharToMultiByte", 8, this::wideCharToMultiByte),
            function("CompareStringOrdinal", 5, this::compareStringOrdinal),

            // A count of nanoseconds at a frequency of 10^9, which is what System.nanoTime is.
            function("QueryPerformanceCounter", 1, (m, a) -> {
                m.writeLong((int) a[0], System.nanoTime());
                return 1;
            }),
            function("QueryPerformanceFrequency", 1, (m, a) -> {
                m.writeLong((int) a[0], 1_000_000_000L);
                return 1;
            }),
            procedure("GetLocalTime", 1, (m, a) -> {
                LocalDateTime now = LocalDateTime.now();
                int at = (int) a[0];
                m.writeShort(at, (short) now.getYear());
                m.writeShort(at + 2, (short) now.getMonthValue());
                m.writeShort(at + 4, (short) (now.getDayOfWeek().getValue() % 7));
                m.writeShort(at + 6, (short) now.getDayOfMonth());
                m.writeShort(at + 8, (short) now.getHour());
                m.writeShort(at + 10, (short) now.getMinute());
                m.writeShort(at + 12, (short) now.getSecond());
                m.writeShort(at + 14, (short) (now.getNano() / 1_000_000));
            }),
            function("GetCurrentProcessId", 0, (m, a) -> (int) ProcessHandle.current().pid()),
            procedure("Sleep", 1, (m, a) -> {
                try {
                    Thread.sleep(a[0] & 0xFFFFFFFFL);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("interrupted in Sleep", interrupted);
                }
            }),
            function("FormatMessageW", 7, (m, a) -> formatMessage(m, a, true)),
            function("FormatMessageA", 7, (m, a) -> formatMessage(m, a, false)),

            // The key source answers for the game's own window, which has keys only while it has the
            // focus, so the foreground window is always the game's.
            // hotkeysStart has checked there is a key source (requireKeySource).
            function("GetAsyncKeyState", 1, (m, a) -> keySource.test((int) a[0]) ? (short) 0x8000 : 0),
            function("GetForegroundWindow", 0, (m, a) -> GAME_WINDOW),
            function("GetWindowThreadProcessId", 2, (m, a) -> {
                if ((int) a[0] != GAME_WINDOW) throw new IllegalStateException("GetWindowThreadProcessId of a window GetForegroundWindow did not answer");
                if (a[1] != 0) m.writeI32((int) a[1], (int) ProcessHandle.current().pid());
                return 1;
            }),
            function("IsWindow", 1, (m, a) -> { throw noWindows("IsWindow"); }),
            function("IsIconic", 1, (m, a) -> { throw noWindows("IsIconic"); }),
            function("IsZoomed", 1, (m, a) -> { throw noWindows("IsZoomed"); }),
            function("GetWindowLongW", 2, (m, a) -> { throw noWindows("GetWindowLongW"); }),
            function("GetWindowRect", 2, (m, a) -> { throw noWindows("GetWindowRect"); }),
            function("MonitorFromWindow", 2, (m, a) -> { throw noWindows("MonitorFromWindow"); }),
            function("GetMonitorInfoW", 2, (m, a) -> { throw noWindows("GetMonitorInfoW"); }),
            function("SetWindowPos", 7, (m, a) -> { throw noWindows("SetWindowPos"); }),

            function("WSAStartup", 2, (m, a) -> 0),
            function("WSACleanup", 0, (m, a) -> 0),
            function("socket", 3, (m, a) -> socket(a)),
            function("ioctlsocket", 3, this::ioctlsocket),
            function("WSAIoctl", 9, this::wsaIoctl),
            function("bind", 3, this::bind),
            function("closesocket", 1, (m, a) -> closesocket(a)),
            procedure("ReceiveOn", 2, (m, a) -> receiveOn((int) a[0], (int) a[1])),
            procedure("StopReceiving", 1, (m, a) -> {
                Receiving stopped = receiving.remove((int) a[0]);
                if (stopped != null) stopped.stopped = true;
            }),

            function("ProbeDefaults", 2, (m, a) -> answer(m, probeDefaults(), (int) a[0], (int) a[1])),
            function("WineDosFileName", 3, (m, a) -> {
                throw new IllegalStateException("WineDosFileName on " + System.getProperty("os.name") + ", where the probe answered platform=native");
            }),
        };
    }
}
