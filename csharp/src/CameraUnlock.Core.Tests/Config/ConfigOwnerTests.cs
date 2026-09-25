using System.Collections.Generic;
using System.Linq;
using CameraUnlock.Core.Config;
using Xunit;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// Runs <see cref="ConfigOwnerScenarios"/> on this test host's runtime. The
    /// CameraUnlock.Core.FrameworkTests console runs the same scenarios on .NET Framework 3.5 and
    /// 4.7.2, and kills a child copy of itself at each import step (pixi run test-framework).
    /// </summary>
    public class ConfigOwnerTests
    {
        public static IEnumerable<object[]> Scenarios()
        {
            return ConfigOwnerScenarios.Names.Select(n => new object[] { n });
        }

        [Fact]
        public void StatusNumbersArePinnedForTheCppTwin()
        {
            Assert.Equal(new[] { 0, 1, 2, 3, 4, 5 }, ((ConfigLoadStatus[])System.Enum.GetValues(typeof(ConfigLoadStatus))).Select(v => (int)v));
            Assert.Equal(0, (int)ConfigLoadStatus.Canonical);
            Assert.Equal(1, (int)ConfigLoadStatus.Migrated);
            Assert.Equal(2, (int)ConfigLoadStatus.Created);
            Assert.Equal(3, (int)ConfigLoadStatus.Deferred);
            Assert.Equal(4, (int)ConfigLoadStatus.LegacyRefused);
            Assert.Equal(5, (int)ConfigLoadStatus.Unreadable);

            Assert.Equal(new[] { 0, 1, 2 }, ((ConfigSaveStatus[])System.Enum.GetValues(typeof(ConfigSaveStatus))).Select(v => (int)v));
            Assert.Equal(0, (int)ConfigSaveStatus.Saved);
            Assert.Equal(1, (int)ConfigSaveStatus.NotSaved);
            Assert.Equal(2, (int)ConfigSaveStatus.Uncertain);

            Assert.Equal(new[] { 0, 1, 3 }, ((ConfigReloadStatus[])System.Enum.GetValues(typeof(ConfigReloadStatus))).Select(v => (int)v));
            Assert.Equal(0, (int)ConfigReloadStatus.Unchanged);
            Assert.Equal(1, (int)ConfigReloadStatus.Applied);
            Assert.Equal(3, (int)ConfigReloadStatus.Unreadable);
        }

        [Theory]
        [MemberData(nameof(Scenarios))]
        public void Scenario(string name)
        {
            string dir = ConfigOwnerScenarios.CreateScratchDirectory();
            try
            {
                ConfigOwnerScenarios.Run(name, dir);
            }
            finally
            {
                ConfigOwnerScenarios.DeleteScratchDirectory(dir);
            }
        }
    }
}
