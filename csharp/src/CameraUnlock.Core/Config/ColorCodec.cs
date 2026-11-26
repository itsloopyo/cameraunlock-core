using System;
using System.Collections.Generic;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Red, green, blue and alpha as a four-element array: four floats in [0,1], written
    /// <c>r, g, b, a</c> with the <see cref="FloatCodec"/> rule, read as exactly four
    /// comma-separated floats, each trimmed of spaces and tabs.
    /// </summary>
    public sealed class ColorCodec : IValueCodec<float[]>
    {
        private static readonly FloatCodec Component = new FloatCodec(0f, 1f);

        /// <inheritdoc/>
#if NULLABLE_ENABLED
        public bool TryParse(byte[] text, out float[] value, out string? error)
#else
        public bool TryParse(byte[] text, out float[] value, out string error)
#endif
        {
            if (text == null) throw new ArgumentNullException("text");

            value = new float[0];
            error = null;
            List<byte[]> parts = CodecText.SplitAtCommasTrimmed(text);
            if (parts.Count != 4)
            {
                error = "expected four numbers from 0.0 to 1.0 separated by commas, such as 1.0, 0.5, 0.0, 1.0";
                return false;
            }
            var rgba = new float[4];
            for (int i = 0; i < 4; i++)
            {
                if (!Component.TryParse(parts[i], out rgba[i], out error))
                {
                    error = "item " + CodecText.Number(i + 1) + " " + CodecText.Quote(parts[i]) + ": " + error;
                    return false;
                }
            }
            value = rgba;
            return true;
        }

        /// <inheritdoc/>
        /// <exception cref="ArgumentNullException"><paramref name="value"/> is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="value"/> does not hold four
        /// components, or one is not finite or is outside [0,1].</exception>
        public byte[] Render(float[] value)
        {
            if (value == null) throw new ArgumentNullException("value");
            if (value.Length != 4) throw new ArgumentException("a color has four components, r, g, b and a", "value");

            var text = new List<byte>();
            for (int i = 0; i < 4; i++)
            {
                if (i > 0)
                {
                    text.Add((byte)',');
                    text.Add((byte)' ');
                }
                text.AddRange(Component.Render(value[i]));
            }
            return text.ToArray();
        }

        /// <summary>Bitwise, component by component.</summary>
        /// <exception cref="ArgumentNullException"><paramref name="a"/> or <paramref name="b"/> is null.</exception>
        public bool Equal(float[] a, float[] b)
        {
            return CodecText.ListEqual(a, b, Component);
        }
    }
}
