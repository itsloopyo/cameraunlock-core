#if NETCOREAPP
#nullable disable
#endif
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using CameraUnlock.Core.Config;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// <see cref="DefaultsIni"/> against data/fixtures/canonical-ini/global, which
    /// cpp/tests/defaults_ini_tests.cpp runs too. The same source runs under xunit on net8.0
    /// (DefaultsIniTests) and in the CameraUnlock.Core.FrameworkTests console on .NET Framework 3.5
    /// and 4.7.2. C# 7.3 and no test framework, so the net35 build can compile it.
    /// </summary>
    internal static class DefaultsIniFixtures
    {
        private const string KeyListStart = "Only these key names are read here: ";
        private const string KeyListEnd = ". A value holding any other key";

        public static string[] Cases(string root)
        {
            string[] names = Directory.GetDirectories(Path.Combine(root, "global"))
                .Select(d => Path.GetFileName(d))
                .Where(n => n.StartsWith("read-", StringComparison.Ordinal))
                .OrderBy(n => n, StringComparer.Ordinal)
                .ToArray();
            if (names.Length == 0) throw new InvalidOperationException("no global/read-* fixtures under " + root);
            return names;
        }

        /// <summary>
        /// Throws unless the render is global/Defaults.ini byte for byte, and reads back with every
        /// concept accepted at the global table's default and nothing else to say.
        /// </summary>
        public static void RunRender(string root)
        {
            byte[] rendered = DefaultsIni.Render();
            byte[] expected = File.ReadAllBytes(Path.Combine(Path.Combine(root, "global"), "Defaults.ini"));
            if (!rendered.SequenceEqual(expected))
            {
                throw new InvalidOperationException("rendered bytes differ:\n" + Encoding.UTF8.GetString(rendered));
            }

            DefaultsIniSnapshot snapshot = DefaultsIni.Read(rendered);
            if (snapshot.Unreadable != null || snapshot.FormatLine != null || snapshot.PairRefused)
            {
                throw new InvalidOperationException("Defaults.ini read back with something to say");
            }
            ConfigTable<HeadTrackingConfigData> table = DefaultsIni.Table();
            HeadTrackingConfigData defaults = table.CreateDefaults();
            foreach (ConceptDescriptor concept in ConfigConcepts.All)
            {
                DefaultsIniValue value = snapshot.Value(concept);
                byte[] builtIn = table.RowRender(table.RowOf(concept), defaults);
                if (value.State != DefaultsIniValueState.Accepted || !value.Value.SequenceEqual(builtIn))
                {
                    throw new InvalidOperationException(concept.Id + " read back as " + value.State + " "
                        + Encoding.UTF8.GetString(value.Value) + ", not the accepted built-in " + Encoding.ASCII.GetString(builtIn));
                }
            }

            CanonicalIni doc = CanonicalIni.Parse(rendered);
            ApplyReport report = table.Apply(doc, new HeadTrackingConfigData());
            if (doc.Diagnostics.Count != 0 || report.Diagnostics.Count != 0)
            {
                throw new InvalidOperationException("Defaults.ini draws a diagnostic from the reader or the global table");
            }
        }

        /// <summary>Throws when the case does not read as its expected.tsv.</summary>
        public static void RunCase(string root, string name)
        {
            string dir = Path.Combine(Path.Combine(root, "global"), name);
            List<string> actual = Rows(DefaultsIni.Read(File.ReadAllBytes(Path.Combine(dir, "input.ini"))));
            var expected = new List<string>();
            foreach (string line in Encoding.ASCII.GetString(File.ReadAllBytes(Path.Combine(dir, "expected.tsv"))).Split('\n'))
            {
                if (line.Length > 0 && line[0] != '#') expected.Add(line);
            }
            if (!actual.SequenceEqual(expected, StringComparer.Ordinal))
            {
                throw new InvalidOperationException(name + " does not read as expected.tsv.\n  expected:\n    "
                    + string.Join("\n    ", expected.ToArray()) + "\n  actual:\n    " + string.Join("\n    ", actual.ToArray()));
            }
        }

        /// <summary>Throws unless the two line functions refuse a value or a pair that is not refused.</summary>
        public static void RunLineArguments()
        {
            DefaultsIniSnapshot snapshot = DefaultsIni.Read(Encoding.ASCII.GetBytes("[Network]\r\nUdpPort=5000\r\n"));
            try
            {
                DefaultsIni.RefusedLine(snapshot.Value(ConfigConcepts.UdpPort), "4242");
                throw new InvalidOperationException("RefusedLine took an accepted value");
            }
            catch (ArgumentException)
            {
            }
            try
            {
                DefaultsIni.PairLine(snapshot, "true", "true");
                throw new InvalidOperationException("PairLine took a snapshot whose pair is not refused");
            }
            catch (ArgumentException)
            {
            }
        }

        /// <summary>
        /// The key names the rendered header lists, with each "X to Y" range written out: A to Z by
        /// letter, the others by the number after a shared prefix.
        /// </summary>
        public static List<string> HeaderKeyNames(byte[] rendered)
        {
            var text = new StringBuilder();
            foreach (string line in Encoding.ASCII.GetString(rendered).Split(new[] { "\r\n" }, StringSplitOptions.None))
            {
                if (line.Length == 0) break;
                text.Append(line.Substring(2)).Append(' ');
            }
            string all = text.ToString();
            int start = all.IndexOf(KeyListStart, StringComparison.Ordinal);
            int end = all.IndexOf(KeyListEnd, StringComparison.Ordinal);
            if (start < 0 || end < start) throw new InvalidOperationException("the header lists no key names");

            var names = new List<string>();
            foreach (string item in all.Substring(start + KeyListStart.Length, end - start - KeyListStart.Length)
                         .Split(new[] { ", " }, StringSplitOptions.None))
            {
                string[] range = item.Split(new[] { " to " }, StringSplitOptions.None);
                if (range.Length == 1)
                {
                    names.Add(item);
                }
                else if (range[0].Length == 1 && range[1].Length == 1)
                {
                    for (char c = range[0][0]; c <= range[1][0]; c++) names.Add(c.ToString());
                }
                else
                {
                    string prefix = range[0].TrimEnd('0', '1', '2', '3', '4', '5', '6', '7', '8', '9');
                    if (prefix.Length == 0 || !range[1].StartsWith(prefix, StringComparison.Ordinal))
                    {
                        throw new InvalidOperationException("'" + item + "' is not a range of letters or of numbered names");
                    }
                    int first = int.Parse(range[0].Substring(prefix.Length), CultureInfo.InvariantCulture);
                    int last = int.Parse(range[1].Substring(prefix.Length), CultureInfo.InvariantCulture);
                    for (int n = first; n <= last; n++) names.Add(prefix + n.ToString(CultureInfo.InvariantCulture));
                }
            }
            return names;
        }

        private static List<string> Rows(DefaultsIniSnapshot snapshot)
        {
            var rows = new List<string>();
            if (snapshot.Unreadable != null)
            {
                rows.Add("unreadable\t" + Escape(snapshot.Unreadable));
                return rows;
            }
            if (snapshot.FormatLine != null) rows.Add("format\t" + Escape(snapshot.FormatLine));

            var lines = new List<string>();
            DefaultsIniSnapshot builtIns = DefaultsIni.Read(DefaultsIni.Render());
            foreach (ConceptDescriptor concept in ConfigConcepts.All)
            {
                DefaultsIniValue value = snapshot.Value(concept);
                string row = "value\t" + concept.Id + "\t";
                switch (value.State)
                {
                    case DefaultsIniValueState.Absent:
                        continue;
                    case DefaultsIniValueState.Accepted:
                        rows.Add(row + "accepted\t" + Number(value.Line) + "\t" + Escape(value.Value));
                        continue;
                }
                rows.Add(row + "refused\t" + Number(value.Line) + "\t" + Escape(value.Section) + "\t" + Escape(value.Key) + "\t"
                    + Escape(value.Value) + "\t" + Escape(value.Reason));
                bool pairRow = concept == ConfigConcepts.RotationEnabled || concept == ConfigConcepts.PositionEnabled;
                if (snapshot.PairRefused && pairRow) continue;
                string builtIn = Encoding.UTF8.GetString(builtIns.Value(concept).Value);
                lines.Add("line\t" + concept.Id + "\t" + Escape(DefaultsIni.RefusedLine(value, builtIn)));
            }
            rows.AddRange(lines);
            if (snapshot.PairRefused)
            {
                rows.Add("pair\t" + Escape(DefaultsIni.PairLine(snapshot,
                    Encoding.UTF8.GetString(builtIns.Value(ConfigConcepts.RotationEnabled).Value),
                    Encoding.UTF8.GetString(builtIns.Value(ConfigConcepts.PositionEnabled).Value))));
            }
            return rows;
        }

        private static string Escape(string text)
        {
            return Escape(Encoding.UTF8.GetBytes(text));
        }

        // The byte escape data/fixtures/canonical-ini/README.md defines.
        private static string Escape(byte[] bytes)
        {
            var text = new StringBuilder();
            foreach (byte b in bytes)
            {
                if (b == (byte)'\\') text.Append("\\\\");
                else if (b >= 0x20 && b <= 0x7E) text.Append((char)b);
                else text.Append("\\x").Append(b.ToString("X2", CultureInfo.InvariantCulture));
            }
            return text.ToString();
        }

        private static string Number(int n)
        {
            return n.ToString(CultureInfo.InvariantCulture);
        }
    }
}
