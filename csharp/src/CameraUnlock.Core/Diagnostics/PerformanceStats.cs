using System.Diagnostics;
using System.Threading;

#if !NET35 && !NET40
using System.Runtime.CompilerServices;
#endif

namespace CameraUnlock.Core.Diagnostics
{
    /// <summary>
    /// Lightweight performance statistics tracking for head tracking operations.
    /// Uses lock-free counters and Stopwatch for minimal overhead.
    /// Stats can be logged periodically or accessed for monitoring.
    /// All methods are thread-safe.
    /// </summary>
    public static class PerformanceStats
    {
        private static readonly double TicksPerMicrosecond = Stopwatch.Frequency / 1_000_000.0;

        // Frame counters
        private static long _totalFrames;
        private static long _trackedFrames;
        private static long _skippedFrames;

        // Timing accumulators (in ticks)
        private static long _totalTrackingTicks;
        private static long _maxTrackingTicks;

        // UDP packet counters
        private static long _packetsReceived;
        private static long _packetsDropped;

        /// <summary>
        /// Total frames processed since stats were reset.
        /// </summary>
        public static long TotalFrames => Interlocked.Read(ref _totalFrames);

        /// <summary>
        /// Frames where tracking was applied.
        /// </summary>
        public static long TrackedFrames => Interlocked.Read(ref _trackedFrames);

        /// <summary>
        /// Frames where tracking was skipped (disabled, no signal, etc).
        /// </summary>
        public static long SkippedFrames => Interlocked.Read(ref _skippedFrames);

        /// <summary>
        /// Average tracking time per frame in microseconds.
        /// </summary>
        public static double AverageTrackingMicroseconds
        {
            get
            {
                long frames = Interlocked.Read(ref _trackedFrames);
                if (frames == 0) return 0;
                long ticks = Interlocked.Read(ref _totalTrackingTicks);
                return (ticks / TicksPerMicrosecond) / frames;
            }
        }

        /// <summary>
        /// Maximum tracking time observed in microseconds.
        /// </summary>
        public static double MaxTrackingMicroseconds
        {
            get
            {
                long ticks = Interlocked.Read(ref _maxTrackingTicks);
                return ticks / TicksPerMicrosecond;
            }
        }

        /// <summary>
        /// Total UDP packets received from OpenTrack.
        /// </summary>
        public static long PacketsReceived => Interlocked.Read(ref _packetsReceived);

        /// <summary>
        /// UDP packets dropped (invalid, undersized, etc).
        /// </summary>
        public static long PacketsDropped => Interlocked.Read(ref _packetsDropped);

        /// <summary>
        /// Records a frame where tracking was applied.
        /// </summary>
        /// <param name="elapsedTicks">Stopwatch ticks spent on tracking</param>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static void RecordTrackedFrame(long elapsedTicks)
        {
            Interlocked.Increment(ref _totalFrames);
            Interlocked.Increment(ref _trackedFrames);
            Interlocked.Add(ref _totalTrackingTicks, elapsedTicks);

            // Update max using compare-exchange loop
            long currentMax;
            do
            {
                currentMax = Interlocked.Read(ref _maxTrackingTicks);
                if (elapsedTicks <= currentMax) break;
            } while (Interlocked.CompareExchange(ref _maxTrackingTicks, elapsedTicks, currentMax) != currentMax);
        }

        /// <summary>
        /// Records a frame where tracking was skipped.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static void RecordSkippedFrame()
        {
            Interlocked.Increment(ref _totalFrames);
            Interlocked.Increment(ref _skippedFrames);
        }

        /// <summary>
        /// Records a UDP packet received.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static void RecordPacketReceived()
        {
            Interlocked.Increment(ref _packetsReceived);
        }

        /// <summary>
        /// Records a UDP packet dropped.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static void RecordPacketDropped()
        {
            Interlocked.Increment(ref _packetsDropped);
        }

        /// <summary>
        /// Resets all statistics to zero.
        /// </summary>
        public static void Reset()
        {
            Interlocked.Exchange(ref _totalFrames, 0);
            Interlocked.Exchange(ref _trackedFrames, 0);
            Interlocked.Exchange(ref _skippedFrames, 0);
            Interlocked.Exchange(ref _totalTrackingTicks, 0);
            Interlocked.Exchange(ref _maxTrackingTicks, 0);
            Interlocked.Exchange(ref _packetsReceived, 0);
            Interlocked.Exchange(ref _packetsDropped, 0);
        }

        /// <summary>
        /// Gets a summary string of current statistics.
        /// </summary>
        public static string GetSummary()
        {
            return string.Format(
                "Frames: {0} total, {1} tracked, {2} skipped | Tracking: {3:F1}us avg, {4:F1}us max | UDP: {5} received, {6} dropped",
                TotalFrames,
                TrackedFrames,
                SkippedFrames,
                AverageTrackingMicroseconds,
                MaxTrackingMicroseconds,
                PacketsReceived,
                PacketsDropped
            );
        }
    }
}
