using System;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;
using CameraUnlock.Core.Protocol;

namespace CameraUnlock.Core.Tracking
{
    /// <summary>
    /// Complete per-frame head tracking pipeline with connection auto-recenter,
    /// tracking-loss hold, and tracking-mode cycling:
    ///
    ///   rotation: OpenTrackReceiver -> PoseInterpolator -> TrackingProcessor
    ///   position: OpenTrackReceiver -> PositionInterpolator -> PositionProcessor
    ///
    /// Framework-agnostic: call <see cref="Update"/> once per frame, then read
    /// <see cref="Rotation"/> and <see cref="PositionOffset"/> and apply them to the
    /// engine's camera however the mod requires.
    ///
    /// The session does not own the receiver's lifecycle - the caller starts and
    /// disposes it.
    /// </summary>
    public sealed class HeadTrackingSession
    {
        private readonly OpenTrackReceiver _receiver;
        private readonly TrackingProcessor _processor;
        private readonly PositionProcessor _positionProcessor;
        private readonly PoseInterpolator _poseInterpolator = new PoseInterpolator();
        private readonly PositionInterpolator _positionInterpolator = new PositionInterpolator();

        private TrackingMode _mode = TrackingMode.RotationAndPosition;
        private bool _wasFresh;
        private int _freshFrames;
        private bool _hasCentered;
        private TrackingPose _heldRotation;
        private Vec3 _heldPositionOffset;
        private bool _hasPose;

        public HeadTrackingSession(OpenTrackReceiver receiver, TrackingProcessor processor, PositionProcessor positionProcessor)
        {
            if (receiver == null) throw new ArgumentNullException("receiver");
            if (processor == null) throw new ArgumentNullException("processor");
            if (positionProcessor == null) throw new ArgumentNullException("positionProcessor");

            _receiver = receiver;
            _processor = processor;
            _positionProcessor = positionProcessor;
        }

        /// <summary>
        /// Active tracking mode. Switching position off resets position smoothing so
        /// re-enabling it does not blend from stale values.
        /// </summary>
        public TrackingMode Mode
        {
            get { return _mode; }
            set
            {
                if (_mode == value) return;
                _mode = value;
                if (!PositionActive)
                {
                    _positionProcessor.ResetSmoothing();
                    _positionInterpolator.Reset();
                }
            }
        }

        /// <summary>Whether head rotation is part of the current mode.</summary>
        public bool RotationActive
        {
            get { return _mode != TrackingMode.PositionOnly; }
        }

        /// <summary>Whether positional (6DOF) offset is part of the current mode.</summary>
        public bool PositionActive
        {
            get { return _mode != TrackingMode.RotationOnly; }
        }

        /// <summary>
        /// Consecutive fresh frames required after the first connection before the
        /// automatic recenter fires, giving phone trackers time to settle. Fires once
        /// per session; later tracking-loss gaps do not re-arm it.
        /// </summary>
        public int StabilizationFrames { get; set; } = 10;

        /// <summary>
        /// Optional logging callback (auto-recenter notifications).
        /// </summary>
#if NULLABLE_ENABLED
        public Action<string>? Log { get; set; }
#else
        public Action<string> Log { get; set; }
#endif

        /// <summary>
        /// True when tracker data is stale and <see cref="Update"/> is returning the
        /// held (last known) pose.
        /// </summary>
        public bool IsHolding { get; private set; }

        /// <summary>Processed head rotation from the latest <see cref="Update"/>.</summary>
        public TrackingPose Rotation { get; private set; }

        /// <summary>
        /// Processed position offset in meters from the latest <see cref="Update"/>.
        /// Zero when position is not part of the current mode.
        /// </summary>
        public Vec3 PositionOffset { get; private set; }

