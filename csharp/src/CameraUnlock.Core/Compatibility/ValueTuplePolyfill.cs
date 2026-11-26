#if NET35 || NET40
// ValueTuple polyfill for .NET 3.5
// Only the tuples actually used in CameraUnlock.Core are defined here.

namespace System
{
    /// <summary>
    /// Represents a 2-tuple value type for .NET 3.5 compatibility.
    /// </summary>
    public struct ValueTuple<T1, T2>
    {
        public T1 Item1;
        public T2 Item2;

        public ValueTuple(T1 item1, T2 item2)
        {
            Item1 = item1;
            Item2 = item2;
        }

        public void Deconstruct(out T1 item1, out T2 item2)
        {
            item1 = Item1;
            item2 = Item2;
        }

        public override bool Equals(object obj)
        {
            if (obj is ValueTuple<T1, T2> other)
            {
                return Equals(Item1, other.Item1) && Equals(Item2, other.Item2);
            }
            return false;
        }

        public override int GetHashCode()
        {
            int h1 = Item1?.GetHashCode() ?? 0;
            int h2 = Item2?.GetHashCode() ?? 0;
            return ((h1 << 5) + h1) ^ h2;
        }

        public override string ToString() => $"({Item1}, {Item2})";
    }

    /// <summary>
    /// Represents a 3-tuple value type for .NET 3.5 compatibility.
    /// </summary>
    public struct ValueTuple<T1, T2, T3>
    {
        public T1 Item1;
        public T2 Item2;
        public T3 Item3;

        public ValueTuple(T1 item1, T2 item2, T3 item3)
        {
            Item1 = item1;
            Item2 = item2;
            Item3 = item3;
        }

        public void Deconstruct(out T1 item1, out T2 item2, out T3 item3)
        {
            item1 = Item1;
            item2 = Item2;
            item3 = Item3;
        }

        public override bool Equals(object obj)
        {
            if (obj is ValueTuple<T1, T2, T3> other)
            {
                return Equals(Item1, other.Item1) &&
                       Equals(Item2, other.Item2) &&
                       Equals(Item3, other.Item3);
            }
            return false;
        }

        public override int GetHashCode()
        {
            int h1 = Item1?.GetHashCode() ?? 0;
            int h2 = Item2?.GetHashCode() ?? 0;
            int h3 = Item3?.GetHashCode() ?? 0;
            return ((h1 << 5) + h1) ^ ((h2 << 3) + h2) ^ h3;
        }

        public override string ToString() => $"({Item1}, {Item2}, {Item3})";
    }

    /// <summary>
    /// Represents a 4-tuple value type for .NET 3.5 compatibility.
    /// </summary>
    public struct ValueTuple<T1, T2, T3, T4>
    {
        public T1 Item1;
        public T2 Item2;
        public T3 Item3;
        public T4 Item4;

        public ValueTuple(T1 item1, T2 item2, T3 item3, T4 item4)
        {
            Item1 = item1;
            Item2 = item2;
            Item3 = item3;
            Item4 = item4;
        }

        public void Deconstruct(out T1 item1, out T2 item2, out T3 item3, out T4 item4)
        {
            item1 = Item1;
            item2 = Item2;
            item3 = Item3;
            item4 = Item4;
        }

        public override bool Equals(object obj)
        {
            if (obj is ValueTuple<T1, T2, T3, T4> other)
            {
                return Equals(Item1, other.Item1) &&
                       Equals(Item2, other.Item2) &&
                       Equals(Item3, other.Item3) &&
                       Equals(Item4, other.Item4);
            }
            return false;
        }

        public override int GetHashCode()
        {
            int h1 = Item1?.GetHashCode() ?? 0;
            int h2 = Item2?.GetHashCode() ?? 0;
            int h3 = Item3?.GetHashCode() ?? 0;
            int h4 = Item4?.GetHashCode() ?? 0;
            return ((h1 << 5) + h1) ^ ((h2 << 3) + h2) ^ ((h3 << 2) + h3) ^ h4;
        }

        public override string ToString() => $"({Item1}, {Item2}, {Item3}, {Item4})";
    }
}

namespace System.Runtime.CompilerServices
{
    /// <summary>
    /// Indicates that a tuple should be displayed with the specified element names.
    /// Required by C# compiler for named tuple support.
    /// </summary>
    [AttributeUsage(AttributeTargets.Field | AttributeTargets.Parameter | AttributeTargets.Property | AttributeTargets.ReturnValue | AttributeTargets.Class | AttributeTargets.Struct | AttributeTargets.Event)]
    public sealed class TupleElementNamesAttribute : Attribute
    {
        public string[] TransformNames { get; }

        public TupleElementNamesAttribute(string[] transformNames)
        {
            TransformNames = transformNames;
        }
    }
}
#endif // NET35 || NET40
