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
    /// <see cref="CanonicalIni"/> against data/fixtures/canonical-ini/reader, which
    /// cpp/tests/canonical_ini_tests.cpp runs through the C++ reader as well. The same source
    /// runs under xunit on net8.0 (CanonicalIniTests) and in the
    /// CameraUnlock.Core.FrameworkTests console on .NET Framework 3.5 and 4.7.2. C# 7.3 and no
    /// test framework, so the net35 build can compile it.
    /// </summary>
    internal static class CanonicalIniFixtures
    {
        private const string Relative = "data/fixtures/canonical-ini";

        /// <summary>The fixture root in the first directory at or above <paramref name="start"/> that has one.</summary>
        public static string FindRoot(string start)
        {
            DirectoryInfo dir = new DirectoryInfo(start);
            while (dir != null)
            {
                string root = Path.Combine(dir.FullName, Relative.Replace('/', Path.DirectorySeparatorChar));
                if (Directory.Exists(root)) return root;
                dir = dir.Parent;
            }
            throw new InvalidOperationException("no " + Relative + " at or above " + start);
        }

        public static string[] ReaderCases(string root)
        {
            string[] names = Directory.GetDirectories(Path.Combine(root, "reader"))
                .Select(d => Path.GetFileName(d))
                .OrderBy(n => n, StringComparer.Ordinal)
                .ToArray();
            if (names.Length == 0) throw new InvalidOperationException("no reader fixtures under " + root);
            return names;
        }

        /// <summary>Throws when the case does not read as its expected.tsv.</summary>
        public static void RunReaderCase(string root, string name)
        {
            string dir = Path.Combine(Path.Combine(root, "reader"), name);
            byte[] input = File.ReadAllBytes(Path.Combine(dir, "input.ini"));
            List<string> expected = ExpectedRows(File.ReadAllBytes(Path.Combine(dir, "expected.tsv")));
            List<string> actual = Rows(input);
            if (!actual.SequenceEqual(expected, StringComparer.Ordinal))
            {
                throw new InvalidOperationException(name + " does not read as expected.tsv.\n  expected:\n    "
                    + string.Join("\n    ", expected.ToArray()) + "\n  actual:\n    " + string.Join("\n    ", actual.ToArray()));
            }

            CanonicalIni doc = CanonicalIni.Parse(input);
            if (doc.IsReadable && CanonicalIni.HasStamp(input) != (doc.FindSection("CameraUnlock") != null))
            {
                throw new InvalidOperationException(name + ": the stamp is not exactly a [CameraUnlock] section the reader opens");
            }
        }

        private static List<string> Rows(byte[] input)
        {
            CanonicalIni doc = CanonicalIni.Parse(input);
            var rows = new List<string>();
            string status = "status\t" + doc.Status;
            if (doc.Status == CanonicalReadStatus.NulByte) status += "\t" + Number(doc.UnreadableLine);
            rows.Add(status);
            rows.Add("format\t" + Number(doc.FormatVersion));
            rows.Add("stamp\t" + (CanonicalIni.HasStamp(input) ? "true" : "false"));

            var keys = new List<KeyValuePair<int, string>>();
            foreach (CanonicalSection section in doc.Sections)
            {
                foreach (CanonicalValue value in section.Values)
                {
                    keys.Add(new KeyValuePair<int, string>(value.Line, "key\t" + Escape(section.Name) + "\t" + Escape(value.Key)
                        + "\t" + Escape(value.Value) + "\t" + Number(value.Line) + "\t" + LineList(value.EarlierLines)));
                }
            }
            rows.AddRange(keys.OrderBy(k => k.Key).Select(k => k.Value));

            foreach (CanonicalDiagnostic d in doc.Diagnostics)
            {
                rows.Add("diagnostic\t" + d.Kind + "\t" + LineList(d.Lines));
            }
            return rows;
        }

        private static List<string> ExpectedRows(byte[] tsv)
        {
            var rows = new List<string>();
            foreach (string line in Encoding.ASCII.GetString(tsv).Split('\n'))
            {
                if (line.Length > 0 && line[0] != '#') rows.Add(line);
            }
            return rows;
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

        private static string LineList(IList<int> lines)
        {
            if (lines.Count == 0) return "-";
            return string.Join(",", lines.Select(Number).ToArray());
        }

        private static string Number(int n)
        {
            return n.ToString(CultureInfo.InvariantCulture);
        }
    }
}
