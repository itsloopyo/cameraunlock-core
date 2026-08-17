using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using CameraUnlock.Core.Math;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Utilities for parsing common configuration value types.
    /// </summary>
    public static class ConfigParsingUtils
    {
        /// <summary>
        /// Parses a color from "R,G,B" or "R,G,B,A" format (values 0-1 or 0-255).
        /// </summary>
        /// <param name="value">The color string to parse.</param>
        /// <param name="rgba">Output array of 4 floats [R,G,B,A].</param>
        /// <returns>True if parsing succeeded.</returns>
        public static bool TryParseColor(string value, out float[] rgba)
        {
            rgba = new float[] { 1f, 1f, 1f, 1f };

            if (string.IsNullOrEmpty(value))
                return false;

            string[] parts = value.Split(',');
            if (parts.Length < 3)
                return false;

            float r, g, b, a = 1f;
            if (!TryParseFloat(parts[0], out r) ||
                !TryParseFloat(parts[1], out g) ||
                !TryParseFloat(parts[2], out b))
                return false;

            // If the user supplied an alpha component, it must parse. Silently
            // falling back to 1.0 hides typos in config files.
            if (parts.Length >= 4 && !TryParseFloat(parts[3], out a))
                return false;

            // Auto-detect 0-255 range vs 0-1 range. Use the max of the RGB
            // channels (not OR-of-channels) so a single out-of-range value
            // is still scaled with its peers; alpha is detected independently
            // because it routinely defaults to 1.0 in 0-255 input as well.
            float maxRgb = r;
            if (g > maxRgb) maxRgb = g;
            if (b > maxRgb) maxRgb = b;
            if (maxRgb > 1f)
            {
                r /= 255f;
                g /= 255f;
                b /= 255f;
            }
            if (a > 1f) a /= 255f;

            rgba[0] = MathUtils.Clamp01(r);
            rgba[1] = MathUtils.Clamp01(g);
            rgba[2] = MathUtils.Clamp01(b);
            rgba[3] = MathUtils.Clamp01(a);
            return true;
        }

        /// <summary>
        /// Parses a finite float value using invariant culture. "NaN", "Infinity" and
        /// "-Infinity" parse successfully as far as <c>float.TryParse</c> is concerned, so
        /// they are rejected here instead: a config file is a system boundary, and a
        /// non-finite value that gets past it is not recoverable downstream. A NaN
        /// smoothing value slips through the clamp in
        /// <see cref="MathUtils.Clamp01"/> and <c>CalculateSmoothingFactor</c> alike -
        /// every comparison against NaN is false, so no bound fires - reaches exp(), and
        /// the smoothed pose is NaN from that frame on, permanently.
        /// </summary>
        public static bool TryParseFloat(string value, out float result)
        {
            if (string.IsNullOrEmpty(value))
            {
                result = 0f;
                return false;
            }
            if (!float.TryParse(value.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out result))
            {
                return false;
            }
            if (float.IsNaN(result) || float.IsInfinity(result))
            {
                result = 0f;
                return false;
            }
            return true;
        }

        /// <summary>
        /// Parses an int value.
        /// </summary>
        public static bool TryParseInt(string value, out int result)
        {
            if (string.IsNullOrEmpty(value))
            {
                result = 0;
                return false;
            }
            // Pinned like TryParseFloat above. The default overload resolves the
            // negative sign and the accepted whitespace from CurrentCulture, so on a
            // locale whose NegativeSign is not U+002D a leading "-" stops parsing. This
            // is the untrusted-input boundary and must not vary by locale.
            return int.TryParse(value.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out result);
        }

        /// <summary>
        /// Parses a bool value (accepts true/false, yes/no, 1/0).
        /// </summary>
        public static bool TryParseBool(string value, out bool result)
        {
            result = false;
            if (string.IsNullOrEmpty(value))
                return false;

            string trimmed = value.Trim().ToLowerInvariant();
            if (trimmed == "true" || trimmed == "yes" || trimmed == "1")
            {
                result = true;
                return true;
            }
            if (trimmed == "false" || trimmed == "no" || trimmed == "0")
            {
                result = false;
                return true;
            }
            return false;
        }

        /// <summary>
        /// Parses an INI-style configuration file into a dictionary.
        /// </summary>
        /// <param name="filePath">Path to the config file.</param>
        /// <returns>Dictionary of key-value pairs (keys are lowercased).</returns>
        public static Dictionary<string, string> ParseIniFile(string filePath)
        {
            var result = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

            if (!File.Exists(filePath))
                return result;

            foreach (string line in File.ReadAllLines(filePath))
            {
                string trimmed = line.Trim();

                // Skip empty lines and comments
                if (string.IsNullOrEmpty(trimmed) ||
                    trimmed.StartsWith("#") ||
                    trimmed.StartsWith(";") ||
                    trimmed.StartsWith("["))
                    continue;

                int eqIndex = trimmed.IndexOf('=');
                if (eqIndex <= 0)
                    continue;

                string key = trimmed.Substring(0, eqIndex).Trim();
                string value = trimmed.Substring(eqIndex + 1).Trim();

                // Remove surrounding quotes if present
                if (value.Length >= 2 &&
                    ((value.StartsWith("\"") && value.EndsWith("\"")) ||
                     (value.StartsWith("'") && value.EndsWith("'"))))
                {
                    value = value.Substring(1, value.Length - 2);
                }

                result[key] = value;
            }

            return result;
        }

        /// <summary>
        /// Gets the directory containing the specified assembly.
        /// </summary>
        public static string GetAssemblyDirectory(System.Reflection.Assembly assembly)
        {
            // Use ReferenceEquals for older Mono compatibility (no Assembly.op_Equality)
            if (ReferenceEquals(assembly, null))
                return string.Empty;

            string location = assembly.Location;
            if (string.IsNullOrEmpty(location))
                return string.Empty;

            return Path.GetDirectoryName(location) ?? string.Empty;
        }
    }
}
