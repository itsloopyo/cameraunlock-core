using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Replaces a file with new bytes only while it still holds the bytes the caller built
    /// them from. The C++ twin is <c>cameraunlock::WriteFileChecked</c>. Windows only.
    /// </summary>
    public static class CheckedFileWriter
    {
        private const int HResultFileExists = unchecked((int)0x80070050);
        private const int HResultAlreadyExists = unchecked((int)0x800700B7);
        private const int HResultUnableToMoveReplacement = unchecked((int)0x80070498);
        private const int HResultUnableToMoveReplacement2 = unchecked((int)0x80070499);
        private const uint GenericWrite = 0x40000000;
        private const uint CreateNew = 1;
        private const uint FileAttributeNormal = 0x80;
        private const uint InvalidFileAttributes = 0xFFFFFFFF;
        private const int ErrorWriteFault = 29;

        /// <summary>
        /// Writes <paramref name="candidate"/> to <paramref name="path"/> if the file there
        /// still holds <paramref name="expected"/>, or is still absent when
        /// <paramref name="expected"/> is null.
        /// <para>
        /// The target is read, bytes and file identity, and compared with
        /// <paramref name="expected"/>. The candidate goes into a new temporary beside it,
        /// named <c>&lt;file name&gt;.&lt;32 hex digits&gt;.tmp</c> and created only if no file
        /// has that name, and is flushed to disk and closed. The target is then read again, and
        /// the write goes ahead only if its bytes still match and it is still the same file.
        /// An existing target is swapped for the temporary with <see cref="File.Replace(string,
        /// string, string)"/>, which keeps the target's attributes, and no backup is made. An
        /// absent one is created by renaming the temporary with <see cref="File.Move"/>, which
        /// fails rather than overwrite a file that appeared after the check.
        /// </para>
        /// <para>
        /// <see cref="File.Replace(string, string, string)"/> failing with
        /// ERROR_UNABLE_TO_MOVE_REPLACEMENT or <c>_2</c> means Windows stopped partway and can
        /// leave nothing at the target path. When nothing is found there, the writer finishes the
        /// job with the same <see cref="File.Move"/>, which still refuses to overwrite, and a move
        /// that succeeds is <see cref="CheckedWriteOutcome.Committed"/>. Otherwise it throws with
        /// <see cref="CheckedWriteException.OutcomeUncertain"/> set.
        /// </para>
        /// <para>
        /// The target is never opened for writing, truncated or deleted. A read-only target
        /// fails with the error Windows gives; its attribute is left alone.
        /// </para>
        /// <para>
        /// On a conflict or a failure the temporary is deleted by the exact name this call
        /// created, never by pattern, so an unrelated <c>.tmp</c> or <c>.bak</c> beside the
        /// target is not touched. The one exception is <see
        /// cref="CheckedWriteException.OutcomeUncertain"/>, where the temporary may be the only
        /// copy left and stays.
        /// </para>
        /// <para>
        /// The final check and the replacement are two operations, so a program that writes the
        /// target in between is overwritten. This is not compare-and-swap. The caller serializes
        /// its own writes to one file; this class holds no lock.
        /// </para>
        /// </summary>
        /// <param name="path">The file to write.</param>
        /// <param name="expected">The bytes the caller read from the file, or null if it read
        /// no file there.</param>
        /// <param name="candidate">The new contents.</param>
        /// <returns><see cref="CheckedWriteOutcome.Committed"/>, or the conflict that stopped
        /// the write. Nothing is left on disk after a conflict.</returns>
        /// <exception cref="CheckedWriteException">A step failed. Its inner exception is the
        /// original error.</exception>
        /// <exception cref="PlatformNotSupportedException">Not running on Windows.</exception>
#if NULLABLE_ENABLED
        public static CheckedWriteOutcome Write(string path, byte[]? expected, byte[] candidate)
#else
        public static CheckedWriteOutcome Write(string path, byte[] expected, byte[] candidate)
#endif
        {
            return Write(path, expected, candidate, null, null);
        }

#if NULLABLE_ENABLED
        internal static CheckedWriteOutcome Write(
            string path, byte[]? expected, byte[] candidate, Action<CheckedWriteStep, string>? beforeStep)
#else
        internal static CheckedWriteOutcome Write(
            string path, byte[] expected, byte[] candidate, Action<CheckedWriteStep, string> beforeStep)
#endif
        {
            return Write(path, expected, candidate, beforeStep, null);
        }

        /// <summary>
        /// <see cref="Write(string, byte[], byte[])"/> with a hook run before each step, given
        /// the step and the path it acts on. A test throws from it to fail that step, or
        /// changes the files to race it. <paramref name="beforeClose"/> is given the temporary's
        /// handle just before it is closed, so a test can make the close itself fail.
        /// </summary>
#if NULLABLE_ENABLED
        internal static CheckedWriteOutcome Write(
            string path, byte[]? expected, byte[] candidate, Action<CheckedWriteStep, string>? beforeStep,
            Action<SafeFileHandle>? beforeClose)
#else
        internal static CheckedWriteOutcome Write(
            string path, byte[] expected, byte[] candidate, Action<CheckedWriteStep, string> beforeStep,
            Action<SafeFileHandle> beforeClose)
#endif
        {
            if (path == null) throw new ArgumentNullException(nameof(path));
            if (candidate == null) throw new ArgumentNullException(nameof(candidate));
            if (path.Length == 0) throw new ArgumentException("The path is empty.", nameof(path));
            if (Environment.OSVersion.Platform != PlatformID.Win32NT)
            {
                throw new PlatformNotSupportedException("CheckedFileWriter runs on Windows only.");
            }

            string target = Path.GetFullPath(path);
            string name = Path.GetFileName(target);
            string directory = Path.GetDirectoryName(target);
            if (string.IsNullOrEmpty(name) || string.IsNullOrEmpty(directory))
            {
                throw new ArgumentException("'" + path + "' does not name a file.", nameof(path));
            }
            string temporary = Path.Combine(directory, name + "." + Guid.NewGuid().ToString("N") + ".tmp");

            return new Attempt(target, temporary, beforeStep, beforeClose).Run(expected, candidate);
        }

        private struct FileIdentity
        {
            public uint Volume;
            public uint IndexHigh;
            public uint IndexLow;

            public bool SameAs(FileIdentity other)
            {
                return Volume == other.Volume && IndexHigh == other.IndexHigh && IndexLow == other.IndexLow;
            }
        }

        private sealed class Snapshot
        {
            public static readonly Snapshot Absent = new Snapshot(false, new byte[0], default(FileIdentity));

            public readonly bool Exists;
            public readonly byte[] Bytes;
            public readonly FileIdentity Identity;

            public Snapshot(bool exists, byte[] bytes, FileIdentity identity)
            {
                Exists = exists;
                Bytes = bytes;
                Identity = identity;
            }
        }

        private sealed class Attempt
        {
            private readonly string _target;
            private readonly string _temporary;
#if NULLABLE_ENABLED
            private readonly Action<CheckedWriteStep, string>? _beforeStep;
            private readonly Action<SafeFileHandle>? _beforeClose;
            private SafeFileHandle? _handle;
#else
            private readonly Action<CheckedWriteStep, string> _beforeStep;
            private readonly Action<SafeFileHandle> _beforeClose;
            private SafeFileHandle _handle;
#endif
            private bool _created;

#if NULLABLE_ENABLED
            public Attempt(
                string target, string temporary, Action<CheckedWriteStep, string>? beforeStep,
                Action<SafeFileHandle>? beforeClose)
#else
            public Attempt(
                string target, string temporary, Action<CheckedWriteStep, string> beforeStep,
                Action<SafeFileHandle> beforeClose)
#endif
            {
                _target = target;
                _temporary = temporary;
                _beforeStep = beforeStep;
                _beforeClose = beforeClose;
            }

#if NULLABLE_ENABLED
            public CheckedWriteOutcome Run(byte[]? expected, byte[] candidate)
#else
            public CheckedWriteOutcome Run(byte[] expected, byte[] candidate)
#endif
            {
                Snapshot first;
                try
                {
                    first = Read(CheckedWriteStep.ReadTarget);
                }
                catch (Exception e)
                {
                    throw Failed(CheckedWriteStep.ReadTarget, e, false, null);
                }

                CheckedWriteOutcome outcome = Compare(expected, first, first);
                if (outcome != CheckedWriteOutcome.Committed) return outcome;

                CheckedWriteStep step = CheckedWriteStep.CreateTemporary;
                bool creating = false;
                try
                {
                    Before(step, _temporary);
                    SafeFileHandle handle = CreateFileW(
                        _temporary, GenericWrite, 0, IntPtr.Zero, CreateNew, FileAttributeNormal, IntPtr.Zero);
                    if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
                    _handle = handle;
                    _created = true;

                    step = CheckedWriteStep.WriteTemporary;
                    Before(step, _temporary);
                    WriteAll(handle, candidate);

                    step = CheckedWriteStep.FlushTemporary;
                    Before(step, _temporary);
                    if (!FlushFileBuffers(handle)) throw new Win32Exception(Marshal.GetLastWin32Error());

                    step = CheckedWriteStep.CloseTemporary;
                    Before(step, _temporary);
                    if (_beforeClose != null) _beforeClose(handle);
                    _handle = null;
                    Close(handle);

                    step = CheckedWriteStep.RecheckTarget;
                    Snapshot second = Read(step);
                    outcome = Compare(expected, first, second);
                    if (outcome == CheckedWriteOutcome.Committed)
                    {
                        step = CheckedWriteStep.Commit;
                        creating = !second.Exists;
                        Before(step, _target);
                        if (creating)
                        {
                            File.Move(_temporary, _target);
                        }
                        else
                        {
                            File.Replace(_temporary, _target, null);
                        }
                        return CheckedWriteOutcome.Committed;
                    }
                }
                catch (Exception e)
                {
                    int hresult = Marshal.GetHRForException(e);
                    if (step == CheckedWriteStep.Commit && creating
                        && (hresult == HResultAlreadyExists || hresult == HResultFileExists))
                    {
                        outcome = CheckedWriteOutcome.TargetAppeared;
                    }
                    else
                    {
                        bool uncertain = step == CheckedWriteStep.Commit && !creating
                            && (hresult == HResultUnableToMoveReplacement || hresult == HResultUnableToMoveReplacement2);
                        // File.Replace can stop with the target already deleted or renamed. Left like
                        // that, the next launch finds no file and writes defaults over the user's values.
                        if (uncertain && GetFileAttributesW(_target) == InvalidFileAttributes)
                        {
                            try
                            {
                                File.Move(_temporary, _target);
                            }
                            catch (Exception moveError)
                            {
                                throw Failed(step, e, true, moveError);
                            }
                            return CheckedWriteOutcome.Committed;
                        }
                        throw Failed(step, e, uncertain, null);
                    }
                }

                try
                {
                    Before(CheckedWriteStep.RemoveTemporary, _temporary);
                    File.Delete(_temporary);
                }
                catch (Exception e)
                {
                    throw new CheckedWriteException(
                        "Writing " + _target + " stopped because the target changed (" + outcome
                            + "), and its temporary " + _temporary + " could not be deleted: " + e.Message,
                        CheckedWriteStep.RemoveTemporary, _target, _temporary, false, false, e, null, null);
                }
                return outcome;
            }

            private void Before(CheckedWriteStep step, string path)
            {
                if (_beforeStep != null) _beforeStep(step, path);
            }

            private static void WriteAll(SafeFileHandle handle, byte[] bytes)
            {
                GCHandle pin = GCHandle.Alloc(bytes, GCHandleType.Pinned);
                try
                {
                    int offset = 0;
                    while (offset < bytes.Length)
                    {
                        int chunk = System.Math.Min(bytes.Length - offset, 1 << 20);
                        uint written;
                        if (!WriteFile(handle, Marshal.UnsafeAddrOfPinnedArrayElement(bytes, offset), (uint)chunk, out written, IntPtr.Zero))
                        {
                            throw new Win32Exception(Marshal.GetLastWin32Error());
                        }
                        // A synchronous WriteFile to a file either writes everything or fails; a zero
                        // count here would otherwise loop forever.
                        if (written == 0) throw new Win32Exception(ErrorWriteFault);
                        offset += (int)written;
                    }
                }
                finally
                {
                    pin.Free();
                }
            }

            // SafeHandle drops CloseHandle's result, so the handle is taken out of it and closed here.
            private static void Close(SafeFileHandle handle)
            {
                IntPtr raw = handle.DangerousGetHandle();
                handle.SetHandleAsInvalid();
                if (!CloseHandle(raw)) throw new Win32Exception(Marshal.GetLastWin32Error());
            }

#if NULLABLE_ENABLED
            private static CheckedWriteOutcome Compare(byte[]? expected, Snapshot first, Snapshot current)
#else
            private static CheckedWriteOutcome Compare(byte[] expected, Snapshot first, Snapshot current)
#endif
            {
                if (expected == null)
                {
                    return current.Exists ? CheckedWriteOutcome.TargetAppeared : CheckedWriteOutcome.Committed;
                }
                if (!current.Exists) return CheckedWriteOutcome.TargetMissing;
                if (!SameBytes(expected, current.Bytes)) return CheckedWriteOutcome.TargetChanged;
                if (!current.Identity.SameAs(first.Identity)) return CheckedWriteOutcome.TargetReplaced;
                return CheckedWriteOutcome.Committed;
            }

            private Snapshot Read(CheckedWriteStep step)
            {
                Before(step, _target);
                FileStream stream;
                try
                {
                    stream = new FileStream(_target, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                }
                catch (FileNotFoundException)
                {
                    return Snapshot.Absent;
                }
                catch (DirectoryNotFoundException)
                {
                    return Snapshot.Absent;
                }

                using (stream)
                {
                    ByHandleFileInformation information;
                    if (!GetFileInformationByHandle(stream.SafeFileHandle, out information))
                    {
                        throw new Win32Exception(Marshal.GetLastWin32Error());
                    }
                    var identity = new FileIdentity
                    {
                        Volume = information.VolumeSerialNumber,
                        IndexHigh = information.FileIndexHigh,
                        IndexLow = information.FileIndexLow,
                    };

                    using (var bytes = new MemoryStream())
                    {
                        var buffer = new byte[4096];
                        int read;
                        while ((read = stream.Read(buffer, 0, buffer.Length)) > 0)
                        {
                            bytes.Write(buffer, 0, read);
                        }
                        return new Snapshot(true, bytes.ToArray(), identity);
                    }
                }
            }

#if NULLABLE_ENABLED
            private CheckedWriteException Failed(
                CheckedWriteStep step, Exception error, bool uncertain, Exception? completionError)
#else
            private CheckedWriteException Failed(
                CheckedWriteStep step, Exception error, bool uncertain, Exception completionError)
#endif
            {
#if NULLABLE_ENABLED
                Exception? closeError = null;
                Exception? removeError = null;
#else
                Exception closeError = null;
                Exception removeError = null;
#endif
                if (_handle != null)
                {
                    SafeFileHandle handle = _handle;
                    _handle = null;
                    try
                    {
                        Close(handle);
                    }
                    catch (Exception e)
                    {
                        closeError = e;
                    }
                }

                bool removed = false;
                if (_created && !uncertain)
                {
                    try
                    {
                        Before(CheckedWriteStep.RemoveTemporary, _temporary);
                        File.Delete(_temporary);
                        removed = true;
                    }
                    catch (Exception e)
                    {
                        removeError = e;
                    }
                }

                string message = "Writing " + _target + " failed at " + step + ": " + error.Message;
                if (uncertain)
                {
                    message += " Windows did not finish the replacement, so the target may be missing or renamed."
                        + " The new contents are in " + _temporary + ", left in place.";
                }
                if (completionError != null)
                {
                    message += " Moving them into place failed: " + completionError.Message;
                }
                if (closeError != null)
                {
                    message += " Closing its temporary also failed: " + closeError.Message;
                }
                if (removeError != null)
                {
                    message += " Its temporary " + _temporary + " could not be deleted: " + removeError.Message;
                }

                return new CheckedWriteException(
                    message, step, _target, _created ? _temporary : null, removed, uncertain, error,
                    removeError ?? closeError, completionError);
            }
        }

        private static bool SameBytes(byte[] a, byte[] b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++)
            {
                if (a[i] != b[i]) return false;
            }
            return true;
        }

#pragma warning disable 0649
        [StructLayout(LayoutKind.Sequential)]
        private struct ByHandleFileInformation
        {
            public uint FileAttributes;
            public uint CreationTimeLow;
            public uint CreationTimeHigh;
            public uint LastAccessTimeLow;
            public uint LastAccessTimeHigh;
            public uint LastWriteTimeLow;
            public uint LastWriteTimeHigh;
            public uint VolumeSerialNumber;
            public uint FileSizeHigh;
            public uint FileSizeLow;
            public uint NumberOfLinks;
            public uint FileIndexHigh;
            public uint FileIndexLow;
        }
#pragma warning restore 0649

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetFileInformationByHandle(SafeFileHandle file, out ByHandleFileInformation information);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool FlushFileBuffers(SafeFileHandle file);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(
            string name, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool WriteFile(
            SafeFileHandle file, IntPtr buffer, uint count, out uint written, IntPtr overlapped);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool CloseHandle(IntPtr handle);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern uint GetFileAttributesW(string name);
    }
}
