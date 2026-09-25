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
                .PerGame()
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
            if (names.Count != 23) throw new InvalidOperationException(names.Count + " table cases under " + root + ", expected 23");
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
                return;
            }
            bool ran = false;
            if (File.Exists(Path.Combine(dir, "fresh.ini")))
            {
                RunFreshCase(table, dir);
                ran = true;
            }
            if (File.Exists(Path.Combine(dir, "expected.ini")))
            {
                RunRenderCase(table, dir);
                ran = true;
            }
            if (File.Exists(Path.Combine(dir, "migration.ini")))
            {
                RunMigrationCase(table, dir);
                ran = true;
            }
            if (!ran) throw new InvalidOperationException(dir + " holds no input.ini, fresh.ini, expected.ini or migration.ini");
        }

        /// <summary>A config for the checks below.</summary>
        public sealed class SmallConfig
        {
            public bool Rotation = true;
            public bool Position = true;
            public int Value = 5;
            public float Scale = 1.0f;
            public string Key = "End";
            public string Keys = "End, Ctrl+Shift+Y";
        }

        private const string FreshRowTail = ". A fresh file writes default on this row, which takes Defaults.ini's value, so "
            + "the row's own default must be the schema's, or the row must be marked PerGame().";

        /// <summary>
        /// Throws unless RenderFresh's gate, PerGame and the default token behave as
        /// cpp/tests/config_table_tests.cpp holds them, with the same messages.
        /// </summary>
        public static void RunGlobalChecks()
        {
            var header = new RenderHeader("G");
            ExpectMessage<ArgumentException>(() => SmallTable().Concept(ConfigConcepts.UdpPort, s => s.Value, (s, v) => s.Value = v)
                .RenderFresh(header), "[Network] UdpPort defaults to 5, and the schema to 4242" + FreshRowTail);
            ExpectMessage<ArgumentException>(() => SmallTable().Concept(ConfigConcepts.ToggleKey, s => s.Key, (s, v) => s.Key = v)
                .RenderFresh(header), "[Hotkeys] ToggleKey defaults to End, and the schema to End, Ctrl+Shift+Y" + FreshRowTail);
            ExpectMessage<ArgumentException>(() => SmallTable()
                .Concept(ConfigConcepts.LocalSmoothing, s => s.Scale, (s, v) => s.Scale = v)
                .RenderFresh(header), "[Smoothing] LocalSmoothing defaults to 1.0, and the schema to 0.0" + FreshRowTail);

            string perGame = Encoding.ASCII.GetString(SmallTable()
                .Concept(ConfigConcepts.UdpPort, s => s.Value, (s, v) => s.Value = v)
                .PerGame()
                .RenderFresh(header));
            if (!perGame.Contains("\r\nUdpPort=5\r\n") || perGame.Contains("Defaults.ini"))
            {
                throw new InvalidOperationException("a PerGame row off the schema's default renders its value, and a table "
                    + "with no other concept row has no Defaults.ini lines:\n" + perGame);
            }

            ExpectMessage<ArgumentException>(() => SmallTable()
                .Concept(ConfigConcepts.RotationEnabled, s => s.Rotation, (s, v) => s.Rotation = v)
                .RenderFresh(header),
                "the table binds [General] RotationEnabled without [Position] PositionEnabled, and the tracking mode is the "
                + "two of them together");
            SmallTable()
                .Concept(ConfigConcepts.PositionEnabled, s => s.Position, (s, v) => s.Position = v)
                .RenderFresh(header);

            ExpectMessage<InvalidOperationException>(() => SmallTable()
                .Local("Camera", "Offset", s => s.Value, (s, v) => s.Value = v, new IntCodec(), "One.")
                .PerGame(), "[Camera] Offset is a local row, which never takes a value from Defaults.ini");

            const string pairMessage = "[Position] PositionEnabled is marked PerGame() and [General] RotationEnabled is not. "
                + "The two are one setting, the tracking mode, so PerGame() marks both or neither.";
            ConfigTable<SmallConfig> halfPerGame = SmallTable()
                .Concept(ConfigConcepts.RotationEnabled, s => s.Rotation, (s, v) => s.Rotation = v)
                .Concept(ConfigConcepts.PositionEnabled, s => s.Position, (s, v) => s.Position = v)
                .PerGame();
            ExpectMessage<ArgumentException>(() => halfPerGame.RenderFresh(header), pairMessage);
            ExpectMessage<ArgumentException>(() => halfPerGame.Apply(CanonicalIni.Parse(new byte[0]), new SmallConfig()),
                pairMessage);
            ExpectMessage<ArgumentException>(
                () => halfPerGame.RenderMigration(new SmallConfig(), new SmallConfig(), header), pairMessage);

            ConfigTable<SmallConfig> twoState = SmallTable()
                .Concept(ConfigConcepts.PositionEnabled, s => s.Position, (s, v) => s.Position = v);
            var twoStateConfig = new SmallConfig();
            TableApplyResult twoStateResult = twoState.Apply(
                CanonicalIni.Parse(Encoding.ASCII.GetBytes("[Position]\r\nPositionEnabled=default\r\n")), twoStateConfig,
                new SmallConfig { Position = false }, new ConceptDescriptor[] { ConfigConcepts.PositionEnabled });
            if (twoStateConfig.Position || twoStateResult.Sources[0] != ConfigValueSource.DefaultsIni
                || twoStateResult.Report.Diagnostics.Count != 0)
            {
                throw new InvalidOperationException("a table without RotationEnabled reads PositionEnabled=default as an "
                    + "effective false from Defaults.ini, and no pair rule fires");
            }

            ConfigTable<SmallConfig> keys = SmallTable()
                .Concept(ConfigConcepts.ToggleKey, s => s.Keys, (s, v) => s.Keys = v)
                .Concept(ConfigConcepts.RotationEnabled, s => s.Rotation, (s, v) => s.Rotation = v)
                .PerGame()
                .Concept(ConfigConcepts.PositionEnabled, s => s.Position, (s, v) => s.Position = v)
                .PerGame();
            var config = new SmallConfig { Keys = "F1" };
            ApplyReport report = keys.Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes("[Hotkeys]\r\nToggleKey=End, default\r\n")),
                config);
            if (report.Diagnostics.Count != 1 || report.Diagnostics[0].Kind != CanonicalDiagnosticKind.InvalidValue
                || config.Keys != "End, Ctrl+Shift+Y")
            {
                throw new InvalidOperationException("End, default is a key list with an item that is no key, not the token");
            }

            ExpectMessage<ArgumentException>(
                () => SmallTable()
                    .Concept(ConfigConcepts.RotationEnabled, s => s.Rotation, (s, v) => s.Rotation = v)
                    .Concept(ConfigConcepts.PositionEnabled, s => s.Position, (s, v) => s.Position = v)
                    .Apply(CanonicalIni.Parse(new byte[0]), new SmallConfig(), new SmallConfig { Rotation = false, Position = false },
                        new ConceptDescriptor[0]),
                null);
            ExpectMessage<ArgumentException>(
                () => keys.Apply(CanonicalIni.Parse(new byte[0]), new SmallConfig(), new SmallConfig(),
                    new ConceptDescriptor[] { ConfigConcepts.PositionEnabled }),
                null);
            ExpectMessage<ArgumentException>(
                () => keys.Apply(CanonicalIni.Parse(new byte[0]), new SmallConfig(), new SmallConfig(),
                    new ConceptDescriptor[] { ConfigConcepts.UdpPort }),
                null);
            var applied = new SmallConfig { Rotation = false, Position = false };
            TableApplyResult perGamePair = keys.Apply(CanonicalIni.Parse(new byte[0]), applied,
                new SmallConfig { Keys = "F1", Rotation = false, Position = false },
                new ConceptDescriptor[] { ConfigConcepts.ToggleKey });
            if (applied.Keys != "F1" || !applied.Rotation || !applied.Position
                || perGamePair.Sources[0] != ConfigValueSource.DefaultsIni
                || perGamePair.Sources[1] != ConfigValueSource.BuiltIn || perGamePair.Sources[2] != ConfigValueSource.BuiltIn)
            {
                throw new InvalidOperationException("a PerGame row starts from the table's default, whatever the effective defaults hold");
            }
        }

        private static ConfigTable<SmallConfig> SmallTable()
        {
            return new ConfigTable<SmallConfig>(() => new SmallConfig());
        }

        // Throws unless action throws a TException, with exactly this message when one is given.
        private static void ExpectMessage<TException>(Action action, string message) where TException : Exception
        {
            try
            {
                action();
            }
            catch (TException e)
            {
                if (message == null || e.Message == message) return;
                throw new InvalidOperationException("expected the message\n  " + message + "\nand got\n  " + e.Message);
            }
            throw new InvalidOperationException("expected a " + typeof(TException).Name + ": " + message);
        }

        private static void RunApplyCase(ConfigTable<FixtureConfig> table, string dir)
        {
            var expectedFields = new List<string>();
            var expectedSources = new List<string>();
            var expectedDiagnostics = new List<string>();
            foreach (string[] row in TsvRows(Path.Combine(dir, "expected.tsv")))
            {
                if (row[0] == "field" && row.Length == 3)
                {
                    expectedFields.Add(row[1] + "=" + Encoding.UTF8.GetString(Unescape(row[2])));
                }
                else if (row[0] == "source" && row.Length == 3)
                {
                    expectedSources.Add(row[1] + "=" + row[2]);
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
            ApplyReport report;
            if (File.Exists(Path.Combine(dir, "effective.tsv")))
            {
                List<ConceptDescriptor> fromDefaultsIni;
                FixtureConfig effective = Effective(dir, out fromDefaultsIni);
                TableApplyResult result = table.Apply(doc, config, effective, fromDefaultsIni);
                report = result.Report;
                SameRows(SourceRows(result.Sources), expectedSources, "sources");
            }
            else
            {
                if (expectedSources.Count != 0) throw new InvalidOperationException(dir + " lists sources and has no effective.tsv");
                report = table.Apply(doc, config);
            }
            SameRows(FieldRows(config), expectedFields, "field values");
            SameRows(DiagnosticRows(report), expectedDiagnostics, "diagnostics");
            if (config.NotInTable != 99) throw new InvalidOperationException("a member no row binds changed");
            CheckRoundTrip(table, config);
        }

        private static void RunRenderCase(ConfigTable<FixtureConfig> table, string dir)
        {
            FixtureConfig values = Values(dir);
            byte[] rendered = table.Render(values, Header);
            SameBytes(rendered, File.ReadAllBytes(Path.Combine(dir, "expected.ini")), "rendered");
            CheckRoundTrip(table, values);
        }

        // The fresh file reads back as the defaults with no diagnostic.
        private static void RunFreshCase(ConfigTable<FixtureConfig> table, string dir)
        {
            byte[] rendered = table.RenderFresh(Header);
            SameBytes(rendered, File.ReadAllBytes(Path.Combine(dir, "fresh.ini")), "fresh");
            CanonicalIni doc = CanonicalIni.Parse(rendered);
            var applied = new FixtureConfig { UdpPort = 1 };
            ApplyReport report = table.Apply(doc, applied);
            if (doc.Diagnostics.Count != 0 || report.Diagnostics.Count != 0)
            {
                throw new InvalidOperationException("the fresh file reads with diagnostics");
            }
            SameRows(FieldRows(applied), FieldRows(new FixtureConfig()), "the fresh file read back");
        }

        // The migrated file reads back, over the same effective defaults, as the values it was written from.
        private static void RunMigrationCase(ConfigTable<FixtureConfig> table, string dir)
        {
            List<ConceptDescriptor> fromDefaultsIni;
            FixtureConfig effective = Effective(dir, out fromDefaultsIni);
            FixtureConfig values = Values(dir);
            byte[] rendered = table.RenderMigration(values, effective, Header);
            SameBytes(rendered, File.ReadAllBytes(Path.Combine(dir, "migration.ini")), "migration");
            CanonicalIni doc = CanonicalIni.Parse(rendered);
            var applied = new FixtureConfig { UdpPort = 1 };
            TableApplyResult result = table.Apply(doc, applied, effective, fromDefaultsIni);
            if (doc.Diagnostics.Count != 0 || result.Report.Diagnostics.Count != 0)
            {
                throw new InvalidOperationException("the migrated file reads with diagnostics");
            }
            SameRows(FieldRows(applied), FieldRows(values), "the migrated file read back");
        }

        private static FixtureConfig Values(string dir)
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
            return values;
        }

        // The defaults with effective.tsv's field rows, and the concepts its defaults_ini rows name.
        private static FixtureConfig Effective(string dir, out List<ConceptDescriptor> fromDefaultsIni)
        {
            Dictionary<string, Field> fields = Fields();
            var effective = new FixtureConfig();
            fromDefaultsIni = new List<ConceptDescriptor>();
            foreach (string[] row in TsvRows(Path.Combine(dir, "effective.tsv")))
            {
                Field field;
                if (row[0] == "field" && row.Length == 3 && fields.TryGetValue(row[1], out field))
                {
                    field.Parse(effective, Unescape(row[2]));
                    continue;
                }
                ConceptDescriptor concept = row[0] == "defaults_ini" && row.Length == 2 ? ConceptNamed(row[1]) : null;
                if (concept == null) throw new InvalidOperationException("malformed row in " + Path.Combine(dir, "effective.tsv"));
                fromDefaultsIni.Add(concept);
            }
            return effective;
        }

        private static ConceptDescriptor ConceptNamed(string id)
        {
            foreach (ConceptDescriptor concept in ConfigConcepts.All)
            {
                if (concept.Id == id) return concept;
            }
            return null;
        }

        private static List<string> SourceRows(ConfigValueSource[] sources)
        {
            var rows = new List<string>();
            for (int i = 0; i < FieldOrder.Length; i++)
            {
                string name;
                switch (sources[i])
                {
                    case ConfigValueSource.File: name = "file"; break;
                    case ConfigValueSource.DefaultsIni: name = "defaults_ini"; break;
                    case ConfigValueSource.BuiltIn: name = "built_in"; break;
                    default: throw new InvalidOperationException("source " + (int)sources[i]);
                }
                rows.Add(FieldOrder[i] + "=" + name);
            }
            if (sources.Length != FieldOrder.Length) throw new InvalidOperationException(sources.Length + " sources, expected one per row");
            return rows;
        }

        private static void SameBytes(byte[] actual, byte[] expected, string what)
        {
            if (!Same(actual, expected))
            {
                throw new InvalidOperationException(what + " bytes differ:\n" + Encoding.UTF8.GetString(actual));
            }
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
