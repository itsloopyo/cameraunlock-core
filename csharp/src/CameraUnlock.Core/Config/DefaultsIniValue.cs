namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// One canonical concept as Defaults.ini holds it. For a line that was read, the section, key
    /// and value are the file's own bytes and the line is 1-based; for an absent concept they are
    /// empty and the line is 0. The reason is empty unless the value is refused, and it and the
    /// lines decode the value by <see cref="CodecText.Utf8Text"/>, as C++ core writes them.
    /// </summary>
    internal sealed class DefaultsIniValue
    {
        internal static readonly DefaultsIniValue Absent =
            new DefaultsIniValue(DefaultsIniValueState.Absent, 0, new byte[0], new byte[0], new byte[0], string.Empty);

        internal DefaultsIniValue(DefaultsIniValueState state, int line, byte[] section, byte[] key, byte[] value, string reason)
        {
            State = state;
            Line = line;
            Section = section;
            Key = key;
            Value = value;
            Reason = reason;
        }

        internal DefaultsIniValueState State { get; }

        internal int Line { get; }

        internal byte[] Section { get; }

        internal byte[] Key { get; }

        internal byte[] Value { get; }

        internal string Reason { get; }
    }
}
