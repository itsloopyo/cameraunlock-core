using System;
using System.Linq.Expressions;
using System.Reflection;

namespace CameraUnlock.Core.Reflection
{
    /// <summary>
    /// Builds expression-compiled getter delegates for reflected game members.
    /// A compiled delegate is an order of magnitude faster than FieldInfo.GetValue /
    /// PropertyInfo.GetValue per call, which matters when game state is read every frame.
    ///
    /// Build each getter once (at type-resolution time) and cache the returned delegate
    /// in a static field; never build per frame.
    /// </summary>
    public static class CompiledGetters
    {
        /// <summary>
        /// Builds a getter for a static field.
        /// </summary>
        public static Func<object> ForStaticField(FieldInfo field)
        {
            Expression body = Expression.Convert(Expression.Field(null, field), typeof(object));
            return Expression.Lambda<Func<object>>(body).Compile();
        }

        /// <summary>
        /// Builds a getter for an instance field. The argument is the instance to read from.
        /// </summary>
        public static Func<object, object> ForInstanceField(FieldInfo field)
        {
            ParameterExpression instance = Expression.Parameter(typeof(object), "instance");
            Expression typedInstance = Expression.Convert(instance, field.DeclaringType);
            Expression body = Expression.Convert(Expression.Field(typedInstance, field), typeof(object));
            return Expression.Lambda<Func<object, object>>(body, instance).Compile();
        }

        /// <summary>
        /// Builds a getter for an instance property. The argument is the instance to read from.
        /// </summary>
        public static Func<object, object> ForInstanceProperty(PropertyInfo property)
        {
            ParameterExpression instance = Expression.Parameter(typeof(object), "instance");
            Expression typedInstance = Expression.Convert(instance, property.DeclaringType);
            Expression body = Expression.Convert(Expression.Property(typedInstance, property), typeof(object));
            return Expression.Lambda<Func<object, object>>(body, instance).Compile();
        }

        /// <summary>
        /// Builds a typed getter for a static property. The typed result avoids boxing
        /// value-type members (e.g. a bool "is loading" flag read every frame).
        /// </summary>
        public static Func<T> ForStaticProperty<T>(PropertyInfo property)
        {
            Expression body = Expression.Convert(Expression.Property(null, property), typeof(T));
            return Expression.Lambda<Func<T>>(body).Compile();
        }

        /// <summary>
        /// Builds a typed getter for an instance field. The argument is the instance to
        /// read from. Use when the field type is known at compile time (e.g. a Unity
        /// component reference) so callers avoid a per-call cast.
        /// </summary>
        public static Func<object, T> ForInstanceField<T>(FieldInfo field)
        {
            ParameterExpression instance = Expression.Parameter(typeof(object), "instance");
            Expression typedInstance = Expression.Convert(instance, field.DeclaringType);
            Expression body = Expression.Convert(Expression.Field(typedInstance, field), typeof(T));
            return Expression.Lambda<Func<object, T>>(body, instance).Compile();
        }
    }
}
