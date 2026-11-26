#if NETCOREAPP
#nullable disable
#endif
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using CameraUnlock.Core.Input;

namespace CameraUnlock.Core.Tests.Input
{
    /// <summary>
    /// <see cref="KeyBindings"/> against the unity rows of
    /// data/fixtures/canonical-ini/keys/cases.tsv, whose native rows
    /// cpp/tests/key_bindings_tests.cpp runs through the C++ codec. The same source runs under
    /// xunit on net8.0 (KeyBindingsTests) and in the CameraUnlock.Core.FrameworkTests console
    /// on .NET Framework 3.5 and 4.7.2. C# 7.3 and no test framework, so the net35 build can
    /// compile it.
    /// </summary>
    internal static class KeyBindingFixtures
    {
        /// <summary>The unity rows of keys/cases.tsv under the fixture root, as written.</summary>
        public static string[] UnityRows(string root)
        {
            byte[] tsv = File.ReadAllBytes(Path.Combine(Path.Combine(root, "keys"), "cases.tsv"));
            var rows = new List<string>();
            foreach (string line in Encoding.ASCII.GetString(tsv).Split('\n'))
            {
                if (line.Length == 0 || line[0] == '#') continue;
                string dialect = line.Split('\t')[0];
                if (dialect == "unity") rows.Add(line);
                else if (dialect != "native") throw new InvalidOperationException("unknown dialect in row " + line);
            }
            if (rows.Count == 0) throw new InvalidOperationException("no unity rows under " + root);
            return rows.ToArray();
        }

        /// <summary>Throws when the row does not read as it says.</summary>
        public static void RunRow(string row)
        {
            string[] fields = row.Split('\t');
            string input = Unescape(fields[1]);
            KeyBinding[] parsed;
            string error;
            bool read = KeyBindings.TryParse(input, out parsed, out error);

            if (fields.Length == 3 && fields[2] == "invalid")
            {
                if (read || parsed.Length != 0 || string.IsNullOrEmpty(error))
                {
                    throw new InvalidOperationException(row + ": read, expected invalid");
                }
                return;
            }
            if (fields.Length != 4 || fields[2] != "canonical") throw new InvalidOperationException("malformed row " + row);

            if (!read || error != null) throw new InvalidOperationException(row + ": " + error);
            string expected = Unescape(fields[3]);
            string formatted = KeyBindings.Format(parsed);
            if (formatted != expected)
            {
                throw new InvalidOperationException(row + ": formatted as '" + formatted + "'");
            }

            KeyBinding[] again;
            if (!KeyBindings.TryParse(formatted, out again, out error) || !Same(again, parsed)
                || KeyBindings.Format(again) != formatted)
            {
                throw new InvalidOperationException(row + ": '" + formatted + "' does not read back as itself");
            }
        }

        private static bool Same(KeyBinding[] a, KeyBinding[] b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++)
            {
                if (a[i] != b[i]) return false;
            }
            return true;
        }

        // The byte escape data/fixtures/canonical-ini/README.md defines, then UTF-8.
        private static string Unescape(string field)
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
            return new UTF8Encoding(false, true).GetString(bytes.ToArray());
        }
    }
}
