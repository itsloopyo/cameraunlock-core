using System;
using UnityEngine;
using CameraUnlock.Core.Aim;

namespace CameraUnlock.Core.Unity.Extensions
{
    /// <summary>
    /// Provides utility methods for compensating Canvas UI element positions during head tracking.
    /// When the view matrix is rotated by head tracking, screen-space UI elements need to be
    /// repositioned to maintain their world-fixed appearance.
    /// </summary>
    public static class CanvasCompensation
    {
        /// <summary>
        /// Repositions all active children of a canvas to compensate for head tracking rotation.
        /// Each child's position is offset by yaw/pitch and rotated by roll to maintain
        /// their apparent world-space positions.
        /// </summary>
        /// <param name="canvasRectTransform">The canvas RectTransform whose children to reposition.</param>
        /// <param name="cam">The camera to get FOV information from.</param>
        /// <param name="yaw">Head tracking yaw in degrees.</param>
        /// <param name="pitch">Head tracking pitch in degrees.</param>
        /// <param name="roll">Head tracking roll in degrees.</param>
        /// <exception cref="ArgumentNullException">Thrown when canvasRectTransform or cam is null.</exception>
        public static void RepositionChildren(
            RectTransform canvasRectTransform,
            Camera cam,
            float yaw,
            float pitch,
            float roll)
        {
            if (canvasRectTransform == null)
            {
                throw new ArgumentNullException(nameof(canvasRectTransform));
            }

            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam));
            }

            // Get canvas dimensions
            float canvasWidth = canvasRectTransform.rect.width;
            float canvasHeight = canvasRectTransform.rect.height;
            float halfWidth = canvasWidth * 0.5f;
            float halfHeight = canvasHeight * 0.5f;

            // Calculate projection parameters
            float verticalFov = cam.fieldOfView;
            float horizontalFov = ScreenOffsetCalculator.CalculateHorizontalFov(verticalFov, cam.aspect);

            // Calculate yaw/pitch offset (same formula as reticle positioning)
            ScreenOffsetCalculator.Calculate(
                yaw, pitch, 0f, // Don't apply roll in Calculate, we do it separately per-element
                horizontalFov, verticalFov,
                canvasWidth, canvasHeight,
                1f,
                out float offsetX, out float offsetY);

            // Pre-calculate roll rotation values
            // Roll is negated to match view matrix convention
            float rollRad = -roll * Mathf.Deg2Rad;
            float cosRoll = Mathf.Cos(rollRad);
            float sinRoll = Mathf.Sin(rollRad);

            // Iterate all children
            int childCount = canvasRectTransform.childCount;
            for (int i = 0; i < childCount; i++)
            {
                Transform child = canvasRectTransform.GetChild(i);
                if (!child.gameObject.activeInHierarchy)
                {
                    continue;
                }

                RectTransform rectTransform = child as RectTransform;
                if (rectTransform == null)
                {
                    continue;
                }

                // Get current position relative to canvas center
                Vector2 pos = rectTransform.anchoredPosition;
                float relX = pos.x - halfWidth;
                float relY = pos.y - halfHeight;

                // Apply roll rotation
                float rotatedRelX = relX * cosRoll - relY * sinRoll;
                float rotatedRelY = relX * sinRoll + relY * cosRoll;

                // Apply yaw/pitch offset (not rotated - same as reticle approach)
                float newX = rotatedRelX + offsetX + halfWidth;
                float newY = rotatedRelY + offsetY + halfHeight;

                rectTransform.anchoredPosition = new Vector2(newX, newY);
            }
        }

        /// <summary>
        /// Repositions a single UI element to compensate for head tracking rotation.
        /// </summary>
        /// <param name="element">The RectTransform to reposition.</param>
        /// <param name="canvasWidth">The width of the containing canvas.</param>
        /// <param name="canvasHeight">The height of the containing canvas.</param>
        /// <param name="cam">The camera to get FOV information from.</param>
        /// <param name="yaw">Head tracking yaw in degrees.</param>
        /// <param name="pitch">Head tracking pitch in degrees.</param>
        /// <param name="roll">Head tracking roll in degrees.</param>
        /// <exception cref="ArgumentNullException">Thrown when element or cam is null.</exception>
        public static void RepositionElement(
            RectTransform element,
            float canvasWidth,
            float canvasHeight,
            Camera cam,
            float yaw,
            float pitch,
            float roll)
        {
            if (element == null)
            {
                throw new ArgumentNullException(nameof(element));
            }

            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam));
            }

            float halfWidth = canvasWidth * 0.5f;
            float halfHeight = canvasHeight * 0.5f;

            // Calculate projection parameters
            float verticalFov = cam.fieldOfView;
            float horizontalFov = ScreenOffsetCalculator.CalculateHorizontalFov(verticalFov, cam.aspect);

            // Calculate yaw/pitch offset
            ScreenOffsetCalculator.Calculate(
                yaw, pitch, 0f,
                horizontalFov, verticalFov,
                canvasWidth, canvasHeight,
                1f,
                out float offsetX, out float offsetY);

            // Get current position relative to canvas center
            Vector2 pos = element.anchoredPosition;
            float relX = pos.x - halfWidth;
            float relY = pos.y - halfHeight;

            // Apply roll rotation
            ScreenOffsetCalculator.ApplyRollRotation(relX, relY, roll, out float rotatedRelX, out float rotatedRelY);

            // Apply yaw/pitch offset
            float newX = rotatedRelX + offsetX + halfWidth;
            float newY = rotatedRelY + offsetY + halfHeight;

            element.anchoredPosition = new Vector2(newX, newY);
        }

        /// <summary>
        /// Calculates the screen offset for a centered UI element (like a reticle) using FOV projection.
        /// This is a convenience wrapper around ScreenOffsetCalculator for centered elements.
        /// </summary>
        /// <param name="cam">The camera to get FOV information from.</param>
        /// <param name="yaw">Head tracking yaw in degrees.</param>
        /// <param name="pitch">Head tracking pitch in degrees.</param>
        /// <param name="roll">Head tracking roll in degrees.</param>
        /// <returns>The screen offset in pixels from center.</returns>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static Vector2 CalculateCenteredOffset(Camera cam, float yaw, float pitch, float roll)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam));
            }

            float verticalFov = cam.fieldOfView;
            float horizontalFov = ScreenOffsetCalculator.CalculateHorizontalFov(verticalFov, cam.aspect);

            // For centered elements, roll has no effect on position - the center stays at center
            // regardless of roll. Only yaw and pitch affect where the aim point projects.
            // Roll rotation is only needed for off-center elements (handled by RepositionChildren).
            ScreenOffsetCalculator.Calculate(
                yaw, pitch, 0f,
                horizontalFov, verticalFov,
                Screen.width, Screen.height,
                1f,
                out float offsetX, out float offsetY);

            return new Vector2(offsetX, offsetY);
        }

        /// <summary>
        /// Calculates the screen offset for a centered UI element, accounting for canvas scale factor.
        /// Use this when positioning elements on a scaled canvas.
        /// </summary>
        /// <param name="cam">The camera to get FOV information from.</param>
        /// <param name="canvasScaleFactor">The canvas scale factor.</param>
        /// <param name="yaw">Head tracking yaw in degrees.</param>
        /// <param name="pitch">Head tracking pitch in degrees.</param>
        /// <param name="roll">Head tracking roll in degrees.</param>
        /// <returns>The screen offset in canvas-scaled units from center.</returns>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static Vector2 CalculateCenteredOffsetScaled(
            Camera cam,
            float canvasScaleFactor,
            float yaw,
            float pitch,
            float roll)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam));
            }

            Vector2 offset = CalculateCenteredOffset(cam, yaw, pitch, roll);

            if (canvasScaleFactor > 0f && canvasScaleFactor != 1f)
            {
                offset.x /= canvasScaleFactor;
                offset.y /= canvasScaleFactor;
            }

            return offset;
        }

        /// <summary>
        /// Calculates the screen offset for a reticle using Unity's WorldToScreenPoint projection.
        /// This method correctly handles all rotation combinations by using Unity's actual
        /// view/projection matrices rather than manual trigonometric calculation.
        ///
        /// Use this when you have the aim direction (where the player is actually aiming,
        /// before head tracking rotation was applied).
        /// </summary>
        /// <param name="cam">The camera (with head tracking already applied).</param>
        /// <param name="aimDirection">The world-space direction the player is aiming.</param>
        /// <returns>Screen offset in pixels from center.</returns>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static Vector2 CalculateAimScreenOffset(Camera cam, Vector3 aimDirection)
        {
            return CalculateAimScreenOffsetCore(cam, aimDirection, 100f);
        }

        /// <summary>
        /// Calculates the screen offset for a reticle using Unity's WorldToScreenPoint projection,
        /// with canvas scale factor applied.
        /// </summary>
        /// <param name="cam">The camera (with head tracking already applied).</param>
        /// <param name="aimDirection">The world-space direction the player is aiming.</param>
        /// <param name="canvasScaleFactor">The canvas scale factor.</param>
        /// <returns>Screen offset in canvas-scaled units from center.</returns>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static Vector2 CalculateAimScreenOffset(Camera cam, Vector3 aimDirection, float canvasScaleFactor)
        {
            Vector2 offset = CalculateAimScreenOffsetCore(cam, aimDirection, 100f);

            if (canvasScaleFactor > 0f && canvasScaleFactor != 1f)
            {
                offset.x /= canvasScaleFactor;
                offset.y /= canvasScaleFactor;
            }

            return offset;
        }

        /// <summary>
        /// Calculates the screen offset for a reticle using Unity's WorldToScreenPoint projection,
        /// with a specified projection distance and canvas scale factor.
        /// Use a shorter projection distance when position tracking is active to correctly
        /// capture position parallax at gameplay distances.
        /// </summary>
        /// <param name="cam">The camera (with head tracking already applied).</param>
        /// <param name="aimDirection">The world-space direction the player is aiming.</param>
        /// <param name="projectionDistance">Distance along aim direction to project (affects position parallax).</param>
        /// <param name="canvasScaleFactor">The canvas scale factor (use 1f for raw pixel offset).</param>
        /// <returns>Screen offset in canvas-scaled units from center.</returns>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static Vector2 CalculateAimScreenOffset(Camera cam, Vector3 aimDirection, float projectionDistance, float canvasScaleFactor)
        {
            Vector2 offset = CalculateAimScreenOffsetCore(cam, aimDirection, projectionDistance);

            if (canvasScaleFactor > 0f && canvasScaleFactor != 1f)
            {
                offset.x /= canvasScaleFactor;
                offset.y /= canvasScaleFactor;
            }

            return offset;
        }

        /// <summary>
        /// Calculates the screen offset for a reticle given the base camera rotation
        /// (before head tracking was applied). Computes the aim direction from the base rotation
        /// and projects it through the head-tracked camera.
        /// </summary>
        /// <param name="cam">The camera (with head tracking already applied).</param>
        /// <param name="baseRotation">The camera's rotation before head tracking was applied.</param>
        /// <returns>Screen offset in pixels from center.</returns>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static Vector2 CalculateAimScreenOffset(Camera cam, Quaternion baseRotation)
        {
            Vector3 aimDirection = baseRotation * Vector3.forward;
            return CalculateAimScreenOffset(cam, aimDirection);
        }

        /// <summary>
        /// Calculates the screen offset for a reticle given the base camera rotation,
        /// with canvas scale factor applied.
        /// </summary>
        /// <param name="cam">The camera (with head tracking already applied).</param>
        /// <param name="baseRotation">The camera's rotation before head tracking was applied.</param>
        /// <param name="canvasScaleFactor">The canvas scale factor.</param>
        /// <returns>Screen offset in canvas-scaled units from center.</returns>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static Vector2 CalculateAimScreenOffset(Camera cam, Quaternion baseRotation, float canvasScaleFactor)
        {
            Vector3 aimDirection = baseRotation * Vector3.forward;
            return CalculateAimScreenOffset(cam, aimDirection, canvasScaleFactor);
        }

        /// <summary>
        /// Calculates the screen offset for a reticle by computing the aim direction from
        /// the head tracking quaternion. The aim direction is the inverse of the tracking
        /// rotation applied to the camera's forward vector.
        /// </summary>
        /// <param name="cam">The camera (with head tracking already applied).</param>
        /// <param name="trackingQuaternion">The head tracking rotation that was applied.</param>
        /// <returns>Screen offset in pixels from center.</returns>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static Vector2 CalculateAimScreenOffsetFromTracking(Camera cam, Quaternion trackingQuaternion)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam));
            }

            // Compute aim direction by undoing the tracking rotation
            Vector3 localAim = Quaternion.Inverse(trackingQuaternion) * Vector3.forward;
            Vector3 aimDirection = cam.transform.rotation * localAim;

            return CalculateAimScreenOffset(cam, aimDirection);
        }

        /// <summary>
        /// Calculates the screen offset for a reticle from head tracking quaternion,
        /// with canvas scale factor applied.
        /// </summary>
        /// <param name="cam">The camera (with head tracking already applied).</param>
        /// <param name="trackingQuaternion">The head tracking rotation that was applied.</param>
        /// <param name="canvasScaleFactor">The canvas scale factor.</param>
        /// <returns>Screen offset in canvas-scaled units from center.</returns>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static Vector2 CalculateAimScreenOffsetFromTracking(Camera cam, Quaternion trackingQuaternion, float canvasScaleFactor)
        {
            Vector2 offset = CalculateAimScreenOffsetFromTracking(cam, trackingQuaternion);

            if (canvasScaleFactor > 0f && canvasScaleFactor != 1f)
            {
                offset.x /= canvasScaleFactor;
                offset.y /= canvasScaleFactor;
            }

            return offset;
        }

        /// <summary>
        /// Calculates the screen offset for a known world-space point, projected through
        /// the camera's current view matrix. Use this for parallax-correct reticle positioning
        /// when position tracking is active — pass the actual aim hit point for exact results.
        /// </summary>
        /// <param name="cam">The camera (with head tracking already applied to the view matrix).</param>
        /// <param name="worldPoint">The world-space point to project (e.g., a raycast hit point).</param>
        /// <param name="canvasScaleFactor">The canvas scale factor (use 1f for raw pixel offset).</param>
        /// <returns>Screen offset in canvas-scaled units from center.</returns>
        /// <exception cref="ArgumentNullException">Thrown when cam is null.</exception>
        public static Vector2 CalculateScreenOffsetFromWorldPoint(Camera cam, Vector3 worldPoint, float canvasScaleFactor)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam));
            }

            Vector3 screenPoint = cam.WorldToScreenPoint(worldPoint);

            if (screenPoint.z <= 0)
            {
                // Point is behind the modified camera — fall back to direction-based edge clamp
                Vector3 dirToPoint = (worldPoint - cam.transform.position).normalized;
                float halfWidth = Screen.width * 0.5f;
                float halfHeight = Screen.height * 0.5f;
                Vector2 edgeOffset = ClampToScreenEdge(dirToPoint, cam, halfWidth, halfHeight);

                if (canvasScaleFactor > 0f && canvasScaleFactor != 1f)
                {
                    edgeOffset.x /= canvasScaleFactor;
                    edgeOffset.y /= canvasScaleFactor;
                }

                return edgeOffset;
            }

            Vector2 offset = new Vector2(
                screenPoint.x - Screen.width * 0.5f,
                screenPoint.y - Screen.height * 0.5f);

            if (canvasScaleFactor > 0f && canvasScaleFactor != 1f)
            {
                offset.x /= canvasScaleFactor;
                offset.y /= canvasScaleFactor;
            }

            return offset;
        }

        private static Vector2 CalculateAimScreenOffsetCore(Camera cam, Vector3 aimDirection, float projectionDistance)
        {
            if (cam == null)
            {
                throw new ArgumentNullException(nameof(cam));
            }

            // Project a point along the aim direction to screen coordinates
            Vector3 aimWorldPoint = cam.transform.position + aimDirection * projectionDistance;
            Vector3 screenPoint = cam.WorldToScreenPoint(aimWorldPoint);

            float halfWidth = Screen.width * 0.5f;
            float halfHeight = Screen.height * 0.5f;

            // screenPoint.z > 0 means the point is in front of the camera
            if (screenPoint.z > 0)
            {
                return new Vector2(screenPoint.x - halfWidth, screenPoint.y - halfHeight);
            }

            // Aim is behind camera - clamp to screen edge
            return ClampToScreenEdge(aimDirection, cam, halfWidth, halfHeight);
        }

        /// <summary>
        /// Clamps the reticle offset to the screen edge when aim direction is behind the camera.
        /// </summary>
        private static Vector2 ClampToScreenEdge(Vector3 aimDirection, Camera cam, float halfWidth, float halfHeight)
        {
            Vector3 localAim = cam.transform.InverseTransformDirection(aimDirection);

            // Invert and normalize to edge
            float x = -localAim.x;
            float y = -localAim.y;

            float maxComponent = Mathf.Max(Mathf.Abs(x), Mathf.Abs(y));
            if (maxComponent > 0.001f)
            {
                x /= maxComponent;
                y /= maxComponent;
            }

            // Scale to 90% of screen edge
            return new Vector2(x * halfWidth * 0.9f, y * halfHeight * 0.9f);
        }
    }
}
