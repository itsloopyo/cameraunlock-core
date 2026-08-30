#include <cameraunlock/reframework/gui_elements.h>

#include <cameraunlock/reframework/log_callback.h>
#include <cameraunlock/reframework/managed_utils.h>

#include <string>
#include <unordered_set>

namespace cameraunlock::reframework {

static GuiMethods g_gui;

const GuiMethods& InitGuiMethods() {
    if (g_gui.ready) return g_gui;

    const auto& api = ::reframework::API::get();

    // The 1-arg findObjects overload taking a System.Type. via.gui.GUI also
    // declares findObjects(System.String), which a plain find_method by name
    // resolves to roughly half the time.
    g_gui.findObjectsByType = FindMethodByParamTypeName("via.gui.GUI", "findObjects", "Type");
    g_gui.setPosition = FindMethodByParamCount("via.gui.TransformObject", "set_Position", 1);
    g_gui.getPosition = FindMethodByParamCount("via.gui.TransformObject", "get_Position", 0);
    g_gui.getGlobalPosition = FindMethodByParamCount("via.gui.TransformObject", "get_GlobalPosition", 0);
    g_gui.setRotation = FindMethodByParamCount("via.gui.TransformObject", "set_Rotation", 1);
    g_gui.getRotation = FindMethodByParamCount("via.gui.TransformObject", "get_Rotation", 0);
    g_gui.viewGetScreenSize = FindMethodByParamCount("via.gui.View", "get_ScreenSize", 0);
    g_gui.playObjectType = api->typeof("via.gui.PlayObject");

    g_gui.ready = g_gui.findObjectsByType && g_gui.setPosition && g_gui.playObjectType;

    // Bounded to two lines for the process: the first attempt, which records
    // what was missing, and the attempt that succeeds. Everything between is
    // silent, so retrying every frame cannot bury the rest of the log.
    static bool s_loggedAttempt = false;
    if (g_gui.ready || !s_loggedAttempt) {
        LogInfo("GUI compensation methods: findObjects(Type)=%p setPos=%p getPos=%p globalPos=%p "
                "setRot=%p screenSize=%p playObjType=%p",
                (void*)g_gui.findObjectsByType, (void*)g_gui.setPosition, (void*)g_gui.getPosition,
                (void*)g_gui.getGlobalPosition, (void*)g_gui.setRotation,
                (void*)g_gui.viewGetScreenSize, g_gui.playObjectType);
    }
    if (!g_gui.ready && !s_loggedAttempt) {
        LogWarning("GUI compensation: findObjects / set_Position / PlayObject type missing - "
                   "retrying until the GUI system resolves");
    }
    s_loggedAttempt = true;

    return g_gui;
}

// Resolution is latched on success only, and retried from here - the accessor
// every GUI draw callback goes through - until it succeeds. The first attempt
// now happens at the first BeginRendering, ahead of any GUI element being
// drawn, and a title whose via.gui types are not populated by then would
// otherwise be left with marker and reticle compensation inert for the whole
// session off one early miss. Retried for the session rather than capped, for
// the same reason the camera controller hook is: a cap turns a GUI system that
// appears late into one that can never be found. Once resolved this is a bool
// test, so the hot path pays nothing.
const GuiMethods& GetGuiMethods() { return g_gui.ready ? g_gui : InitGuiMethods(); }

// Instance getters invoked per GUI element per frame. Resolving the Method*
// once and invoking it directly avoids the per-call get_type_definition() +
// find_method(name) string search that ManagedObject::invoke("name", ...)
// performs. via.gui elements share these on their common base type, so a
// Method* resolved from one element dispatches correctly on every element.
static ::reframework::API::Method* g_elemGetGameObject = nullptr;
static ::reframework::API::Method* g_elemGetView = nullptr;
static ::reframework::API::Method* g_gameObjectGetName = nullptr;

bool ReadGuiElementName(::reframework::API::ManagedObject* element, char* out, size_t outSize) {
    out[0] = 0;
    if (!element) return false;

    auto goRet = InvokeCached(element, g_elemGetGameObject, "get_GameObject", EmptyArgs());
    if (goRet.exception_thrown || !goRet.ptr) return false;

    auto goMo = reinterpret_cast<::reframework::API::ManagedObject*>(goRet.ptr);
    auto nameRet = InvokeCached(goMo, g_gameObjectGetName, "get_Name", EmptyArgs());
    if (nameRet.exception_thrown || !nameRet.ptr) return false;

    ReadManagedString(nameRet.ptr, out, outSize);
    return out[0] != 0;
}

void LogGuiElementNameOnce(const char* goName) {
    // This callback fires once per GUI element per frame, so the lookup must
    // not allocate in steady state: assign into a reused buffer (no realloc
    // once warmed) and only construct a stored std::string when the name is
    // genuinely new, which is a finite, small set.
    static std::unordered_set<std::string> s_logged;
    static std::string s_query;
    if (s_logged.size() >= kMaxLoggedGuiNames) return;
    s_query.assign(goName);
    if (s_logged.find(s_query) != s_logged.end()) return;
    s_logged.insert(s_query);
    LogInfo("GUI element: \"%s\"", goName);
}

::reframework::API::ManagedObject* GetElementView(::reframework::API::ManagedObject* element) {
    if (!element) return nullptr;
    auto viewRet = InvokeCached(element, g_elemGetView, "get_View", EmptyArgs());
    if (viewRet.exception_thrown || !viewRet.ptr) return nullptr;
    return reinterpret_cast<::reframework::API::ManagedObject*>(viewRet.ptr);
}

void SetTransformPosition(::reframework::API::ManagedObject* transform, float x, float y, float z) {
    if (!transform || !g_gui.setPosition) return;
    float pos[3] = { x, y, z };
    InvokeMethodWithArg(g_gui.setPosition, transform, (void*)&pos[0]);
}

// Both position reads go through TryInvoke rather than Method::invoke. A read
// that failed at the SDK level comes back zero-filled with exception_thrown
// clear, and (0, 0) is a position a GUI element genuinely holds - so without the
// SDK's own result there is nothing to tell a broken read from the canvas
// origin, and a caller offsetting from it would move the element to a place it
// was never at.
bool GetTransformPosition(::reframework::API::ManagedObject* transform, float& x, float& y) {
    ::reframework::InvokeRet ret;
    if (!TryInvoke(g_gui.getPosition, transform, ret)) return false;
    x = *reinterpret_cast<const float*>(&ret.bytes[0]);
    y = *reinterpret_cast<const float*>(&ret.bytes[4]);
    return true;
}

bool GetTransformGlobalPosition(::reframework::API::ManagedObject* transform, float& x, float& y) {
    ::reframework::InvokeRet ret;
    if (!TryInvoke(g_gui.getGlobalPosition, transform, ret)) return false;
    x = *reinterpret_cast<const float*>(&ret.bytes[0]);
    y = *reinterpret_cast<const float*>(&ret.bytes[4]);
    return true;
}

bool ShiftElementView(::reframework::API::ManagedObject* element, float dx, float dy) {
    if (!g_gui.setPosition) return false;
    auto view = GetElementView(element);
    if (!view) return false;
    SetTransformPosition(view, dx, dy);
    return true;
}

bool GetElementCanvasSize(::reframework::API::ManagedObject* element,
                          float& canvasW, float& canvasH) {
    if (!g_gui.viewGetScreenSize) return false;
    auto view = GetElementView(element);
    if (!view) return false;

    ::reframework::InvokeRet sizeRet;
    if (!TryInvoke(g_gui.viewGetScreenSize, view, sizeRet)) return false;

    canvasW = *reinterpret_cast<const float*>(&sizeRet.bytes[0]);
    canvasH = *reinterpret_cast<const float*>(&sizeRet.bytes[4]);
    return canvasW > 100.f && canvasW < 16384.f && canvasH > 100.f && canvasH < 16384.f;
}

::reframework::API::ManagedObject* FindPlayObjects(::reframework::API::ManagedObject* element,
                                                   uint32_t& count) {
    count = 0;
    if (!element || !g_gui.findObjectsByType || !g_gui.playObjectType) return nullptr;

    std::vector<void*> findArgs = { g_gui.playObjectType };
    auto arrRet = g_gui.findObjectsByType->invoke(element, findArgs);
    if (arrRet.exception_thrown || !arrRet.ptr) return nullptr;

    auto arr = reinterpret_cast<::reframework::API::ManagedObject*>(arrRet.ptr);
    static ::reframework::API::Method* s_getLength = nullptr;
    auto lenRet = InvokeCached(arr, s_getLength, "get_Length", EmptyArgs());
    if (lenRet.exception_thrown) return nullptr;

    count = lenRet.dword;
    return arr;
}

} // namespace cameraunlock::reframework
