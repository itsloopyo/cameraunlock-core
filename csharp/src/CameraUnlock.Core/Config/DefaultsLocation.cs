using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Where Defaults.ini is: the probes, a pure resolver over what they found, the choice
    /// between the candidates by the files that exist, and the creation of the CameraUnlock
    /// folder. The C++ twin is cameraunlock/config/defaults_location.h, and
    /// data/fixtures/canonical-ini/global/resolve.tsv holds both to the same candidates and lines.
    /// </summary>
    internal static class DefaultsLocation
    {
        internal const int NoPackage = 15700;
        internal const int ErrorPathNotFound = 3;
        private const int ErrorAlreadyExists = 183;
        private const uint UnixCodePage = 65010;
        private const string FolderName = "CameraUnlock";
        private const string FileName = "Defaults.ini";
        private const string NoKnownFolder = "Windows reported no roaming AppData folder";
        internal const string BuiltIn = " Settings set to default use the built-in values.";
        private const string ThisPrefix = " (this Wine prefix)";

        /// <summary>
        /// The candidates for Defaults.ini in the order they are tried, or none with the reason.
        /// Pure. On Windows, the roaming AppData known folder, which a packaged process never
        /// creates in. Under Wine, the host's config folder where Wine maps it to a drive letter,
        /// then the prefix's roaming AppData folder. Natively, $XDG_CONFIG_HOME or
        /// $HOME/.config, then $HOME/Library/Application Support, none of them created.
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="probe"/> is null.</exception>
        internal static DefaultsResolution Resolve(DefaultsProbe probe)
        {
            if (probe == null) throw new ArgumentNullException("probe");
            switch (probe.Platform)
            {
                case DefaultsPlatform.Windows:
                    int package = probe.PackageResult.HasValue ? probe.PackageResult.Value : NoPackage;
                    if (probe.KnownFolder.Length == 0)
                    {
                        return new DefaultsResolution(
                            probe.Platform, new DefaultsCandidate[0], NoKnownFolder, string.Empty, string.Empty, string.Empty,
                            package);
                    }
                    return new DefaultsResolution(
                        probe.Platform, new[] { AppData(DefaultsCandidateKind.Windows, probe.KnownFolder, package == NoPackage) },
                        string.Empty, string.Empty, string.Empty, string.Empty, package);
                case DefaultsPlatform.Wine:
                    return ResolveWine(probe);
                case DefaultsPlatform.Native:
                    return ResolveNative(probe);
                default:
                    throw new ArgumentException("the probe names no platform", "probe");
            }
        }

        /// <summary>
        /// What to do with the candidates. Pure. The first candidate whose file exists is read,
        /// and a later one that also exists draws a message. With none, each candidate that may
        /// be created is tried in order: <see cref="DefaultsChoice.Create"/> names the next, the
        /// caller creates it and calls again with its outcome. Once one is created, or nothing is
        /// left to try, the choice gives the one log line.
        /// </summary>
        /// <param name="resolution">What <see cref="Resolve"/> gave.</param>
        /// <param name="exists">Whether each candidate's file exists.</param>
        /// <param name="outcomes">What became of creating each candidate so far.</param>
        /// <exception cref="ArgumentNullException">An argument is null.</exception>
        /// <exception cref="ArgumentException">An array's length is not the number of candidates.</exception>
        internal static DefaultsChoice Choose(DefaultsResolution resolution, bool[] exists, DefaultsCreationOutcome[] outcomes)
        {
            if (resolution == null) throw new ArgumentNullException("resolution");
            if (exists == null) throw new ArgumentNullException("exists");
            if (outcomes == null) throw new ArgumentNullException("outcomes");
            IList<DefaultsCandidate> candidates = resolution.Candidates;
            if (exists.Length != candidates.Count || outcomes.Length != candidates.Count)
            {
                throw new ArgumentException("the arrays must hold one entry per candidate");
            }

            if (candidates.Count == 0)
            {
                string noLocation = "Defaults.ini: no location: " + resolution.NoLocation + ".";
                if (resolution.Platform == DefaultsPlatform.Wine) noLocation += HostSentence(resolution, outcomes);
                return Final(noLocation + BuiltIn);
            }

            int read = Array.IndexOf(exists, true);
            if (read >= 0)
            {
                int ignored = Array.IndexOf(exists, true, read + 1);
                if (ignored < 0) return new DefaultsChoice(read, -1, Found(resolution, read, "read", outcomes), string.Empty);
                string both = Named(candidates[read]) + " is read, and " + Named(candidates[ignored]) + " is not.";
                return new DefaultsChoice(read, -1, "Defaults.ini: " + both,
                    "Two Defaults.ini files: this game reads " + Named(candidates[read]) + " and ignores "
                        + Named(candidates[ignored]) + ".");
            }

            if (resolution.Platform == DefaultsPlatform.Native)
            {
                var shown = new string[candidates.Count];
                for (int i = 0; i < shown.Length; i++) shown[i] = candidates[i].Shown;
                return Final("Defaults.ini: no file at " + string.Join(" or ", shown)
                    + "; on this system the mod reads Defaults.ini but does not create it." + BuiltIn);
            }

            for (int i = 0; i < candidates.Count; i++)
            {
                if (!candidates[i].MayCreate) continue;
                switch (outcomes[i].Kind)
                {
                    case DefaultsCreation.NotTried:
                        return new DefaultsChoice(-1, i, string.Empty, string.Empty);
                    case DefaultsCreation.Created:
                        return new DefaultsChoice(i, -1, Found(resolution, i, "created with the built-in values", outcomes), string.Empty);
                    case DefaultsCreation.Appeared:
                        return new DefaultsChoice(i, -1,
                            Found(resolution, i, "created by another program at the same time, and read", outcomes), string.Empty);
                }
            }

            if (resolution.Platform == DefaultsPlatform.Windows)
            {
                DefaultsCandidate only = candidates[0];
                if (only.MayCreate) return Final("Defaults.ini: " + Failure(only, outcomes[0], string.Empty) + BuiltIn);
                return Final("Defaults.ini: not created, because this game runs as a packaged app (GetCurrentPackageFullName returned "
                    + resolution.PackageResult.ToString(CultureInfo.InvariantCulture) + "); " + only.Shown
                    + " is created by the next game that is not packaged, or by Lopari.");
            }

            int prefix = candidates.Count - 1;
            string failure = candidates[prefix].Kind == DefaultsCandidateKind.WinePrefix
                ? Failure(candidates[prefix], outcomes[prefix], ThisPrefix)
                : "no location: " + NoKnownFolder + ".";
            return Final("Defaults.ini: " + failure + HostSentence(resolution, outcomes) + BuiltIn);
        }

        /// <summary>
        /// Probes what <see cref="Resolve"/> reads. Natively only the environment is read, with no
        /// native call. On Windows and under Wine, kernel32 is reached through P/Invoke and every
        /// function that may be missing through GetProcAddress, so none is ever bound by name.
        /// </summary>
        internal static DefaultsProbe Probe()
        {
            var probe = new DefaultsProbe();
            if (Environment.OSVersion.Platform != PlatformID.Win32NT)
            {
                probe.Platform = DefaultsPlatform.Native;
                probe.Home = Variable("HOME");
                probe.XdgConfigHome = Variable("XDG_CONFIG_HOME");
                return probe;
            }

            probe.KnownFolder = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
            IntPtr ntdll = GetModuleHandleW("ntdll.dll");
            IntPtr wineGetVersion = GetProcAddress(ntdll, "wine_get_version");
            if (wineGetVersion == IntPtr.Zero)
            {
                probe.Platform = DefaultsPlatform.Windows;
                IntPtr packageName = GetProcAddress(GetModuleHandleW("kernel32.dll"), "GetCurrentPackageFullName");
                if (packageName != IntPtr.Zero)
                {
                    uint length = 0;
                    var function = (GetCurrentPackageFullName)Marshal.GetDelegateForFunctionPointer(
                        packageName, typeof(GetCurrentPackageFullName));
                    probe.PackageResult = function(ref length, IntPtr.Zero);
                }
                return probe;
            }

            probe.Platform = DefaultsPlatform.Wine;
            var version = (WineGetVersion)Marshal.GetDelegateForFunctionPointer(wineGetVersion, typeof(WineGetVersion));
            probe.WineVersion = Ansi(version());
            IntPtr wineGetHostVersion = GetProcAddress(ntdll, "wine_get_host_version");
            if (wineGetHostVersion != IntPtr.Zero)
            {
                var hostVersion = (WineGetHostVersion)Marshal.GetDelegateForFunctionPointer(
                    wineGetHostVersion, typeof(WineGetHostVersion));
                IntPtr sysname;
                IntPtr release;
                hostVersion(out sysname, out release);
                probe.HostSystem = Ansi(sysname);
            }
            probe.WineHomeDir = Variable("WINEHOMEDIR");
            probe.WineHostXdgConfigHome = Variable("WINE_HOST_XDG_CONFIG_HOME");
            probe.XdgConfigHome = Variable("XDG_CONFIG_HOME");

            string variable;
            string unix = HostUnixFolder(probe, out variable);
            if (unix.Length == 0) return probe;
            byte[] bytes = UnixBytes(unix);
            if (bytes.Length == 0)
            {
                probe.CodePageFailed = true;
                return probe;
            }
            probe.DosFileName = DosFileName(bytes);
            return probe;
        }

        /// <summary>
        /// Creates <paramref name="folder"/>, the CameraUnlock folder, with CreateDirectoryW, so
        /// only that one level: its parent must already exist. Windows and Wine only.
        /// </summary>
        /// <returns>0 when CreateDirectoryW created it or gave ERROR_ALREADY_EXISTS, which a file of that
        /// name gives too, so the folder may not be there and creating Defaults.ini in it then fails;
        /// <see cref="ErrorPathNotFound"/> when its parent does not exist; any other Win32 error it
        /// failed with.</returns>
        /// <exception cref="ArgumentNullException"><paramref name="folder"/> is null.</exception>
        /// <exception cref="PlatformNotSupportedException">Not running on Windows or under Wine.</exception>
        internal static int CreateFolder(string folder)
        {
            if (folder == null) throw new ArgumentNullException("folder");
            if (Environment.OSVersion.Platform != PlatformID.Win32NT)
            {
                throw new PlatformNotSupportedException("Defaults.ini's folder is created only on Windows and under Wine.");
            }
            if (CheckedFileWriter.CreateDirectoryW(folder, IntPtr.Zero)) return 0;
            int error = Marshal.GetLastWin32Error();
            return error == ErrorAlreadyExists ? 0 : error;
        }

        private static DefaultsResolution ResolveWine(DefaultsProbe probe)
        {
            string wine = "Wine " + probe.WineVersion + (probe.HostSystem.Length == 0 ? string.Empty : " on " + probe.HostSystem);
            string variable;
            string unix = HostUnixFolder(probe, out variable);
            string home = DosHome(probe.WineHomeDir);
            string why = string.Empty;
            string hostFolder = string.Empty;
            if (probe.HostSystem.Length == 0)
            {
                why = "Wine did not report the host system";
            }
            else if (unix.Length > 0)
            {
                string named = "$" + variable + "/" + FolderName;
                if (probe.CodePageFailed) why = named + " could not be converted to the Unix code page";
                else if (probe.DosFileName.Length == 0) why = "Wine could not convert " + named + " to a Windows path";
                else if (!IsDrivePath(probe.DosFileName)) why = named + " has no drive letter in this Wine prefix";
                else hostFolder = probe.DosFileName;
            }
            else if (probe.WineHomeDir.Length == 0)
            {
                why = "WINEHOMEDIR is not set";
            }
            else if (home.Length == 0)
            {
                why = "the home folder has no drive letter in this Wine prefix";
            }
            else
            {
                hostFolder = home + (probe.HostSystem == "Darwin" ? @"\Library\Application Support\" : @"\.config\") + FolderName;
            }

            var candidates = new List<DefaultsCandidate>();
            if (hostFolder.Length > 0)
            {
                string parent = hostFolder.Substring(0, hostFolder.LastIndexOf('\\'));
                string path = hostFolder + @"\" + FileName;
                string root = home.Length > 0 ? home : parent;
                string rootName = home.Length > 0 ? "~" : "$" + variable;
                candidates.Add(new DefaultsCandidate(
                    DefaultsCandidateKind.WineHost, true, path, hostFolder, parent, Show(path, root, rootName, '\\'),
                    Show(hostFolder, root, rootName, '\\'), Show(parent, root, rootName, '\\')));
            }
            if (probe.KnownFolder.Length > 0) candidates.Add(AppData(DefaultsCandidateKind.WinePrefix, probe.KnownFolder, true));
            return new DefaultsResolution(
                probe.Platform, candidates.ToArray(), candidates.Count == 0 ? NoKnownFolder : string.Empty, why, unix, wine,
                NoPackage);
        }

        private static DefaultsResolution ResolveNative(DefaultsProbe probe)
        {
            bool hasHome = IsUnixAbsolute(probe.Home);
            string home = hasHome ? probe.Home.TrimEnd('/') : string.Empty;
            var candidates = new List<DefaultsCandidate>();
            if (IsUnixAbsolute(probe.XdgConfigHome))
            {
                string xdg = probe.XdgConfigHome.TrimEnd('/');
                candidates.Add(hasHome ? Native(xdg, home, "~") : Native(xdg, xdg, "$XDG_CONFIG_HOME"));
            }
            else if (hasHome)
            {
                candidates.Add(Native(home + "/.config", home, "~"));
            }
            if (hasHome) candidates.Add(Native(home + "/Library/Application Support", home, "~"));
            return new DefaultsResolution(
                probe.Platform, candidates.ToArray(), candidates.Count == 0 ? "HOME is not set to an absolute path" : string.Empty,
                string.Empty, string.Empty, string.Empty, NoPackage);
        }

        private static DefaultsCandidate AppData(DefaultsCandidateKind kind, string knownFolder, bool mayCreate)
        {
            string folder = knownFolder + @"\" + FolderName;
            return new DefaultsCandidate(
                kind, mayCreate, folder + @"\" + FileName, folder, knownFolder, @"%AppData%\CameraUnlock\Defaults.ini",
                @"%AppData%\CameraUnlock", "%AppData%");
        }

        private static DefaultsCandidate Native(string parent, string root, string rootName)
        {
            string folder = parent + "/" + FolderName;
            string path = folder + "/" + FileName;
            return new DefaultsCandidate(
                DefaultsCandidateKind.Native, false, path, folder, parent, Show(path, root, rootName, '/'),
                Show(folder, root, rootName, '/'), Show(parent, root, rootName, '/'));
        }

        // The first XDG spelling that is absolute, as the dirs crate reads it, joined with the
        // folder name; empty on Darwin, with no host system, or with neither spelling absolute.
        private static string HostUnixFolder(DefaultsProbe probe, out string variable)
        {
            variable = string.Empty;
            if (probe.HostSystem.Length == 0 || probe.HostSystem == "Darwin") return string.Empty;
            string xdg;
            if (IsUnixAbsolute(probe.WineHostXdgConfigHome))
            {
                variable = "WINE_HOST_XDG_CONFIG_HOME";
                xdg = probe.WineHostXdgConfigHome;
            }
            else if (IsUnixAbsolute(probe.XdgConfigHome))
            {
                variable = "XDG_CONFIG_HOME";
                xdg = probe.XdgConfigHome;
            }
            else
            {
                return string.Empty;
            }
            return xdg.TrimEnd('/') + "/" + FolderName;
        }

        // WINEHOMEDIR is an NT path: \??\X:\... when a drive maps the home, whose DOS form drops
        // the \??\, and \??\unix\... when none does.
        private static string DosHome(string wineHomeDir)
        {
            const string prefix = @"\??\";
            if (!wineHomeDir.StartsWith(prefix, StringComparison.Ordinal)) return string.Empty;
            string dos = wineHomeDir.Substring(prefix.Length);
            return IsDrivePath(dos) ? dos.TrimEnd('\\') : string.Empty;
        }

        private static bool IsDrivePath(string path)
        {
            return path.Length >= 3 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
                && path[1] == ':' && path[2] == '\\';
        }

        private static bool IsUnixAbsolute(string path)
        {
            return path.Length > 0 && path[0] == '/';
        }

        // The root is the home, shown as ~, or with no home known the XDG folder the path came
        // from, shown as its variable, so an account name in either never reaches the log.
        private static string Show(string path, string root, string rootName, char separator)
        {
            bool underRoot = root.Length > 0 && path.StartsWith(root, StringComparison.Ordinal)
                && (path.Length == root.Length || path[root.Length] == separator);
            return underRoot ? rootName + path.Substring(root.Length) : path;
        }

        /// <summary>The candidate as a line names it: its shown path, and for the prefix's file under Wine
        /// <c> (this Wine prefix)</c> after it.</summary>
        internal static string Named(DefaultsCandidate candidate)
        {
            return candidate.Kind == DefaultsCandidateKind.WinePrefix ? candidate.Shown + ThisPrefix : candidate.Shown;
        }

        private static string Found(DefaultsResolution resolution, int index, string what, DefaultsCreationOutcome[] outcomes)
        {
            DefaultsCandidate candidate = resolution.Candidates[index];
            switch (candidate.Kind)
            {
                case DefaultsCandidateKind.WineHost:
                    return "Defaults.ini: " + candidate.Shown + " (" + resolution.Wine + ", the host's config folder, " + what + ")";
                case DefaultsCandidateKind.WinePrefix:
                    return "Defaults.ini: " + candidate.Shown + " (" + resolution.Wine + ", this Wine prefix, " + what + "): the host's config folder "
                        + HostFolder(resolution) + " could not be used: " + HostWhy(resolution, outcomes) + ".";
                default:
                    return "Defaults.ini: " + candidate.Shown + " (" + what + ")";
            }
        }

        private static string HostSentence(DefaultsResolution resolution, DefaultsCreationOutcome[] outcomes)
        {
            return " The host's config folder " + HostFolder(resolution) + " could not be used: " + HostWhy(resolution, outcomes) + ".";
        }

        private static string HostFolder(DefaultsResolution resolution)
        {
            return HasHost(resolution) ? resolution.Candidates[0].ShownFolder : "none";
        }

        private static string HostWhy(DefaultsResolution resolution, DefaultsCreationOutcome[] outcomes)
        {
            if (!HasHost(resolution)) return resolution.HostUnusable;
            DefaultsCandidate host = resolution.Candidates[0];
            switch (outcomes[0].Kind)
            {
                case DefaultsCreation.ParentMissing:
                    return host.ShownParent + " does not exist";
                case DefaultsCreation.FolderFailed:
                    return "it could not be created: " + outcomes[0].Why;
                case DefaultsCreation.FileFailed:
                    return "Defaults.ini was not created there: " + outcomes[0].Why;
                default:
                    return "it holds no Defaults.ini, and one there would be shared by every Wine prefix";
            }
        }

        private static bool HasHost(DefaultsResolution resolution)
        {
            return resolution.Candidates.Count > 0 && resolution.Candidates[0].Kind == DefaultsCandidateKind.WineHost;
        }

        private static string Failure(DefaultsCandidate candidate, DefaultsCreationOutcome outcome, string suffix)
        {
            switch (outcome.Kind)
            {
                case DefaultsCreation.ParentMissing:
                    return candidate.ShownFolder + suffix + " was not created, because " + candidate.ShownParent + " does not exist.";
                case DefaultsCreation.FolderFailed:
                    return candidate.ShownFolder + suffix + " could not be created: " + outcome.Why + ".";
                case DefaultsCreation.FileFailed:
                    return candidate.Shown + suffix + " was not created: " + outcome.Why + ".";
                default:
                    throw new ArgumentException(outcome.Kind + " is not a failure", "outcome");
            }
        }

        private static DefaultsChoice Final(string line)
        {
            return new DefaultsChoice(-1, -1, line, string.Empty);
        }

        private static string Variable(string name)
        {
            return Environment.GetEnvironmentVariable(name) ?? string.Empty;
        }

        private static string Ansi(IntPtr text)
        {
            return text == IntPtr.Zero ? string.Empty : Marshal.PtrToStringAnsi(text);
        }

        // The Unix text as bytes in Wine's Unix code page, the inverse of how Wine decoded the
        // environment, NUL-terminated; empty when the conversion failed.
        private static byte[] UnixBytes(string text)
        {
            int length = WideCharToMultiByte(UnixCodePage, 0, text, text.Length, new byte[0], 0, IntPtr.Zero, IntPtr.Zero);
            if (length <= 0) return new byte[0];
            var bytes = new byte[length + 1];
            int written = WideCharToMultiByte(UnixCodePage, 0, text, text.Length, bytes, length, IntPtr.Zero, IntPtr.Zero);
            return written == length ? bytes : new byte[0];
        }

        // Empty when kernel32 has no wine_get_dos_file_name or it returned NULL.
        private static string DosFileName(byte[] unix)
        {
            IntPtr export = GetProcAddress(GetModuleHandleW("kernel32.dll"), "wine_get_dos_file_name");
            if (export == IntPtr.Zero) return string.Empty;
            var convert = (WineGetDosFileName)Marshal.GetDelegateForFunctionPointer(export, typeof(WineGetDosFileName));
            IntPtr dos = convert(unix);
            if (dos == IntPtr.Zero) return string.Empty;
            try
            {
                return Marshal.PtrToStringUni(dos);
            }
            finally
            {
                HeapFree(GetProcessHeap(), 0, dos);
            }
        }

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int GetCurrentPackageFullName(ref uint length, IntPtr name);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr WineGetVersion();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void WineGetHostVersion(out IntPtr sysname, out IntPtr release);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr WineGetDosFileName(byte[] path);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandleW(string name);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);

        [DllImport("kernel32.dll", ExactSpelling = true)]
        private static extern int WideCharToMultiByte(
            uint codePage, uint flags, [MarshalAs(UnmanagedType.LPWStr)] string text, int textLength, byte[] bytes,
            int byteLength, IntPtr defaultChar, IntPtr usedDefaultChar);

        [DllImport("kernel32.dll", ExactSpelling = true)]
        private static extern IntPtr GetProcessHeap();

        [DllImport("kernel32.dll", ExactSpelling = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool HeapFree(IntPtr heap, uint flags, IntPtr memory);
    }
}
