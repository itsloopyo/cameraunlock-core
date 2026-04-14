#pragma once

#include <reframework/API.hpp>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace cameraunlock::reframework {

// Shared empty args vector for invoke calls with no parameters.
const std::vector<void*>& EmptyArgs();

// Read a managed String object (System.String / via.gui) into a narrow buffer.
// RE Engine managed strings store UTF-16 at offset 0x14.
void ReadManagedString(void* stringPtr, char* out, size_t outSize);

// Invoke a method on a managed object with no arguments, returning the raw pointer.
void* CallMethod(::reframework::API::Method* method, void* obj);

// Read element i from a managed System.Array via GetValue(int).
::reframework::API::ManagedObject* ArrayGetValue(
    ::reframework::API::ManagedObject* arr, int i);

// Find a method on a type with a specific parameter count (disambiguates overloads).
::reframework::API::Method* FindMethodByParamCount(
    const char* typeName, const char* methodName, uint32_t paramCount);

} // namespace cameraunlock::reframework
