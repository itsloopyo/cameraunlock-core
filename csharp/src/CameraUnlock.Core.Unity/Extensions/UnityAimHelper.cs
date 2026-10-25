using UnityEngine;
using CameraUnlock.Core.Aim;

namespace CameraUnlock.Core.Unity.Extensions
{
    /// <summary>
    /// Unity-specific helper methods for aim decoupling.
    /// </summary>
    public static class UnityAimHelper
    {
        /// <summary>
        /// Computes the decoupled aim direction in world space.
        /// </summary>
        /// <param name="cameraRotation">Current camera rotation (after head tracking applied).</param>
        /// <param name="yaw">Head tracking yaw in degrees.</param>
        /// <param name="pitch">Head tracking pitch in degrees.</param>
        /// <param name="roll">Head tracking roll in degrees.</param>
        /// <returns>World-space aim direction vector.</returns>
        public static Vector3 ComputeAimDirectionWorld(Quaternion cameraRotation, float yaw, float pitch, float roll)
        {
            // Get local aim direction (inverse of tracking rotation applied to forward)
            AimDecoupler.ComputeAimDirection(yaw, pitch, roll, out float ax, out float ay, out float az);
            Vector3 localAim = new Vector3(ax, ay, az);
            
            return cameraRotation * localAim;
        }

        /// <summary>
        /// Computes the decoupled aim direction using the split inverse method.
        /// This matches the pattern used by CameraRotationComposer.ComposeAdditive:
        ///   applied = yawWorld * gameRotation * Euler(-pitch, 0, roll)
        ///
        /// To get original game aim direction:
        ///   aimDir = inverseYawWorld * camera.rotation * inversePitchRollLocal * forward
        ///
        /// IMPORTANT: We use Quaternion.Inverse() for pitchRollLocal because Euler angles
        /// have application order (ZXY in Unity). Simply negating components gives wrong results.
        /// </summary>
        /// <param name="cameraRotation">Current camera rotation (after head tracking applied).</param>
        /// <param name="yaw">Head tracking yaw in degrees.</param>
        /// <param name="pitch">Head tracking pitch in degrees.</param>
        /// <param name="roll">Head tracking roll in degrees.</param>
        /// <returns>World-space aim direction vector.</returns>
        public static Vector3 ComputeAimDirectionWorldSplit(Quaternion cameraRotation, float yaw, float pitch, float roll)
        {
            // Inverse yaw around world up axis
            Quaternion inverseYaw = Quaternion.AngleAxis(-yaw, Vector3.up);

            // Pitch/roll as applied by CameraRotationComposer.ComposeAdditive
            // Must use Quaternion.Inverse() for mathematically correct inverse
            Quaternion pitchRollLocal = Quaternion.Euler(-pitch, 0f, roll);
            Quaternion inversePitchRoll = Quaternion.Inverse(pitchRollLocal);

            // Apply: inverseYaw * cameraRotation * inversePitchRoll * forward
            return inverseYaw * cameraRotation * inversePitchRoll * Vector3.forward;
        }

        /// <summary>
        /// Computes screen offset for crosshair positioning using Unity's camera projection.
        /// This method uses WorldToScreenPoint which automatically handles all rotation math.
        /// Returns zero if aim direction is behind the camera. Use ComputeScreenOffsetClamped
        /// if you want edge clamping instead.
        /// </summary>
        /// <param name="camera">The camera to project through.</param>
        /// <param name="aimDirection">World-space aim direction.</param>
        /// <returns>Screen offset from center in pixels, or zero if behind camera.</returns>
        public static Vector2 ComputeScreenOffset(Camera camera, Vector3 aimDirection)
        {
            Vector3 aimWorldPoint = camera.transform.position + aimDirection * 10f;
            Vector3 screenPoint = camera.WorldToScreenPoint(aimWorldPoint);

            float halfWidth = Screen.width * 0.5f;
            float halfHeight = Screen.height * 0.5f;

            // screenPoint.z > 0 means the point is in front of the camera
            if (screenPoint.z > 0)
            {
                return new Vector2(screenPoint.x - halfWidth, screenPoint.y - halfHeight);
            }

            return Vector2.zero;
        }

        /// <summary>
        /// Computes screen offset for crosshair positioning, clamping to screen edge
        /// when the aim direction is behind the camera.
        /// </summary>
        /// <param name="camera">The camera to project through.</param>
        /// <param name="aimDirection">World-space aim direction.</param>
        /// <param name="edgeMargin">Margin from screen edge (0-1, default 0.9 = 10% from edge).</param>
        /// <returns>Screen offset from center in pixels.</returns>
        public static Vector2 ComputeScreenOffsetClamped(Camera camera, Vector3 aimDirection, float edgeMargin = 0.9f)
        {
            return ProjectAimClamped(camera, aimDirection, 10f, edgeMargin);
        }

