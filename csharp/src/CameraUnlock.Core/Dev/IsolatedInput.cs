using System;
using System.IO;
using System.Runtime.InteropServices;

namespace CameraUnlock.Core.Dev
{
    /// <summary>
    /// Isolated input for a C# mod: the game takes its keyboard and mouse from a command
    /// file instead of the real devices, so a test session can run in the background
    /// (docs/isolated-input.md). The detours are native, in CameraUnlockIsolatedInput.dll
    /// (cpp/tools/isolated_input_host), which a test session copies beside the mod.
    /// Called from a mod's dev build only: with it on, the game does not answer to the
    /// real keyboard.
    /// </summary>
    public static class IsolatedInput
    {
        public const string CommandFileName = "CameraUnlockInput.txt";
        public const string HostDllName = "CameraUnlockIsolatedInput.dll";

        private const string StartExport = "CameraUnlockStartIsolatedInput";

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private delegate int StartFunction(string commandFile);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryW(string path);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true, BestFitMapping = false)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);

        /// <summary>
        /// Starts isolated input when CameraUnlockInput.txt is in <paramref name="modFolder"/>,
        /// and does nothing when it is not, so the same dev build answers to the real keyboard
        /// without the file. True when it started. Throws when the file is there and the host
        /// DLL is missing, does not load, or does not start.
        /// </summary>
        public static bool StartIfAsked(string modFolder, Action<string> log)
        {
            string commandFile = Path.Combine(modFolder, CommandFileName);
            if (!File.Exists(commandFile)) return false;

            string hostDll = Path.Combine(modFolder, HostDllName);
            if (!File.Exists(hostDll))
            {
                throw new FileNotFoundException(
                    "Isolated input was asked for (" + commandFile + " is there) and its host DLL is missing: " + hostDll
                    + ". Start-IsolatedGame -ModHost managed copies it there.", hostDll);
            }

            IntPtr module = LoadLibraryW(hostDll);
            if (module == IntPtr.Zero)
            {
                int error = Marshal.GetLastWin32Error();
                throw new InvalidOperationException(
                    "Isolated input was asked for and its host DLL did not load: " + hostDll + ", Win32 error " + error
                    + (error == 193 ? " (the DLL and the game are not the same bitness)" : "") + ".");
            }

            IntPtr export = GetProcAddress(module, StartExport);
            if (export == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    "Isolated input was asked for and its host DLL has no " + StartExport + " export: " + hostDll + ".");
            }

            StartFunction start = (StartFunction)Marshal.GetDelegateForFunctionPointer(export, typeof(StartFunction));
            int result = start(commandFile);
            if (result != 0)
            {
                throw new InvalidOperationException(
                    "Isolated input was asked for and its host DLL did not start: " + Describe(result) + ".");
            }

            log("isolated input: the game takes its keyboard and mouse from " + commandFile
                + ", not from the real devices. Log: " + Path.Combine(modFolder, "CameraUnlockIsolatedInput.log"));
            return true;
        }

        // The numbers are StartResult in cpp/tools/isolated_input_host/host.cpp.
        private static string Describe(int result)
        {
            switch (result)
            {
                case 1: return "MinHook did not initialise (CameraUnlockIsolatedInput.log has the reason)";
                case 2: return "a detour could not be installed (CameraUnlockIsolatedInput.log names it)";
                case 3: return "it was already started in this process";
                default: return "it answered " + result + ", which this build of CameraUnlock.Core does not know";
            }
        }
    }
}
