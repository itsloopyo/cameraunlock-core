using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.Json;
using Xunit;
using CameraUnlock.Core.Config;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// data/config-schema.json declares a type and a default for every concept, and neither
    /// the generator nor the library reads them. AGENTS.md makes a changed default a breaking
    /// change for every mod that pins this core, so a schema default free to drift from the
    /// shipped one is a published claim nothing holds to. These tests are what holds it.
    /// </summary>
    public class ConfigSchemaDefaultsTests
    {
        private static string RepoRoot([CallerFilePath] string sourceFile = "")
        {
            DirectoryInfo? dir = new DirectoryInfo(Path.GetDirectoryName(sourceFile)!);
            while (dir != null && !File.Exists(Path.Combine(dir.FullName, "data", "config-schema.json")))
            {
                dir = dir.Parent;
            }
            if (dir == null)
            {
                throw new InvalidOperationException(
                    "no data/config-schema.json above " + sourceFile);
            }
            return dir.FullName;
        }

        private static JsonDocument ReadSchema()
        {
            return JsonDocument.Parse(
                File.ReadAllText(Path.Combine(RepoRoot(), "data", "config-schema.json")));
        }

        // Every concept in the schema, bound to the field the parser writes it to. A concept
        // added without an entry here fails the test rather than shipping an unchecked default.
        private static Dictionary<string, object> ShippedDefaults()
        {
            var config = new HeadTrackingConfigData();
            return new Dictionary<string, object>
            {
                { "UdpPort", config.UdpPort },
                { "EnableOnStartup", config.EnableOnStartup },
                { "YawSensitivity", config.Sensitivity.Yaw },
                { "PitchSensitivity", config.Sensitivity.Pitch },
                { "RollSensitivity", config.Sensitivity.Roll },
                { "InvertYaw", config.Sensitivity.InvertYaw },
                { "InvertPitch", config.Sensitivity.InvertPitch },
                { "InvertRoll", config.Sensitivity.InvertRoll },
                { "LocalSmoothing", config.LocalSmoothing },
                { "RemoteSmoothing", config.RemoteSmoothing },
                { "WorldSpaceYaw", config.WorldSpaceYaw },
                { "AimDecoupling", config.AimDecouplingEnabled },
                { "ShowReticle", config.ShowDecoupledReticle },
                { "ReticleColor", config.ReticleColorRgba },
                { "PositionEnabled", config.PositionEnabled },
                { "PositionSensitivityX", config.Position.SensitivityX },
                { "PositionSensitivityY", config.Position.SensitivityY },
                { "PositionSensitivityZ", config.Position.SensitivityZ },
                { "PositionLimitX", config.Position.LimitX },
                { "PositionLimitY", config.Position.LimitY },
                { "PositionLimitYDown", config.Position.LimitYDown },
                { "PositionLimitZ", config.Position.LimitZ },
                { "PositionLimitZBack", config.Position.LimitZBack },
                { "InvertPositionX", config.Position.InvertX },
                { "InvertPositionY", config.Position.InvertY },
                { "InvertPositionZ", config.Position.InvertZ },
                { "TrackerPivotForward", config.TrackerPivotForward },
                { "TrackerPivotUp", config.TrackerPivotUp },
                { "ToggleKey", config.ToggleKeyName },
                { "PositionToggleKey", config.PositionToggleKeyName },
                { "ReticleToggleKey", config.ReticleToggleKeyName },
                { "CycleTrackingModeKey", config.CycleTrackingModeKeyName },
                { "YawModeKey", config.YawModeKeyName },
                { "RecenterKey", config.RecenterKeyName },
            };
        }

        [Fact]
        public void SchemaDefaults_MatchTheShippedDefaults()
        {
            Dictionary<string, object> shipped = ShippedDefaults();

            using (JsonDocument schema = ReadSchema())
            {
                foreach (JsonElement concept in schema.RootElement.GetProperty("concepts").EnumerateArray())
                {
                    string id = concept.GetProperty("id").GetString()!;
                    string type = concept.GetProperty("type").GetString()!;
                    JsonElement declared = concept.GetProperty("default");

                    Assert.True(shipped.ContainsKey(id),
                        "concept '" + id + "' declares a default in data/config-schema.json but nothing " +
                        "in ConfigSchemaDefaultsTests binds it to the field the parser writes it to");

                    object actual = shipped[id];
                    switch (type)
                    {
                        case "int":
                            Assert.True(declared.GetInt32() == (int)actual, Mismatch(id, declared, actual));
                            break;
                        case "bool":
                            Assert.True(declared.GetBoolean() == (bool)actual, Mismatch(id, declared, actual));
                            break;
                        case "float":
                            Assert.True((float)declared.GetDouble() == (float)actual, Mismatch(id, declared, actual));
                            break;
                        case "string":
                            Assert.True(declared.GetString() == (string)actual, Mismatch(id, declared, actual));
                            break;
                        case "color":
                            var rgba = (float[])actual;
                            int channel = 0;
                            foreach (JsonElement component in declared.EnumerateArray())
                            {
                                Assert.True((float)component.GetDouble() == rgba[channel],
                                    Mismatch(id, declared, actual));
                                channel++;
                            }
                            Assert.Equal(rgba.Length, channel);
                            break;
                        default:
                            throw new InvalidOperationException(
                                "concept '" + id + "' has type '" + type + "', which this test cannot compare");
                    }
                }
            }
        }

        private static string Mismatch(string id, JsonElement declared, object actual)
        {
            return "concept '" + id + "': data/config-schema.json declares default " +
                declared.GetRawText() + ", the shipped default is " + Describe(actual) +
                ". Changing a default is a breaking change for every mod that pins this core, " +
                "so move both together or neither.";
        }

        private static string Describe(object value)
        {
            var rgba = value as float[];
            if (rgba != null)
            {
                return "[" + string.Join(", ", Array.ConvertAll(
                    rgba, c => c.ToString(CultureInfo.InvariantCulture))) + "]";
            }
            return Convert.ToString(value, CultureInfo.InvariantCulture)!;
        }

        // Regression: the generator built the retired set by mapping over ALIASES, so the one
        // retired concept emitted two identical rows. HashSet swallowed the duplicate, which
        // is why it survived; the generated source is the artifact that has to be right.
        [Fact]
        public void GeneratedSchema_ListsEachRetiredConceptOnce()
        {
            string root = RepoRoot();
            string generated = File.ReadAllText(Path.Combine(
                root, "csharp", "src", "CameraUnlock.Core", "Config", "ConfigKeySchema.g.cs"));

            const string opener = "HashSet<string> Retired = new HashSet<string>";
            int start = generated.IndexOf(opener, StringComparison.Ordinal);
            Assert.True(start >= 0, "ConfigKeySchema.g.cs no longer declares a Retired set");
            int begin = generated.IndexOf('{', start + opener.Length) + 1;
            int end = generated.IndexOf("};", begin, StringComparison.Ordinal);

            var rows = new List<string>();
            foreach (string line in generated.Substring(begin, end - begin).Split('\n'))
            {
                string trimmed = line.Trim();
                if (trimmed.Length > 0) rows.Add(trimmed);
            }

            using (JsonDocument schema = ReadSchema())
            {
                int retiredConcepts = schema.RootElement.GetProperty("retired").GetArrayLength();
                Assert.Equal(retiredConcepts, rows.Count);
            }
            Assert.Equal(rows.Count, new HashSet<string>(rows).Count);
        }
    }
}
