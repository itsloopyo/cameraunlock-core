/* CameraUnlockCoreOtherAbi.dll: what a host finds beside it when the library is from an older
 * release than the host. It answers the one function every version has, with the number before
 * this header's. */
#include "cameraunlock/c/cameraunlock.h"

int32_t cameraunlock_abi(void) {
    return CAMERAUNLOCK_ABI - 1;
}
