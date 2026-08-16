using System;

#if !NET35 && !NET40
using System.Runtime.CompilerServices;
#endif

namespace CameraUnlock.Core.Math
{
    /// <summary>
    /// Frame-rate independent exponential smoothing utilities.
    /// <para>
    /// Two distinct concerns are handled by the same exponential SLERP/lerp:
    /// <list type="bullet">
    /// <item><b>Frame interpolation (always on)</b>: Ensures smooth output at any display
    /// refresh rate. Even at smoothing=0, the output is interpolated between tracker samples
    /// so that e.g. 30Hz tracker data looks smooth on a 240Hz display. Speed = <see cref="FrameInterpolationSpeed"/>.</item>
    /// <item><b>User smoothing (configurable)</b>: Reduces jitter/noise at the cost of added
    /// latency. Controlled by two user parameters, <see cref="DefaultLocalSmoothing"/> and
    /// <see cref="DefaultRemoteSmoothing"/>, selected per connection by
    /// <see cref="GetEffectiveSmoothing(float, float, bool)"/>.</item>
    /// </list>
    /// </para>
    /// <para>
    /// There is no smoothing floor. Whatever the user configured is what the pipeline uses,
    /// including 0. Frame interpolation survives that because the speed clamp inside
    /// <see cref="CalculateSmoothingFactor"/> never lets the per-frame factor reach 1.
    /// </para>
    /// </summary>
    public static class SmoothingUtils
    {
        /// <summary>
        /// Default smoothing for connections originating on the machine running the mod
        /// (loopback / same-host sender). Zero: a same-machine tracker is already stable,
        /// so smoothing only buys latency.
        /// </summary>
        public const float DefaultLocalSmoothing = 0.0f;

        /// <summary>
        /// Default smoothing for connections from a remote network device.
        /// 0.15 gives ~40% per frame at 60fps, settling in ~100-150ms, which covers the
        /// jitter a WiFi/phone tracker adds over the network.
        /// </summary>
        public const float DefaultRemoteSmoothing = 0.15f;

        /// <summary>
        /// Maximum interpolation speed (used at smoothing=0). This is the frame interpolation
        /// floor: fast enough to be responsive, slow enough to hide discrete tracker sample
        /// boundaries at high refresh rates.
        /// At 240Hz: t ≈ 0.19/frame, settles to 95% in ~60ms (~20ms average lag).
        /// At 60Hz:  t ≈ 0.57/frame, settles to 95% in ~50ms (~20ms average lag).
        /// </summary>
        public const float FrameInterpolationSpeed = 50f;

        // Minimum speed at maximum user smoothing (smoothing=1). ~5 second settling time.
        private const float MaxSmoothing = 0.1f;
        private const float SpeedRange = FrameInterpolationSpeed - MaxSmoothing;

        /// <summary>
        /// Calculates the smoothing interpolation factor for the current frame.
        /// Uses frame-rate independent exponential smoothing: t = 1 - exp(-speed * dt).
        /// The speed is always clamped to [<see cref="MaxSmoothing"/>, <see cref="FrameInterpolationSpeed"/>],
        /// guaranteeing frame interpolation regardless of the smoothing input value.
        /// </summary>
        /// <param name="smoothing">Smoothing factor 0-1. 0 = frame interpolation only, 1 = heavy smoothing.</param>
        /// <param name="deltaTime">Time since last frame in seconds.</param>
        /// <returns>Interpolation factor to use with Lerp/Slerp (always in (0, 1)).</returns>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static float CalculateSmoothingFactor(float smoothing, float deltaTime)
        {
            float speed = FrameInterpolationSpeed - SpeedRange * smoothing;
            if (speed > FrameInterpolationSpeed) speed = FrameInterpolationSpeed;
            if (speed < MaxSmoothing) speed = MaxSmoothing;
            return 1f - (float)System.Math.Exp(-speed * deltaTime);
        }

        /// <summary>
        /// Applies smoothing to a single value.
        /// </summary>
        /// <param name="current">Current smoothed value.</param>
        /// <param name="target">Target value to smooth towards.</param>
        /// <param name="smoothing">Smoothing factor 0-1.</param>
        /// <param name="deltaTime">Time since last frame in seconds.</param>
        /// <returns>New smoothed value.</returns>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static float Smooth(float current, float target, float smoothing, float deltaTime)
        {
            float t = CalculateSmoothingFactor(smoothing, deltaTime);
            return current + (target - current) * t;
        }

        /// <summary>
        /// Applies smoothing to a double value.
        /// </summary>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static double Smooth(double current, double target, float smoothing, float deltaTime)
        {
            float t = CalculateSmoothingFactor(smoothing, deltaTime);
            return current + (target - current) * t;
        }

        /// <summary>
        /// Selects the smoothing value for the current connection. This is the only path by
        /// which a smoothing value reaches a processor - no caller picks the value itself.
        /// </summary>
        /// <param name="localSmoothing">Smoothing configured for same-machine (loopback) senders.</param>
        /// <param name="remoteSmoothing">Smoothing configured for remote network senders.</param>
        /// <param name="isRemoteConnection">True when the packet source is a remote device.</param>
        /// <returns>The configured value for this connection, unmodified.</returns>
#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        public static float GetEffectiveSmoothing(float localSmoothing, float remoteSmoothing, bool isRemoteConnection)
        {
            return isRemoteConnection ? remoteSmoothing : localSmoothing;
        }
    }
}
