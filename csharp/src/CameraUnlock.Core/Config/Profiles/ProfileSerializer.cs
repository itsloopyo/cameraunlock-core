using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using CameraUnlock.Core.Processing.AxisTransform;

namespace CameraUnlock.Core.Config.Profiles
{
    /// <summary>
    /// Serializes and deserializes ConfigProfile objects to/from .profile files.
    /// Uses a simple key=value format that's human-readable and editable.
    /// </summary>
    public static class ProfileSerializer
    {
        private const string FileHeader = "# CameraUnlock Configuration Profile";
        private const string DateFormat = "yyyy-MM-dd HH:mm:ss";

        // .NET 3.5 compatibility helper
        private static bool IsNullOrWhiteSpace(string value)
        {
            if (value == null) return true;
            for (int i = 0; i < value.Length; i++)
            {
                if (!char.IsWhiteSpace(value[i])) return false;
            }
            return true;
        }

        // .NET 3.5 compatibility helper for Enum.TryParse
        // Only catches ArgumentException (invalid enum value) - other exceptions propagate
        private static bool TryParseEnum<T>(string value, out T result) where T : struct
        {
            result = default(T);
            if (string.IsNullOrEmpty(value)) return false;
            try
            {
                result = (T)Enum.Parse(typeof(T), value, true);
            }
            catch (ArgumentException)
            {
                // Value is not a valid member of the enum - expected TryParse behavior
                return false;
            }

            // Enum.Parse does not throw for a bare number ("99") or a comma list
            // ("Yaw,Pitch", which it ORs into an unrelated member), so a corrupt file
            // would otherwise assign an out-of-range Source and silently kill the axis.
            return Enum.IsDefined(typeof(T), result);
        }

        // A key or value carrying a CR/LF would split into extra key=value lines on the
        // way back in, silently truncating the field and letting a setting value inject
        // metadata (a "Note" holding "x\nIsReadOnly=True" comes back read-only, which
        // makes the profile permanently unsaveable). Flattening to a space keeps the
        // whole text and needs no matching unescape, so files written by older versions
        // still read back byte-identically.
        private static string Flatten(string value)
        {
            if (string.IsNullOrEmpty(value)) return value;
            if (value.IndexOf('\r') < 0 && value.IndexOf('\n') < 0) return value;
            return value.Replace('\r', ' ').Replace('\n', ' ');
        }

        /// <summary>
        /// Serializes a profile to a string.
        /// </summary>
        /// <param name="profile">The profile to serialize.</param>
        /// <returns>String content suitable for writing to a .profile file.</returns>
        /// <exception cref="ArgumentNullException">Thrown if profile is null.</exception>
        public static string Serialize(ConfigProfile profile)
        {
            if (profile == null)
            {
                throw new ArgumentNullException(nameof(profile));
            }

            var sb = new StringBuilder();

            // Header
            sb.AppendLine(FileHeader);
            sb.AppendLine("# Generated: " + DateTime.Now.ToString(DateFormat, CultureInfo.InvariantCulture));
            sb.AppendLine();

            // Metadata
            sb.AppendLine("Name=" + Flatten(profile.Name ?? ""));
            sb.AppendLine("Description=" + Flatten(profile.Description ?? ""));
            sb.AppendLine("GameName=" + Flatten(profile.GameName ?? "General"));
            sb.AppendLine("CreatedDate=" + profile.CreatedDate.ToString(DateFormat, CultureInfo.InvariantCulture));
            sb.AppendLine("ModifiedDate=" + profile.ModifiedDate.ToString(DateFormat, CultureInfo.InvariantCulture));
            sb.AppendLine("IsDefault=" + profile.IsDefault.ToString());
            sb.AppendLine("IsReadOnly=" + profile.IsReadOnly.ToString());
            sb.AppendLine();

            // Settings
            if (profile.Settings != null && profile.Settings.Count > 0)
            {
                sb.AppendLine("# Configuration Settings");
                foreach (var kvp in profile.Settings)
                {
                    string valueStr = SerializeValue(kvp.Value);
                    sb.AppendLine("Setting." + Flatten(kvp.Key) + "=" + Flatten(valueStr));
                }
                sb.AppendLine();
            }

            // Axis mappings
            if (profile.AxisMapping != null)
            {
                sb.AppendLine("# Axis Mapping Configuration");
                SerializeAxisConfig(sb, "Yaw", profile.AxisMapping.YawConfig);
                SerializeAxisConfig(sb, "Pitch", profile.AxisMapping.PitchConfig);
                SerializeAxisConfig(sb, "Roll", profile.AxisMapping.RollConfig);
            }

            return sb.ToString();
        }

