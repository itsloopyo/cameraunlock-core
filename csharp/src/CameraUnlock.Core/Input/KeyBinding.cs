using System;

namespace CameraUnlock.Core.Input
{
    /// <summary>
    /// The modifier keys a binding can name. The numbers match the C++
    /// cameraunlock::input::KeyModifiers.
    /// </summary>
    [Flags]
    public enum KeyModifiers
    {
        None = 0,
        Ctrl = 1,
        Shift = 2,
        Alt = 4,
    }

    /// <summary>
    /// One binding of a hotkey list: a UnityEngine.KeyCode value and the modifiers held with
    /// it. <see cref="KeyBindings"/> reads and writes lists of them.
    /// </summary>
    public struct KeyBinding : IEquatable<KeyBinding>
    {
        private const KeyModifiers AllModifiers = KeyModifiers.Ctrl | KeyModifiers.Shift | KeyModifiers.Alt;

        /// <exception cref="ArgumentOutOfRangeException"><paramref name="modifiers"/> holds a bit that is not a <see cref="KeyModifiers"/> flag.</exception>
        public KeyBinding(KeyModifiers modifiers, int unityKeyCode)
        {
            if ((modifiers & ~AllModifiers) != 0)
            {
                throw new ArgumentOutOfRangeException("modifiers", modifiers, "not a set of KeyModifiers flags");
            }
            Modifiers = modifiers;
            UnityKeyCode = unityKeyCode;
        }

        public KeyModifiers Modifiers { get; }

        /// <summary>The key as a UnityEngine.KeyCode value.</summary>
        public int UnityKeyCode { get; }

        public bool Equals(KeyBinding other)
        {
            return Modifiers == other.Modifiers && UnityKeyCode == other.UnityKeyCode;
        }

#if NULLABLE_ENABLED
        public override bool Equals(object? obj)
#else
        public override bool Equals(object obj)
#endif
        {
            return obj is KeyBinding && Equals((KeyBinding)obj);
        }

        public override int GetHashCode()
        {
            return (UnityKeyCode * 8) ^ (int)Modifiers;
        }

        public static bool operator ==(KeyBinding a, KeyBinding b)
        {
            return a.Equals(b);
        }

        public static bool operator !=(KeyBinding a, KeyBinding b)
        {
            return !a.Equals(b);
        }
    }
}
