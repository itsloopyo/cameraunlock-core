using System.Collections.Generic;
using System.Linq;
using CameraUnlock.Core.Config;
using Xunit;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// Runs <see cref="CheckedFileWriterScenarios"/> on this test host's runtime. The
    /// CameraUnlock.Core.FrameworkTests console runs the same scenarios on .NET Framework 3.5
    /// and 4.7.2 (pixi run test-framework).
    /// </summary>
    public class CheckedFileWriterTests
    {
        public static IEnumerable<object[]> Scenarios()
        {
            return CheckedFileWriterScenarios.Names.Select(n => new object[] { n });
        }

        [Fact]
        public void EnumNumbersMatchTheCppTwin()
        {
            Assert.Equal(new[] { 0, 1, 2, 3, 4 }, ((CheckedWriteOutcome[])System.Enum.GetValues(typeof(CheckedWriteOutcome))).Select(v => (int)v));
            Assert.Equal(0, (int)CheckedWriteOutcome.Committed);
            Assert.Equal(1, (int)CheckedWriteOutcome.TargetChanged);
            Assert.Equal(2, (int)CheckedWriteOutcome.TargetAppeared);
            Assert.Equal(3, (int)CheckedWriteOutcome.TargetMissing);
            Assert.Equal(4, (int)CheckedWriteOutcome.TargetReplaced);

            Assert.Equal(new[] { 1, 2, 3, 4, 5, 6, 7, 8 }, ((CheckedWriteStep[])System.Enum.GetValues(typeof(CheckedWriteStep))).Select(v => (int)v));
            Assert.Equal(1, (int)CheckedWriteStep.ReadTarget);
            Assert.Equal(2, (int)CheckedWriteStep.CreateTemporary);
            Assert.Equal(3, (int)CheckedWriteStep.WriteTemporary);
            Assert.Equal(4, (int)CheckedWriteStep.FlushTemporary);
            Assert.Equal(5, (int)CheckedWriteStep.CloseTemporary);
            Assert.Equal(6, (int)CheckedWriteStep.RecheckTarget);
            Assert.Equal(7, (int)CheckedWriteStep.Commit);
            Assert.Equal(8, (int)CheckedWriteStep.RemoveTemporary);
        }

        [Theory]
        [MemberData(nameof(Scenarios))]
        public void Scenario(string name)
        {
            string dir = CheckedFileWriterScenarios.CreateScratchDirectory();
            try
            {
                CheckedFileWriterScenarios.Run(name, dir);
            }
            finally
            {
                CheckedFileWriterScenarios.DeleteScratchDirectory(dir);
            }
        }
    }
}