        /// <summary>
        /// Writes a profile to a file.
        /// </summary>
        /// <param name="profile">The profile to write.</param>
        /// <param name="filePath">Path to the file.</param>
        /// <exception cref="ArgumentNullException">Thrown if profile or filePath is null.</exception>
        public static void WriteToFile(ConfigProfile profile, string filePath)
        {
            if (profile == null)
            {
                throw new ArgumentNullException(nameof(profile));
            }
            if (string.IsNullOrEmpty(filePath))
            {
                throw new ArgumentNullException(nameof(filePath));
            }

            string content = Serialize(profile);

            // Write-then-rename. A direct WriteAllText truncates in place, so a crash
            // mid-write leaves a half-file that Deserialize happily accepts (it skips
            // unparseable lines and never throws), silently replacing the user's tuned
            // profile with constructor defaults that still load clean.
            string tempPath = filePath + ".tmp";
            File.WriteAllText(tempPath, content, Encoding.UTF8);
            if (File.Exists(filePath))
            {
                File.Delete(filePath);
            }
            File.Move(tempPath, filePath);
        }

        /// <summary>
        /// Deserializes a profile from a string.
        /// </summary>
        /// <param name="content">The profile file content.</param>
        /// <returns>The deserialized profile.</returns>
        /// <exception cref="ArgumentNullException">Thrown if content is null.</exception>
        public static ConfigProfile Deserialize(string content)
        {
            if (content == null)
            {
                throw new ArgumentNullException(nameof(content));
            }

            var profile = new ConfigProfile();
            var lines = content.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);

            foreach (var line in lines)
            {
                if (IsNullOrWhiteSpace(line) || line.StartsWith("#"))
                {
                    continue;
                }

                int eqIndex = line.IndexOf('=');
                if (eqIndex < 0)
                {
                    continue;
                }

                string key = line.Substring(0, eqIndex).Trim();
                string value = line.Substring(eqIndex + 1).Trim();

                ParseLine(profile, key, value);
            }

            return profile;
        }

        /// <summary>
        /// Reads a profile from a file.
        /// </summary>
        /// <param name="filePath">Path to the file.</param>
        /// <returns>The deserialized profile.</returns>
        /// <exception cref="ArgumentNullException">Thrown if filePath is null.</exception>
        /// <exception cref="FileNotFoundException">Thrown if file doesn't exist.</exception>
        public static ConfigProfile ReadFromFile(string filePath)
        {
            if (string.IsNullOrEmpty(filePath))
            {
                throw new ArgumentNullException(nameof(filePath));
            }
            if (!File.Exists(filePath))
            {
                throw new FileNotFoundException("Profile file not found", filePath);
            }

            string content = File.ReadAllText(filePath, Encoding.UTF8);
            return Deserialize(content);
        }

        private static void SerializeAxisConfig(StringBuilder sb, string axisName, AxisConfig config)
        {
            if (config == null) return;

            string prefix = "AxisMapping." + axisName + ".";
            sb.AppendLine(prefix + "Source=" + config.Source);
            sb.AppendLine(prefix + "Sensitivity=" + config.Sensitivity.ToString("F4", CultureInfo.InvariantCulture));
            sb.AppendLine(prefix + "Inverted=" + config.Inverted);
            sb.AppendLine(prefix + "DeadzoneMin=" + config.DeadzoneMin.ToString("F4", CultureInfo.InvariantCulture));
            sb.AppendLine(prefix + "DeadzoneMax=" + config.DeadzoneMax.ToString("F4", CultureInfo.InvariantCulture));
            sb.AppendLine(prefix + "MinLimit=" + config.MinLimit.ToString("F4", CultureInfo.InvariantCulture));
            sb.AppendLine(prefix + "MaxLimit=" + config.MaxLimit.ToString("F4", CultureInfo.InvariantCulture));
            sb.AppendLine(prefix + "EnableLimits=" + config.EnableLimits);
            sb.AppendLine(prefix + "SensitivityCurve=" + config.SensitivityCurve);
            sb.AppendLine(prefix + "CurveStrength=" + config.CurveStrength.ToString("F4", CultureInfo.InvariantCulture));
            sb.AppendLine(prefix + "MaxInputRange=" + config.MaxInputRange.ToString("F4", CultureInfo.InvariantCulture));
        }

