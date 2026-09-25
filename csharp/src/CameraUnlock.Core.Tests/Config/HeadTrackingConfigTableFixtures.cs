#if NETCOREAPP
#nullable disable
#endif
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// <see cref="HeadTrackingConfigTable"/> against data/fixtures/canonical-ini/head-tracking, which
    /// cpp/tests/head_tracking_config_table_tests.cpp runs too. The same source runs under xunit on
    /// net8.0 (HeadTrackingConfigTableTests) and in the CameraUnlock.Core.FrameworkTests console on
    /// .NET Framework 3.5 and 4.7.2. C# 7.3 and no test framework, so the net35 build can compile it.
    /// </summary>
    internal static class HeadTrackingConfigTableFixtures
    {
        public static readonly RenderHeader Header = new RenderHeader("Fixture Game");

        public static readonly string[] Cases = { "apply-empty", "apply-position-off", "apply-values" };

        public static ConfigTable<HeadTrackingConfigData> AllConceptsTable()
        {
            return HeadTrackingConfigTable.Create(ConfigConcepts.All);
        }

        /// <summary>Throws when rendering the defaults with every concept does not give all-concepts.ini.</summary>
        public static void RunRender(string root)
        {
            ConfigTable<HeadTrackingConfigData> table = AllConceptsTable();
            byte[] rendered = table.Render(Defaults(table), Header);
            byte[] expected = File.ReadAllBytes(Path.Combine(Path.Combine(root, "head-tracking"), "all-concepts.ini"));
            if (!Same(rendered, expected))
            {
                throw new InvalidOperationException("rendered bytes differ:\n" + Encoding.UTF8.GetString(rendered));
            }
        }

        /// <summary>
        /// Throws when the fresh render of the table with every concept does not give
        /// all-concepts-fresh.ini, which holds core's own table to the fresh render's gate, or does not
        /// read back as the defaults.
        /// </summary>
        public static void RunFreshRender(string root)
        {
            ConfigTable<HeadTrackingConfigData> table = AllConceptsTable();
            byte[] rendered = table.RenderFresh(Header);
            byte[] expected = File.ReadAllBytes(Path.Combine(Path.Combine(root, "head-tracking"), "all-concepts-fresh.ini"));
            if (!Same(rendered, expected))
            {
                throw new InvalidOperationException("rendered bytes differ:\n" + Encoding.UTF8.GetString(rendered));
            }
            SameRows(FieldRows(Applied(table, rendered, new HeadTrackingConfigData())), FieldRows(Defaults(table)),
                "the fresh file read back");
        }

        /// <summary>
        /// Throws unless effective LocalSmoothing and RemoteSmoothing values also reach the position
        /// settings' copy, and the migration render writes CollisionChannel, an Engine row that follows
        /// Defaults.ini, as an active value where it differs from its effective default.
        /// </summary>
        public static void RunEffectiveDefaults()
        {
            ConfigTable<HeadTrackingConfigData> table = HeadTrackingConfigTable.Create(
                ConfigConcepts.LocalSmoothing, ConfigConcepts.RemoteSmoothing, ConfigConcepts.CollisionChannel);
            var effective = new HeadTrackingConfigData { LocalSmoothing = 0.25f, RemoteSmoothing = 0.5f, CollisionChannel = 2 };
            ConceptDescriptor[] fromDefaultsIni =
            {
                ConfigConcepts.LocalSmoothing, ConfigConcepts.RemoteSmoothing, ConfigConcepts.CollisionChannel,
            };
            var config = new HeadTrackingConfigData();
            TableApplyResult result = table.Apply(CanonicalIni.Parse(new byte[0]), config, effective, fromDefaultsIni);
            if (config.LocalSmoothing != 0.25f || config.RemoteSmoothing != 0.5f || config.Position.LocalSmoothing != 0.25f
                || config.Position.RemoteSmoothing != 0.5f || config.CollisionChannel != 2)
            {
                throw new InvalidOperationException("the effective defaults did not reach every field, the position copy included");
            }
            foreach (ConfigValueSource source in result.Sources)
            {
                if (source != ConfigValueSource.DefaultsIni) throw new InvalidOperationException("a row's source is " + source);
            }

            HeadTrackingConfigData values = Defaults(table);
            values.LocalSmoothing = 0.25f;
            string migrated = Encoding.ASCII.GetString(table.RenderMigration(values, effective, Header));
            if (!migrated.Contains("\r\nLocalSmoothing=default\r\n") || !migrated.Contains("\r\nRemoteSmoothing=0.15\r\n")
                || !migrated.Contains("\r\nCollisionChannel=0\r\n") || migrated.Contains("; CollisionChannel"))
            {
                throw new InvalidOperationException("the migration render wrote:\n" + migrated);
            }
            var reread = new HeadTrackingConfigData();
            TableApplyResult back = table.Apply(CanonicalIni.Parse(Encoding.ASCII.GetBytes(migrated)), reread, effective,
                fromDefaultsIni);
            if (back.Report.Diagnostics.Count != 0 || reread.LocalSmoothing != 0.25f || reread.RemoteSmoothing != 0.15f
                || reread.CollisionChannel != 0)
            {
                throw new InvalidOperationException("the migrated file did not read back as its values");
            }
        }

        /// <summary>
        /// Throws when the case, applied onto a new config and onto apply-values' result, does not give
        /// its expected.tsv, reads with a diagnostic, changes a field no row binds, or renders a file
        /// that does not read back as itself.
        /// </summary>
        public static void RunCase(string root, string name)
        {
            string dir = Path.Combine(Path.Combine(root, "head-tracking"), name);
            ConfigTable<HeadTrackingConfigData> table = AllConceptsTable();
            List<string> expected = ExpectedFields(Path.Combine(dir, "expected.tsv"));
            byte[] input = File.ReadAllBytes(Path.Combine(dir, "input.ini"));

            HeadTrackingConfigData fromNew = Applied(table, input, Marked(new HeadTrackingConfigData()));
            SameRows(FieldRows(fromNew), expected, "applied onto a new config");
            CheckMarksKept(fromNew);

            byte[] values = File.ReadAllBytes(Path.Combine(Path.Combine(Path.Combine(root, "head-tracking"), "apply-values"), "input.ini"));
            HeadTrackingConfigData moved = Applied(table, values, Marked(new HeadTrackingConfigData()));
            HeadTrackingConfigData fromMoved = Applied(table, input, moved);
            SameRows(FieldRows(fromMoved), expected, "applied onto apply-values' result");
            CheckMarksKept(fromMoved);

            byte[] rendered = table.Render(fromNew, Header);
            HeadTrackingConfigData reread = Applied(table, rendered, new HeadTrackingConfigData());
            SameRows(FieldRows(reread), expected, "its render read back");
            if (!Same(table.Render(reread, Header), rendered))
            {
                throw new InvalidOperationException("rendering what a rendered file read gives other bytes");
            }
        }

        /// <summary>
        /// Throws unless apply-empty's expected.tsv is the defaults and every field is off its default
        /// in some case, so the cases between them move every field a concept reaches.
        /// </summary>
        public static void RunCoverage(string root)
        {
            List<string> defaults = FieldRows(Defaults(AllConceptsTable()));
            var covered = new bool[defaults.Count];
            foreach (string name in Cases)
            {
                List<string> expected = ExpectedFields(Path.Combine(Path.Combine(Path.Combine(root, "head-tracking"), name), "expected.tsv"));
                SameRows(new List<string>(Names(expected)), new List<string>(Names(defaults)), name + " field names");
                if (name == "apply-empty") SameRows(expected, defaults, "apply-empty against the defaults");
                for (int i = 0; i < defaults.Count; i++) covered[i] |= expected[i] != defaults[i];
            }
            for (int i = 0; i < defaults.Count; i++)
            {
                if (!covered[i]) throw new InvalidOperationException("no case moves " + defaults[i] + " off its default");
            }
        }

        // Every row of a table at its default: Apply starts each row from the defaults instance, and
        // an empty file sets nothing after that.
        public static HeadTrackingConfigData Defaults(ConfigTable<HeadTrackingConfigData> table)
        {
            return Applied(table, new byte[0], new HeadTrackingConfigData());
        }

        private static HeadTrackingConfigData Applied(ConfigTable<HeadTrackingConfigData> table, byte[] bytes,
            HeadTrackingConfigData config)
        {
            CanonicalIni doc = CanonicalIni.Parse(bytes);
            ApplyReport report = table.Apply(doc, config);
            if (doc.Diagnostics.Count != 0 || report.Diagnostics.Count != 0)
            {
                throw new InvalidOperationException("the file reads with diagnostics");
            }
            return config;
        }

        // Fields no row binds, set before Apply so a case can see they are left alone.
        private static HeadTrackingConfigData Marked(HeadTrackingConfigData config)
        {
            config.RecenterKeyName = "F1";
            PositionSettings p = config.Position;
            config.Position = new PositionSettings(2.0f, p.SensitivityY, p.SensitivityZ,
                p.LimitX, p.LimitY, p.LimitYDown, p.LimitZ, p.LimitZBack,
                p.LocalSmoothing, p.RemoteSmoothing, p.InvertX, true, p.InvertZ);
            return config;
        }

        private static void CheckMarksKept(HeadTrackingConfigData config)
        {
            if (config.RecenterKeyName != "F1" || config.Position.SensitivityX != 2.0f || !config.Position.InvertY)
            {
                throw new InvalidOperationException("a field no row binds changed");
            }
        }

        private sealed class FieldRead
        {
            public readonly string Name;
            public readonly Func<HeadTrackingConfigData, byte[]> Render;

            public FieldRead(string name, Func<HeadTrackingConfigData, byte[]> render)
            {
                Name = name;
                Render = render;
            }
        }

        // Each field a concept reaches, read straight off the config rather than through the table,
        // in the order of expected.tsv.
        private static FieldRead[] Fields()
        {
            var b = new BoolCodec();
            var i = new IntCodec();
            var f = new FloatCodec();
            var k = new HotkeyCodec();
            return new[]
            {
                new FieldRead("UdpPort", c => i.Render(c.UdpPort)),
                new FieldRead("EnableOnStartup", c => b.Render(c.EnableOnStartup)),
                new FieldRead("LocalSmoothing", c => f.Render(c.LocalSmoothing)),
                new FieldRead("RemoteSmoothing", c => f.Render(c.RemoteSmoothing)),
                new FieldRead("WorldSpaceYaw", c => b.Render(c.WorldSpaceYaw)),
                new FieldRead("AimDecoupling", c => b.Render(c.AimDecouplingEnabled)),
                new FieldRead("RotationEnabled", c => b.Render(c.RotationEnabled)),
                new FieldRead("DataFreshnessMs", c => i.Render(c.DataFreshnessMs)),
                new FieldRead("PositionEnabled", c => b.Render(c.PositionEnabled)),
                new FieldRead("PositionAllowed", c => b.Render(c.PositionAllowed)),
                new FieldRead("TrueFreeLook", c => b.Render(c.TrueFreeLook)),
                new FieldRead("PositionLimitX", c => f.Render(c.Position.LimitX)),
                new FieldRead("PositionLimitY", c => f.Render(c.Position.LimitY)),
                new FieldRead("PositionLimitYDown", c => f.Render(c.Position.LimitYDown)),
                new FieldRead("PositionLimitZ", c => f.Render(c.Position.LimitZ)),
                new FieldRead("PositionLimitZBack", c => f.Render(c.Position.LimitZBack)),
                new FieldRead("CollisionEnabled", c => b.Render(c.CollisionEnabled)),
                new FieldRead("CollisionMargin", c => f.Render(c.CollisionMargin)),
                new FieldRead("CollisionChannel", c => i.Render(c.CollisionChannel)),
                new FieldRead("CollisionReleaseSmoothing", c => f.Render(c.CollisionReleaseSmoothing)),
                new FieldRead("TrackerPivotForward", c => f.Render(c.TrackerPivotForward)),
                new FieldRead("TrackerPivotUp", c => f.Render(c.TrackerPivotUp)),
                new FieldRead("ToggleKey", c => k.Render(c.ToggleKeyName)),
                new FieldRead("CycleTrackingModeKey", c => k.Render(c.CycleTrackingModeKeyName)),
                new FieldRead("YawModeKey", c => k.Render(c.YawModeKeyName)),
                new FieldRead("TrueFreeLookKey", c => k.Render(c.TrueFreeLookKeyName)),
                new FieldRead("LightFollowsHead", c => b.Render(c.Light.FollowsHead)),
                new FieldRead("LightMultiplier", c => f.Render(c.Light.Multiplier)),
                new FieldRead("PositionLocalSmoothing", c => f.Render(c.Position.LocalSmoothing)),
                new FieldRead("PositionRemoteSmoothing", c => f.Render(c.Position.RemoteSmoothing)),
            };
        }

        public static List<string> FieldRows(HeadTrackingConfigData config)
        {
            var rows = new List<string>();
            foreach (FieldRead field in Fields())
            {
                rows.Add(field.Name + "=" + Encoding.ASCII.GetString(field.Render(config)));
            }
            return rows;
        }

        private static IEnumerable<string> Names(List<string> rows)
        {
            foreach (string row in rows) yield return row.Substring(0, row.IndexOf('='));
        }

        private static List<string> ExpectedFields(string path)
        {
            var fields = new List<string>();
            foreach (string line in Encoding.ASCII.GetString(File.ReadAllBytes(path)).Split('\n'))
            {
                if (line.Length == 0 || line[0] == '#') continue;
                string[] row = line.Split('\t');
                if (row.Length != 3 || row[0] != "field") throw new InvalidOperationException("malformed row in " + path);
                fields.Add(row[1] + "=" + Encoding.UTF8.GetString(Unescape(row[2])));
            }
            return fields;
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
