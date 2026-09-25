using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using Xunit;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// Runs <see cref="DefaultsLocationFixtures"/> on this test host's runtime. The
    /// CameraUnlock.Core.FrameworkTests console runs the same cases on .NET Framework 3.5 and
    /// 4.7.2 (pixi run test-framework).
    /// </summary>
    public class DefaultsLocationTests
    {
        private static string Root([CallerFilePath] string sourceFile = "")
        {
            return CanonicalIniFixtures.FindRoot(Path.GetDirectoryName(sourceFile)!);
        }

        public static IEnumerable<object[]> Cases()
        {
            return DefaultsLocationFixtures.Cases(Root()).Select(c => new object[] { c });
        }

        [Theory]
        [MemberData(nameof(Cases))]
        public void Fixture(string name)
        {
            DefaultsLocationFixtures.RunCase(Root(), name);
        }

        [Fact]
        public void ChooseRefusesArraysOfTheWrongLength()
        {
            DefaultsLocationFixtures.RunChooseArguments();
        }

        [Fact]
        public void TheProbeFindsThisMachinesRoamingFolderAndCreatesNothing()
        {
            DefaultsLocationFixtures.RunRealProbe();
        }

        [Fact]
        public void TheFolderIsCreatedOneLevelOnly()
        {
            string dir = CheckedFileWriterScenarios.CreateScratchDirectory();
            try
            {
                DefaultsLocationFixtures.RunCreateFolder(dir);
            }
            finally
            {
                CheckedFileWriterScenarios.DeleteScratchDirectory(dir);
            }
        }
    }
}