        /// <summary>
        /// Runs the pipeline for this frame. Fresh tracker data is interpolated and
        /// processed; stale data holds the last pose (no snap to center).
        /// </summary>
        /// <param name="deltaTime">Frame delta time in seconds.</param>
        /// <returns>True when a pose is available (fresh or held); false only when no tracker data has ever arrived.</returns>
        public bool Update(float deltaTime)
        {
            if (_receiver.IsDataFresh())
            {
                HandleConnectionRecenter();

                if (_receiver.TryConsumeRecenterRequest())
                {
                    // Counts as the connection recenter too: without this the
                    // stabilization countdown fires a few frames later and
                    // re-captures the center from wherever the head moved to,
                    // wiping the press the user just made.
                    _hasCentered = true;
                    // The tracker app zeroes its own output before signaling, so
                    // the packet carrying the counter already holds the new
                    // center. Recenter() would fold the previous smoothed pose -
                    // which the tracker just subtracted at its end - into the
                    // offset a second time, parking the view mirrored from the
                    // pre-press drift.
                    _processor.RecenterTo(_receiver.GetLatestPose());
                    _poseInterpolator.Reset();
                    _positionProcessor.SetCenter(_receiver.GetLatestPosition());
                    _positionInterpolator.Reset();
                    Log?.Invoke("Recentered by tracker app");
                }

                TrackingPose rawPose = _receiver.GetLatestPose();
                TrackingPose interpolated = _poseInterpolator.Update(rawPose, deltaTime);
                TrackingPose rotation = _processor.Process(interpolated, deltaTime);

                Vec3 positionOffset = Vec3.Zero;
                if (PositionActive)
                {
                    PositionData rawPosition = _receiver.GetLatestPosition();
                    PositionData interpolatedPosition = _positionInterpolator.Update(rawPosition, deltaTime);
                    Quat4 rotationQ = QuaternionUtils.FromYawPitchRoll(rotation.Yaw, rotation.Pitch, rotation.Roll);
                    positionOffset = _positionProcessor.Process(interpolatedPosition, rotationQ, deltaTime);
                }

                Rotation = rotation;
                PositionOffset = positionOffset;
                _heldRotation = rotation;
                _heldPositionOffset = positionOffset;
                _hasPose = true;
                IsHolding = false;
                return true;
            }

            _wasFresh = false;
            _freshFrames = 0;

            if (!_hasPose)
            {
                return false;
            }

            // Tracking loss: hold the last pose rather than snapping to center.
            IsHolding = true;
            Rotation = _heldRotation;
            PositionOffset = PositionActive ? _heldPositionOffset : Vec3.Zero;
            return true;
        }

        /// <summary>
        /// Advances to the next tracking mode
        /// (6DOF -> rotation only -> position only -> 6DOF) and returns it.
        /// </summary>
        public TrackingMode CycleMode()
        {
            Mode = (TrackingMode)(((int)_mode + 1) % 3);
            return _mode;
        }

        /// <summary>
        /// Sets the current head pose and position as the new center.
        ///
        /// Rotation centering happens ONLY in the processor (quaternion-based,
        /// gimbal-safe). Never also call OpenTrackReceiver.Recenter() - centering at
        /// both levels subtracts the offset twice and mirrors the pose instead of
        /// zeroing it.
        /// </summary>
        public void Recenter()
        {
            // Disarms the automatic recenter. A deliberate recenter is the
            // definitive answer to where centre is, and leaving the automatic one
            // armed means it fires the moment the player next holds still for
            // long enough and silently replaces the centre they just chose - the
            // same trap the remote recenter path above already guards against.
            _hasCentered = true;
            _processor.Recenter();
            _poseInterpolator.Reset();
            _positionProcessor.SetCenter(_receiver.GetLatestPosition());
            _positionInterpolator.Reset();
        }

        /// <summary>
        /// Resets transient pipeline state (interpolators, smoothing, held pose) while
        /// preserving center offsets. Call when tracking is re-enabled after being
        /// toggled off, so the view does not blend from stale values.
        /// </summary>
        public void Reset()
        {
            _poseInterpolator.Reset();
            _processor.ResetSmoothing();
            _positionInterpolator.Reset();
            _positionProcessor.ResetSmoothing();
            _hasPose = false;
            IsHolding = false;
        }

        // Fires once per session. Deliberately NOT re-armed by tracking-loss
        // gaps: the tracker app stops sending while the face is lost, and
        // recentering on packet resumption would capture whatever pose the
        // user happens to hold while sitting back down. Re-acquisition
        // recentering is the app's decision - it signals through the packet
        // trailer after walking the user through its hold-still flow.
        private void HandleConnectionRecenter()
        {
            if (_hasCentered) return;

            if (!_wasFresh)
            {
                _freshFrames = 0;
                _wasFresh = true;
            }

            _freshFrames++;
            if (_freshFrames >= StabilizationFrames)
            {
                _hasCentered = true;
                Recenter();
                Log?.Invoke("Auto-recentered on tracker connection");
            }
        }
    }
}
