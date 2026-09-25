using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using CameraUnlock.Core.Config;
using Xunit;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// Runs <see cref="ConfigTableFixtures"/> on this test host's runtime, plus the construction
    /// checks, the modifiers, the Unity hotkey dialect and the generated concept table against
    /// data/config-schema.json. The CameraUnlock.Core.FrameworkTests console runs the same fixtures on
    /// .NET Framework 3.5 and 4.7.2 (pixi run test-framework).
    /// </summary>
    public class ConfigTableTests
    {
        private static string Root([CallerFilePath] string sourceFile = "")
        {
            return CanonicalIniFixtures.FindRoot(Path.GetDirectoryName(sourceFile)!);
        }

        public static IEnumerable<object[]> Cases()
        {
            return ConfigTableFixtures.Cases(Root()).Select(c => new object[] { c });
        }

        [Theory]
        [MemberData(nameof(Cases))]
        public void Fixture(string name)
        {
            ConfigTableFixtures.RunCase(Root(), name);
        }

        private sealed class Small
        {
            public bool Rotation = true;
            public bool Position = true;
            public int Value = 5;
            public int Other = 1;
            public float Scale = 1.0f;
            public string Text = "a";
            public string Key = "End";
            public bool Flag;
        }

        private static ConfigTable<Small> NewTable()
        {
            return new ConfigTable<Small>(() => new Small());
        }

        private static ConfigTable<Small> WithOffset(ConfigTable<Small> table, string section, string key, string comment)
        {
            return table.Local(section, key, s => s.Value, (s, v) => s.Value = v, new IntCodec(), comment);
        }

        private static string Rendered(ConfigTable<Small> table, Small values)
        {
            return Encoding.ASCII.GetString(table.Render(values, new RenderHeader("G")));
        }

        [Fact]
        public void TwoRowsWithOneKeyNameThrow()
        {
            var e = Assert.Throws<ArgumentException>(() =>
                WithOffset(NewTable(), "Camera", "Offset", "One.")
                    .Local("Debug", "OFFSET", s => s.Other, (s, v) => s.Other = v, new IntCodec(), "Two."));
            Assert.Contains("a key name is used once in the file", e.Message);
            Assert.Throws<ArgumentException>(() => NewTable()
                .Concept(ConfigConcepts.RotationEnabled, s => s.Rotation, (s, v) => s.Rotation = v)
                .Concept(ConfigConcepts.RotationEnabled, s => s.Position, (s, v) => s.Position = v));
        }

        [Theory]
        [InlineData("ConfigFormat")]
        [InlineData("Configformat")]
        public void ALocalKeyNamedLikeTheFormatStampThrows(string key)
        {
            var e = Assert.Throws<ArgumentException>(() => WithOffset(NewTable(), "Debug", key, "One."));
            Assert.Contains("[CameraUnlock] ConfigFormat already has that key, and a key name is used once in the file",
                e.Message);
        }

        [Theory]
        [InlineData("CameraUnlock", "[CameraUnlock] belongs to core")]
        [InlineData("cameraunlock", "PascalCase")]
        [InlineData("Sensitivity", "holds none of the settings a canonical file writes")]
        [InlineData("Inversion", "holds none of the settings a canonical file writes")]
        [InlineData("Reticle", "holds none of the settings a canonical file writes")]
        [InlineData("POSITION", "the schema spells this section [Position]")]
        public void LocalSectionsThatThrow(string section, string message)
        {
            var e = Assert.Throws<ArgumentException>(() => WithOffset(NewTable(), section, "Offset", "One."));
            Assert.Contains(message, e.Message);
        }

        [Fact]
        public void LocalRowsInAPositionSectionAndAnotherSpellingOfALocalSection()
        {
            WithOffset(NewTable(), "Position", "LeanOffset", "One.");
            var e = Assert.Throws<ArgumentException>(() =>
                WithOffset(WithOffset(NewTable(), "Camera", "Offset", "One."), "CAMERA", "Other", "Two."));
            Assert.Contains("spells this section [Camera]", e.Message);
        }

        [Theory]
        [InlineData("Port")]
        [InlineData("UdpPort")]
        [InlineData("YawSensitivity")]
        [InlineData("InvertX")]
        [InlineData("Smoothing")]
        [InlineData("RecenterKey")]
        [InlineData("LimitYDown")]
        public void LocalKeysNamingAConceptThrow(string key)
        {
            var e = Assert.Throws<ArgumentException>(() => WithOffset(NewTable(), "Camera", key, "One."));
            Assert.Contains("is the key or an alias of the schema concept", e.Message);
        }

        [Theory]
        [InlineData("Deadzone")]
        [InlineData("DeadzoneDeg")]
        [InlineData("YawDeadzone")]
        [InlineData("EnableDeadzone")]
        [InlineData("ResponseCurve")]
        [InlineData("RollCurve")]
        public void LocalKeysNamingANonCanonicalSettingThrow(string key)
        {
            var e = Assert.Throws<ArgumentException>(() => WithOffset(NewTable(), "Camera", key, "One."));
            Assert.Contains("names a setting a canonical file does not carry, so a game-local row cannot use it: The mod "
                + "applies the head pose as the tracker sends it", e.Message);
        }

        [Fact]
        public void TheNonCanonicalKeysAreTheSchemasAndNoFlatReaderResolvesThem()
        {
            var expected = new Dictionary<string, string>();
            foreach (JsonElement group in Schema().GetProperty("non_canonical_keys").EnumerateArray())
            {
                foreach (JsonElement spelling in group.GetProperty("spellings").EnumerateArray())
                {
                    expected.Add(ConfigKeySchema.Normalize(spelling.GetString()!), group.GetProperty("canonical_reason").GetString()!);
                }
            }
            Assert.Equal(expected, ConfigConcepts.NonCanonicalKeyReasons);
            foreach (string normalized in ConfigConcepts.NonCanonicalKeyReasons.Keys)
            {
                Assert.Null(ConfigKeySchema.Resolve(normalized));
            }
        }

        [Fact]
        public void CommentRules()
        {
            Assert.Contains("PascalCase", Assert.Throws<ArgumentException>(() =>
                WithOffset(NewTable(), "Camera", "cb_size", "One.")).Message);
            Assert.Contains("needs a comment", Assert.Throws<ArgumentException>(() =>
                WithOffset(NewTable(), "Camera", "Offset", "")).Message);
            Assert.Contains("needs a comment", Assert.Throws<ArgumentException>(() =>
                WithOffset(NewTable(), "Camera", "Offset", "One.")
                    .Local("Debug", "Other", s => s.Other, (s, v) => s.Other = v, new IntCodec(), "")).Message);
            Assert.Contains("needs a comment", Assert.Throws<ArgumentException>(() =>
                NewTable()
                    .Concept(ConfigConcepts.PositionEnabled, s => s.Position, (s, v) => s.Position = v)
                    .Local("Position", "LeanScale", s => s.Scale, (s, v) => s.Scale = v, new FloatCodec(), "")).Message);
            WithOffset(NewTable(), "Camera", "Offset", "One.")
                .Local("Debug", "Flag", s => s.Flag, (s, v) => s.Flag = v, new BoolCodec(), "Two.")
                .Local("Camera", "Other", s => s.Other, (s, v) => s.Other = v, new IntCodec(), "");
            foreach (string comment in new[] { "one\n\ntwo", " lead", "trail ", "tab\there", "caf" + (char)0xE9, "cr\rhere" })
            {
                Assert.Contains("comment line", Assert.Throws<ArgumentException>(() =>
                    WithOffset(NewTable(), "Camera", "Offset", comment)).Message);
            }
        }

        [Fact]
        public void DefaultsThatCannotBeWrittenThrow()
        {
            Assert.Contains("has a default it cannot write", Assert.Throws<ArgumentException>(() =>
                NewTable().Local("Camera", "Offset", s => s.Value, (s, v) => s.Value = v, new IntCodec(1, 3), "One.")).Message);
            Assert.Contains("[Network] UdpPort has a default it cannot write", Assert.Throws<ArgumentException>(() =>
                new ConfigTable<Small>(() => new Small { Value = 0 })
                    .Concept(ConfigConcepts.UdpPort, s => s.Value, (s, v) => s.Value = v)).Message);
            Assert.Contains("expected 'End'", Assert.Throws<ArgumentException>(() =>
                new ConfigTable<Small>(() => new Small { Key = "end" })
                    .Concept(ConfigConcepts.ToggleKey, s => s.Key, (s, v) => s.Key = v)).Message);
            Assert.Contains("not a tracking mode", Assert.Throws<ArgumentException>(() =>
                new ConfigTable<Small>(() => new Small { Rotation = false, Position = false })
                    .Concept(ConfigConcepts.RotationEnabled, s => s.Rotation, (s, v) => s.Rotation = v)
                    .Concept(ConfigConcepts.PositionEnabled, s => s.Position, (s, v) => s.Position = v)).Message);
            Assert.Throws<InvalidOperationException>(() => new ConfigTable<Small>(() => null!));
        }

        [Fact]
        public void AFailedRowLeavesTheTableAsItWas()
        {
            ConfigTable<Small> table = WithOffset(NewTable(), "Camera", "Offset", "One.");
            Assert.Throws<ArgumentException>(() =>
                table.Local("Camera", "OFFSET", s => s.Other, (s, v) => s.Other = v, new IntCodec(), "Two."));
            Assert.DoesNotContain("OFFSET", Rendered(table, new Small()));
        }

        [Fact]
        public void Modifiers()
        {
            Assert.Contains("Engine needs a row", Assert.Throws<InvalidOperationException>(() => NewTable().Engine()).Message);
            Assert.Contains("carries its comment in Local", Assert.Throws<InvalidOperationException>(() =>
                WithOffset(NewTable(), "Camera", "Offset", "One.").Comment("Two.")).Message);
            Assert.Contains("needs a comment", Assert.Throws<ArgumentException>(() =>
                NewTable().Concept(ConfigConcepts.EnableOnStartup, s => s.Flag, (s, v) => s.Flag = v).Comment("")).Message);
            Assert.Contains("has the schema's range", Assert.Throws<InvalidOperationException>(() =>
                NewTable().Concept(ConfigConcepts.DataFreshnessMs, s => s.Value, (s, v) => s.Value = v).Range(1, 2)).Message);
            Assert.Contains("Range applies to an int, float or double row", Assert.Throws<InvalidOperationException>(() =>
                NewTable().Local("Camera", "Text", s => s.Text, (s, v) => s.Text = v, new StringCodec(), "One.").Range(0, 1)).Message);
            Assert.Contains("not two whole numbers", Assert.Throws<ArgumentException>(() =>
                WithOffset(NewTable(), "Camera", "Offset", "One.").Range(0.5, 10)).Message);
            Assert.Contains("not two whole numbers", Assert.Throws<ArgumentException>(() =>
                WithOffset(NewTable(), "Camera", "Offset", "One.").Range(0, 3e9)).Message);
            Assert.Contains("has a default it cannot write", Assert.Throws<ArgumentException>(() =>
                WithOffset(NewTable(), "Camera", "Offset", "One.").Range(6, 10)).Message);
            Assert.Contains("no row for UdpPort", Assert.Throws<InvalidOperationException>(() =>
                NewTable().Select(ConfigConcepts.UdpPort)).Message);

            ConfigTable<Small> table = NewTable()
                .Concept(ConfigConcepts.EnableOnStartup, s => s.Flag, (s, v) => s.Flag = v)
                .Concept(ConfigConcepts.RotationEnabled, s => s.Rotation, (s, v) => s.Rotation = v);
            WithOffset(table, "Camera", "Offset", "One.")
                .Select(ConfigConcepts.EnableOnStartup)
                .Comment("Starts it.")
                .Engine();
            string rendered = Rendered(table, new Small());
            Assert.Contains("\r\n[General]\r\n; Starts it.\r\n; EnableOnStartup=false\r\n; true: turning", rendered);
            Assert.Contains("; Starts it.\r\nEnableOnStartup=true\r\n", Rendered(table, new Small { Flag = true }));
            Assert.DoesNotContain("Hotkeys are key names", rendered);
            Assert.StartsWith("; G head tracking settings.\r\n; Comments start with", rendered);
        }

        [Fact]
        public void RenderAndApply()
        {
            ConfigTable<Small> table = WithOffset(NewTable(), "Camera", "Offset", "One.")
                .Range(0, 10)
                .Concept(ConfigConcepts.ToggleKey, s => s.Key, (s, v) => s.Key = v);

            foreach (string name in new[] { "", " G", "G ", "Caf" + (char)0xE9, "A\tB" })
            {
                Assert.Contains("display name", Assert.Throws<ArgumentException>(() =>
                    table.Render(new Small(), new RenderHeader(name))).Message);
            }
            Assert.Contains("[Camera] Offset: 11 is outside", Assert.Throws<ArgumentException>(() =>
                table.Render(new Small { Value = 11 }, new RenderHeader("G"))).Message);
            Assert.Contains("[Hotkeys] ToggleKey: 'end'", Assert.Throws<ArgumentException>(() =>
                table.Render(new Small { Key = "end" }, new RenderHeader("G"))).Message);
            Assert.Contains("NulByte", Assert.Throws<ArgumentException>(() =>
                table.Apply(CanonicalIni.Parse(new byte[] { (byte)'[', (byte)'A', (byte)']', 0 }), new Small())).Message);

            var small = new Small();
            ApplyReport report = table.Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes(
                "[Hotkeys]\nToggleKey=0x23\n[Camera]\nOffset=4\n")), small);
            CanonicalDiagnostic invalid = Assert.Single(report.Diagnostics);
            Assert.Equal(CanonicalDiagnosticKind.InvalidValue, invalid.Kind);
            Assert.Equal("'0x23' is a key code: expected a key name such as End or F9, because a Unity mod reads key names only",
                invalid.Detail);
            Assert.Equal("End", small.Key);
            Assert.Equal(4, small.Value);
        }

        [Fact]
        public void HotkeyCodecReadsAnySpellingAndWritesOnlyCanonicalText()
        {
            var codec = new HotkeyCodec();
            string value;
            string? error;
            Assert.True(codec.TryParse(Encoding.ASCII.GetBytes("ctrl+shift+y,END"), out value, out error));
            Assert.Equal("Ctrl+Shift+Y, End", value);
            Assert.Null(error);
            Assert.False(codec.TryParse(Encoding.ASCII.GetBytes("Nope"), out value, out error));
            Assert.Equal(string.Empty, value);
            Assert.Equal("Ctrl+Shift+Y, End", Encoding.ASCII.GetString(codec.Render("Ctrl+Shift+Y, End")));
            Assert.Equal(string.Empty, Encoding.ASCII.GetString(codec.Render(string.Empty)));
            Assert.Throws<ArgumentException>(() => codec.Render("end"));
            Assert.Throws<ArgumentException>(() => codec.Render("Nope"));
            Assert.Throws<ArgumentNullException>(() => codec.Render(null!));
        }

        private static JsonElement Schema([CallerFilePath] string sourceFile = "")
        {
            string root = Path.GetDirectoryName(Path.GetDirectoryName(Root(sourceFile)))!;
            return JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "config-schema.json"))).RootElement;
        }

        [Fact]
        public void ConfigConceptsHoldsEveryCanonicalConceptAsTheSchemaDescribesIt()
        {
            JsonElement schema = Schema();
            var canonical = schema.GetProperty("concepts").EnumerateArray()
                .Where(c => c.GetProperty("canonical").GetBoolean()).ToList();
            Assert.Equal(canonical.Count, ConfigConcepts.All.Length);
            for (int i = 0; i < canonical.Count; i++)
            {
                JsonElement c = canonical[i];
                ConceptDescriptor d = ConfigConcepts.All[i];
                Assert.Equal(c.GetProperty("id").GetString(), d.Id);
                Assert.Same(d, typeof(ConfigConcepts).GetField(d.Id)!.GetValue(null));
                Assert.Equal(c.GetProperty("section").GetString(), d.Section);
                Assert.Equal(c.GetProperty("key").GetString(), d.Key);
                Assert.Equal(c.GetProperty("file_comment").EnumerateArray().Select(l => l.GetString()), d.FileComment);
                Assert.Equal(c.TryGetProperty("canonical_default", out JsonElement def) ? def.GetString() : null,
                    d.CanonicalDefault);
                string type = c.GetProperty("type").GetString()!;
                bool hasRange = c.TryGetProperty("range", out JsonElement range);
                switch (type)
                {
                    case "bool":
                        Assert.Equal(ConceptValueFamily.Bool, d.Family);
                        Assert.IsType<BoolCodec>(((ConceptDescriptor<bool>)d).Codec);
                        break;
                    case "int":
                        Assert.Equal(ConceptValueFamily.Integer, d.Family);
                        var ints = Assert.IsType<IntCodec>(((ConceptDescriptor<int>)d).Codec);
                        Assert.Equal(hasRange && range.TryGetProperty("min", out JsonElement imin) ? imin.GetInt32() : int.MinValue, ints.Min);
                        Assert.Equal(hasRange && range.TryGetProperty("max", out JsonElement imax) ? imax.GetInt32() : int.MaxValue, ints.Max);
                        break;
                    case "float":
                        Assert.Equal(ConceptValueFamily.Floating, d.Family);
                        var floats = Assert.IsType<FloatCodec>(((ConceptDescriptor<float>)d).Codec);
                        Assert.Equal(hasRange && range.TryGetProperty("min", out JsonElement fmin) ? fmin.GetSingle() : float.MinValue, floats.Min);
                        Assert.Equal(hasRange && range.TryGetProperty("max", out JsonElement fmax) ? fmax.GetSingle() : float.MaxValue, floats.Max);
                        break;
                    default:
                        Assert.Equal("string", type);
                        Assert.Equal(ConceptValueFamily.Hotkey, d.Family);
                        Assert.IsType<HotkeyCodec>(((ConceptDescriptor<string>)d).Codec);
                        break;
                }
            }

            Assert.Equal(schema.GetProperty("sections").EnumerateArray().Select(s => s.GetString()), ConfigConcepts.Sections);
            var others = schema.GetProperty("concepts").EnumerateArray()
                .Where(c => !c.GetProperty("canonical").GetBoolean()).ToList();
            Assert.Equal(others.Count, ConfigConcepts.NonCanonicalReasons.Count);
            foreach (JsonElement c in others)
            {
                Assert.Equal(c.GetProperty("canonical_reason").GetString(),
                    ConfigConcepts.NonCanonicalReasons[ConfigKeySchema.Normalize(c.GetProperty("key").GetString()!)]);
            }
        }
    }
}
