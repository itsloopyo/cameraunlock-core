using System.IO;
using System.Runtime.CompilerServices;
using Xunit;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// Runs <see cref="CanonicalConfigExample"/> on this test host's runtime. The
    /// CameraUnlock.Core.FrameworkTests console runs it on .NET Framework 3.5 and 4.7.2.
    /// </summary>
    public class CanonicalConfigExampleTests
    {
        private static string Root([CallerFilePath] string sourceFile = "")
        {
            return CanonicalIniFixtures.FindRoot(Path.GetDirectoryName(sourceFile)!);
        }

        [Fact]
        public void TheTableRendersTheExampleFile()
        {
            CanonicalConfigExample.RunRender(Root());
        }

        [Fact]
        public void TheOwnerCreatesSavesAndReadsTheFile()
        {
            string dir = ConfigOwnerScenarios.CreateScratchDirectory();
            try
            {
                CanonicalConfigExample.RunOwner(Root(), dir);
            }
            finally
            {
                ConfigOwnerScenarios.DeleteScratchDirectory(dir);
            }
        }
    }
}
