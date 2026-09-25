#if NETCOREAPP
#nullable disable
#endif
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using CameraUnlock.Core.Config;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// Config tables against data/fixtures/canonical-ini/table, which cpp/tests/config_table_tests.cpp
    /// runs over the same fixture table. The same source runs under xunit on net8.0
    /// (ConfigTableTests) and in the CameraUnlock.Core.FrameworkTests console on .NET Framework 3.5
    /// and 4.7.2. C# 7.3 and no test framework, so the net35 build can compile it.
    /// </summary>
    internal static class ConfigTableFixtures
    {
        public enum CameraMode
        {
            ControlRotation = 0,
            UpdateCamera = 1,
        }

        /// <summary>The fixture config, defined identically in the C++ suite.</summary>
        public sealed class FixtureConfig
        {
            public string ToggleKey = "End, Ctrl+Shift+Y";
            public int UdpPort = 4242;
            public bool PositionEnabled = true;
            public bool RotationEnabled = true;
            public bool EnableOnStartup = true;
            public float LocalSmoothing;
            public float PositionLimitX = 0.3f;
            public int CollisionChannel = 3;
            public string CycleTrackingModeKey = "PageUp, Ctrl+Shift+G";
            public CameraMode Mode = CameraMode.UpdateCamera;
            public int LeanDelayMs = 50;
            public float LeanTraceLength = 1.0f;
            public double NearClip = 0.1;
            public int UpdateCameraSlot = 196;
            public uint PovOffset = 0x404;
            public ulong CleanCameraReader = 0x1402A0B10;
            public uint[] HookOffsets = { 0x10, 0x2A };
            public ulong[] AimCallers = new ulong[0];
            public string[] WidgetNames = { "Crosshair", "Compass" };
            public float[] MarkerColor = { 1.0f, 0.5f, 0.0f, 1.0f };
            public string LogPath = "HeadTracking.log";
            public bool WriteLog;
            public string ReloadKey = "F10";
            public int NotInTable = 7;
        }

        public static readonly RenderHeader Header = new RenderHeader("Fixture Game");

        public static EnumCodec<CameraMode> ModeCodec()
        {
            return new EnumCodec<CameraMode>(
                new EnumToken<CameraMode>("ControlRotation", CameraMode.ControlRotation),
                new EnumToken<CameraMode>("UpdateCamera", CameraMode.UpdateCamera));
        }

        /// <summary>The fixture table, defined identically in the C++ suite.</summary>
        public static ConfigTable<FixtureConfig> Table()
        {
            return new ConfigTable<FixtureConfig>(() => new FixtureConfig())
                .Concept(ConfigConcepts.ToggleKey, c => c.ToggleKey, (c, v) => c.ToggleKey = v)
                .Writable()
                .Concept(ConfigConcepts.UdpPort, c => c.UdpPort, (c, v) => c.UdpPort = v)
                .Concept(ConfigConcepts.PositionEnabled, c => c.PositionEnabled, (c, v) => c.PositionEnabled = v)
                .Writable()
                .Concept(ConfigConcepts.RotationEnabled, c => c.RotationEnabled, (c, v) => c.RotationEnabled = v)
                .Writable()
                .Concept(ConfigConcepts.EnableOnStartup, c => c.EnableOnStartup, (c, v) => c.EnableOnStartup = v)
                .Concept(ConfigConcepts.LocalSmoothing, c => c.LocalSmoothing, (c, v) => c.LocalSmoothing = v)
                .Concept(ConfigConcepts.PositionLimitX, c => c.PositionLimitX, (c, v) => c.PositionLimitX = v)
                .Comment("How far, in metres, leaning sideways moves the view.\nThe fixture's own wording.")
                .Concept(ConfigConcepts.CollisionChannel, c => c.CollisionChannel, (c, v) => c.CollisionChannel = v)
                .Engine()
                .Concept(ConfigConcepts.CycleTrackingModeKey, c => c.CycleTrackingModeKey, (c, v) => c.CycleTrackingModeKey = v)
                .Local("Camera", "Mode", c => c.Mode, (c, v) => c.Mode = v, ModeCodec(),
                    "ControlRotation or UpdateCamera (decoupled).")
                .Local("Position", "LeanDelayMs", c => c.LeanDelayMs, (c, v) => c.LeanDelayMs = v, new IntCodec(),
                    "Milliseconds before a lean starts, and the metres its wall trace reaches.")
                .Range(0, 1000)
                .Local("Position", "LeanTraceLength", c => c.LeanTraceLength, (c, v) => c.LeanTraceLength = v, new FloatCodec(), "")
                .Range(0, 2)
                .Local("Camera", "NearClip", c => c.NearClip, (c, v) => c.NearClip = v, new DoubleCodec(),
                    "Near clip distance, in the game's units.")
                .Local("Camera", "UpdateCameraSlot", c => c.UpdateCameraSlot, (c, v) => c.UpdateCameraSlot = v, new IntCodec(),
                    "Engine values. The commented lines show the built-in values.\nDelete the ; to pin your own.")
                .Engine()
                .Local("Camera", "PovOffset", c => c.PovOffset, (c, v) => c.PovOffset = v, new Hex32Codec(), "")
                .Engine()
                .Local("Camera", "CleanCameraReader", c => c.CleanCameraReader, (c, v) => c.CleanCameraReader = v,
                    new Hex64Codec(), "")
                .Engine()
                .Local("Camera", "HookOffsets", c => c.HookOffsets, (c, v) => c.HookOffsets = v, new Hex32ListCodec(),
                    "Offsets the camera hook patches.")
                .Local("Camera", "AimCallers", c => c.AimCallers, (c, v) => c.AimCallers = v, new Hex64ListCodec(),
                    "Return addresses whose aim is left alone. Empty for none.")
                .Local("Camera", "WidgetNames", c => c.WidgetNames, (c, v) => c.WidgetNames = v, new StringListCodec(),
                    "Widgets that follow the head.")
                .Local("Camera", "MarkerColor", c => c.MarkerColor, (c, v) => c.MarkerColor = v, new ColorCodec(),
                    "Marker colour: red, green, blue and opacity, each 0 to 1.")
                .Local("Logging", "LogPath", c => c.LogPath, (c, v) => c.LogPath = v, new StringCodec(),
                    "Log file, beside the game's executable.")
                .Local("Logging", "WriteLog", c => c.WriteLog, (c, v) => c.WriteLog = v, new BoolCodec(), "true: write the log.")
                .Engine()
                .Local("Logging", "ReloadKey", c => c.ReloadKey, (c, v) => c.ReloadKey = v, new HotkeyCodec(),
                    "Reads this file again.");
        }

        /// <summary>The case directories under table/, sorted.</summary>
        public static string[] Cases(string root)
        {
            var names = new List<string>();
            foreach (string dir in Directory.GetDirectories(Path.Combine(root, "table")))
            {
                names.Add(Path.GetFileName(dir));
            }
            names.Sort(StringComparer.Ordinal);
            if (names.Count != 12) throw new InvalidOperationException(names.Count + " table cases under " + root + ", expected 12");
            return names.ToArray();
        }

        /// <summary>Throws when the case does not hold.</summary>
        public static void RunCase(string root, string name)
        {
            string dir = Path.Combine(Path.Combine(root, "table"), name);
            ConfigTable<FixtureConfig> table = Table();
            if (File.Exists(Path.Combine(dir, "input.ini")))
            {
                RunApplyCase(table, dir);
            }
            else
            {
                RunRenderCase(table, dir);
            }
        }

        private static void RunApplyCase(ConfigTable<FixtureConfig> table, string dir)
        {
            var expectedFields = new List<string>();
            var expectedDiagnostics = new List<string>();
            foreach (string[] row in TsvRows(Path.Combine(dir, "expected.tsv")))
            {
                if (row[0] == "field" && row.Length == 3)
                {
                    expectedFields.Add(row[1] + "=" + Encoding.UTF8.GetString(Unescape(row[2])));
                }
                else if (row[0] == "diagnostic" && row.Length == 4)
                {
                    expectedDiagnostics.Add(row[1] + " " + row[2] + " " + Encoding.UTF8.GetString(Unescape(row[3])));
                }
                else
                {
                    throw new InvalidOperationException("malformed row in " + dir);
                }
            }

            CanonicalIni doc = CanonicalIni.Parse(File.ReadAllBytes(Path.Combine(dir, "input.ini")));
            var config = new FixtureConfig { NotInTable = 99, UdpPort = 1, WriteLog = true, HookOffsets = new uint[0] };
            ApplyReport report = table.Apply(doc, config);
            SameRows(FieldRows(config), expectedFields, "field values");
            SameRows(DiagnosticRows(report), expectedDiagnostics, "diagnostics");
            if (config.NotInTable != 99) throw new InvalidOperationException("a member no row binds changed");
            CheckRoundTrip(table, config);
        }

        private static void RunRenderCase(ConfigTable<FixtureConfig> table, string dir)
        {
            Dictionary<string, Field> fields = Fields();
            var values = new FixtureConfig();
            foreach (string[] row in TsvRows(Path.Combine(dir, "values.tsv")))
            {
                Field field;
                if (row[0] != "field" || row.Length != 3 || !fields.TryGetValue(row[1], out field))
                {
                    throw new InvalidOperationException("malformed row in " + dir);
                }
                field.Parse(values, Unescape(row[2]));
            }
            byte[] rendered = table.Render(values, Header);
            byte[] expected = File.ReadAllBytes(Path.Combine(dir, "expected.ini"));
            if (!Same(rendered, expected))
            {
                throw new InvalidOperationException("rendered bytes differ:\n" + Encoding.UTF8.GetString(rendered));
            }
            CheckRoundTrip(table, values);
        }

        // Rendering what a rendered file applies to gives the same bytes and the same fields, with
        // no diagnostic from either the reader or the table.
        private static void CheckRoundTrip(ConfigTable<FixtureConfig> table, FixtureConfig values)
        {
            byte[] rendered = table.Render(values, Header);
            CanonicalIni doc = CanonicalIni.Parse(rendered);
            var applied = new FixtureConfig { NotInTable = 99 };
            ApplyReport report = table.Apply(doc, applied);
            if (doc.Diagnostics.Count != 0 || report.Diagnostics.Count != 0)
            {
                throw new InvalidOperationException("a rendered file reads with diagnostics");
            }
            SameRows(FieldRows(applied), FieldRows(values), "a rendered file read back");
            if (applied.NotInTable != 99) throw new InvalidOperationException("a member no row binds changed");
            if (!Same(table.Render(applied, Header), rendered))
            {
                throw new InvalidOperationException("rendering what a rendered file read gives other bytes");
            }
        }

        private sealed class Field
        {
            public Func<FixtureConfig, byte[]> Render;
            public Action<FixtureConfig, byte[]> Parse;
        }

        private static Field MakeField<T>(IValueCodec<T> codec, Func<FixtureConfig, T> get, Action<FixtureConfig, T> set)
        {
            return new Field
            {
                Render = c => codec.Render(get(c)),
                Parse = (c, text) =>
                {
                    T value;
                    string error;
                    if (!codec.TryParse(text, out value, out error)) throw new InvalidOperationException("fixture value: " + error);
                    set(c, value);
                },
            };
        }

        // Each fixture field by its key, as its row's codec reads and writes it, in table order.
        private static Dictionary<string, Field> Fields()
        {
            return new Dictionary<string, Field>
            {
                { "ToggleKey", MakeField(new HotkeyCodec(), c => c.ToggleKey, (c, v) => c.ToggleKey = v) },
                { "UdpPort", MakeField(new IntCodec(), c => c.UdpPort, (c, v) => c.UdpPort = v) },
                { "PositionEnabled", MakeField(new BoolCodec(), c => c.PositionEnabled, (c, v) => c.PositionEnabled = v) },
                { "RotationEnabled", MakeField(new BoolCodec(), c => c.RotationEnabled, (c, v) => c.RotationEnabled = v) },
                { "EnableOnStartup", MakeField(new BoolCodec(), c => c.EnableOnStartup, (c, v) => c.EnableOnStartup = v) },
                { "LocalSmoothing", MakeField(new FloatCodec(), c => c.LocalSmoothing, (c, v) => c.LocalSmoothing = v) },
                { "PositionLimitX", MakeField(new FloatCodec(), c => c.PositionLimitX, (c, v) => c.PositionLimitX = v) },
                { "CollisionChannel", MakeField(new IntCodec(), c => c.CollisionChannel, (c, v) => c.CollisionChannel = v) },
                { "CycleTrackingModeKey", MakeField(new HotkeyCodec(), c => c.CycleTrackingModeKey, (c, v) => c.CycleTrackingModeKey = v) },
                { "Mode", MakeField(ModeCodec(), c => c.Mode, (c, v) => c.Mode = v) },
                { "LeanDelayMs", MakeField(new IntCodec(), c => c.LeanDelayMs, (c, v) => c.LeanDelayMs = v) },
                { "LeanTraceLength", MakeField(new FloatCodec(), c => c.LeanTraceLength, (c, v) => c.LeanTraceLength = v) },
                { "NearClip", MakeField(new DoubleCodec(), c => c.NearClip, (c, v) => c.NearClip = v) },
                { "UpdateCameraSlot", MakeField(new IntCodec(), c => c.UpdateCameraSlot, (c, v) => c.UpdateCameraSlot = v) },
                { "PovOffset", MakeField(new Hex32Codec(), c => c.PovOffset, (c, v) => c.PovOffset = v) },
                { "CleanCameraReader", MakeField(new Hex64Codec(), c => c.CleanCameraReader, (c, v) => c.CleanCameraReader = v) },
                { "HookOffsets", MakeField(new Hex32ListCodec(), c => c.HookOffsets, (c, v) => c.HookOffsets = v) },
                { "AimCallers", MakeField(new Hex64ListCodec(), c => c.AimCallers, (c, v) => c.AimCallers = v) },
                { "WidgetNames", MakeField(new StringListCodec(), c => c.WidgetNames, (c, v) => c.WidgetNames = v) },
                { "MarkerColor", MakeField(new ColorCodec(), c => c.MarkerColor, (c, v) => c.MarkerColor = v) },
                { "LogPath", MakeField(new StringCodec(), c => c.LogPath, (c, v) => c.LogPath = v) },
                { "WriteLog", MakeField(new BoolCodec(), c => c.WriteLog, (c, v) => c.WriteLog = v) },
                { "ReloadKey", MakeField(new HotkeyCodec(), c => c.ReloadKey, (c, v) => c.ReloadKey = v) },
            };
        }

        private static readonly string[] FieldOrder =
        {
            "ToggleKey", "UdpPort", "PositionEnabled", "RotationEnabled", "EnableOnStartup", "LocalSmoothing",
            "PositionLimitX", "CollisionChannel", "CycleTrackingModeKey", "Mode", "LeanDelayMs", "LeanTraceLength", "NearClip",
            "UpdateCameraSlot", "PovOffset", "CleanCameraReader", "HookOffsets", "AimCallers", "WidgetNames", "MarkerColor",
            "LogPath", "WriteLog", "ReloadKey",
        };

        public static List<string> FieldRows(FixtureConfig config)
        {
            Dictionary<string, Field> fields = Fields();
            var rows = new List<string>();
            foreach (string name in FieldOrder)
            {
                rows.Add(name + "=" + Encoding.UTF8.GetString(fields[name].Render(config)));
            }
            return rows;
        }

        public static List<string> DiagnosticRows(ApplyReport report)
        {
            var rows = new List<string>();
            foreach (CanonicalDiagnostic d in report.Diagnostics)
            {
                var lines = new StringBuilder();
                for (int i = 0; i < d.Lines.Count; i++)
                {
                    if (i > 0) lines.Append(',');
                    lines.Append(d.Lines[i].ToString(CultureInfo.InvariantCulture));
                }
                rows.Add(d.Kind + " " + lines + " " + d.Describe());
            }
            return rows;
        }

        private static void SameRows(List<string> actual, List<string> expected, string what)
        {
            bool same = actual.Count == expected.Count;
            for (int i = 0; same && i < actual.Count; i++) same = actual[i] == expected[i];
            if (!same)
            {
                throw new InvalidOperationException(what + " differ.\n  expected:\n    " + string.Join("\n    ", expected.ToArray())
                    + "\n  actual:\n    " + string.Join("\n    ", actual.ToArray()));
            }
        }

        private static List<string[]> TsvRows(string path)
        {
            var rows = new List<string[]>();
            foreach (string line in Encoding.ASCII.GetString(File.ReadAllBytes(path)).Split('\n'))
            {
                if (line.Length == 0 || line[0] == '#') continue;
                rows.Add(line.Split('\t'));
            }
            return rows;
        }

        private static bool Same(byte[] a, byte[] b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++)
            {
                if (a[i] != b[i]) return false;
            }
            return true;
        }

        // The byte escape data/fixtures/canonical-ini/README.md defines.
        private static byte[] Unescape(string field)
        {
            var bytes = new List<byte>();
            for (int i = 0; i < field.Length; i++)
            {
                if (field[i] != '\\')
                {
                    bytes.Add((byte)field[i]);
                }
                else if (i + 1 < field.Length && field[i + 1] == '\\')
                {
                    bytes.Add((byte)'\\');
                    i++;
                }
                else if (i + 3 < field.Length && field[i + 1] == 'x')
                {
                    bytes.Add(byte.Parse(field.Substring(i + 2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture));
                    i += 3;
                }
                else
                {
                    throw new InvalidOperationException("bad escape in fixture field " + field);
                }
            }
            return bytes.ToArray();
        }
    }
}
