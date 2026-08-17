using System.Diagnostics;

#if !NET35 && !NET40
using System.Runtime.CompilerServices;
#endif

namespace CameraUnlock.Core.Diagnostics
{
    /// <summary>
    /// Instance-based performance monitoring with rolling average for tracking frame times.
    /// Uses O(1) running sum for average calculation instead of iterating samples.
    /// For global static counters, use <see cref="PerformanceStats"/> instead.
    /// </summary>
    public sealed class PerformanceMonitor
    {
        // Pre-computed constant for ticks to microseconds conversion
        private static readonly double TicksToMicroseconds = 1_000_000.0 / Stopwatch.Frequency;

        private readonly long[] _samples;
        private readonly int _sampleCount;
        private int _sampleIndex;
        private int _validSampleCount;
        private long _runningSum;
        private readonly Stopwatch _stopwatch;
        private long _currentStart;

        /// <summary>
        /// Creates a new performance monitor with the specified sample count.
        /// </summary>
        /// <param name="sampleCount">Number of samples for rolling average (default 60).</param>
        public PerformanceMonitor(int sampleCount = 60)
        {
            _sampleCount = sampleCount > 0 ? sampleCount : 60;
            _samples = new long[_sampleCount];
            _stopwatch = new Stopwatch();
            _stopwatch.Start();
        }

        /// <summary>
        /// Average time in microseconds over the sample window.
        /// O(1) complexity using cached running sum.
        /// </summary>
        public double AverageMicroseconds
        {
#if !NET35 && !NET40
            [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
            get
            {
                if (_validSampleCount == 0)
                {
                    return 0.0;
                }
                double averageTicks = (double)_runningSum / _validSampleCount;
                return averageTicks * TicksToMicroseconds;
            }
        }

        /// <summary>
        /// Mark the start of a timing section.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public void BeginTiming()
        {
            _currentStart = _stopwatch.ElapsedTicks;
        }

        /// <summary>
        /// Mark the end of a timing section and record the sample.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public void EndTiming()
        {
            long elapsed = _stopwatch.ElapsedTicks - _currentStart;
            RecordTicksInternal(elapsed);
        }

        /// <summary>
        /// Record a timing value directly in ticks.
        /// </summary>
        /// <param name="ticks">Elapsed ticks to record.</param>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public void RecordTicks(long ticks)
        {
            RecordTicksInternal(ticks);
        }

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        private void RecordTicksInternal(long ticks)
        {
            // The valid-sample bookkeeping below only handles 0 <-> positive transitions,
            // so a negative sample matches neither branch while the running sum still
            // takes it: the count desynchronises from the sum permanently, and on a
            // small window it can pin AverageMicroseconds at 0 forever despite valid
            // samples arriving. RecordTicks is public and unvalidated, and a caller
            // differencing a Stopwatch across a Restart hands it a negative.
            if (ticks < 0) ticks = 0;

            long oldValue = _samples[_sampleIndex];

            // Update running sum: subtract old value, add new value
            _runningSum -= oldValue;
            _runningSum += ticks;

            // Track valid sample count for accurate average during warmup
            if (oldValue == 0 && ticks > 0)
            {
                _validSampleCount++;
            }
            else if (oldValue > 0 && ticks == 0)
            {
                _validSampleCount--;
            }

            _samples[_sampleIndex] = ticks;
            _sampleIndex = (_sampleIndex + 1) % _sampleCount;
        }

        /// <summary>
        /// Reset all samples to zero.
        /// </summary>
        public void Reset()
        {
            for (int i = 0; i < _samples.Length; i++)
            {
                _samples[i] = 0;
            }
            _sampleIndex = 0;
            _validSampleCount = 0;
            _runningSum = 0;
        }

        /// <summary>
        /// Get a formatted report string showing average time.
        /// </summary>
        /// <param name="label">Label to include in the report.</param>
        /// <returns>Formatted string like "Label: 123.4µs".</returns>
        public string GetReport(string label)
        {
            return string.Format("{0}: {1:F1}µs", label, AverageMicroseconds);
        }
    }
}
