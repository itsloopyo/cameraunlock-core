using System;

namespace CameraUnlock.Core.State
{
    /// <summary>
    /// Interface for detecting game state to enable head tracking only during active gameplay.
    /// Implementations should detect menus, pause screens, loading, and cutscenes.
    /// </summary>
    public interface IGameStateDetector : IDisposable
    {
        /// <summary>
        /// Gets whether the player is currently in active gameplay.
        /// Returns false when in menus, pause screens, loading, or cutscenes.
        /// </summary>
        bool IsInGameplay { get; }

        /// <summary>
        /// Forces a cache invalidation for immediate re-check.
        /// Call when game state may have changed without detection.
        /// </summary>
        void InvalidateCache();
    }

    /// <summary>
    /// Delegate for checking if a specific gameplay condition is met.
    /// </summary>
    public delegate bool GameplayConditionCheck();

    /// <summary>
    /// Delegate for getting the current time (for throttling).
    /// </summary>
    public delegate float GetCurrentTime();
}