        private static string SerializeValue(object value)
        {
            if (value == null) return "";
            if (value is float f) return f.ToString("F6", CultureInfo.InvariantCulture);
            if (value is double d) return d.ToString("F6", CultureInfo.InvariantCulture);
            if (value is bool b) return b.ToString();
            if (value is Enum e) return e.ToString();
            // ParseValue reads back with InvariantCulture, so anything formattable has to
            // be written that way too or a profile stops round-tripping on a locale whose
            // negative sign or decimal separator differs (int -5 writes as U+2212 5 under
            // ICU, comes back as a string, and GetSetting<int> then throws).
            if (value is IFormattable formattable) return formattable.ToString(null, CultureInfo.InvariantCulture);
            return value.ToString();
        }

        private static void ParseLine(ConfigProfile profile, string key, string value)
        {
            // Metadata
            switch (key)
            {
                case "Name":
                    profile.Name = value;
                    return;
                case "Description":
                    profile.Description = value;
                    return;
                case "GameName":
                    profile.GameName = value;
                    return;
                case "CreatedDate":
                    if (DateTime.TryParseExact(value, DateFormat, CultureInfo.InvariantCulture, DateTimeStyles.None, out var created))
                        profile.CreatedDate = created;
                    return;
                case "ModifiedDate":
                    if (DateTime.TryParseExact(value, DateFormat, CultureInfo.InvariantCulture, DateTimeStyles.None, out var modified))
                        profile.ModifiedDate = modified;
                    return;
                case "IsDefault":
                    if (bool.TryParse(value, out var isDefault))
                        profile.IsDefault = isDefault;
                    return;
                case "IsReadOnly":
                    if (bool.TryParse(value, out var isReadOnly))
                        profile.IsReadOnly = isReadOnly;
                    return;
            }

            // Settings
            if (key.StartsWith("Setting."))
            {
                string settingKey = key.Substring(8);
                profile.Settings[settingKey] = ParseValue(value);
                return;
            }

            // Axis mappings
            if (key.StartsWith("AxisMapping."))
            {
                ParseAxisMapping(profile, key.Substring(12), value);
            }
        }

        private static object ParseValue(string value)
        {
            if (bool.TryParse(value, out bool boolVal))
                return boolVal;
            if (int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out int intVal))
                return intVal;
            if (float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out float floatVal))
                return floatVal;
            return value;
        }

        private static void ParseAxisMapping(ConfigProfile profile, string key, string value)
        {
            if (profile.AxisMapping == null)
            {
                profile.AxisMapping = new MappingConfig();
            }

            var parts = key.Split('.');
            if (parts.Length < 2) return;

            AxisConfig config;
            switch (parts[0])
            {
                case "Yaw":
                    config = profile.AxisMapping.YawConfig;
                    break;
                case "Pitch":
                    config = profile.AxisMapping.PitchConfig;
                    break;
                case "Roll":
                    config = profile.AxisMapping.RollConfig;
                    break;
                default:
                    return;
            }

            // Declare temp variables for TryParse (C# 7.3 compatibility)
            AxisSource sourceTemp;
            SensitivityCurve curveTemp;
            float floatTemp;
            bool boolTemp;

            switch (parts[1])
            {
                case "Source":
                    if (TryParseEnum(value, out sourceTemp))
                        config.Source = sourceTemp;
                    break;
                case "Sensitivity":
                    if (float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out floatTemp))
                        config.Sensitivity = floatTemp;
                    break;
                case "Inverted":
                    if (bool.TryParse(value, out boolTemp))
                        config.Inverted = boolTemp;
                    break;
                case "DeadzoneMin":
                    if (float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out floatTemp))
                        config.DeadzoneMin = floatTemp;
                    break;
                case "DeadzoneMax":
                    if (float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out floatTemp))
                        config.DeadzoneMax = floatTemp;
                    break;
                case "MinLimit":
                    if (float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out floatTemp))
                        config.MinLimit = floatTemp;
                    break;
                case "MaxLimit":
                    if (float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out floatTemp))
                        config.MaxLimit = floatTemp;
                    break;
                case "EnableLimits":
                    if (bool.TryParse(value, out boolTemp))
                        config.EnableLimits = boolTemp;
                    break;
                case "SensitivityCurve":
                    if (TryParseEnum(value, out curveTemp))
                        config.SensitivityCurve = curveTemp;
                    break;
                case "CurveStrength":
                    if (float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out floatTemp))
                        config.CurveStrength = floatTemp;
                    break;
                case "MaxInputRange":
                    if (float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out floatTemp))
                        config.MaxInputRange = floatTemp;
                    break;
            }
        }
    }
}
