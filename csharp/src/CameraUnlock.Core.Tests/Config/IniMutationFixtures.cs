#if NETCOREAPP
#nullable disable
#endif
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using CameraUnlock.Core.Config.Testing;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// csharp/testing/IniMutations.cs against data/fixtures/canonical-ini/mutations, which
    /// cpp/tests/ini_mutations_tests.cpp runs too: every output's name and SHA-256, in order. The
    /// same source runs under xunit on net8.0 (IniMutationsTests) and in the
    /// CameraUnlock.Core.FrameworkTests console on .NET Framework 3.5 and 4.7.2. C# 7.3 and no test
    /// framework, so the net35 build can compile it.
    /// </summary>
    internal static class IniMutationFixtures
    {
        public static string[] Cases(string root)
        {
            string[] dirs = Directory.GetDirectories(Path.Combine(root, "mutations"));
            var names = new List<string>();
            foreach (string dir in dirs) names.Add(Path.GetFileName(dir));
            names.Sort(StringComparer.Ordinal);
            if (names.Count == 0) throw new InvalidOperationException("no mutation fixtures under " + root);
            return names.ToArray();
        }

        /// <summary>
        /// Throws when the case's outputs differ from its expected.tsv, a name repeats, or a second
        /// run gives other outputs.
        /// </summary>
        public static void RunCase(string root, string name)
        {
            string dir = Path.Combine(Path.Combine(root, "mutations"), name);
            byte[] input = File.ReadAllBytes(Path.Combine(dir, "input.ini"));
            List<MutationKey> keys = ReadKeys(Path.Combine(dir, "keys.tsv"));
            List<IniMutation> outputs = IniMutations.Generate(input, keys);

            var actual = new List<string>();
            foreach (IniMutation m in outputs) actual.Add(m.Name + "\t" + Sha256(m.Bytes));
            var expected = new List<string>();
            foreach (string row in Rows(Path.Combine(dir, "expected.tsv")))
            {
                string[] f = row.Split('\t');
                if (f.Length != 2) throw new InvalidOperationException("malformed expected.tsv row: " + row);
                expected.Add(Encoding.ASCII.GetString(Unescape(f[0])) + "\t" + f[1]);
            }
            for (int i = 0; i < System.Math.Max(actual.Count, expected.Count); i++)
            {
                string got = i < actual.Count ? actual[i] : "(none)";
                string want = i < expected.Count ? expected[i] : "(none)";
                if (got != want)
                {
                    throw new InvalidOperationException("output " + i + " is '" + got + "', expected '" + want + "'");
                }
            }

            var names = new HashSet<string>();
            foreach (IniMutation m in outputs)
            {
                if (!names.Add(m.Name)) throw new InvalidOperationException("the name '" + m.Name + "' repeats");
            }

            List<IniMutation> again = IniMutations.Generate(input, keys);
            for (int i = 0; i < outputs.Count; i++)
            {
                if (again[i].Name != outputs[i].Name || !Same(again[i].Bytes, outputs[i].Bytes))
                {
                    throw new InvalidOperationException("a second run differs at output " + i);
                }
            }
        }

        public static string Sha256(byte[] data)
        {
            using (SHA256 sha = SHA256.Create())
            {
                var hex = new StringBuilder();
                foreach (byte b in sha.ComputeHash(data)) hex.Append(b.ToString("x2", CultureInfo.InvariantCulture));
                return hex.ToString();
            }
        }

        private static List<MutationKey> ReadKeys(string path)
        {
            var rows = new List<string[]>();
            foreach (string row in Rows(path)) rows.Add(row.Split('\t'));
            var keys = new List<MutationKey>();
            for (int i = 0; i < rows.Count;)
            {
                string[] f = rows[i++];
                if (f[0] != "key" || f.Length != 5) throw new InvalidOperationException("expected a key row in " + path);
                var ranges = new List<string>();
                var chords = new List<ChordSwitch>();
                while (i < rows.Count && rows[i][0] != "key")
                {
                    string[] g = rows[i++];
                    if (g[0] == "range" && g.Length == 2) ranges.Add(Text(g[1]));
                    else if (g[0] == "chord" && g.Length == 5) chords.Add(new ChordSwitch(Text(g[1]), Text(g[2]), Text(g[3]), Text(g[4])));
                    else throw new InvalidOperationException("malformed keys.tsv row in " + path);
                }
                keys.Add(new MutationKey(Text(f[1]), Text(f[2]), Text(f[3]), ranges, f[4] == "true", chords));
            }
            return keys;
        }

        private static string Text(string field)
        {
            return Encoding.ASCII.GetString(Unescape(field));
        }

        private static List<string> Rows(string path)
        {
            var rows = new List<string>();
            foreach (string line in Encoding.ASCII.GetString(File.ReadAllBytes(path)).Split('\n'))
            {
                if (line.Length != 0 && line[0] != '#') rows.Add(line);
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
