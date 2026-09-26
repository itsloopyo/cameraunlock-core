using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.CompilerServices;
using System.Text.Json;
using Xunit;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Effects;

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

        // Every concept in the schema, bound to the field that holds it. A concept
        // added without an entry here fails the test rather than shipping an unchecked default.
        private static Dictionary<string, object> ShippedDefaults()
        {
            var config = new HeadTrackingConfigData();
            return new Dictionary<string, object>
            {
                { "UdpPort", config.UdpPort },
                { "EnableOnStartup", config.EnableOnStartup },
                { "DataFreshnessMs", config.DataFreshnessMs },
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
                { "RotationEnabled", config.RotationEnabled },
                { "PositionEnabled", config.PositionEnabled },
                { "PositionAllowed", config.PositionAllowed },
                { "TrueFreeLook", config.TrueFreeLook },
                { "PositionSensitivityX", config.Position.SensitivityX },
                { "PositionSensitivityY", config.Position.SensitivityY },
                { "PositionSensitivityZ", config.Position.SensitivityZ },
                { "PositionLimitX", config.Position.LimitX },
                { "PositionLimitY", config.Position.LimitY },
                { "PositionLimitYDown", config.Position.LimitYDown },
                { "PositionLimitZ", config.Position.LimitZ },
                { "PositionLimitZBack", config.Position.LimitZBack },
                { "CollisionEnabled", config.CollisionEnabled },
                { "CollisionMargin", config.CollisionMargin },
                { "CollisionChannel", config.CollisionChannel },
                { "CollisionReleaseSmoothing", config.CollisionReleaseSmoothing },
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
                { "TrueFreeLookKey", config.TrueFreeLookKeyName },
                { "RecenterKey", config.RecenterKeyName },
                { "LightFollowsHead", config.Light.FollowsHead },
                { "LightMultiplier", config.Light.Multiplier },
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
                        "in ConfigSchemaDefaultsTests binds it to the field that holds it");

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

        // A config table's fresh render compares a row's default with the concept's DefaultText as
        // the row's codec reads it, so every canonical concept's text reads, and reads as the schema's
        // default: the canonical_default where the concept has one (the hotkey lists,
        // CollisionEnabled), else the default.
        [Fact]
        public void EachCanonicalConceptsDefaultTextReadsAsItsSchemaDefault()
        {
            using (JsonDocument schema = ReadSchema())
            {
                var concepts = new Dictionary<string, JsonElement>();
                foreach (JsonElement concept in schema.RootElement.GetProperty("concepts").EnumerateArray())
                {
                    concepts.Add(concept.GetProperty("id").GetString()!, concept);
                }
                foreach (ConceptDescriptor descriptor in ConfigConcepts.All)
                {
                    JsonElement declared = concepts[descriptor.Id];
                    byte[] text = System.Text.Encoding.ASCII.GetBytes(descriptor.DefaultText);
                    string? error;
                    switch (descriptor)
                    {
                        case ConceptDescriptor<bool> b:
                            Assert.True(b.Codec.TryParse(text, out bool flag, out error), descriptor.Id + ": " + error);
                            Assert.Equal(declared.TryGetProperty("canonical_default", out JsonElement start)
                                ? start.GetBoolean()
                                : declared.GetProperty("default").GetBoolean(), flag);
                            break;
                        case ConceptDescriptor<int> i:
                            Assert.True(i.Codec.TryParse(text, out int whole, out error), descriptor.Id + ": " + error);
                            Assert.Equal(declared.GetProperty("default").GetInt32(), whole);
                            break;
                        case ConceptDescriptor<float> f:
                            Assert.True(f.Codec.TryParse(text, out float number, out error), descriptor.Id + ": " + error);
                            Assert.Equal((float)declared.GetProperty("default").GetDouble(), number);
                            break;
                        case ConceptDescriptor<string> s:
                            Assert.True(s.Codec.TryParse(text, out string? keys, out error), descriptor.Id + ": " + error);
                            Assert.Equal(declared.GetProperty("canonical_default").GetString(), keys);
                            break;
                        default:
                            throw new InvalidOperationException(descriptor.Id + " has a type this test cannot read");
                    }
                }
            }
        }

        // Every range the schema declares, held to the number it stands for. The position
        // limits, the tracker pivots and the light multiplier name the guard constants, so the
        // schema and the guards cannot move apart. Null is a side the range leaves open.
        private static Dictionary<string, double?[]> ExpectedRanges()
        {
            var unit = new double?[] { 0, 1 };
            var metres = new double?[] { 0, HeadTrackingConfigData.MaxDistanceMetres };
            return new Dictionary<string, double?[]>
            {
                { "UdpPort", new double?[] { 1, 65535 } },
                { "DataFreshnessMs", new double?[] { 1, int.MaxValue } },
                { "LocalSmoothing", unit },
                { "RemoteSmoothing", unit },
                { "CollisionReleaseSmoothing", unit },
                { "PositionLimitX", metres },
                { "PositionLimitY", metres },
                { "PositionLimitYDown", metres },
                { "PositionLimitZ", metres },
                { "PositionLimitZBack", metres },
                { "TrackerPivotForward", metres },
                { "TrackerPivotUp", metres },
                { "CollisionMargin", new double?[] { 0, null } },
                { "LightMultiplier", new double?[] { 0, HeadFollowLightSettings.MaxMultiplier } },
            };
        }

        [Fact]
        public void SchemaRanges_MatchTheGuards()
        {
            Dictionary<string, double?[]> expected = ExpectedRanges();
            var declaredIds = new List<string>();

            using (JsonDocument schema = ReadSchema())
            {
                foreach (JsonElement concept in schema.RootElement.GetProperty("concepts").EnumerateArray())
                {
                    JsonElement range;
                    if (!concept.TryGetProperty("range", out range)) continue;
                    string id = concept.GetProperty("id").GetString()!;
                    declaredIds.Add(id);

                    Assert.True(expected.ContainsKey(id),
                        "concept '" + id + "' declares a range that ConfigSchemaDefaultsTests does not expect");
                    Assert.True(Bound(range, "min") == expected[id][0],
                        "concept '" + id + "': range min " + range.GetRawText() + " is not " + expected[id][0]);
                    Assert.True(Bound(range, "max") == expected[id][1],
                        "concept '" + id + "': range max " + range.GetRawText() + " is not " + expected[id][1]);
                }
            }

            Assert.Equal(expected.Count, declaredIds.Count);
        }

        private static double? Bound(JsonElement range, string side)
        {
            JsonElement value;
            return range.TryGetProperty(side, out value) ? value.GetDouble() : (double?)null;
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
