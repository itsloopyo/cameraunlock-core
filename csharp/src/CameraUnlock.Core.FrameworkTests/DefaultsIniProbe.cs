using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Tests.Config;

namespace CameraUnlock.Core.Tests
{
    /// <summary>
    /// <c>--probe-defaults-ini &lt;game folder&gt; [--probe-save] [--probe-legacy]</c>. Not part of the
    /// suite: it finds, and on Windows and under Wine creates, the player's own Defaults.ini through
    /// <see cref="DefaultsFile.PerUser"/>, so it only ever runs where that is a scratch home.
    /// </summary>
    internal static class DefaultsIniProbe
    {
        internal const string Flag = "--probe-defaults-ini";

        internal static int Run(string[] args)
        {
            bool save = false;
            bool legacy = false;
            foreach (string flag in args.Skip(2))
            {
                if (flag == "--probe-save") save = true;
                else if (flag == "--probe-legacy") legacy = true;
                else return Usage();
            }
            if (args.Length < 2) return Usage();

            var output = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false)) { AutoFlush = true, NewLine = "\n" };
            try
            {
                Probe(output, args[1], save, legacy);
                return 0;
            }
            catch (Exception e)
            {
                Line(output, "error", e.ToString());
                return 1;
            }
        }

        private static int Usage()
        {
            Console.Error.WriteLine("usage: CameraUnlock.Core.FrameworkTests.exe " + Flag + " <game folder> [--probe-save] [--probe-legacy]");
            return 2;
        }

        private static void Probe(TextWriter output, string folder, bool save, bool legacy)
        {
#if NET35
            Line(output, "runtime", "build", "net35");
#else
            Line(output, "runtime", "build", "net472");
#endif
            Line(output, "runtime", "clr", Environment.Version.ToString());
            Line(output, "runtime", "os", Environment.OSVersion.ToString());

            DefaultsProbe probe = DefaultsLocation.Probe();
            Line(output, "input", "platform", probe.Platform.ToString());
            Line(output, "input", "known_folder", probe.KnownFolder);
            Line(output, "input", "package_result", probe.PackageResult.HasValue ? probe.PackageResult.Value.ToString(CultureInfo.InvariantCulture) : "");
            Line(output, "input", "wine_version", probe.WineVersion);
            Line(output, "input", "host_system", probe.HostSystem);
            Line(output, "input", "WINEHOMEDIR", probe.WineHomeDir);
            Line(output, "input", "WINE_HOST_XDG_CONFIG_HOME", probe.WineHostXdgConfigHome);
            Line(output, "input", "XDG_CONFIG_HOME", probe.XdgConfigHome);
            Line(output, "input", "HOME", probe.Home);
            Line(output, "input", "code_page_failed", probe.CodePageFailed ? "true" : "false");
            Line(output, "input", "dos_file_name", probe.DosFileName);

            DefaultsResolution resolution = DefaultsLocation.Resolve(probe);
            Line(output, "resolution", "platform", resolution.Platform.ToString());
            Line(output, "resolution", "no_location", resolution.NoLocation);
            Line(output, "resolution", "host_unusable", resolution.HostUnusable);
            Line(output, "resolution", "unix_folder", resolution.UnixFolder);
            Line(output, "resolution", "wine", resolution.Wine);
            Line(output, "resolution", "package_result", resolution.PackageResult.ToString(CultureInfo.InvariantCulture));
            var exists = new bool[resolution.Candidates.Count];
            for (int i = 0; i < exists.Length; i++)
            {
                DefaultsCandidate candidate = resolution.Candidates[i];
                string index = i.ToString(CultureInfo.InvariantCulture);
                Line(output, "candidate", index, candidate.Kind.ToString());
                Line(output, "candidate-may-create", index, candidate.MayCreate ? "true" : "false");
                Line(output, "candidate-path", index, candidate.Path);
                Line(output, "candidate-shown", index, candidate.Shown);
                Line(output, "candidate-shown-folder", index, candidate.ShownFolder);
                Line(output, "candidate-shown-parent", index, candidate.ShownParent);
                exists[i] = File.Exists(candidate.Path) || Directory.Exists(candidate.Path);
                Line(output, "candidate-exists", index, exists[i] ? "true" : "false");
            }
            DefaultsChoice choice = DefaultsLocation.Choose(resolution, exists, new DefaultsCreationOutcome[exists.Length]);
            Line(output, "choice", "read", choice.Read.ToString(CultureInfo.InvariantCulture));
            Line(output, "choice", "create", choice.Create.ToString(CultureInfo.InvariantCulture));
            Line(output, "choice", "line", choice.Line);
            Line(output, "choice", "message", choice.Message);

            if (legacy)
            {
                Owner(output, ConfigOwnerScenarios.ImportOptions(folder), save);
            }
            else
            {
                Owner(output, new ConfigOwnerOptions<CanonicalConfigExample.ModConfig>
                {
                    Path = Path.Combine(folder, "CameraUnlock.ini"),
                    Table = CanonicalConfigExample.ModConfigTable(),
                    Header = new RenderHeader("Example Game"),
                }, save);
            }

            Files(output, folder);
            foreach (DefaultsCandidate candidate in resolution.Candidates) Files(output, candidate.Folder);
            Line(output, "end", "ok");
        }

        private static void Owner<T>(TextWriter output, ConfigOwnerOptions<T> options, bool save) where T : HeadTrackingConfigData
        {
            options.Defaults = DefaultsFile.PerUser();
            options.StatusSink = message => Line(output, "sink", message);
            var owner = new ConfigOwner<T>(options);
            ConfigLoadResult<T> load = owner.Load();
            Line(output, "load", "status", load.Status.ToString());
            Line(output, "load", "reason", load.Reason ?? "");
            foreach (string line in load.Log) Line(output, "log", line);
            if (!save) return;

            bool yaw = load.Config.WorldSpaceYaw;
            ConfigSaveResult saved = owner.Save(c => c.WorldSpaceYaw = !yaw);
            Line(output, "save", "status", saved.Status.ToString());
            Line(output, "save", "reason", saved.Reason ?? "");
            foreach (string line in saved.Log) Line(output, "save-log", line);
        }

        private static void Files(TextWriter output, string folder)
        {
            if (!Directory.Exists(folder)) return;
            string[] files = Directory.GetFiles(folder);
            Array.Sort(files, StringComparer.Ordinal);
            foreach (string file in files)
            {
                using (SHA256 sha = SHA256.Create())
                {
                    byte[] digest = sha.ComputeHash(File.ReadAllBytes(file));
                    Line(output, "file", file, BitConverter.ToString(digest).Replace("-", "").ToLowerInvariant());
                }
            }
        }

        private static void Line(TextWriter output, params string[] fields)
        {
            output.WriteLine(string.Join("\t", fields.Select(Field).ToArray()));
        }

        // A value never holds a tab or a line break in the output, so each line splits cleanly.
        private static string Field(string text)
        {
            return text.Replace("\t", "\\t").Replace("\r", "\\r").Replace("\n", "\\n");
        }
    }
}
