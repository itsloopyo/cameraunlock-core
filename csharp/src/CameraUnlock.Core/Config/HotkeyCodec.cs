using System;
using System.Text;
using CameraUnlock.Core.Input;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A hotkey list as Unity mods read it (<see cref="KeyBindings.TryParse"/>), held as its
    /// canonical text: <c>End, Ctrl+Shift+Y</c>, or empty for unbound. TryParse reads every
    /// spelling KeyBindings reads and gives the canonical text of what it read, so
    /// <c>ctrl+shift+y,end</c> reads as <c>Ctrl+Shift+Y, End</c>. The C++ twin,
    /// cameraunlock::config::HotkeyCodec, reads the native dialect of the same syntax.
    /// </summary>
    public sealed class HotkeyCodec : IValueCodec<string>
    {
        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out string value, out string? error)
#else
        public bool TryParse(byte[] text, out string value, out string error)
#endif
        {
            if (text == null) throw new ArgumentNullException("text");

            value = string.Empty;
            KeyBinding[] bindings;
            if (!KeyBindings.TryParse(CodecText.Utf8Text(text), out bindings, out error)) return false;
            value = KeyBindings.Format(bindings);
            return true;
        }

        /// <inheritdoc/>
        /// <exception cref="ArgumentNullException"><paramref name="value"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="value"/> is not the canonical text
        /// of a hotkey list, which would not read back as itself.</exception>
        public byte[] Render(string value)
        {
            if (value == null) throw new ArgumentNullException("value");

            KeyBinding[] bindings;
#if NULLABLE_ENABLED
            string? error;
#else
            string error;
#endif
            if (!KeyBindings.TryParse(value, out bindings, out error))
            {
                throw new ArgumentException("'" + value + "' is not a hotkey list: " + error, "value");
            }
            string canonical = KeyBindings.Format(bindings);
            if (canonical != value)
            {
                throw new ArgumentException("'" + value + "' is not written as a hotkey list is written: expected '"
                    + canonical + "'", "value");
            }
            return Encoding.ASCII.GetBytes(canonical);
        }

        /// <inheritdoc/>
        public bool Equal(string a, string b)
        {
            return string.Equals(a, b, StringComparison.Ordinal);
        }
    }
}
