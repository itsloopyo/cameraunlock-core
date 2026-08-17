using System;
using UnityEngine;

namespace CameraUnlock.Core.Unity.UI
{
    /// <summary>
    /// Base class for UI elements that need to be offset based on aim decoupling.
    /// Handles the common lifecycle: acquire target, track state, apply offset, restore on disable.
    ///
    /// <example>
    /// Example implementation for a game's reticle:
    /// <code>
    /// public class MyReticleController : UIElementOffsetController
    /// {
    ///     private MyGameReticle _reticle;
    ///     private RectTransform _reticleRect;
    ///     private Vector2 _originalPosition;
    ///
    ///     protected override bool TryAcquireTarget()
    ///     {
    ///         _reticle = FindObjectOfType&lt;MyGameReticle&gt;();
    ///         if (_reticle == null) return false;
    ///         _reticleRect = _reticle.GetComponent&lt;RectTransform&gt;();
    ///         return _reticleRect != null;
    ///     }
    ///
    ///     protected override void CaptureOriginalState()
    ///     {
    ///         _originalPosition = _reticleRect.anchoredPosition;
    ///     }
    ///
    ///     protected override void ApplyOffset(Vector2 screenOffset)
    ///     {
    ///         _reticleRect.anchoredPosition = _originalPosition + screenOffset;
    ///     }
    ///
    ///     protected override void RestoreOriginalState()
    ///     {
    ///         _reticleRect.anchoredPosition = _originalPosition;
    ///     }
    ///
    ///     protected override void ClearReferences()
    ///     {
    ///         _reticle = null;
    ///         _reticleRect = null;
    ///     }
    /// }
    /// </code>
    /// </example>
    /// </summary>
    public abstract class UIElementOffsetController : MonoBehaviour
    {
        private bool _targetAcquired;
        private bool _originalStateCaptured;
        private bool _wasDecoupled;

        /// <summary>
        /// Gets or sets whether logging is enabled for debugging.
        /// </summary>
        public bool EnableLogging { get; set; }

        /// <summary>
        /// Optional logging action. Set this to enable debug logging.
        /// </summary>
        public Action<string> Log { get; set; }

        /// <summary>
        /// Delegate that checks if aim decoupling is currently active.
        /// Must be set by the consumer.
        /// </summary>
        public Func<bool> IsDecouplingActive { get; set; }

        /// <summary>
        /// Delegate that computes the current screen offset in pixels.
        /// Must be set by the consumer.
        /// </summary>
        public Func<Vector2> GetScreenOffset { get; set; }

        /// <summary>
        /// Optional delegate that gets canvas scale factor for offset compensation.
        /// If not set, assumes scale factor of 1.0.
        /// </summary>
        public Func<float> GetCanvasScaleFactor { get; set; }

        /// <summary>
        /// Called by Unity when component starts.
        /// </summary>
        protected virtual void Start()
        {
            _targetAcquired = false;
            _originalStateCaptured = false;
            _wasDecoupled = false;
            LogMessage(GetType().Name + " initialized");
        }

        /// <summary>
        /// Called by Unity every frame after Update.
        /// Handles the offset state machine.
        /// </summary>
        protected virtual void LateUpdate()
        {
            // Ensure we have valid target (only tries to acquire when not acquired)
            if (!EnsureTarget())
            {
                return;
            }

            // Check if decoupling should be active
            bool isDecoupled = IsDecouplingActive != null && IsDecouplingActive();

            if (isDecoupled)
            {
                ApplyCurrentOffset();
            }
            else if (_wasDecoupled)
            {
                RestoreOriginalState();
            }

            _wasDecoupled = isDecoupled;
        }

        /// <summary>
        /// Called by Unity when component is destroyed.
        /// Restores original state before destruction.
        /// </summary>
        protected virtual void OnDestroy()
        {
            if (_originalStateCaptured && IsTargetValid())
            {
                RestoreOriginalState();
            }
        }

        /// <summary>
        /// Whether the cached target is still usable. Override and return false once the
        /// cached reference has been destroyed (a scene change destroys and recreates the
        /// game's HUD), so the controller drops it and re-acquires instead of writing to a
        /// destroyed RectTransform forever while the new element sits at screen centre.
        ///
        /// The default returns true, preserving the acquire-once behaviour for controllers
        /// that have not implemented it yet.
        /// </summary>
        protected virtual bool IsTargetValid()
        {
            return true;
        }

        /// <summary>
        /// Ensures target is acquired and original state is captured.
        /// </summary>
        private bool EnsureTarget()
        {
            if (_targetAcquired && !IsTargetValid())
            {
                ClearReferences();
                _targetAcquired = false;
                _originalStateCaptured = false;
                _wasDecoupled = false;
                LogMessage("Target went stale, re-acquiring");
            }

            if (!_targetAcquired)
            {
                if (!TryAcquireTarget())
                {
                    return false;
                }
                _targetAcquired = true;
                LogMessage("Target acquired");
            }

            if (!_originalStateCaptured)
            {
                CaptureOriginalState();
                _originalStateCaptured = true;
                LogMessage("Original state captured");
            }

            return true;
        }

        /// <summary>
        /// Computes and applies the current offset.
        /// </summary>
        private void ApplyCurrentOffset()
        {
            if (GetScreenOffset == null)
            {
                return;
            }

            Vector2 screenOffset = GetScreenOffset();

            // Compensate for canvas scale if provided
            if (GetCanvasScaleFactor != null)
            {
                float scale = GetCanvasScaleFactor();
                if (scale > 0f)
                {
                    screenOffset /= scale;
                }
            }

            ApplyOffset(screenOffset);
        }

        /// <summary>
        /// Resets all references and state. Call when scene changes or target is lost.
        /// </summary>
        public void ResetReferences()
        {
            if (_originalStateCaptured)
            {
                RestoreOriginalState();
            }

            ClearReferences();
            _targetAcquired = false;
            _originalStateCaptured = false;
            _wasDecoupled = false;

            LogMessage("References reset");
        }

        /// <summary>
        /// Logs a message if logging is enabled.
        /// </summary>
        protected void LogMessage(string message)
        {
            if (EnableLogging && Log != null)
            {
                Log("[" + GetType().Name + "] " + message);
            }
        }

        #region Abstract Methods - Must Override

        /// <summary>
        /// Attempts to find and cache the target UI element.
        /// Called once when the controller first needs the target.
        /// </summary>
        /// <returns>True if target was successfully acquired, false otherwise.</returns>
        protected abstract bool TryAcquireTarget();

        /// <summary>
        /// Captures the original state (position, etc.) of the target element.
        /// Called once after target is successfully acquired.
        /// </summary>
        protected abstract void CaptureOriginalState();

        /// <summary>
        /// Applies the computed offset to the target element.
        /// Called every frame while decoupling is active.
        /// </summary>
        /// <param name="offset">The offset to apply, already compensated for canvas scale.</param>
        protected abstract void ApplyOffset(Vector2 offset);

        /// <summary>
        /// Restores the target element to its original state.
        /// Called when decoupling becomes inactive or on destroy.
        /// </summary>
        protected abstract void RestoreOriginalState();

        /// <summary>
        /// Clears all cached references to target elements.
        /// Called during ResetReferences().
        /// </summary>
        protected abstract void ClearReferences();

        #endregion
    }
}