        /// <summary>
        /// The offset from screen centre of the point <paramref name="projectionDistance"/> along the
        /// aim, or the screen edge in the aim's direction when that point is behind the camera.
        /// </summary>
        internal static Vector2 ProjectAimClamped(Camera camera, Vector3 aimDirection, float projectionDistance, float edgeMargin)
        {
            Vector3 aimWorldPoint = camera.transform.position + aimDirection * projectionDistance;
            Vector3 screenPoint = camera.WorldToScreenPoint(aimWorldPoint);

            float halfWidth = Screen.width * 0.5f;
            float halfHeight = Screen.height * 0.5f;

            // screenPoint.z > 0 means the point is in front of the camera
            if (screenPoint.z > 0)
            {
                return new Vector2(screenPoint.x - halfWidth, screenPoint.y - halfHeight);
            }

            return ClampToScreenEdge(aimDirection, camera, halfWidth, halfHeight, edgeMargin);
        }

        /// <summary>
        /// Clamps an aim direction to the screen edge when it's behind the camera.
        /// Useful for keeping UI indicators visible even when aim is extreme.
        /// </summary>
        /// <param name="aimDirection">World-space aim direction.</param>
        /// <param name="camera">The camera for coordinate transformation.</param>
        /// <param name="halfWidth">Half screen width in pixels.</param>
        /// <param name="halfHeight">Half screen height in pixels.</param>
        /// <param name="edgeMargin">Margin from edge (0-1, 0.9 = 10% from edge).</param>
        /// <returns>Screen offset clamped to edge.</returns>
        public static Vector2 ClampToScreenEdge(Vector3 aimDirection, Camera camera, float halfWidth, float halfHeight, float edgeMargin = 0.9f)
        {
            Vector3 localAim = camera.transform.InverseTransformDirection(aimDirection);

            // Negate because we want to point toward the edge in the direction of the aim
            float x = -localAim.x;
            float y = -localAim.y;

            // Normalize to the larger component so we hit the edge
            float maxComponent = Mathf.Max(Mathf.Abs(x), Mathf.Abs(y));
            if (maxComponent > 0.001f)
            {
                x /= maxComponent;
                y /= maxComponent;
            }

            // Scale to screen edge with margin
            return new Vector2(x * halfWidth * edgeMargin, y * halfHeight * edgeMargin);
        }

        /// <summary>
        /// Computes screen offset using FOV-based tangent projection (no camera required).
        /// </summary>
        /// <param name="yaw">Head tracking yaw in degrees.</param>
        /// <param name="pitch">Head tracking pitch in degrees.</param>
        /// <param name="roll">Head tracking roll in degrees.</param>
        /// <param name="camera">Camera for FOV information.</param>
        /// <param name="compensationScale">Scale factor (default 1.0).</param>
        /// <returns>Screen offset from center in pixels.</returns>
        public static Vector2 ComputeScreenOffsetFOV(float yaw, float pitch, float roll, Camera camera, float compensationScale = 1.0f)
        {
            float verticalFov = camera.fieldOfView;
            float aspectRatio = camera.aspect;
            float horizontalFov = ScreenOffsetCalculator.CalculateHorizontalFov(verticalFov, aspectRatio);

            ScreenOffsetCalculator.Calculate(
                yaw, pitch, roll,
                horizontalFov, verticalFov,
                Screen.width, Screen.height,
                compensationScale,
                out float offsetX, out float offsetY);

            return new Vector2(offsetX, offsetY);
        }

        /// <summary>
        /// Creates a ray for aim-based raycasting.
        /// </summary>
        /// <param name="camera">The camera (for origin position).</param>
        /// <param name="aimDirection">World-space aim direction.</param>
        /// <returns>A ray from camera position along aim direction.</returns>
        public static Ray CreateAimRay(Camera camera, Vector3 aimDirection)
        {
            return new Ray(camera.transform.position, aimDirection);
        }

        /// <summary>
        /// Performs a raycast along the aim direction.
        /// </summary>
        /// <param name="camera">The camera (for origin position).</param>
        /// <param name="aimDirection">World-space aim direction.</param>
        /// <param name="maxDistance">Maximum raycast distance.</param>
        /// <param name="hit">Raycast hit info.</param>
        /// <param name="layerMask">Optional layer mask (default: all layers).</param>
        /// <returns>True if something was hit.</returns>
        public static bool AimRaycast(Camera camera, Vector3 aimDirection, float maxDistance, out RaycastHit hit, int layerMask = -1)
        {
            Ray ray = CreateAimRay(camera, aimDirection);
            return Physics.Raycast(ray, out hit, maxDistance, layerMask);
        }
    }
}
