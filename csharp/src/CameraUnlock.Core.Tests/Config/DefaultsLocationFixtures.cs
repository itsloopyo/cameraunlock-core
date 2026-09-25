#if NETCOREAPP
#nullable disable
#endif
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using CameraUnlock.Core.Config;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// <see cref="DefaultsLocation"/> against data/fixtures/canonical-ini/global/resolve.tsv, which
    /// cpp/tests/defaults_location_tests.cpp runs too, plus the real probe on this machine and the
    /// folder creation in scratch folders. The same source runs under xunit on net8.0
    /// (DefaultsLocationTests) and in the CameraUnlock.Core.FrameworkTests console on .NET
    /// Framework 3.5 and 4.7.2. C# 7.3 and no test framework, so the net35 build can compile it.
    /// </summary>
    internal static class DefaultsLocationFixtures
    {
        private const int ErrorInvalidName = 123;
        private const uint RegStringType = 0x2;
        private static readonly IntPtr CurrentUser = new IntPtr(unchecked((int)0x80000001));

        public static string[] Cases(string root)
        {
            string[] names = Blocks(root).Select(b => b[0][1]).ToArray();
            if (names.Length == 0) throw new InvalidOperationException("no cases in global/resolve.tsv under " + root);
            if (names.Distinct(StringComparer.Ordinal).Count() != names.Length)
            {
                throw new InvalidOperationException("global/resolve.tsv repeats a case name");
            }
            return names;
        }

        /// <summary>Throws when the case's resolution or any of its choices differs from resolve.tsv.</summary>
        public static void RunCase(string root, string name)
        {
            List<string[]> block = Blocks(root).Single(b => b[0][1] == name);
            var actual = new List<string>();
            var probe = new DefaultsProbe();
            int row = 0;
            for (; row < block.Count && block[row][0] != "choice"; row++)
            {
                if (block[row][0] == "case" || block[row][0] == "input") actual.Add(string.Join("\t", block[row]));
                if (block[row][0] == "input") Apply(probe, block[row]);
            }
            DefaultsResolution resolution = DefaultsLocation.Resolve(probe);
            if (resolution.UnixFolder.Length > 0) actual.Add("unix\t" + Escape(resolution.UnixFolder));
            if (resolution.HostUnusable.Length > 0) actual.Add("host\t" + Escape(resolution.HostUnusable));
            foreach (DefaultsCandidate candidate in resolution.Candidates)
            {
                actual.Add("candidate\t" + Kind(candidate.Kind) + "\t" + (candidate.MayCreate ? "yes" : "no") + "\t"
                    + Escape(candidate.Path) + "\t" + Escape(candidate.Shown));
            }
            if (resolution.Candidates.Count == 0) actual.Add("none\t" + Escape(resolution.NoLocation));

            while (row < block.Count)
            {
                actual.Add("choice");
                row++;
                var exists = new bool[resolution.Candidates.Count];
                var outcomes = new DefaultsCreationOutcome[resolution.Candidates.Count];
                for (; row < block.Count && block[row][0] != "choice"; row++)
                {
                    string[] f = block[row];
                    if (f[0] == "exists")
                    {
                        exists[Index(f[1])] = true;
                    }
                    else if (f[0] == "outcome")
                    {
                        outcomes[Index(f[1])] = new DefaultsCreationOutcome(Creation(f[2]), f.Length > 3 ? Text(f[3]) : string.Empty);
                    }
                    else
                    {
                        continue;
                    }
                    actual.Add(string.Join("\t", f));
                }
                DefaultsChoice choice = DefaultsLocation.Choose(resolution, exists, outcomes);
                actual.Add("read\t" + Position(choice.Read));
                actual.Add("create\t" + Position(choice.Create));
                if (choice.Line.Length > 0) actual.Add("line\t" + Escape(choice.Line));
                if (choice.Message.Length > 0) actual.Add("message\t" + Escape(choice.Message));
            }

            List<string> expected = block.Select(f => string.Join("\t", f)).ToList();
            if (!actual.SequenceEqual(expected, StringComparer.Ordinal))
            {
                throw new InvalidOperationException(name + " does not resolve as resolve.tsv says.\n  expected:\n    "
                    + string.Join("\n    ", expected.ToArray()) + "\n  actual:\n    " + string.Join("\n    ", actual.ToArray()));
            }
        }

        /// <summary>Throws unless Choose refuses arrays that do not hold one entry per candidate.</summary>
        public static void RunChooseArguments()
        {
            var probe = new DefaultsProbe { KnownFolder = @"C:\Users\Ann\AppData\Roaming" };
            DefaultsResolution resolution = DefaultsLocation.Resolve(probe);
            try
            {
                DefaultsLocation.Choose(resolution, new bool[0], new DefaultsCreationOutcome[1]);
            }
            catch (ArgumentException)
            {
                return;
            }
            throw new InvalidOperationException("Choose took an exists array with no entry for the candidate");
        }

        /// <summary>
        /// The probe on this Windows machine: Windows, the roaming folder the profile's registry
        /// names, not packaged, and nothing created.
        /// </summary>
        public static void RunRealProbe()
        {
            string roaming = RegistryRoamingFolder();
            string folder = Path.Combine(roaming, "CameraUnlock");
            bool before = Directory.Exists(folder) || File.Exists(folder);

            DefaultsProbe probe = DefaultsLocation.Probe();
            DefaultsResolution resolution = DefaultsLocation.Resolve(probe);

            Require(probe.Platform == DefaultsPlatform.Windows, "the platform is " + probe.Platform);
            Require(string.Equals(probe.KnownFolder, roaming, StringComparison.Ordinal),
                "the known folder is '" + probe.KnownFolder + "', the registry names '" + roaming + "'");
            Require(probe.PackageResult == DefaultsLocation.NoPackage, "GetCurrentPackageFullName returned " + probe.PackageResult);
            Require(resolution.Candidates.Count == 1 && resolution.Candidates[0].MayCreate
                && resolution.Candidates[0].Path == Path.Combine(folder, "Defaults.ini"), "the candidate is not the roaming file");
            Require((Directory.Exists(folder) || File.Exists(folder)) == before, folder + " changed");
        }

        /// <summary>Creates only the last folder, counts one that is there as done, and reports the rest.</summary>
        public static void RunCreateFolder(string dir)
        {
            string folder = Path.Combine(dir, "CameraUnlock");
            Require(DefaultsLocation.CreateFolder(folder) == 0 && Directory.Exists(folder), "the folder was not created");
            Require(DefaultsLocation.CreateFolder(folder) == 0, "an existing folder is not done");

            string missing = Path.Combine(dir, "missing");
            int orphan = DefaultsLocation.CreateFolder(Path.Combine(missing, "CameraUnlock"));
            Require(orphan == DefaultsLocation.ErrorPathNotFound, "a missing parent gave " + orphan);
            Require(!Directory.Exists(missing), "the missing parent was created");

            int invalid = DefaultsLocation.CreateFolder(dir + "\\bad?name");
            Require(invalid == ErrorInvalidName, "an invalid name gave " + invalid);

            string file = Path.Combine(dir, "file");
            File.WriteAllBytes(file, new byte[0]);
            Require(DefaultsLocation.CreateFolder(file) == 0 && File.Exists(file) && !Directory.Exists(file),
                "a file of that name did not give ERROR_ALREADY_EXISTS, counted as done");
            Require(Directory.GetFileSystemEntries(dir).Length == 2, "something besides CameraUnlock and the file was created");
        }

        private static List<List<string[]>> Blocks(string root)
        {
            string path = Path.Combine(Path.Combine(root, "global"), "resolve.tsv");
            var blocks = new List<List<string[]>>();
            foreach (string line in Encoding.ASCII.GetString(File.ReadAllBytes(path)).Split('\n'))
            {
                if (line.Length == 0 || line[0] == '#') continue;
                string[] fields = line.Split('\t');
                if (fields[0] == "case") blocks.Add(new List<string[]>());
                if (blocks.Count == 0) throw new InvalidOperationException("resolve.tsv has a row before its first case: " + line);
                blocks[blocks.Count - 1].Add(fields);
            }
            return blocks;
        }

        private static void Apply(DefaultsProbe probe, string[] row)
        {
            string value = Text(row[2]);
            switch (row[1])
            {
                case "platform":
                    probe.Platform = value == "windows" ? DefaultsPlatform.Windows
                        : value == "wine" ? DefaultsPlatform.Wine
                        : value == "native" ? DefaultsPlatform.Native
                        : throw new InvalidOperationException("unknown platform " + value);
                    break;
                case "known_folder": probe.KnownFolder = value; break;
                case "package": probe.PackageResult = int.Parse(value, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture); break;
                case "wine_version": probe.WineVersion = value; break;
                case "host": probe.HostSystem = value; break;
                case "WINEHOMEDIR": probe.WineHomeDir = value; break;
                case "WINE_HOST_XDG_CONFIG_HOME": probe.WineHostXdgConfigHome = value; break;
                case "XDG_CONFIG_HOME": probe.XdgConfigHome = value; break;
                case "HOME": probe.Home = value; break;
                case "codepage":
                    if (value != "failed") throw new InvalidOperationException("codepage is only ever failed");
                    probe.CodePageFailed = true;
                    break;
                case "dos_file_name": probe.DosFileName = value; break;
                default: throw new InvalidOperationException("unknown input " + row[1]);
            }
        }

        private static DefaultsCreation Creation(string name)
        {
            switch (name)
            {
                case "created": return DefaultsCreation.Created;
                case "appeared": return DefaultsCreation.Appeared;
                case "parent_missing": return DefaultsCreation.ParentMissing;
                case "folder_failed": return DefaultsCreation.FolderFailed;
                case "file_failed": return DefaultsCreation.FileFailed;
                default: throw new InvalidOperationException("unknown outcome " + name);
            }
        }

        private static string Kind(DefaultsCandidateKind kind)
        {
            switch (kind)
            {
                case DefaultsCandidateKind.Windows: return "windows";
                case DefaultsCandidateKind.WineHost: return "wine_host";
                case DefaultsCandidateKind.WinePrefix: return "wine_prefix";
                case DefaultsCandidateKind.Native: return "native";
                default: throw new InvalidOperationException("unknown kind " + kind);
            }
        }

        private static int Index(string field)
        {
            return int.Parse(field, NumberStyles.None, CultureInfo.InvariantCulture);
        }

        private static string Position(int index)
        {
            return index < 0 ? "-" : index.ToString(CultureInfo.InvariantCulture);
        }

        private static string Text(string field)
        {
            return Encoding.UTF8.GetString(Unescape(field));
        }

        // The byte escape data/fixtures/canonical-ini/README.md defines, over the text's UTF-8.
        private static string Escape(string text)
        {
            var escaped = new StringBuilder();
            foreach (byte b in Encoding.UTF8.GetBytes(text))
            {
                if (b == (byte)'\\') escaped.Append("\\\\");
                else if (b >= 0x20 && b <= 0x7E) escaped.Append((char)b);
                else escaped.Append("\\x").Append(b.ToString("X2", CultureInfo.InvariantCulture));
            }
            return escaped.ToString();
        }

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

        private static void Require(bool condition, string message)
        {
            if (!condition) throw new InvalidOperationException(message);
        }

        // The profile's AppData entry under User Shell Folders, expanded: the roaming folder by a
        // path that does not go through Environment.GetFolderPath or the APPDATA variable.
        private static string RegistryRoamingFolder()
        {
            var data = new StringBuilder(32768);
            uint size = (uint)data.Capacity * 2;
            int error = RegGetValueW(CurrentUser, @"Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders",
                "AppData", RegStringType, IntPtr.Zero, data, ref size);
            if (error != 0) throw new InvalidOperationException("RegGetValueW failed with " + error);
            return data.ToString();
        }

        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern int RegGetValueW(
            IntPtr key, string subKey, string value, uint flags, IntPtr type, StringBuilder data, ref uint size);
    }
}
