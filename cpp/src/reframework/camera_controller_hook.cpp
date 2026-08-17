#include <cameraunlock/reframework/camera_controller_hook.h>
#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>

#include <cstdio>
#include <cstring>

namespace cameraunlock::reframework {

namespace {

// Update-method name candidates shared across RE Engine titles. The gameplay
// camera controller's per-frame update goes by one of these regardless of the
// game-specific controller type name. updateCameraPosition is the RE2/RE3
// generation's per-frame transform writer (REFramework's own camera hook
// target); onCameraUpdate is its RE8/RE9 generation counterpart. Both sit
// ahead of the generic lateUpdate/update names so the dedicated writer wins
// on types that expose both.
const char* const kUpdateMethodNames[] = {
    "onCameraUpdate",
    "updateCameraPosition",
    "lateUpdate",
    "update",
    "doUpdate",
    "lateUpdateImpl",
};

} // namespace

bool IsEffectControllerName(const char* typeName) {
    static const char* const kEffectMarkers[] = {
        "DOF", "Fog", "ToneMap", "Bloom", "SSAO", "SSR", "ColorCorrect",
        "LensDistortion", "RenderResolutio", "Shake", "Vignette", "MotionBlur",
        "DepthOfField", "PostProcess", "Effect", "Exposure", "Vibration",
    };
    for (auto marker : kEffectMarkers) {
        if (strstr(typeName, marker)) return true;
    }
    return false;
}

bool CameraControllerHooker::TryHookTypeDef(::reframework::API::TypeDefinition* type,
                                            const char* fullTypeName) {
    for (auto methodName : kUpdateMethodNames) {
        auto method = type->find_method(methodName);
        if (!method) continue;
        // REFramework returns 0 when the hook could not be installed. Latching
        // m_hooked on that made TryHook() return true forever, so the discovery
        // ladder never ran again and aim decoupling did nothing for the rest of
        // the session while the log claimed "Hooked".
        auto id = method->add_hook(m_preHook, m_postHook, false);
        if (id == 0) {
            Log(LogLevel::Warning, "add_hook failed on %s.%s", fullTypeName, methodName);
            continue;
        }
        Log(LogLevel::Info, "Hooked %s.%s (id=%u)", fullTypeName, methodName, id);
        m_hookedMethod = method;
        m_hookId = id;
        m_hooked = true;
        return true;
    }
    return false;
}

void CameraControllerHooker::Unhook() {
    if (!m_hooked) return;
    m_hookedMethod->remove_hook(m_hookId);
    m_hookedMethod = nullptr;
    m_hookId = 0;
    m_hooked = false;
}

bool CameraControllerHooker::TryHookType(const char* fullTypeName) {
    auto tdb = ::reframework::API::get()->tdb();
    auto type = tdb->find_type(fullTypeName);
    if (!type) return false;
    return TryHookTypeDef(type, fullTypeName);
}

bool CameraControllerHooker::WalkParentChain(void* cameraTransform) {
    auto txMo = reinterpret_cast<::reframework::API::ManagedObject*>(cameraTransform);

    for (int depth = 0; depth < 8; depth++) {
        auto goRet = txMo->invoke("get_GameObject", EmptyArgs());
        if (goRet.exception_thrown || !goRet.ptr) break;
        auto goMo = reinterpret_cast<::reframework::API::ManagedObject*>(goRet.ptr);

        char goName[128] = "?";
        auto nameRet = goMo->invoke("get_Name", EmptyArgs());
        if (!nameRet.exception_thrown && nameRet.ptr) {
            ReadManagedString(nameRet.ptr, goName, sizeof(goName));
        }

        auto compsRet = goMo->invoke("get_Components", EmptyArgs());
        if (compsRet.exception_thrown || !compsRet.ptr) {
            Log(LogLevel::Info, "  parent[%d] GO=\"%s\": no components", depth, goName);
        } else {
            auto compArr = reinterpret_cast<::reframework::API::ManagedObject*>(compsRet.ptr);
            auto lenRet = compArr->invoke("get_Length", EmptyArgs());
            uint32_t compCount = lenRet.exception_thrown ? 0 : lenRet.dword;
            Log(LogLevel::Info, "  parent[%d] GO=\"%s\": %u components", depth, goName, compCount);

            for (uint32_t i = 0; i < compCount && i < 32; i++) {
                auto comp = ArrayGetValue(compArr, (int)i);
                if (!comp) continue;
                auto compTd = comp->get_type_definition();
                if (!compTd) continue;
                const char* cns = compTd->get_namespace();
                const char* cnm = compTd->get_name();
                if (!cns) cns = "";
                if (!cnm) cnm = "?";
                Log(LogLevel::Info, "    [%u] %s.%s", i, cns, cnm);

                // Accept a player camera controller (preferred) or a generic
                // "Camera*Controller", but never a render/post-process effect
                // controller (DOF, fog, bloom, ...) which shares the shape.
                bool isCameraController =
                    ((strstr(cnm, "Camera") && strstr(cnm, "Controller"))
                        || (strstr(cnm, "Player") && strstr(cnm, "Camera")))
                    && !IsEffectControllerName(cnm);
                if (!isCameraController) continue;

                char fullName[256];
                snprintf(fullName, sizeof(fullName), "%s.%s", cns, cnm);
                Log(LogLevel::Info, "  -> Candidate camera controller: %s", fullName);

                if (TryHookType(fullName)) return true;
            }
        }

        auto parentRet = txMo->invoke("get_Parent", EmptyArgs());
        if (parentRet.exception_thrown || !parentRet.ptr) break;
        txMo = reinterpret_cast<::reframework::API::ManagedObject*>(parentRet.ptr);
    }

    return false;
}

bool CameraControllerHooker::TryHook(void* cameraTransform) {
    if (m_hooked) return true;
    m_attempts++;

    for (int i = 0; i < m_candidateTypeCount; i++) {
        if (TryHookType(m_candidateTypes[i])) return true;
    }

    // Namespace-agnostic fallback: RE Engine titles move the player camera
    // controller between namespaces across releases (app.ropeway.* vs
    // offline.* vs app.*) but keep the short type name. Exact short-name
    // matching cannot hit the Camera*Controller effect-controller shapes.
    for (auto type : FindTypesByShortName("PlayerCameraController")) {
        const char* ns = type->get_namespace();
        char fullName[256];
        snprintf(fullName, sizeof(fullName), "%s.%s", ns ? ns : "", "PlayerCameraController");
        if (TryHookTypeDef(type, fullName)) return true;
    }

    if (!cameraTransform) return false;

    Log(LogLevel::Info, "Camera controller: hardcoded names failed, walking parent chain...");
    return WalkParentChain(cameraTransform);
}

} // namespace cameraunlock::reframework
