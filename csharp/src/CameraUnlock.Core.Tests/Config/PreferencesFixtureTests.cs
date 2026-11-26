using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using Xunit;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// Runs <see cref="PreferencesFixtures"/> on this test host's runtime. The
    /// CameraUnlock.Core.FrameworkTests console runs the same cases on .NET Framework 3.5 and 4.7.2.
    /// </summary>
    public class PreferencesFixtureTests
    {
        private static string Root([CallerFilePath] string sourceFile = "")
        {
            return CanonicalIniFixtures.FindRoot(Path.GetDirectoryName(sourceFile)!);
        }

        public static IEnumerable<object[]> Cases()
        {
            return PreferencesFixtures.Cases(Root()).Select(c => new object[] { c });
        }

        [Theory]
        [MemberData(nameof(Cases))]
        public void Fixture(string name)
        {
            string dir = ConfigOwnerScenarios.CreateScratchDirectory();
            try
            {
                PreferencesFixtures.RunCase(Root(), name, dir);
            }
            finally
            {
                ConfigOwnerScenarios.DeleteScratchDirectory(dir);
            }
        }
    }
}
