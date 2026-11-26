/* The entry points the WebAssembly build of the C interface has for its host, and a DLL host
 * never calls (docs/core-jar.md). That build has no threads, so its host runs what the DLL's
 * threads do: it receives on each socket a receiver opens, hands each result here, and calls
 * cameraunlock_host_tick about every 16 ms.
 */
#ifndef CAMERAUNLOCK_C_HOST_CAMERAUNLOCK_HOST_H
#define CAMERAUNLOCK_C_HOST_CAMERAUNLOCK_HOST_H

#include "cameraunlock/c/cameraunlock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One look at the keys for the hotkeys, once they are started, and one step of each receiver's
 * supervisor: a bind retried while it fails, a socket reopened after its receive loop gave up. */
CAMERAUNLOCK_C int32_t cameraunlock_host_tick(void);

/* What one recvfrom on the receiver's socket gave: `bytes` from `sender` (a sockaddr_in), or -1
 * and Winsock's `error`. `receiver` is what the host was handed when it was asked to receive. */
CAMERAUNLOCK_C int32_t cameraunlock_host_received(void* receiver, const char* buffer, int32_t bytes, int32_t error,
                                                  const void* sender);

/* The host stopped waiting on the receiver's socket: `what` says why, in UTF-8. */
CAMERAUNLOCK_C int32_t cameraunlock_host_receive_failed(void* receiver, const char* what);

#ifdef __cplusplus
}
#endif

#endif
