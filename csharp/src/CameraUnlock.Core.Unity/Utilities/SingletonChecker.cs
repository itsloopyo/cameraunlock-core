using System;
using System.Collections.Generic;
using System.Reflection;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Utilities
{
    /// <summary>
    /// Provides reflection-based checks for singleton pattern instances commonly found in Unity games.
    /// Many games use a pattern like "Player.main" or "GameManager.instance" for their singletons.
    /// This utility simplifies checking if these singletons exist and retrieving their values.
    /// </summary>
    public static class SingletonChecker
    {
        private const BindingFlags StaticFieldFlags = BindingFlags.Public | BindingFlags.Static;
        private const BindingFlags InstanceFieldFlags = BindingFlags.Public | BindingFlags.Instance;

        /// <summary>
        /// Checks if a singleton instance exists and is not null.
        /// Handles Unity's "fake null" (destroyed but not garbage collected) objects.
        /// </summary>
        /// <param name="typeName">The full type name (e.g., "Player" or "MyNamespace.Player").</param>
        /// <param name="fieldName">The static field name containing the singleton (default: "main").</param>
        /// <param name="assemblyName">The assembly name to search in (default: "Assembly-CSharp").</param>
        /// <returns>True if the singleton exists and is not null/destroyed.</returns>
        /// <exception cref="ArgumentException">Thrown when typeName is null or empty.</exception>
        public static bool Exists(string typeName, string fieldName = "main", string assemblyName = "Assembly-CSharp")
        {
            if (string.IsNullOrEmpty(typeName))
            {
                throw new ArgumentException("Type name cannot be null or empty", nameof(typeName));
            }

            Type type = ResolveType(typeName, assemblyName);
            if (type == null)
            {
                return false;
            }

            FieldInfo field = ResolveField(type, fieldName, StaticFieldFlags);
            if (field == null)
            {
                return false;
            }

            object value = field.GetValue(null);
            if (value == null)
            {
                return false;
            }

            // Handle Unity's "fake null" - destroyed objects that override == to return true for null
            if (value is UnityEngine.Object unityObj)
            {
                return unityObj != null; // Uses Unity's overloaded == operator
            }

            return true;
        }

        /// <summary>
        /// Gets the singleton instance value.
        /// </summary>
        /// <typeparam name="T">The expected type of the singleton.</typeparam>
        /// <param name="typeName">The full type name.</param>
        /// <param name="fieldName">The static field name (default: "main").</param>
        /// <param name="assemblyName">The assembly name (default: "Assembly-CSharp").</param>
        /// <returns>The singleton instance.</returns>
        /// <exception cref="ArgumentException">Thrown when typeName is null or empty.</exception>
        /// <exception cref="InvalidOperationException">Thrown when the type or field cannot be found.</exception>
        public static T GetValue<T>(string typeName, string fieldName = "main", string assemblyName = "Assembly-CSharp")
            where T : class
        {
            if (string.IsNullOrEmpty(typeName))
            {
                throw new ArgumentException("Type name cannot be null or empty", nameof(typeName));
            }

            Type type = ResolveType(typeName, assemblyName);
            if (type == null)
            {
                throw new InvalidOperationException($"Type '{typeName}' not found in assembly '{assemblyName}'");
            }

            FieldInfo field = ResolveField(type, fieldName, StaticFieldFlags);
            if (field == null)
            {
                throw new InvalidOperationException($"Static field '{fieldName}' not found on type '{typeName}'");
            }

            object value = field.GetValue(null);
            return value as T;
        }

        /// <summary>
        /// Tries to get the singleton instance value.
        /// </summary>
        /// <typeparam name="T">The expected type of the singleton.</typeparam>
        /// <param name="typeName">The full type name.</param>
        /// <param name="fieldName">The static field name (default: "main").</param>
        /// <param name="value">The singleton instance if found.</param>
        /// <param name="assemblyName">The assembly name (default: "Assembly-CSharp").</param>
        /// <returns>True if the singleton was found and is of the expected type.</returns>
        public static bool TryGetValue<T>(string typeName, string fieldName, out T value, string assemblyName = "Assembly-CSharp")
            where T : class
        {
            value = null;

            if (string.IsNullOrEmpty(typeName))
            {
                return false;
            }

            Type type = ResolveType(typeName, assemblyName);
            if (type == null)
            {
                return false;
            }

            FieldInfo field = ResolveField(type, fieldName, StaticFieldFlags);
            if (field == null)
            {
                return false;
            }

            object rawValue = field.GetValue(null);
            if (rawValue == null)
            {
                return false;
            }

            // Handle Unity fake null
            if (rawValue is UnityEngine.Object unityObj && unityObj == null)
            {
                return false;
            }

            value = rawValue as T;
            return value != null;
        }

        /// <summary>
        /// Gets the value of an instance field from a singleton object.
        /// Useful for checking properties like "isSelected" or "isPaused".
        /// </summary>
        /// <typeparam name="T">The expected type of the field value.</typeparam>
        /// <param name="singletonTypeName">The type name of the singleton.</param>
        /// <param name="singletonFieldName">The static field name for the singleton instance.</param>
        /// <param name="instanceFieldName">The instance field name to read.</param>
        /// <param name="assemblyName">The assembly name (default: "Assembly-CSharp").</param>
        /// <returns>The field value.</returns>
        /// <exception cref="ArgumentException">Thrown when required parameters are null or empty.</exception>
        /// <exception cref="InvalidOperationException">Thrown when type, field, or singleton is not found.</exception>
        public static T GetInstanceField<T>(
            string singletonTypeName,
            string singletonFieldName,
            string instanceFieldName,
            string assemblyName = "Assembly-CSharp")
        {
            if (string.IsNullOrEmpty(singletonTypeName))
            {
                throw new ArgumentException("Singleton type name cannot be null or empty", nameof(singletonTypeName));
            }

            if (string.IsNullOrEmpty(instanceFieldName))
            {
                throw new ArgumentException("Instance field name cannot be null or empty", nameof(instanceFieldName));
            }

            Type type = ResolveType(singletonTypeName, assemblyName);
            if (type == null)
            {
                throw new InvalidOperationException($"Type '{singletonTypeName}' not found");
            }

            FieldInfo singletonField = ResolveField(type, singletonFieldName, StaticFieldFlags);
            if (singletonField == null)
            {
                throw new InvalidOperationException($"Static field '{singletonFieldName}' not found on type '{singletonTypeName}'");
            }

            object singleton = singletonField.GetValue(null);
            if (singleton == null)
            {
                throw new InvalidOperationException($"Singleton '{singletonTypeName}.{singletonFieldName}' is null");
            }

            // Handle Unity fake null
            if (singleton is UnityEngine.Object singletonUnityObj && singletonUnityObj == null)
            {
                throw new InvalidOperationException($"Singleton '{singletonTypeName}.{singletonFieldName}' is destroyed");
            }

            FieldInfo instanceField = ResolveField(type, instanceFieldName, InstanceFieldFlags);
            if (instanceField == null)
            {
                throw new InvalidOperationException($"Instance field '{instanceFieldName}' not found on type '{singletonTypeName}'");
            }

            object value = instanceField.GetValue(singleton);
            if (value is T typedValue)
            {
                return typedValue;
            }

            return default;
        }

        /// <summary>
        /// Tries to get the value of an instance field from a singleton object.
        /// </summary>
        /// <typeparam name="T">The expected type of the field value.</typeparam>
        /// <param name="singletonTypeName">The type name of the singleton.</param>
        /// <param name="singletonFieldName">The static field name for the singleton instance.</param>
        /// <param name="instanceFieldName">The instance field name to read.</param>
        /// <param name="value">The field value if found.</param>
        /// <param name="assemblyName">The assembly name (default: "Assembly-CSharp").</param>
        /// <returns>True if the field was found and read successfully.</returns>
        public static bool TryGetInstanceField<T>(
            string singletonTypeName,
            string singletonFieldName,
            string instanceFieldName,
            out T value,
            string assemblyName = "Assembly-CSharp")
        {
            value = default;

            if (string.IsNullOrEmpty(singletonTypeName) || string.IsNullOrEmpty(instanceFieldName))
            {
                return false;
            }

            Type type = ResolveType(singletonTypeName, assemblyName);
            if (type == null)
            {
                return false;
            }

            FieldInfo singletonField = ResolveField(type, singletonFieldName, StaticFieldFlags);
            if (singletonField == null)
            {
                return false;
            }

            object singleton = singletonField.GetValue(null);
            if (singleton == null)
            {
                return false;
            }

            // Handle Unity fake null
            if (singleton is UnityEngine.Object singletonUnityObj && singletonUnityObj == null)
            {
                return false;
            }

            FieldInfo instanceField = ResolveField(type, instanceFieldName, InstanceFieldFlags);
            if (instanceField == null)
            {
                return false;
            }

            object rawValue = instanceField.GetValue(singleton);
            if (rawValue is T typedValue)
            {
                value = typedValue;
                return true;
            }

            return false;
        }

        /// <summary>
        /// Checks if a singleton's GameObject is active in the hierarchy.
        /// Useful for menu detection where menus exist but may be inactive.
        /// </summary>
        /// <param name="typeName">The type name of the singleton (must derive from Component).</param>
        /// <param name="fieldName">The static field name (default: "main").</param>
        /// <param name="assemblyName">The assembly name (default: "Assembly-CSharp").</param>
        /// <returns>True if the singleton exists and its GameObject is active.</returns>
        public static bool IsActiveInHierarchy(string typeName, string fieldName = "main", string assemblyName = "Assembly-CSharp")
        {
            if (!TryGetValue<Component>(typeName, fieldName, out var component, assemblyName))
            {
                return false;
            }

            return component != null && component.gameObject.activeInHierarchy;
        }

        // Exists/TryGetValue are exactly what a mod's per-frame ShouldApplyTracking() calls, and
        // an uncached miss allocated an assembly-qualified name string and a fresh ~100-entry
        // array from AppDomain.GetAssemblies() every frame.
        //
        // Only successful resolutions are cached. Caching a miss would latch the failure for
        // the process even after the game assembly that owns the type finishes loading.
        private static readonly Dictionary<TypeKey, Type> TypeCache = new Dictionary<TypeKey, Type>();

        // A loaded Type's member set is immutable, so misses are safe to cache here.
        private static readonly Dictionary<FieldKey, FieldInfo> FieldCache = new Dictionary<FieldKey, FieldInfo>();

        private static Type ResolveType(string typeName, string assemblyName)
        {
            var key = new TypeKey(typeName, assemblyName);
            Type cached;
            if (TypeCache.TryGetValue(key, out cached))
            {
                return cached;
            }

            Type type = Resolve(typeName, assemblyName);
            if (type != null)
            {
                TypeCache[key] = type;
            }
            return type;
        }

        private static Type Resolve(string typeName, string assemblyName)
        {
            // Try fully qualified name first
            Type type = Type.GetType($"{typeName}, {assemblyName}");
            if (type != null)
            {
                return type;
            }

            // Try just the type name (might be in a loaded assembly already)
            type = Type.GetType(typeName);
            if (type != null)
            {
                return type;
            }

            // Search loaded assemblies
            foreach (Assembly assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                type = assembly.GetType(typeName);
                if (type != null)
                {
                    return type;
                }
            }

            return null;
        }

        private static FieldInfo ResolveField(Type type, string fieldName, BindingFlags flags)
        {
            var key = new FieldKey(type, fieldName, flags);
            FieldInfo cached;
            if (FieldCache.TryGetValue(key, out cached))
            {
                return cached;
            }

            FieldInfo field = type.GetField(fieldName, flags);
            FieldCache[key] = field;
            return field;
        }

        private struct TypeKey : IEquatable<TypeKey>
        {
            private readonly string _typeName;
            private readonly string _assemblyName;

            public TypeKey(string typeName, string assemblyName)
            {
                _typeName = typeName;
                _assemblyName = assemblyName;
            }

            public bool Equals(TypeKey other)
            {
                return _typeName == other._typeName && _assemblyName == other._assemblyName;
            }

            public override bool Equals(object obj)
            {
                return obj is TypeKey && Equals((TypeKey)obj);
            }

            public override int GetHashCode()
            {
                int hash = _typeName != null ? _typeName.GetHashCode() : 0;
                return (hash * 397) ^ (_assemblyName != null ? _assemblyName.GetHashCode() : 0);
            }
        }

        private struct FieldKey : IEquatable<FieldKey>
        {
            private readonly Type _type;
            private readonly string _fieldName;
            private readonly BindingFlags _flags;

            public FieldKey(Type type, string fieldName, BindingFlags flags)
            {
                _type = type;
                _fieldName = fieldName;
                _flags = flags;
            }

            public bool Equals(FieldKey other)
            {
                return _type == other._type && _fieldName == other._fieldName && _flags == other._flags;
            }

            public override bool Equals(object obj)
            {
                return obj is FieldKey && Equals((FieldKey)obj);
            }

            public override int GetHashCode()
            {
                int hash = _type != null ? _type.GetHashCode() : 0;
                hash = (hash * 397) ^ (_fieldName != null ? _fieldName.GetHashCode() : 0);
                return (hash * 397) ^ (int)_flags;
            }
        }
    }
}
