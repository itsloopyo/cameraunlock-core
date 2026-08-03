using System;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Protocol;
using CameraUnlock.Core.Unity.Rendering;
using CameraUnlock.Core.Unity.Utilities;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Per-frame head tracking controller that applies the full pipeline
    /// (receiver -> interpolator -> processor) to the game camera by modifying
    /// worldToCameraMatrix, leaving camera.transform untouched so game logic is unaffected.
    ///
    /// Handles render-hook registration (legacy and SRP), smooth transition in/out,
    /// 6DOF detection, and view-matrix reset when tracking stops.
    ///
    /// Usage:
    /// 1. Construct with the pipeline components and an optional game-specific camera resolver.
    /// 2. Call Enable() once, then ProcessFrame(shouldTrack) every LateUpdate.
    /// 3. Call Disable() on shutdown.
    /// </summary>
    public class ViewMatrixTrackingController
    {
        private const float TransitionInDuration = 0.5f;
        private const float TransitionOutDuration = 0.3f;
        private const float ProjectionEpsilon = 1e-6f;

        private readonly OpenTrackReceiver _receiver;
        private readonly TrackingProcessor _processor;
        private readonly PoseInterpolator _interpolator;
        private readonly PositionProcessor _positionProcessor;
        private readonly PositionInterpolator _positionInterpolator;
        private readonly Func<Camera> _cameraResolver;
        private readonly PerFrameCache<Camera> _mainCameraCache;

        // Current processed values (set in ProcessFrame, applied in the render hook).
        private float _currentYaw;
        private float _currentPitch;
        private float _currentRoll;
        private Vec3 _currentPosition;
        private bool _hasPosition;
        private bool _shouldApply;

        // Last applied values, used for the fade-out lerp.
        private float _lastYaw;
        private float _lastPitch;
        private float _lastRoll;
        private Vec3 _lastPosition;

        private bool _wasApplyingTracking;
        private bool _isTransitioningIn;
        private float _transitionInProgress;
        private bool _isTransitioningOut;
        private float _transitionOutProgress;

        // 6DOF stays off until the tracker delivers a non-zero position sample,
        // so 3DOF-only users don't get a position offset before recentering.
        private bool _detected6DOF;

        // Centering fires once per enable, not on every IsReceiving resume:
        // the tracker app stops sending while the face is lost, so capturing
        // a center when packets resume bakes in whatever pose the user holds
        // while sitting back down. Re-acquisition recentering is the app's
        // decision, signaled through the packet trailer.
        private bool _hasCentered;
        private bool _recenterOnStabilize;

        // worldToCameraMatrix is a sticky override: once set, Unity stops
        // recomputing it from camera.transform each frame. When we stop
        // applying tracking we must call ResetWorldToCameraMatrix() once,
        // otherwise the last head-rotated matrix sticks - producing a
        // permanent residual offset in menus / after toggle-off.
        private bool _needsMatrixReset;

        public bool PositionEnabled { get; set; }
        public bool RotationEnabled { get; set; }
        public bool WorldSpaceYaw { get; set; }

        /// <summary>
        /// Invoked after the controller consumes a tracker-app recenter request
        /// (packet trailer) inside ProcessFrame. Hook notifications/logging here
        /// instead of calling receiver.TryConsumeRecenterRequest() in mod code -
        /// the controller already consumes the request, so a second consumer
        /// races it and only one of the two ever sees a given press.
        /// </summary>
        public Action OnRemoteRecenter { get; set; }
        public bool IsApplyingTracking
        {
            get { return _wasApplyingTracking && !_isTransitioningOut; }
        }

        public float LastTrackingYaw
        {
            get { return _lastYaw; }
        }

        public float LastTrackingPitch
        {
            get { return _lastPitch; }
        }

        public float LastTrackingRoll
        {
            get { return _lastRoll; }
        }

        /// <summary>
        /// Per-frame cached rendering camera, resolved through the camera resolver.
        /// </summary>
        public Camera MainCamera
        {
            get { return _mainCameraCache.Get(); }
        }

        /// <param name="cameraResolver">
        /// Game-specific camera lookup (e.g. the game's camera manager singleton).
        /// Null falls back to Camera.main. The result is cached per frame.
        /// </param>
        public ViewMatrixTrackingController(
            OpenTrackReceiver receiver, TrackingProcessor processor, PoseInterpolator interpolator,
            PositionProcessor positionProcessor, PositionInterpolator positionInterpolator,
            Func<Camera> cameraResolver = null)
        {
            if (receiver == null) throw new ArgumentNullException("receiver");
            if (processor == null) throw new ArgumentNullException("processor");
            if (interpolator == null) throw new ArgumentNullException("interpolator");
            if (positionProcessor == null) throw new ArgumentNullException("positionProcessor");
            if (positionInterpolator == null) throw new ArgumentNullException("positionInterpolator");

            _receiver = receiver;
            _processor = processor;
            _interpolator = interpolator;
            _positionProcessor = positionProcessor;
            _positionInterpolator = positionInterpolator;
            _cameraResolver = cameraResolver;
            _mainCameraCache = new PerFrameCache<Camera>(ResolveCamera);

            PositionEnabled = true;
            RotationEnabled = true;
            WorldSpaceYaw = true;
        }

        public void Enable()
        {
            // Both hooks are needed: onPreCull doesn't fire under SRP/URP, but legacy
            // pipelines don't fire beginCameraRendering. Subscribing to both is safe -
            // a given Unity build only invokes one path per frame. Both go through
            // reflection: SRP-only Unity 6 builds strip the legacy Camera.onPreCull
            // accessor, so a direct reference throws MissingMethodException at JIT time.
            RenderPipelineHelper.AddOnPreCull(OnPreCull);
            RenderPipelineHelper.AddBeginCameraRendering(OnPreCull);
        }

        public void Disable()
        {
            RenderPipelineHelper.RemoveOnPreCull();
            RenderPipelineHelper.RemoveBeginCameraRendering();

            var cam = _mainCameraCache.Get();
            if (cam != null)
                cam.ResetWorldToCameraMatrix();
        }

        /// <summary>
        /// Process tracking data for this frame. Call from LateUpdate.
        /// </summary>
        /// <returns>True when tracking is being applied to the camera this frame.</returns>
        public bool ProcessFrame(bool enabled)
        {
            if (enabled && _receiver.IsReceiving)
            {
                _isTransitioningOut = false;

                if (!_wasApplyingTracking)
                    BeginTrackingSession();

                if (_receiver.TryConsumeRecenterRequest())
                {
                    Recenter();
                    OnRemoteRecenter?.Invoke();
                }

                float scale = AdvanceTransitionIn();

                var rawPose = _receiver.GetLatestPose();
                var interpolated = _interpolator.Update(rawPose, Time.deltaTime);
                var processed = _processor.Process(interpolated, Time.deltaTime);

                ApplyRotation(processed, scale);
                ApplyPosition(interpolated, scale);

                _lastYaw = _currentYaw;
                _lastPitch = _currentPitch;
                _lastRoll = _currentRoll;
                _lastPosition = _currentPosition;
                _shouldApply = true;
                _wasApplyingTracking = true;
                return true;
            }

            if (_isTransitioningOut)
            {
                AdvanceTransitionOut();
            }
            else if (_wasApplyingTracking)
            {
                _isTransitioningOut = true;
                _transitionOutProgress = 0f;
                AdvanceTransitionOut();
            }

            return false;
        }

        /// <summary>
        /// Projects the game's clean aim direction into the head-tracked view and returns its
        /// screen offset from center in pixels (+X right, +Y up, matching uGUI anchoredPosition).
        /// Uses the same rotation composition as the camera modification, selected by
        /// <see cref="WorldSpaceYaw"/>, so the reticle lands exactly on the aim point.
        /// Call after ProcessFrame. Returns false when tracking is not being applied, no camera
        /// is available, or the aim point is outside the tracked view (behind the camera).
        /// </summary>
        public bool TryGetAimScreenOffset(out Vector2 screenOffset)
        {
            screenOffset = Vector2.zero;

            if (!IsApplyingTracking)
                return false;

            var cam = _mainCameraCache.Get();
            if (cam == null)
                return false;

            Vector3 aimDirection = WorldSpaceYaw
                ? ViewMatrixModifier.ComputeAimDirectionInTrackedViewDecomposed(
                    cam.transform.rotation, _lastYaw, _lastPitch, _lastRoll)
                : ViewMatrixModifier.ComputeAimDirectionInTrackedView(_lastYaw, _lastPitch, _lastRoll);

            float forward = -aimDirection.z;
            if (forward < ProjectionEpsilon)
                return false;

            float tanHalfFovY = Mathf.Tan(cam.fieldOfView * Mathf.Deg2Rad * 0.5f);
            float tanHalfFovX = tanHalfFovY * cam.aspect;
            if (tanHalfFovX < ProjectionEpsilon || tanHalfFovY < ProjectionEpsilon)
                return false;

            screenOffset = new Vector2(
                aimDirection.x / forward / tanHalfFovX * (Screen.width * 0.5f),
                aimDirection.y / forward / tanHalfFovY * (Screen.height * 0.5f));
            return true;
        }

        public void OnTrackingEnabled()
        {
            ResetSmoothingState();
            ResetInterpolators();
            _isTransitioningOut = false;
            // A deliberate user re-enable recaptures the center; data gaps do not.
            _hasCentered = false;
        }

        /// <summary>
        /// Recenter to the latest received pose. Safe to call regardless of whether
        /// tracking is currently being applied.
        /// </summary>
        public void Recenter()
        {
            RecenterToLatest();
            ResetInterpolators();
            _hasCentered = true;
            _recenterOnStabilize = false;
        }

        public void OnTrackingDisabled()
        {
            if (_wasApplyingTracking)
            {
                _isTransitioningOut = true;
                _transitionOutProgress = 0f;
            }
        }

        public void ResetState()
        {
            if (_wasApplyingTracking || _isTransitioningOut)
                _needsMatrixReset = true;
            _mainCameraCache.Invalidate();
            _isTransitioningOut = false;
            _isTransitioningIn = false;
            _transitionInProgress = 0f;
            _wasApplyingTracking = false;
            _shouldApply = false;
            _lastYaw = 0f;
            _lastPitch = 0f;
            _lastRoll = 0f;
            _lastPosition = Vec3.Zero;
            _currentPosition = Vec3.Zero;
            _hasPosition = false;
            _detected6DOF = false;
            _hasCentered = false;
            _recenterOnStabilize = false;
            ResetSmoothingState();
            ResetInterpolators();
        }

        private void BeginTrackingSession()
        {
            if (!_hasCentered)
            {
                RecenterToLatest();
                _hasCentered = true;
                _recenterOnStabilize = true;
            }
            _isTransitioningIn = true;
            _transitionInProgress = 0f;
            _detected6DOF = false;
            ResetInterpolators();
            ResetSmoothingState();
        }

        private void RecenterToLatest()
        {
            _processor.RecenterTo(_receiver.GetLatestPose());
            _positionProcessor.SetCenter(_receiver.GetLatestPosition());
        }

        private void ResetInterpolators()
        {
            _interpolator.Reset();
            _positionInterpolator.Reset();
        }

        private void ResetSmoothingState()
        {
            _processor.ResetSmoothing();
            _positionProcessor.ResetSmoothing();
        }

        private float AdvanceTransitionIn()
        {
            if (!_isTransitioningIn)
                return 1f;

            _transitionInProgress += Time.deltaTime / TransitionInDuration;
            if (_transitionInProgress >= 1f)
            {
                _transitionInProgress = 1f;
                _isTransitioningIn = false;

                // Re-recenter after stabilization so the rest of the session uses
                // a clean reference pose rather than whatever was first received.
                // Only when this transition-in captured the center: skipped on
                // re-acquisition resumes and after a deliberate recenter.
                if (_recenterOnStabilize && _receiver.IsReceiving)
                    RecenterToLatest();
                _recenterOnStabilize = false;
            }
            return _transitionInProgress * _transitionInProgress;
        }

        private void ApplyRotation(TrackingPose processed, float scale)
        {
            if (RotationEnabled)
            {
                _currentYaw = processed.Yaw * scale;
                _currentPitch = processed.Pitch * scale;
                _currentRoll = processed.Roll * scale;
            }
            else
            {
                _currentYaw = 0f;
                _currentPitch = 0f;
                _currentRoll = 0f;
            }
        }

        private void ApplyPosition(TrackingPose interpolated, float scale)
        {
            if (!PositionEnabled)
            {
                _currentPosition = Vec3.Zero;
                _hasPosition = false;
                return;
            }

            var rawPos = _receiver.GetLatestPosition();
            if (!_detected6DOF && (rawPos.X != 0f || rawPos.Y != 0f || rawPos.Z != 0f))
                _detected6DOF = true;

            if (!_detected6DOF)
            {
                _currentPosition = Vec3.Zero;
                _hasPosition = false;
                return;
            }

            var interpolatedPos = _positionInterpolator.Update(rawPos, Time.deltaTime);
            // Pivot compensation needs the physical head orientation (interpolated, pre-processing),
            // with pitch negated to match tracker conventions.
            var physicalRotQ = QuaternionUtils.FromYawPitchRoll(
                interpolated.Yaw, -interpolated.Pitch, interpolated.Roll);
            var finalPos = _positionProcessor.Process(interpolatedPos, physicalRotQ, Time.deltaTime);
            _currentPosition = finalPos * scale;
            _hasPosition = true;
        }

        private Camera ResolveCamera()
        {
            if (_cameraResolver != null)
            {
                var cam = _cameraResolver();
                if (cam != null)
                    return cam;
            }
            return Camera.main;
        }

        private void OnPreCull(Camera cam)
        {
            var mainCam = _mainCameraCache.Get();
            if (cam != mainCam || mainCam == null)
                return;

            if (_needsMatrixReset && !_shouldApply && !_isTransitioningOut)
            {
                cam.ResetWorldToCameraMatrix();
                _needsMatrixReset = false;
                return;
            }

            if (_shouldApply)
            {
                ApplyToCamera(cam, _currentYaw, _currentPitch, _currentRoll,
                    _hasPosition ? _currentPosition : Vec3.Zero);
                _shouldApply = false;
                return;
            }

            if (_isTransitioningOut)
            {
                float t = _transitionOutProgress;
                float fadedYaw = Mathf.Lerp(_lastYaw, 0f, t);
                float fadedPitch = Mathf.Lerp(_lastPitch, 0f, t);
                float fadedRoll = Mathf.Lerp(_lastRoll, 0f, t);
                var fadedPos = Vec3.Lerp(_lastPosition, Vec3.Zero, t);

                if (fadedYaw != 0f || fadedPitch != 0f || fadedRoll != 0f ||
                    fadedPos.X != 0f || fadedPos.Y != 0f || fadedPos.Z != 0f)
                {
                    ApplyToCamera(cam, fadedYaw, fadedPitch, fadedRoll, fadedPos);
                }
            }
        }

        private void ApplyToCamera(Camera cam, float yaw, float pitch, float roll, Vec3 position)
        {
            var offset = new Vector3(position.X, position.Y, position.Z);
            if (WorldSpaceYaw)
                ViewMatrixModifier.ApplyHeadRotationDecomposed(cam, yaw, pitch, roll, offset);
            else
                ViewMatrixModifier.ApplyHeadRotation(cam, yaw, pitch, roll, offset);
        }

        private void AdvanceTransitionOut()
        {
            _transitionOutProgress += Time.deltaTime / TransitionOutDuration;
            if (_transitionOutProgress >= 1f)
            {
                _isTransitioningOut = false;
                _wasApplyingTracking = false;
                _shouldApply = false;
                _needsMatrixReset = true;
            }
        }
    }
}
