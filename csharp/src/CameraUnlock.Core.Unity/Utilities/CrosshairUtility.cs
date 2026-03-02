using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using UnityEngine;
using UnityEngine.UI;

namespace CameraUnlock.Core.Unity.Utilities
{
    /// <summary>
    /// Utility methods for finding and manipulating game crosshairs.
    /// These are helpers - game-specific code will need to identify the exact crosshair elements.
    /// </summary>
    public static class CrosshairUtility
    {
        /// <summary>
        /// Searches all loaded Image components for ones likely to be crosshairs.
        /// Returns candidates matching common crosshair naming patterns.
        /// </summary>
        /// <param name="searchInactive">Include inactive GameObjects in search.</param>
        /// <returns>List of potential crosshair Image components.</returns>
        public static List<Image> FindCrosshairCandidates(bool searchInactive = true)
        {
            var candidates = new List<Image>();
            var images = searchInactive
                ? Resources.FindObjectsOfTypeAll<Image>()
                #pragma warning disable CS0618 // FindObjectsByType unavailable in older Unity versions
                : UnityEngine.Object.FindObjectsOfType<Image>();
                #pragma warning restore CS0618

            foreach (var image in images)
            {
                if (image == null) continue;

                string name = image.name.ToLowerInvariant();
                string goName = image.gameObject.name.ToLowerInvariant();

                if (name.Contains("crosshair") || name.Contains("reticle") ||
                    name.Contains("reticule") || name.Contains("aim") ||
                    goName.Contains("crosshair") || goName.Contains("reticle") ||
                    goName.Contains("reticule"))
                {
                    candidates.Add(image);
                }
            }

            return candidates;
        }

        /// <summary>
        /// Searches all loaded RawImage components for crosshair candidates.
        /// </summary>
        public static List<RawImage> FindRawImageCrosshairCandidates(bool searchInactive = true)
        {
            var candidates = new List<RawImage>();
            var images = searchInactive
                ? Resources.FindObjectsOfTypeAll<RawImage>()
                #pragma warning disable CS0618 // FindObjectsByType unavailable in older Unity versions
                : UnityEngine.Object.FindObjectsOfType<RawImage>();
                #pragma warning restore CS0618

            foreach (var image in images)
            {
                if (image == null) continue;

                string name = image.name.ToLowerInvariant();
                string goName = image.gameObject.name.ToLowerInvariant();

                if (name.Contains("crosshair") || name.Contains("reticle") ||
                    name.Contains("reticule") || name.Contains("aim") ||
                    goName.Contains("crosshair") || goName.Contains("reticle") ||
                    goName.Contains("reticule"))
                {
                    candidates.Add(image);
                }
            }

            return candidates;
        }

        /// <summary>
        /// Finds a type by name across all loaded assemblies.
        /// Useful for finding game HUD classes like "HUDCrosshair", "NGUI_HUD", etc.
        /// </summary>
        /// <param name="typeName">Simple type name (e.g., "HUDCrosshair").</param>
        /// <returns>The Type if found, null otherwise.</returns>
        public static Type FindTypeByName(string typeName)
        {
            foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                try
                {
                    var type = assembly.GetType(typeName);
                    if (type != null) return type;
                }
                catch (ReflectionTypeLoadException) { }
                catch (FileNotFoundException) { }
            }
            return null;
        }

        /// <summary>
        /// Offsets a UI RectTransform by screen pixels, accounting for canvas scale.
        /// </summary>
        /// <param name="rectTransform">The RectTransform to offset.</param>
        /// <param name="screenOffset">Offset in screen pixels.</param>
        public static void OffsetByScreenPixels(RectTransform rectTransform, Vector2 screenOffset)
        {
            if (rectTransform == null) return;

            // Get the hierarchy scale which includes canvas scale
            var lossyScale = rectTransform.lossyScale;
            var scaleX = lossyScale.x > 0.001f ? lossyScale.x : 1f;
            var scaleY = lossyScale.y > 0.001f ? lossyScale.y : 1f;

            // Convert screen pixels to local units
            rectTransform.anchoredPosition = new Vector2(
                screenOffset.x / scaleX,
                screenOffset.y / scaleY
            );
        }

        /// <summary>
        /// Hides a Graphic component (Image, RawImage, etc.) by disabling its GameObject.
        /// Returns the previous active state for restoration.
        /// </summary>
        /// <param name="graphic">The graphic to hide.</param>
        /// <returns>Previous active state.</returns>
        public static bool HideGraphic(Graphic graphic)
        {
            if (graphic == null || graphic.gameObject == null) return false;

            bool wasActive = graphic.gameObject.activeSelf;
            graphic.gameObject.SetActive(false);
            return wasActive;
        }

        /// <summary>
        /// Shows a Graphic component by enabling its GameObject.
        /// </summary>
        /// <param name="graphic">The graphic to show.</param>
        /// <param name="active">Active state to set (default true).</param>
        public static void ShowGraphic(Graphic graphic, bool active = true)
        {
            if (graphic == null || graphic.gameObject == null) return;
            graphic.gameObject.SetActive(active);
        }

        /// <summary>
        /// Gets the Canvas scale factor for a UI element.
        /// </summary>
        /// <param name="graphic">Any UI graphic in the canvas hierarchy.</param>
        /// <returns>Canvas scale factor, or 1.0 if not found.</returns>
        public static float GetCanvasScaleFactor(Graphic graphic)
        {
            if (graphic == null) return 1f;

            var canvas = graphic.GetComponentInParent<Canvas>();
            return canvas != null ? canvas.scaleFactor : 1f;
        }
    }
}
