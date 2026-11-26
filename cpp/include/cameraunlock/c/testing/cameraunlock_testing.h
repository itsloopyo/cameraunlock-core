/* Test-only entry points of the C interface. They are exported by CameraUnlockCoreTesting.dll,
 * which core's tests and the pipeline vectors' harness load, and not by CameraUnlockCore.dll,
 * which a mod ships. No host has a use for them: a tracker's poses reach a mod as datagrams.
 * A session of this build listens on loopback alone.
 */
#ifndef CAMERAUNLOCK_C_TESTING_CAMERAUNLOCK_TESTING_H
#define CAMERAUNLOCK_C_TESTING_CAMERAUNLOCK_TESTING_H

#include "cameraunlock/c/cameraunlock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A session as a new process has it, in every view: nothing received, nothing configured. No view
 * may be started. */
CAMERAUNLOCK_C int32_t cameraunlock_testing_reset(void);

/* Hands the session one datagram as if it had just arrived, from loopback or, with `remote`,
 * from another machine, through everything a datagram off the socket goes through. On the
 * caller's thread, to a session that is not started, so a run needs no socket and no waiting. */
CAMERAUNLOCK_C int32_t cameraunlock_testing_deliver(const void* datagram, int32_t length, int32_t remote);
/* The same for one view. cameraunlock_testing_deliver is view 0. */
CAMERAUNLOCK_C int32_t cameraunlock_testing_deliver_view(int32_t view, const void* datagram, int32_t length,
                                                         int32_t remote);
/* Where the player's Defaults.ini is, as the config owner finds it with no defaults path given: the
 * first candidate's path in UTF-8, or the reason there is none. Answers its length and copies it, with
 * its NUL, when `capacity` holds length + 1. */
CAMERAUNLOCK_C int32_t cameraunlock_testing_defaults_path(char* buffer, int32_t capacity);
/* One of the Windows calls core makes, made as core makes it, so a test can hold this build's
 * answer to the one Windows gives. `call` names it and `argument` is its input, in UTF-8; the
 * answer is text in UTF-8, copied as cameraunlock_testing_defaults_path copies its path.
 *   find        FindFirstFileW of the path `argument`, then FindClose: the name found
 *   compare     CompareStringOrdinal, ignoring case, of the two lines of `argument`: 1, 2 or 3
 *   code_page   GetACP
 *   narrow      WideCharToMultiByte to the ANSI code page of `argument`: its bytes in hex
 *   read_from   the file on the second line of `argument` from the offset on the first, through
 *               SetFilePointerEx and ReadFile
 *   error_text  the config owner's text for the Windows error number `argument` (FormatMessageW)
 *   delete      DeleteFileW of the path `argument`: 1
 *   sleep       Sleep for the milliseconds of `argument`: the milliseconds that passed, measured
 *               with QueryPerformanceCounter
 * A call that fails answers CAMERAUNLOCK_ERROR with the Windows error. */
CAMERAUNLOCK_C int32_t cameraunlock_testing_win32(const char* call, const char* argument, char* buffer, int32_t capacity);
#if defined(CAMERAUNLOCK_WIN32_HOST)
/* Traps, as a fault in core would, so a test can see what the host does with a module that stopped.
 * The WebAssembly build alone: in the DLL a trap ends the process. */
CAMERAUNLOCK_C int32_t cameraunlock_testing_trap(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
