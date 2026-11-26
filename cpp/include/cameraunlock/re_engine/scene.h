// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <cameraunlock/re_engine/discovery.h>
#include <cameraunlock/re_engine/tdb.h>

// The current scene's components, found by type, and what hangs off them.
namespace cameraunlock::re_engine {

class Scene {
public:
    // Finds the methods it calls and the layouts it reads in the type database.
    // Throws naming what is missing.
    Scene();

    // The first component of `type` in the current scene, or null when there is
    // none or the scene cannot be read this frame. Game thread.
    void* FirstComponent(void* ctx, const Type& type);
    // Every component of `type` in the current scene; empty when there is none or
    // the scene cannot be read this frame. Game thread.
    std::vector<void*> Components(void* ctx, const Type& type);

    // A component's or a GameObject's transform, or null. Game thread.
    void* TransformOf(void* ctx, void* component) const;
    // A component's GameObject, or null. Game thread.
    void* GameObjectOf(void* ctx, void* component) const;

    // Where `transform` keeps the world matrix the renderer draws from: the one
    // place in a via.Transform's bytes that holds what get_WorldMatrix hands
    // back (discovery::MatrixPlace), the same in every transform once found.
    // Null when the transform does not read as a via.Transform or one deriving from it, and while more than one
    // place still holds it. Throws when no place does. Game thread.
    void* WorldMatrix(void* ctx, void* transform);

    // A new instance of `type`, made by System.Activator.CreateInstance. Nothing
    // roots it, so it is used within the frame and let go. Game thread.
    void* Create(void* ctx, const Type& type);

    // The type's System.Type object, made once. Game thread.
    void* RuntimeType(void* ctx, const Type& type);

    // A managed array's elements, by System.Array's get_Length and GetValue;
    // empty for null or an array whose object info does not read. Game thread.
    std::vector<void*> Elements(void* ctx, void* array) const;

    // A System.String's characters, at the places the type database gives its
    // _stringLength and _firstChar fields; empty for null. Throws when the
    // length reads below 0 or above 4096.
    std::wstring Text(void* systemString) const;
    // A new System.String holding `text`, made by System.String.FastAllocateString;
    // null when it could not be made. Nothing roots it, so it is used within the
    // frame and let go. Game thread.
    void* NewString(void* ctx, std::wstring_view text) const;

    // Checks where FieldAddress puts managed fields and where TypeOf reads an
    // object's type: a System.String made with a length has to read it back at
    // _stringLength, TypeOf it has to be System.String, and System.Object.GetType
    // on it has to hand back the System.Type RuntimeType makes for System.String.
    // False while the string could not be made this frame; throws when any of
    // it reads otherwise. Game thread.
    bool CheckFields(void* ctx);

    // Whether `object` still reads as an instance of `type`: false for one freed,
    // or whose memory went to something else.
    static bool Is(void* object, const Type* type);
    // Whether `object` reads as a `type` or as a type deriving from it; false
    // for null and for one that faults as its type is read.
    static bool IsA(void* object, const Type* type);
    // Whether `object` reads as a live managed object: its object info names a
    // type in the type database. What a call into the game is checked with
    // before it is handed an object whose type is not known here.
    static bool Live(void* object);

private:
    // The managed array findComponents hands back, or null.
    void* Search(void* ctx, void* runtimeType) const;

    using Static = void* (*)(void* ctx);
    using Getter = void* (*)(void* ctx, void* self);
    using Find = void* (*)(void* ctx, void* self, void* type);
    using Length = std::int32_t (*)(void* ctx, void* self);
    using At = void* (*)(void* ctx, void* self, std::int32_t index);
    using Allocate = void* (*)(void* ctx, std::int32_t length);
    using Cut = void* (*)(void* ctx, void* self, std::int32_t start, std::int32_t length);
    using Pad = void* (*)(void* ctx, void* self, std::int32_t width, std::uint32_t with);
    // A new string `length` long in `out`; false when a string on the way does
    // not read as a System.String.
    bool MakeString(void* ctx, std::int32_t length, void*& out) const;
    using MatrixGetter = void* (*)(void* out, void* ctx, void* self);

    Static getCurrentScene_ = nullptr;
    Find findComponents_ = nullptr;
    Getter getGameObject_ = nullptr;
    Getter getTransform_ = nullptr;
    Getter createInstance_ = nullptr;
    Length arrayLength_ = nullptr;
    At arrayAt_ = nullptr;
    Allocate allocateString_ = nullptr;
    Static seedString_ = nullptr;
    Cut substring_ = nullptr;
    Pad padLeft_ = nullptr;
    mutable bool saidMakeFailed_ = false;
    MatrixGetter worldMatrixOf_ = nullptr;
    const Type* gameObjectType_ = nullptr;
    const Type* componentType_ = nullptr;
    const Type* transformType_ = nullptr;
    const Type* stringType_ = nullptr;
    int stringLength_ = -1;
    int stringChars_ = -1;
    discovery::MatrixPlace worldMatrixPlace_;
    std::optional<std::size_t> worldMatrix_;
    bool saidPlaces_ = false;
    std::unordered_map<const Type*, void*> runtimeTypes_;
};

// A name from a profile as the engine's UTF-16, widened byte for byte: profile
// names are printable ASCII.
std::wstring Wide(std::string_view ascii);
// A game name, kept to ASCII for the log; anything wider shows as ?.
std::string Narrow(std::wstring_view name);

// A method's code, by type and name, and when `returns` is given, the type it
// returns. Throws naming what is missing or different.
void* MethodCode(const char* type, const char* name, int numParams, const char* returns = nullptr);
// The same for a static method, called without an object. Throws when the type
// database says it takes one.
void* StaticMethodCode(const char* type, const char* name, int numParams, const char* returns = nullptr);
// The code of the one-parameter method `name` of `type` whose parameter is a
// `paramType`. Throws naming what is missing, the method after `who`, the
// feature asking.
void* MethodCodeTaking(const std::string& who, const char* type, const char* name, const char* paramType);
// The type a profile names in full or by its name after the last '.'. Throws
// after `who` when the game has none, and when more than one carries the name.
const Type& TypeNamed(const std::string& who, const std::string& name);

}  // namespace cameraunlock::re_engine
