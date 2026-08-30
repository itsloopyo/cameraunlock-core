#pragma once

#include <reframework/API.hpp>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace cameraunlock::reframework {

// Shared empty args vector for invoke calls with no parameters.
const std::vector<void*>& EmptyArgs();

// Read a managed String object (System.String / via.gui) into a narrow buffer.
// RE Engine managed strings store the int32 length at offset 0x10 and UTF-16
// char data at offset 0x14. SEH-guarded: a faulting read leaves out empty.
void ReadManagedString(void* stringPtr, char* out, size_t outSize);

// Invoke a method on a managed object with no arguments, returning the raw
// pointer. Returns nullptr if the invocation threw a managed exception.
void* CallMethod(::reframework::API::Method* method, void* obj);

// Invoke a no-argument method returning a managed bool (byte-sized result).
// Returns false if the invocation threw a managed exception.
bool CallMethodBool(::reframework::API::Method* method, void* obj);

// Invoke a method taking a single argument (managed object or System.Type),
// returning the raw pointer. Returns nullptr if the invocation threw a managed
// exception. Covers getComponent(System.Type)-style lookups.
void* CallMethodArg(::reframework::API::Method* method, void* obj, void* arg);

// Invoke a method taking a single argument through a stack-backed argument
// span, returning the full InvokeRet (for value returns the vector overloads
// cannot express). Avoids the per-call heap vector the vector-based invoke
// incurs - hot GUI compensation paths call set_Position dozens of times per
// frame, each of which would otherwise be an alloc+free pair.
::reframework::InvokeRet InvokeMethodWithArg(
    ::reframework::API::Method* method, ::reframework::API::ManagedObject* obj, void* arg);

// Invoke a no-argument method and report whether the call actually ran.
//
// InvokeRet::exception_thrown covers a MANAGED exception only. The SDK's own
// REFrameworkResult - which is what reports a bad object or an argument-size
// mismatch - is discarded by Method::invoke unless REFRAMEWORK_API_EXCEPTIONS is
// defined, and on that path the caller gets a zero-filled InvokeRet with the
// flag clear. A caller reading a float out of bytes[] then cannot tell a failed
// call from an element genuinely sitting at the origin. This calls the SDK entry
// point directly and returns false unless the invocation succeeded and no
// managed exception was raised.
bool TryInvoke(::reframework::API::Method* method, ::reframework::API::ManagedObject* obj,
               ::reframework::InvokeRet& out);

// No-arg getter whose resolved Method* is cached against the object's concrete
// type, re-resolving when the type changes. Per-frame loops over objects of
// varying runtime types (e.g. GUI draw elements) turn a per-call string method
// search (a cross-DLL scan of the type's method table) into a pointer compare
// on the steady-state path. Resolution is identical to invoke(name, ...):
// same type, same find_method.
struct CachedGetter {
    const char* name;
    ::reframework::API::TypeDefinition* td = nullptr;
    ::reframework::API::Method* method = nullptr;

    explicit CachedGetter(const char* n) : name(n) {}

    ::reframework::InvokeRet Invoke(::reframework::API::ManagedObject* obj);
};

// Scan the TDB for every type whose short name (namespace ignored) equals
// shortName, in TDB order. RE Engine titles move types between namespaces
// across releases (app.ropeway.* vs offline.* vs app.*); this is the
// namespace-agnostic fallback when fully-qualified lookups come up empty.
std::vector<::reframework::API::TypeDefinition*> FindTypesByShortName(const char* shortName);

// Read element i from a managed System.Array via GetValue(int).
::reframework::API::ManagedObject* ArrayGetValue(
    ::reframework::API::ManagedObject* arr, int i);

// Invoke a named method on a managed object, resolving the Method* once into
// the caller-provided cache slot. Getters and array accessors are inherited,
// so the resolved Method* is valid for every instance of the type; caching it
// removes the per-call type-definition + find_method lookup pair from
// per-frame loops. Falls back to the string-keyed invoke when resolution
// fails (e.g. the method only exists on a derived runtime type).
::reframework::InvokeRet InvokeCached(::reframework::API::ManagedObject* obj,
                                      ::reframework::API::Method*& cache,
                                      const char* methodName,
                                      const std::vector<void*>& args);

// Find a method on a type with a specific parameter count (disambiguates overloads).
::reframework::API::Method* FindMethodByParamCount(
    const char* typeName, const char* methodName, uint32_t paramCount);

// Find a single-parameter method overload whose parameter type's short name
// matches paramTypeShortName (namespace ignored). Disambiguates overload sets
// like via.gui.GUI.findObjects(System.Type) vs findObjects(System.String).
::reframework::API::Method* FindMethodByParamTypeName(
    const char* typeName, const char* methodName, const char* paramTypeShortName);

} // namespace cameraunlock::reframework
