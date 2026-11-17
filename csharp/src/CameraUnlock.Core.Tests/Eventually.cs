using System;
using System.Diagnostics;
using System.Threading;
using Xunit;

namespace CameraUnlock.Core.Tests
{
    // Waits for something another thread does, such as a receiver publishing a datagram.
    // The bound is only how long to wait before calling it a failure, so it is long: a
    // loaded machine running the suites in parallel is slow, not broken.
    internal static class Eventually
    {
        public const int TimeoutMs = 10000;

        public static void True(Func<bool> condition, string failure)
        {
            True(condition, () => failure);
        }

        public static void True(Func<bool> condition, Func<string> failure)
        {
            var elapsed = Stopwatch.StartNew();
            while (!condition())
            {
                if (elapsed.ElapsedMilliseconds > TimeoutMs)
                {
                    Assert.Fail(failure() + " within " + TimeoutMs + " ms");
                }
                Thread.Sleep(1);
            }
        }
    }
}
