#pragma once

#include <reframework/API.hpp>
#include <vector>

namespace cameraunlock::reframework {

// Field type classification for runtime inspection.
enum class FieldKind {
    Unknown, String, Bool, Int32, UInt32, Int64, UInt64,
    Float, Double, Vec2, Vec3, Vec4, Reference
};

// Classify a field's declared type into a known bucket.
FieldKind ClassifyFieldType(::reframework::API::TypeDefinition* td);

// Dump all instance fields of the given type on the given object.
void DumpFieldsForType(
    ::reframework::API::TypeDefinition* type,
    ::reframework::API::ManagedObject* obj,
    int indent);

// Walk the type chain (declared -> parents -> Object) and dump fields at each level.
void DumpFieldsRecursive(
    ::reframework::API::TypeDefinition* type,
    ::reframework::API::ManagedObject* obj,
    int indent);

// Enumerate methods on a type, logging those matching any substring (empty = log all).
void EnumerateMethods(const char* typeName, const std::vector<const char*>& substrings);

// Log every overload of `methodName` on `typeName`, including parameter signatures.
void LogMethodOverloads(const char* typeName, const char* methodName);

// Log the inheritance chain of a type.
void LogInheritanceChain(const char* typeName);

// Log an object's type name + its GameObject name (if it's a Component).
void LogObjectIdentity(const char* label, ::reframework::API::ManagedObject* mo);

// Invoke a getter and log the result. Skips silently if method doesn't exist.
void LogGetterString(::reframework::API::ManagedObject* obj, const char* method, const char* label);
void LogGetterBool(::reframework::API::ManagedObject* obj, const char* method, const char* label);
void LogGetterU32(::reframework::API::ManagedObject* obj, const char* method, const char* label);
void LogGetterPtr(::reframework::API::ManagedObject* obj, const char* method, const char* label);

} // namespace cameraunlock::reframework
