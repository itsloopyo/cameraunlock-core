using UnityEngine;
using CameraUnlock.Core.Math;
using CameraUnlock.Core.Processing;

namespace CameraUnlock.Core.Unity.Tracking
{
    /// <summary>
    /// Reduces head tracking influence when the game is rapidly moving the camera.
    /// Useful for reducing head tracking during dialogues, cutscenes, or other
    /// scripted camera movements where head tracking would be disorienting.
    /// </summary>
    public class CameraSpeedInfluenceModifier : IInfluenceModifier
    {
        /// <summary>
        /// Camera speed (degrees/frame) above which influence starts to reduce.
        /// </summary>
        public float SpeedThreshold { get; set; } = 2.0f;

        /// <summary>
        /// Range of speeds over which influence decreases from 1 to MinInfluence.
        /// At speed = Threshold + Range, influence will be at MinInfluence.
        /// </summary>
        public float SpeedRange { get; set; } = 8.0f;

        /// <summary>
        /// Minimum influence when camera is moving rapidly.
        /// Set to 0 to completely disable tracking during fast movement.
        /// </summary>
        public float MinInfluence { get; set; } = 0.15f;

        private float _currentSpeed;
        private float _lastDegreesX;
        private float _lastDegreesY;
        private bool _initialized;

        /// <summary>
        /// Gets the current camera speed being tracked.
        /// </summary>
        public float CurrentSpeed => _currentSpeed;

        /// <summary>
        /// Creates a new CameraSpeedInfluenceModifier with default settings.
        /// Default: threshold=2, range=8, minInfluence=0.15
        /// </summary>
        public CameraSpeedInfluenceModifier()
        {
        }

        /// <summary>
        /// Creates a new CameraSpeedInfluenceModifier with custom settings.
        /// </summary>
        /// <param name="speedThreshold">Speed threshold to start reduction.</param>
        /// <param name="speedRange">Range over which reduction occurs.</param>
        /// <param name="minInfluence">Minimum influence at high speed.</param>
        public CameraSpeedInfluenceModifier(float speedThreshold, float speedRange, float minInfluence)
        {
            SpeedThreshold = speedThreshold;
            SpeedRange = speedRange;
            MinInfluence = minInfluence;
        }

        /// <summary>
        /// Updates the tracked camera speed. Call this every frame with the game's
        /// camera rotation values (typically from reflection on the camera controller).
        /// </summary>
        /// <param name="degreesX">Current X rotation in degrees (yaw).</param>
        /// <param name="degreesY">Current Y rotation in degrees (pitch).</param>
        public void UpdateFromDegrees(float degreesX, float degreesY)
        {
            if (!_initialized)
            {
                _lastDegreesX = degreesX;
                _lastDegreesY = degreesY;
                _initialized = true;
                _currentSpeed = 0f;
                return;
            }

            // Game yaw fields are commonly 0..360, so a plain subtraction reads a turn
            // past the wrap point as a 359°/frame slew and clamps influence to its floor
            // for that frame - a visible lurch every time the player rotates past north.
            // UpdateFromRotation already measures this correctly via Quaternion.Angle.
            float deltaX = AngleUtils.ShortestAngleDelta(_lastDegreesX, degreesX);
            float deltaY = AngleUtils.ShortestAngleDelta(_lastDegreesY, degreesY);
            _currentSpeed = Mathf.Sqrt(deltaX * deltaX + deltaY * deltaY);

            _lastDegreesX = degreesX;
            _lastDegreesY = degreesY;
        }

        /// <summary>
        /// Updates the tracked camera speed from a rotation change.
        /// </summary>
        /// <param name="previousRotation">Previous frame's rotation.</param>
        /// <param name="currentRotation">Current frame's rotation.</param>
        public void UpdateFromRotation(Quaternion previousRotation, Quaternion currentRotation)
        {
            float angle = Quaternion.Angle(previousRotation, currentRotation);
            _currentSpeed = angle;
        }

        /// <summary>
        /// Directly sets the camera speed. Use when you've already calculated
        /// the speed elsewhere.
        /// </summary>
        /// <param name="speed">The camera movement speed in degrees/frame.</param>
        public void SetSpeed(float speed)
        {
            _currentSpeed = speed;
        }

        /// <summary>
        /// Gets the current influence based on camera speed.
        /// </summary>
        /// <returns>Influence from MinInfluence to 1.</returns>
        public float GetInfluence()
        {
            if (_currentSpeed <= SpeedThreshold)
            {
                return 1f;
            }

            if (SpeedRange <= 0f)
            {
                return MinInfluence;
            }

            float reduction = Mathf.Clamp01((_currentSpeed - SpeedThreshold) / SpeedRange);
            return Mathf.Lerp(1f, MinInfluence, reduction);
        }

        /// <summary>
        /// Resets the speed tracking state.
        /// </summary>
        public void Reset()
        {
            _currentSpeed = 0f;
            _lastDegreesX = 0f;
            _lastDegreesY = 0f;
            _initialized = false;
        }
    }
}
