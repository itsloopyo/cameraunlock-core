using System;
using System.IO;
using System.Collections.Generic;
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
        private static readonly Dictionary<string, Type> _typeCache =
            new Dictionary<string, Type>(StringComparer.Ordinal);

        /// <summary>
        /// Searches all loaded Image components for ones likely to be crosshairs.
        /// Returns candidates matching common crosshair naming patterns.
        /// </summary>
        /// <param name="searchInactive">Include inactive GameObjects in search.</param>
        /// <returns>List of potential crosshair Image components.</returns>
        public static List<Image> FindCrosshairCandidates(bool searchInactive = true)
        {
            return FindCandidates<Image>(searchInactive);
        }

        /// <summary>
        /// Searches all loaded RawImage components for crosshair candidates.
        /// </summary>
        public static List<RawImage> FindRawImageCrosshairCandidates(bool searchInactive = true)
        {
            return FindCandidates<RawImage>(searchInactive);
        }

        private static List<T> FindCandidates<T>(bool searchInactive) where T : Component
        {
            var candidates = new List<T>();
            var components = searchInactive
                ? Resources.FindObjectsOfTypeAll<T>()
                #pragma warning disable CS0618 // FindObjectsByType unavailable in older Unity versions
                : UnityEngine.Object.FindObjectsOfType<T>();
                #pragma warning restore CS0618

            foreach (var component in components)
            {
                if (component == null) continue;

                // Component.name forwards to gameObject.name, so one check covers both.
                if (IsCrosshairName(component.name))
                {
                    candidates.Add(component);
                }
            }

            return candidates;
        }

        private static bool IsCrosshairName(string name)
        {
            return ContainsIgnoreCase(name, "crosshair")
                || ContainsIgnoreCase(name, "reticle")
                || ContainsIgnoreCase(name, "reticule")
                || ContainsIgnoreCase(name, "aim");
        }

        private static bool ContainsIgnoreCase(string haystack, string needle)
        {
            return haystack.IndexOf(needle, StringComparison.OrdinalIgnoreCase) >= 0;
        }

        /// <summary>
        /// Finds a type by name across all loaded assemblies.
        /// Useful for finding game HUD classes like "HUDCrosshair", "NGUI_HUD", etc.
        /// </summary>
        /// <param name="typeName">Simple type name (e.g., "HUDCrosshair") or namespace-qualified name.</param>
        /// <returns>The Type if found, null otherwise.</returns>
        public static Type FindTypeByName(string typeName)
        {
            if (string.IsNullOrEmpty(typeName)) return null;

            // Cached. The miss path below materialises every type in the process, and the
            // documented usage is polling for a game HUD type that may not exist yet, so
            // an uncached miss is a full reflection sweep every frame. Only successful
            // resolutions are cached: a negative would latch a failure across the assembly
            // load that would have satisfied it.
            Type cached;
            if (_typeCache.TryGetValue(typeName, out cached)) return cached;

            var assemblies = AppDomain.CurrentDomain.GetAssemblies();

            foreach (var assembly in assemblies)
            {
                // Guarded: a single broken assembly in a modded process must not abort the
                // search for everyone. GetType can throw FileLoadException and
                // BadImageFormatException as well as the load exceptions below - the
                // FileNotFoundException handler that used to be here is evidence this is
                // hit in the wild.
                Type type;
                try
                {
                    type = assembly.GetType(typeName);
                }
                catch (ReflectionTypeLoadException) { continue; }
                catch (FileNotFoundException) { continue; }
                catch (FileLoadException) { continue; }
                catch (BadImageFormatException) { continue; }

                if (type != null)
                {
                    _typeCache[typeName] = type;
                    return type;
                }
            }

            // Assembly.GetType only matches the namespace-qualified name, so the documented
            // simple-name lookup missed every game type that lives in a namespace - and the
            // caller's crosshair-hiding path then silently never engaged.
            foreach (var assembly in assemblies)
            {
                Type[] types;
                try
                {
                    types = assembly.GetTypes();
                }
                catch (ReflectionTypeLoadException ex)
                {
                    // An assembly with unresolvable references still yields every type that
                    // did load, and ex.Types is that partial result. Skipping the assembly
                    // outright is what hid the failure.
                    types = ex.Types;
                }

                for (int i = 0; i < types.Length; i++)
                {
                    if (types[i] != null && types[i].Name == typeName) return types[i];
                }
            }

            return null;
        }

        /// <summary>
        /// Offsets a UI RectTransform by screen pixels, accounting for canvas scale.
        /// </summary>
        /// <param name="rectTransform">The RectTransform to offset.</param>
        /// <param name="screenOffset">Offset in screen pixels.</param>
        [Obsolete("Assigns anchoredPosition instead of offsetting it, discarding the element's authored position - a crosshair authored at (0,-40) jumps 40px on the first call with a zero head offset and can never be restored. Use the overload taking the original anchoredPosition.")]
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
        /// Offsets a UI RectTransform from its authored anchoredPosition by screen pixels,
        /// accounting for canvas scale. Always writes original + offset, so repeated per-frame
        /// calls neither accumulate nor lose the authored position.
        /// </summary>
        /// <param name="rectTransform">The RectTransform to offset.</param>
        /// <param name="originalAnchoredPosition">The element's authored anchoredPosition, captured
        /// once by the caller before any offset was applied.</param>
        /// <param name="screenOffset">Offset in screen pixels.</param>
        public static void OffsetByScreenPixels(
            RectTransform rectTransform,
            Vector2 originalAnchoredPosition,
            Vector2 screenOffset)
        {
            if (rectTransform == null) return;

            var lossyScale = rectTransform.lossyScale;
            var scaleX = lossyScale.x > 0.001f ? lossyScale.x : 1f;
            var scaleY = lossyScale.y > 0.001f ? lossyScale.y : 1f;

            rectTransform.anchoredPosition = new Vector2(
                originalAnchoredPosition.x + screenOffset.x / scaleX,
                originalAnchoredPosition.y + screenOffset.y / scaleY
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
