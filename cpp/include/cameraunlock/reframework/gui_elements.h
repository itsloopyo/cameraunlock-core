#pragma once

#include <reframework/API.hpp>

#include <cstddef>
#include <cstdint>

namespace cameraunlock::reframework {

// The via.gui reflection every RE Engine GUI compensation path needs. Resolved
// once; `ready` is false when the engine did not hand over the two methods
// nothing works without (findObjects and set_Position).
struct GuiMethods {
    ::reframework::API::Method* findObjectsByType = nullptr;
    ::reframework::API::Method* setPosition = nullptr;
    ::reframework::API::Method* getPosition = nullptr;
    ::reframework::API::Method* getGlobalPosition = nullptr;
    ::reframework::API::Method* setRotation = nullptr;
    ::reframework::API::Method* getRotation = nullptr;
    ::reframework::API::Method* viewGetScreenSize = nullptr;
    // System.Type of via.gui.PlayObject, the argument findObjects takes.
    void* playObjectType = nullptr;
    bool ready = false;
};

// Resolve the GUI methods on first call and log what was found. Subsequent
// calls return the cached result.
const GuiMethods& InitGuiMethods();
const GuiMethods& GetGuiMethods();

// Read a draw element's GameObject name. Returns false and leaves `out` empty
// when the element has no reachable GameObject or name.
bool ReadGuiElementName(::reframework::API::ManagedObject* element, char* out, size_t outSize);

// Log each distinct GUI element name once, up to a bound. This is the discovery
// aid every one of these titles' compensated element names came out of, so it
// stays in the shipped build.
void LogGuiElementNameOnce(const char* goName);

// element.get_View(), or nullptr.
::reframework::API::ManagedObject* GetElementView(::reframework::API::ManagedObject* element);

// Write a screen-space pixel offset into an element's root View, which moves
// the whole element. Returns false when the View or set_Position is missing.
bool ShiftElementView(::reframework::API::ManagedObject* element, float dx, float dy);

// The GUI canvas this element is laid out on, read from its own View. Every
// screen-space constant that is hardcoded to 1920x1080 instead silently
// mislocates on any other canvas, and each GUI carries its own size, so this is
// per element rather than global. Returns false for an implausible size.
bool GetElementCanvasSize(::reframework::API::ManagedObject* element,
                          float& canvasW, float& canvasH);

// findObjects(via.gui.PlayObject) on this element: every descendant PlayObject
// in tree order. Returns nullptr when the call fails; `count` is the array
// length on success.
::reframework::API::ManagedObject* FindPlayObjects(::reframework::API::ManagedObject* element,
                                                   uint32_t& count);

// Write a position into a via.gui.TransformObject (a PlayObject or a View).
void SetTransformPosition(::reframework::API::ManagedObject* transform,
                          float x, float y, float z = 0.f);

// Read a via.gui.TransformObject's local position. Returns false when
// get_Position is missing or threw.
bool GetTransformPosition(::reframework::API::ManagedObject* transform, float& x, float& y);

// Read a via.gui.TransformObject's canvas-space position. Returns false when
// get_GlobalPosition is missing or threw.
bool GetTransformGlobalPosition(::reframework::API::ManagedObject* transform, float& x, float& y);

} // namespace cameraunlock::reframework
