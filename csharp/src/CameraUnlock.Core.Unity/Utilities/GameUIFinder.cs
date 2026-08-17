using System;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Utilities
{
    /// <summary>
    /// Utility for finding UI elements like reticles/crosshairs in games.
    /// Uses multiple search strategies with fallbacks.
    /// </summary>
    public static class GameUIFinder
    {
        /// <summary>
        /// Common names for reticle/crosshair UI elements.
        /// </summary>
        public static readonly string[] CommonReticleNames = new string[]
        {
            "Reticle",
            "reticle",
            "CrossHair",
            "Crosshair",
            "crosshair",
            "Cursor",
            "cursor",
            "AimIndicator",
            "CenterDot",
            "HUD_Reticle",
            "UIReticle",
            "InteractionCursor",
            "LookCursor",
            "Dot",
            "dot"
        };

        /// <summary>
        /// Keywords that typically indicate a reticle element.
        /// </summary>
        public static readonly string[] ReticleKeywords = new string[]
        {
            "reticle",
            "crosshair",
            "aimpoint",
            "centerdot",
            "cursor"
        };

        /// <summary>
        /// Finds a GameObject by exact name from a list of names.
        /// </summary>
        /// <param name="names">Names to search for.</param>
        /// <returns>The first matching GameObject, or null if not found.</returns>
        public static GameObject FindByNames(params string[] names)
        {
            if (names == null) return null;

            foreach (string name in names)
            {
                if (string.IsNullOrEmpty(name)) continue;
                GameObject found = GameObject.Find(name);
                if (found != null)
                {
                    return found;
                }
            }
            return null;
        }

        /// <summary>
        /// Finds a GameObject whose name contains any of the specified keywords.
        /// </summary>
        /// <param name="keywords">Keywords to search for (case-insensitive).</param>
        /// <returns>The first matching GameObject, or null if not found.</returns>
        public static GameObject FindByKeywords(params string[] keywords)
        {
            if (keywords == null || keywords.Length == 0) return null;

            // Lowered once. Doing it inside the per-object loop allocated a string per
            // object per keyword on top of a whole-scene scan, and mods poll this every
            // frame until the HUD exists - tens of thousands of allocations per frame in
            // any scene where the reticle is absent.
            string[] loweredKeywords = LowerKeywords(keywords);

            #pragma warning disable CS0618 // FindObjectsByType unavailable in older Unity versions
            GameObject[] allObjects = UnityEngine.Object.FindObjectsOfType<GameObject>();
            #pragma warning restore CS0618
            foreach (GameObject obj in allObjects)
            {
                if (obj == null) continue;
                string objName = obj.name.ToLowerInvariant();

                foreach (string keyword in loweredKeywords)
                {
                    if (keyword == null) continue;
                    if (objName.Contains(keyword))
                    {
                        return obj;
                    }
                }
            }
            return null;
        }

        /// <summary>
        /// Searches all Canvas children for UI elements matching the keywords.
        /// </summary>
        /// <param name="keywords">Keywords to search for (case-insensitive).</param>
        /// <returns>The first matching GameObject, or null if not found.</returns>
        public static GameObject FindInCanvas(params string[] keywords)
        {
            if (keywords == null || keywords.Length == 0) return null;

            string[] loweredKeywords = LowerKeywords(keywords);

            #pragma warning disable CS0618 // FindObjectsByType unavailable in older Unity versions
            Canvas[] canvases = UnityEngine.Object.FindObjectsOfType<Canvas>();
            #pragma warning restore CS0618
            foreach (Canvas canvas in canvases)
            {
                if (canvas == null) continue;

                // Walked rather than GetComponentsInChildren<Transform>(true), which allocates
                // a full array of every Transform under the canvas on every call.
                GameObject match = FindInChildren(canvas.transform, loweredKeywords);
                if (match != null) return match;
            }
            return null;
        }

        private static GameObject FindInChildren(Transform parent, string[] loweredKeywords)
        {
            string name = parent.name;
            foreach (string keyword in loweredKeywords)
            {
                if (keyword == null) continue;
                if (name.IndexOf(keyword, StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    return parent.gameObject;
                }
            }

            int childCount = parent.childCount;
            for (int i = 0; i < childCount; i++)
            {
                GameObject match = FindInChildren(parent.GetChild(i), loweredKeywords);
                if (match != null) return match;
            }

            return null;
        }

        /// <summary>
        /// Attempts to find a reticle/crosshair using multiple strategies.
        /// </summary>
        /// <param name="customNames">Optional custom names to search first.</param>
        /// <returns>The found reticle GameObject, or null if not found.</returns>
        public static GameObject FindReticle(string[] customNames = null)
        {
            // Try custom names first
            if (customNames != null && customNames.Length > 0)
            {
                GameObject found = FindByNames(customNames);
                if (found != null) return found;
            }

            // Try common reticle names
            GameObject result = FindByNames(CommonReticleNames);
            if (result != null) return result;

            // Try keyword search in all GameObjects
            result = FindByKeywords(ReticleKeywords);
            if (result != null) return result;

            // Try canvas search
            result = FindInCanvas(ReticleKeywords);
            return result;
        }

        /// <summary>
        /// Gets the RectTransform of a UI element, if it has one.
        /// Uses reflection to avoid hard dependency on RectTransform type issues.
        /// </summary>
        /// <param name="gameObject">The GameObject to check.</param>
        /// <returns>The RectTransform component, or null if not found.</returns>
        public static RectTransform GetRectTransform(GameObject gameObject)
        {
            if (gameObject == null) return null;
            return gameObject.GetComponent<RectTransform>();
        }

        /// <summary>
        /// Finds a UI element by navigating a hierarchy path.
        /// </summary>
        /// <param name="rootName">Name of the root object to find first.</param>
        /// <param name="path">Child path separated by '/' (e.g., "Canvas/HUD/Reticle").</param>
        /// <returns>The found GameObject, or null if not found.</returns>
        public static GameObject FindByPath(string rootName, string path)
        {
            if (string.IsNullOrEmpty(rootName) && string.IsNullOrEmpty(path))
                return null;

            GameObject root = null;

            if (!string.IsNullOrEmpty(rootName))
            {
                root = GameObject.Find(rootName);
                if (root == null) return null;
            }

            if (string.IsNullOrEmpty(path))
                return root;

            if (root == null)
            {
                // Path only - try to find directly
                return GameObject.Find(path);
            }

            // Navigate the path
            Transform current = root.transform;
            string[] parts = path.Split('/');

            foreach (string part in parts)
            {
                if (string.IsNullOrEmpty(part)) continue;
                Transform child = current.Find(part);
                if (child == null) return null;
                current = child;
            }

            return current.gameObject;
        }

        // Null entries mark keywords the caller left empty, so the scan loops can skip
        // them with a reference test instead of re-checking IsNullOrEmpty per object.
        private static string[] LowerKeywords(string[] keywords)
        {
            var lowered = new string[keywords.Length];
            for (int i = 0; i < keywords.Length; i++)
            {
                lowered[i] = string.IsNullOrEmpty(keywords[i]) ? null : keywords[i].ToLowerInvariant();
            }
            return lowered;
        }
    }
}
