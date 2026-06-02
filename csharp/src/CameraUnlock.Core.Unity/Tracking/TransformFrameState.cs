using UnityEngine;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Per-camera transform save/apply/restore state for the transform-modification
    /// tracking approach (the alternative to view-matrix modification).
    ///
    /// Render callbacks can fire multiple times per camera per frame (shadow passes,
    /// reflections, secondary cameras). This state captures the game's clean transform
    /// exactly once per frame, lets tracking be applied exactly once, and restores only
    /// the components that were modified so game logic never observes tracked state.
    ///
    /// Usage (one instance per camera):
    /// <code>
    /// // begin-render callback:
    /// if (state.BeginFrame(cam.transform, Time.frameCount))
    /// {
    ///     state.SetPosition(cam.transform, trackedPosition);
    ///     state.SetRotation(cam.transform, trackedRotation);
    /// }
    ///
    /// // end-render callback:
    /// state.Restore(cam.transform, Time.frameCount);
    /// </code>
    /// </summary>
    public sealed class TransformFrameState
    {
        private Quaternion _storedRotation;
        private Vector3 _storedPosition;
        private int _lastStoredFrame = -1;
        private bool _rotationModified;
        private bool _positionModified;

        /// <summary>The game's clean world rotation captured at the start of the frame.</summary>
        public Quaternion StoredRotation => _storedRotation;

        /// <summary>The game's clean world position captured at the start of the frame.</summary>
        public Vector3 StoredPosition => _storedPosition;

        /// <summary>
        /// Captures the transform's clean state on the first call of the frame.
        /// Returns true when tracking should be applied (first render pass of the frame);
        /// false when tracking was already applied this frame (subsequent passes).
        /// </summary>
        public bool BeginFrame(Transform transform, int frameCount)
        {
            if (_lastStoredFrame != frameCount)
            {
                _storedRotation = transform.rotation;
                _storedPosition = transform.position;
                _lastStoredFrame = frameCount;
                _rotationModified = false;
                _positionModified = false;
            }

            return !_rotationModified && !_positionModified;
        }

        /// <summary>Applies a tracked world rotation and marks it for restoration.</summary>
        public void SetRotation(Transform transform, Quaternion rotation)
        {
            transform.rotation = rotation;
            _rotationModified = true;
        }

        /// <summary>Applies a tracked world position and marks it for restoration.</summary>
        public void SetPosition(Transform transform, Vector3 position)
        {
            transform.position = position;
            _positionModified = true;
        }

        /// <summary>
        /// Restores the clean transform components that were modified this frame, so game
        /// logic (aim, physics, raycasts) reads the un-tracked state. No-op when nothing
        /// was modified or the stored state is from a previous frame.
        /// </summary>
        public void Restore(Transform transform, int frameCount)
        {
            if (_lastStoredFrame != frameCount)
            {
                return;
            }

            if (_rotationModified)
            {
                transform.rotation = _storedRotation;
                _rotationModified = false;
            }

            if (_positionModified)
            {
                transform.position = _storedPosition;
                _positionModified = false;
            }
        }
    }
}
