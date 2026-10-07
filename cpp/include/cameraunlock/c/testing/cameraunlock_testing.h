/* Test-only entry points of the C interface. They are exported by CameraUnlockCoreTesting.dll,
 * which core's tests and the pipeline vectors' harness load, and not by CameraUnlockCore.dll,
 * which a mod ships. No host has a use for them: a tracker's poses reach a mod as datagrams.
 */
#ifndef CAMERAUNLOCK_C_TESTING_CAMERAUNLOCK_TESTING_H
#define CAMERAUNLOCK_C_TESTING_CAMERAUNLOCK_TESTING_H

#include "cameraunlock/c/cameraunlock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A session as a new process has it: nothing received, nothing configured. The session must
 * not be started. */
CAMERAUNLOCK_C int32_t cameraunlock_testing_reset(void);

/* Hands the session one datagram as if it had just arrived, from loopback or, with `remote`,
 * from another machine, through everything a datagram off the socket goes through. On the
 * caller's thread, to a session that is not started, so a run needs no socket and no waiting. */
CAMERAUNLOCK_C int32_t cameraunlock_testing_deliver(const void* datagram, int32_t length, int32_t remote);

#ifdef __cplusplus
}
#endif

#endif
