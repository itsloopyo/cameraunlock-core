using UnityEngine;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Base class for implementing look/aim decoupling in FPS head tracking mods.
    ///
    /// CRITICAL CONCEPT - Look/Aim Decoupling:
    /// - LOOK direction = where the player's head is pointing (for rendering)
    /// - AIM direction = where the player is aiming (for game interactions)
    ///
    /// This component applies head tracking rotation ONLY during rendering (OnPreCull),
    /// then restores the original rotation after rendering (OnPostRender).
    ///
    /// Result:
    /// - Game logic (interactions, raycasts, FrobManager, etc.) sees the AIM direction
    /// - Rendering sees the LOOK direction with head tracking applied
    /// - Player can look around with head tracking while interactions remain at screen center
    ///
    /// Usage:
    /// 1. Inherit from this class
    /// 2. Override ComputeTrackedRotation() to return the head-tracked rotation
    /// 3. Override ShouldApplyTracking() to control when tracking is active
    /// 4. Optionally override OnPreCullComplete() and OnPostRenderComplete() for additional work
    /// 5. Attach to the main camera GameObject
    ///
    /// LEGACY PIPELINE ONLY. OnPreCull/OnPostRender are built-in-pipeline magic methods; URP
    /// and HDRP never invoke them, so on an SRP game this component applies nothing at all and
    /// does so silently. Use ViewMatrixTrackingController (which subscribes to both
    /// Camera.onPreCull and RenderPipelineManager.beginCameraRendering) for SRP titles.
    /// </summary>
    public abstract class LookAimDecoupledHook : MonoBehaviour
    {
        private Camera _camera;
        private Quaternion _preTrackingRotation;
        private bool _trackingAppliedThisFrame;
        private bool _transformDirty;
        private bool _isEnabled = true;

        /// <summary>
        /// Gets or sets whether tracking is enabled.
        /// When disabled, no rotation changes are applied.
        /// </summary>
        public bool IsEnabled
        {
            get => _isEnabled;
            set => _isEnabled = value;
        }

        /// <summary>
        /// Gets the camera this hook is attached to.
        /// </summary>
        protected Camera Camera => _camera;

        /// <summary>
        /// Gets whether tracking was applied this frame.
        /// </summary>
        protected bool TrackingAppliedThisFrame => _trackingAppliedThisFrame;

        /// <summary>
        /// Gets the pre-tracking rotation that was stored this frame.
        /// This is the AIM direction.
        /// </summary>
        protected Quaternion PreTrackingRotation => _preTrackingRotation;

        /// <summary>
        /// Called when the component is created.
        /// Override to perform initialization, but always call base.Awake().
        /// </summary>
        protected virtual void Awake()
        {
            _camera = GetComponent<Camera>();
        }

        /// <summary>
        /// Computes the rotation to apply during rendering (the LOOK direction).
        /// </summary>
        /// <param name="gameRotation">The game's current camera rotation (AIM direction).</param>
        /// <returns>The head-tracked rotation to use for rendering.</returns>
        protected abstract Quaternion ComputeTrackedRotation(Quaternion gameRotation);

        /// <summary>
        /// Determines whether tracking should be applied this frame.
        /// Override to implement game-specific conditions (e.g., in gameplay, connected, etc.).
        /// </summary>
        /// <returns>True if tracking should be applied, false otherwise.</returns>
        protected abstract bool ShouldApplyTracking();

        /// <summary>
        /// Called after tracking is applied in OnPreCull.
        /// Override to perform additional work (e.g., update aim offset, hide game UI).
        /// </summary>
        /// <param name="gameRotation">The original game rotation (AIM direction).</param>
        /// <param name="trackedRotation">The applied tracked rotation (LOOK direction).</param>
        protected virtual void OnPreCullComplete(Quaternion gameRotation, Quaternion trackedRotation)
        {
        }

        /// <summary>
        /// Called after rotation is restored in OnPostRender.
        /// Override to perform additional work after rendering completes.
        /// </summary>
        protected virtual void OnPostRenderComplete()
        {
        }

        /// <summary>
        /// Called just before this camera culls the scene.
        /// Stores the game rotation and applies head tracking for rendering.
        /// </summary>
        private void OnPreCull()
        {
            // Still dirty on entry means OnPostRender never ran for the previous cull - the
            // camera was disabled between cull and render, or something called Camera.Render()
            // directly. The transform is therefore still at LOOK, and capturing it below as the
            // AIM direction would compound tracking on itself every frame.
            if (_transformDirty && _camera != null)
            {
                _camera.transform.rotation = _preTrackingRotation;
                _transformDirty = false;
            }

            _trackingAppliedThisFrame = false;

            if (!_isEnabled || _camera == null)
            {
                return;
            }

            if (!ShouldApplyTracking())
            {
                return;
            }

            // Store the pre-tracking rotation (this is the AIM direction)
            _preTrackingRotation = _camera.transform.rotation;
            _trackingAppliedThisFrame = true;

            // Compute and apply head tracking (this is the LOOK direction)
            Quaternion trackedRotation = ComputeTrackedRotation(_preTrackingRotation);
            _camera.transform.rotation = trackedRotation;
            _transformDirty = true;

            // Notify subclass
            OnPreCullComplete(_preTrackingRotation, trackedRotation);
        }

        /// <summary>
        /// Called after this camera has finished rendering.
        /// Restores the original game rotation for game logic.
        /// </summary>
        private void OnPostRender()
        {
            // Only restore if we actually applied tracking this frame
            if (!_trackingAppliedThisFrame)
            {
                return;
            }

            if (_camera != null)
            {
                // Restore the AIM direction for game logic
                _camera.transform.rotation = _preTrackingRotation;
                _transformDirty = false;
            }

            // Notify subclass
            OnPostRenderComplete();
        }
    }
}
