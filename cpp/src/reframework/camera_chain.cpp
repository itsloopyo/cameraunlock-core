#include <cameraunlock/reframework/camera_chain.h>
#include <cameraunlock/memory/safe_memory.h>
#include <cameraunlock/reframework/managed_utils.h>
#include <cameraunlock/rendering/gui_marker_compensation.h>

#include <windows.h>

#include <cstdint>

namespace cameraunlock::reframework {

bool CameraTransformResolver::Initialize() {
    if (m_initialized) return !m_failed;
    m_initialized = true;

    const auto& api = ::reframework::API::get();
    auto tdb = api->tdb();
    auto smType = tdb->find_type("via.SceneManager");
    auto svType = tdb->find_type("via.SceneView");
    auto camType = tdb->find_type("via.Camera");
    auto goType = tdb->find_type("via.GameObject");

    if (!smType || !svType || !camType || !goType) {
        m_failed = true;
        return false;
    }

    m_getMainView = smType->find_method("get_MainView");
    m_getPrimaryCamera = svType->find_method("get_PrimaryCamera");
    m_getGameObject = camType->find_method("get_GameObject");
    m_getTransform = goType->find_method("get_Transform");
    m_getFov = camType->find_method("get_FOV");

    if (!m_getMainView || !m_getPrimaryCamera || !m_getGameObject || !m_getTransform) {
        m_failed = true;
        return false;
    }
    return true;
}

void* CameraTransformResolver::ResolveCamera() {
    if (!m_initialized || m_failed) return nullptr;

    auto sm = ::reframework::API::get()->get_native_singleton("via.SceneManager");
    if (!sm) return nullptr;

    __try {
        auto mv = CallMethod(m_getMainView, sm);
        if (!mv) return nullptr;
        return CallMethod(m_getPrimaryCamera, mv);
    } __except(cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
        return nullptr;
    }
}

float CameraTransformResolver::ResolveFovDegrees(void* camera) {
    if (!m_getFov) return 0.f;
    void* cam = camera ? camera : ResolveCamera();
    if (!cam) return 0.f;

    __try {
        auto ret = m_getFov->invoke(
            reinterpret_cast<::reframework::API::ManagedObject*>(cam), EmptyArgs());
        if (ret.exception_thrown) return 0.f;
        return rendering::ReadFovFromInvokeRet(ret.f, ret.d);
    } __except(cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
        return 0.f;
    }
}

void* CameraTransformResolver::ResolveTransform(void** outCamera) {
    if (outCamera) *outCamera = nullptr;
    if (!m_initialized || m_failed) return nullptr;

    auto sm = ::reframework::API::get()->get_native_singleton("via.SceneManager");
    if (!sm) return nullptr;

    // Reflecting through a camera that was torn down during a scene transition
    // can dereference freed memory inside the engine; guard the whole chain so
    // a stale object yields nullptr instead of crashing.
    __try {
        auto mv = CallMethod(m_getMainView, sm);
        if (!mv) return nullptr;
        auto cam = CallMethod(m_getPrimaryCamera, mv);
        if (!cam) return nullptr;
        auto go = CallMethod(m_getGameObject, cam);
        if (!go) return nullptr;
        void* transform = CallMethod(m_getTransform, go);
        if (transform && outCamera) *outCamera = cam;
        return transform;
    } __except(cameraunlock::memory::AccessViolationFilter(GetExceptionCode())) {
        return nullptr;
    }
}

Matrix4x4f* CameraTransformResolver::ResolveWorldMatrix(ptrdiff_t worldMatrixOffset) {
    void* transform = ResolveTransform();
    if (!transform) return nullptr;
    return reinterpret_cast<Matrix4x4f*>(
        reinterpret_cast<uint8_t*>(transform) + worldMatrixOffset);
}

} // namespace cameraunlock::reframework
