using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using CameraUnlock.Core.Dev;
using Xunit;

namespace CameraUnlock.Core.Tests.Dev
{
    // The start itself needs the native host and detours user32 in whatever process runs it,
    // so it is covered by powershell/tests/IsolatedInputHost.Tests.ps1 in a process of its own.
    public sealed class IsolatedInputTests : IDisposable
    {
        private readonly string _folder = Path.Combine(Path.GetTempPath(), "isolated-input-" + Guid.NewGuid().ToString("N"));
        private readonly List<string> _log = new List<string>();

        public IsolatedInputTests()
        {
            Directory.CreateDirectory(_folder);
        }

        public void Dispose()
        {
            Directory.Delete(_folder, true);
        }

        [Fact]
        public void DoesNothingWithoutTheCommandFile()
        {
            File.WriteAllText(Path.Combine(_folder, IsolatedInput.HostDllName), "not loaded");

            Assert.False(IsolatedInput.StartIfAsked(_folder, _log.Add));
            Assert.Empty(_log);
        }

        [Fact]
        public void ThrowsNamingTheHostDllWhenItIsMissing()
        {
            File.WriteAllText(Path.Combine(_folder, IsolatedInput.CommandFileName), "0");

            FileNotFoundException thrown = Assert.Throws<FileNotFoundException>(() => IsolatedInput.StartIfAsked(_folder, _log.Add));
            Assert.Contains("host DLL is missing", thrown.Message);
            Assert.Contains(Path.Combine(_folder, IsolatedInput.HostDllName), thrown.Message);
        }

        [Fact]
        public void ThrowsNamingTheHostDllWhenItDoesNotLoad()
        {
            if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) return;
            File.WriteAllText(Path.Combine(_folder, IsolatedInput.CommandFileName), "0");
            File.WriteAllText(Path.Combine(_folder, IsolatedInput.HostDllName), "not a DLL");

            InvalidOperationException thrown = Assert.Throws<InvalidOperationException>(() => IsolatedInput.StartIfAsked(_folder, _log.Add));
            Assert.Contains("did not load", thrown.Message);
            Assert.Contains(Path.Combine(_folder, IsolatedInput.HostDllName), thrown.Message);
            Assert.Empty(_log);
        }
    }
}
