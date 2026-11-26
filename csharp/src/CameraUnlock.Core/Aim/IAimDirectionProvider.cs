using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Aim
{
    /// <summary>
    /// Interface for providing the current aim direction.
    /// When head tracking is active, the aim direction differs from the view direction.
    /// Implementations provide the game's intended aim point (where weapons/interactions target)
    /// rather than the current view direction (which includes head tracking offset).
    /// </summary>
    public interface IAimDirectionProvider
    {
        /// <summary>
        /// Gets the current aim direction in world space.
        /// When tracking is active, this is the decoupled aim direction.
        /// When tracking is inactive, this typically returns the camera forward.
        /// </summary>
        Vec3 AimDirection { get; }

        /// <summary>
        /// Gets whether head tracking is currently active and affecting the view.
        /// </summary>
        bool IsTrackingActive { get; }
    }

    /// <summary>
    /// Delegate for getting the aim direction. Alternative to interface for simpler scenarios.
    /// </summary>
    /// <returns>The current aim direction in world space.</returns>
    public delegate Vec3 GetAimDirectionDelegate();

    /// <summary>
    /// Simple implementation that always returns forward direction.
    /// Use when no head tracking is active.
    /// </summary>
    public sealed class ForwardAimProvider : IAimDirectionProvider
    {
        /// <summary>
        /// Singleton instance.
        /// </summary>
        public static readonly ForwardAimProvider Instance = new ForwardAimProvider();

        private ForwardAimProvider() { }

        /// <summary>
        /// Always returns forward (0, 0, 1).
        /// </summary>
        public Vec3 AimDirection => Vec3.Forward;

        /// <summary>
        /// Always returns false.
        /// </summary>
        public bool IsTrackingActive => false;
    }

    /// <summary>
    /// Implementation that uses a delegate to get the aim direction.
    /// Useful for bridging to existing code without implementing a full interface.
    /// </summary>
    public sealed class DelegateAimProvider : IAimDirectionProvider
    {
        private readonly GetAimDirectionDelegate _getAimDirection;
        private readonly System.Func<bool> _isTrackingActive;

        /// <summary>
        /// Creates a delegate-based aim provider.
        /// </summary>
        /// <param name="getAimDirection">Delegate that returns the aim direction.</param>
        /// <param name="isTrackingActive">Function that returns whether tracking is active.</param>
        public DelegateAimProvider(GetAimDirectionDelegate getAimDirection, System.Func<bool> isTrackingActive)
        {
            _getAimDirection = getAimDirection;
            _isTrackingActive = isTrackingActive;
        }

        /// <summary>
        /// Gets the aim direction from the delegate.
        /// </summary>
        public Vec3 AimDirection => _getAimDirection();

        /// <summary>
        /// Gets whether tracking is active from the function.
        /// </summary>
        public bool IsTrackingActive => _isTrackingActive();
    }
}
