/* cameraunlock/c/cameraunlock.h compiled as C, with the layouts docs/c-interface.md states. */
#include <stddef.h>

#include "cameraunlock/c/cameraunlock.h"

#define LAYOUT(name, held) typedef char name[(held) ? 1 : -1]

LAYOUT(settings_size, sizeof(CameraUnlockSettings) == 60);
LAYOUT(settings_light, offsetof(CameraUnlockSettings, light_multiplier) == 56);
LAYOUT(input_size, sizeof(CameraUnlockFrameInput) == 80);
LAYOUT(input_now, offsetof(CameraUnlockFrameInput, now_ms) == 8);
LAYOUT(input_delta, offsetof(CameraUnlockFrameInput, delta_seconds) == 16);
LAYOUT(input_aim, offsetof(CameraUnlockFrameInput, aim_forward) == 32);
LAYOUT(input_matrix, offsetof(CameraUnlockFrameInput, tracker_to_world) == 44);
LAYOUT(frame_size, sizeof(CameraUnlockFrame) == 92);
LAYOUT(frame_yaw, offsetof(CameraUnlockFrame, yaw) == 16);
LAYOUT(frame_query, offsetof(CameraUnlockFrame, query_direction) == 76);
LAYOUT(obstruction_size, sizeof(CameraUnlockObstruction) == 16);
LAYOUT(lean_size, sizeof(CameraUnlockLean) == 40);
LAYOUT(lean_rig, offsetof(CameraUnlockLean, rig) == 20);
LAYOUT(config_size, sizeof(CameraUnlockConfig) == 76);
LAYOUT(config_settings, offsetof(CameraUnlockConfig, settings) == 16);

int CameraUnlockHeaderIsC(void) {
    return cameraunlock_abi() == CAMERAUNLOCK_ABI;
}
