using System;
using System.Collections.Generic;
using CameraUnlock.Core.Os;
using Xunit;

namespace CameraUnlock.Core.Tests.Os
{
    // The same cases as TestCenteredOrigin in cpp/tests/os_tests.cpp, which also moves a
    // hidden window of its own with the Win32 half.
    public sealed class GameWindowTests
    {
        [Theory]
        [InlineData(0, 1920, 1280, 320)]       // a window in a work area at the origin
        [InlineData(0, 1040, 720, 160)]        // the work area's length, not the monitor's
        [InlineData(1920, 2560, 1936, 2232)]   // a monitor right of the primary
        [InlineData(-1920, 1920, 1280, -1600)] // a monitor left of the primary
        [InlineData(-1080, 1040, 720, -920)]   // a monitor above the primary
        [InlineData(-1000, 3000, 500, 250)]    // a work area that spans zero
        [InlineData(0, 1921, 1280, 320)]       // an odd gap leaves the spare pixel after the window
        [InlineData(-1921, 1921, 1280, -1601)] // and rounds the same way at a negative start
        [InlineData(100, 1280, 1280, 100)]     // a window as long as the work area
        [InlineData(0, 1920, 0, 960)]          // a zero-length window
        [InlineData(0, 1080, 1100, -10)]       // a longer window starts before the work area
        [InlineData(0, 1080, 1083, -1)]        // an odd overhang is halved toward zero
        [InlineData(-1080, 1080, 1100, -1090)] // a longer window at a negative start
        public void CenteredOriginIsTheStartPlusHalfTheGap(int workStart, int workLength, int windowLength, int expected)
        {
            Assert.Equal(expected, GameWindow.CenteredOrigin(workStart, workLength, windowLength));
        }

        [Fact]
        public void AHandleThatIsNotAWindowIsRefusedAndReported()
        {
            if (Environment.OSVersion.Platform != PlatformID.Win32NT) return;

            var log = new List<string>();
            Assert.False(GameWindow.CenterWindowInWorkArea(IntPtr.Zero, log.Add));
            Assert.Single(log);
            Assert.False(GameWindow.CenterWindowInWorkArea(IntPtr.Zero, null));
        }
    }
}
