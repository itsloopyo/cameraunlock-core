#nullable enable
using System;
using System.Collections.Generic;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Il2Cpp
{
    /// <summary>
    /// Multi-camera head tracking injection for camera-relative rendering pipelines
    /// (HDRP/URP) in IL2CPP games, driven from LateUpdate because IL2CPP ClassInjector
    /// types receive Update/LateUpdate but not OnPreCull/SRP callbacks.
    ///
    /// Injection is split across two channels:
    ///   - ROTATION goes into a manually-set <c>camera.worldToCameraMatrix</c>. The
    ///     renderer reads the matrix's rotation; the transform rotation stays game-owned
    ///     so mouse look and aim are unaffected.
    ///   - POSITION goes into <c>camera.transform.position</c>, because camera-relative
    ///     rendering discards the matrix's translation column. The write is undone in
    ///     LOCAL space by <see cref="RestorePositions"/> at the start of the next frame
    ///     (the camera's parent rig moves every frame so world-space comparisons never
    ///     match, but the game leaves local position alone).
    ///
    /// Roll is passed through un-negated in both yaw modes; per-game sign conventions
    /// are the caller's responsibility (config inversion).
    ///
    /// Per-frame call order: <see cref="RestorePositions"/> -> [caller gating] ->
    /// <see cref="RefreshTargetsIfDue"/> -> <see cref="Apply"/>. Call
    /// <see cref="ResetAll"/> when tracking turns off.
    /// </summary>
    public sealed class SplitInjectionCameraTracker
    {
        private sealed class TrackedCamera
        {
            public TrackedCamera(Camera camera) { Camera = camera; }

            public readonly Camera Camera;
            public Vector3 AppliedLocalDelta;
            public bool HasWrite;
            public bool HasMatrixWrite;
        }

        private static readonly string[] ExcludedTokens = { "ui", "inventory", "overlay" };

        private readonly List<TrackedCamera> _targets = new List<TrackedCamera>();

        private int _lastRefreshFrame = -1;
        private bool _loggedEmptyScan;

        /// <summary>Frames between target-set rebuilds.</summary>
        public int RefreshInterval { get; set; } = 60;

        /// <summary>
        /// Minimum frames between rebuild attempts while nothing matches. Without it, every
        /// loading screen, menu and name-filtered-out scene ran the full Camera.allCameras scan
        /// plus a fresh list and a per-camera string every single frame.
        /// </summary>
        public int EmptyRetryInterval { get; set; } = 10;

        /// <summary>Diagnostic logging callback (target-set changes).</summary>
        public Action<string>? Log { get; set; }

        /// <summary>Number of cameras currently tracked.</summary>
        public int TargetCount => _targets.Count;

        /// <summary>
        /// Rebuilds the tracked camera set from Camera.allCameras when due: every enabled,
        /// screen-targeting world camera that is not obviously a UI/inventory camera.
        /// Cameras that leave the set get their tracking state fully reset.
        /// </summary>
        public void RefreshTargetsIfDue()
        {
            bool due = _targets.Count == 0
                ? _lastRefreshFrame < 0 || Time.frameCount - _lastRefreshFrame >= EmptyRetryInterval
                : Time.frameCount % RefreshInterval == 0;
            if (!due) return;

            _lastRefreshFrame = Time.frameCount;

            var all = Camera.allCameras;
            var fresh = new List<Camera>();
            List<string>? excluded = Log != null ? new List<string>() : null;
            for (int i = 0; i < all.Length; i++)
            {
                Camera cam = all[i];
                if (cam == null) continue;
                if (cam.cameraType != CameraType.Game) continue;
                if (cam.targetTexture != null) continue;

                string? matchedToken = MatchExcludedToken(cam.name);
                if (matchedToken != null)
                {
                    excluded?.Add($"  excluded '{cam.name}' (matched token '{matchedToken}')");
                    continue;
                }

                fresh.Add(cam);
            }

            bool changed = fresh.Count != _targets.Count;
            if (!changed)
            {
                for (int i = 0; i < fresh.Count; i++)
                {
                    if (fresh[i] != _targets[i].Camera) { changed = true; break; }
                }
            }
            // Also logged when the scan finds nothing at all, which the changed check alone
            // would swallow - the exact case where a wrongly excluded main camera has to be
            // visible in the log.
            if (excluded != null && excluded.Count > 0 && (changed || (fresh.Count == 0 && !_loggedEmptyScan)))
            {
                Log?.Invoke($"=== Excluded cameras ({excluded.Count}) ===");
                for (int i = 0; i < excluded.Count; i++)
                {
                    Log?.Invoke(excluded[i]);
                }
            }
            _loggedEmptyScan = fresh.Count == 0;

            if (!changed) return;

            ResetAll();
            _targets.Clear();

            Log?.Invoke($"=== Tracking targets ({fresh.Count}) [Camera.allCameras={all.Length}] ===");
            Camera? main = Camera.main;
            for (int i = 0; i < fresh.Count; i++)
            {
                Camera cam = fresh[i];
                string flags = main != null && cam == main ? " [Camera.main]" : "";
                Log?.Invoke($"  '{cam.name}' depth={cam.depth} fov={cam.fieldOfView:F1} path={HierarchyPath(cam.transform)}{flags}");
                _targets.Add(new TrackedCamera(cam));
            }
        }

        /// <summary>
        /// Returns the exclusion token the camera name matches as a WORD, or null.
        /// A plain substring test on "ui" excluded BuildCam, GuideCamera and FluidCam - the
        /// game's own main camera, silently and with no diagnostic.
        /// </summary>
        private static string? MatchExcludedToken(string name)
        {
            for (int i = 0; i < ExcludedTokens.Length; i++)
            {
                if (ContainsToken(name, ExcludedTokens[i])) return ExcludedTokens[i];
            }
            return null;
        }

        private static bool ContainsToken(string name, string token)
        {
            int index = 0;
            while (index <= name.Length - token.Length)
            {
                index = name.IndexOf(token, index, StringComparison.OrdinalIgnoreCase);
                if (index < 0) return false;

                int end = index + token.Length;
                if (IsTokenStart(name, index) && IsTokenEnd(name, end)) return true;
                index = end;
            }
            return false;
        }

        // A token starts at the string start, after a separator, or at a camel-case hump
        // ("hudUI"). An uppercase run does NOT start one, so "GUICamera" never matches "ui".
        private static bool IsTokenStart(string name, int index)
        {
            if (index == 0) return true;
            char previous = name[index - 1];
            return !char.IsLetterOrDigit(previous) || char.IsLower(previous);
        }

        // Words a token may run straight into in an all-lowercase name. "uicam" has no
        // separator and no camel hump, so the boundary rules below cannot see the join -
        // yet it is obviously a UI camera and must stay excluded. Kept deliberately short
        // and camera-specific so it cannot swallow an unrelated word: "guidance" is still
        // not a "gui" match, because "dance" is not in here.
        private static readonly string[] TokenTailWords =
        {
            "cam", "camera", "canvas", "overlay", "layer", "view", "root"
        };

        // A token ends at the string end, before a separator, before the next camel-case
        // hump ("UICamera"), or immediately before one of the tail words above.
        private static bool IsTokenEnd(string name, int end)
        {
            if (end >= name.Length) return true;
            char next = name[end];
            if (!char.IsLetterOrDigit(next) || char.IsUpper(next) || char.IsDigit(next))
            {
                return true;
            }

            for (int i = 0; i < TokenTailWords.Length; i++)
            {
                string tail = TokenTailWords[i];
                if (name.Length - end >= tail.Length &&
                    string.Compare(name, end, tail, 0, tail.Length,
                                   StringComparison.OrdinalIgnoreCase) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        /// <summary>
        /// Applies the head pose to every tracked camera. The caller must have called
        /// <see cref="RestorePositions"/> earlier this frame so the cameras are clean.
        /// </summary>
        /// <param name="positionOffset">Head position offset in meters, applied in the
        /// camera's clean orientation basis (before head rotation) so leaning follows
        /// the body, not the head-rotated view.</param>
        /// <param name="worldSpaceYaw">True = yaw around world up (horizon-locked,
        /// prevents leaning artifacts at extreme angles); false = full YPR in the
        /// camera's own space.</param>
        public void Apply(float yaw, float pitch, float roll, Vector3 positionOffset,
            bool rotationActive, bool positionActive, bool worldSpaceYaw)
        {
            for (int i = 0; i < _targets.Count; i++)
            {
                var t = _targets[i];
                Camera cam = t.Camera;
                if (cam == null) continue;

                // The game's fresh pose. Rotation is never written by us (always includes
                // this frame's mouse look); position was returned to clean by RestorePositions.
                Quaternion baseWorld = cam.transform.rotation;
                Vector3 basePosition = cam.transform.position;

                Vector3 finalPosition = basePosition;
                if (positionActive)
                {
                    Vector3 cleanLocalPosition = cam.transform.localPosition;
                    finalPosition = basePosition + baseWorld * positionOffset;
                    cam.transform.position = finalPosition;
                    t.AppliedLocalDelta = cam.transform.localPosition - cleanLocalPosition;
                    t.HasWrite = true;
                }

                if (rotationActive)
                {
                    Quaternion finalWorld = ComposeRotation(baseWorld, yaw, pitch, roll, worldSpaceYaw);
                    cam.worldToCameraMatrix = BuildViewMatrix(finalWorld, finalPosition);
                    t.HasMatrixWrite = true;
                }
                else if (t.HasMatrixWrite)
                {
                    // worldToCameraMatrix is a sticky override: without this the view freezes
                    // at the last head rotation when rotation tracking is turned off, and the
                    // position offset still being written above becomes invisible because the
                    // manual matrix overrides the transform.
                    cam.ResetWorldToCameraMatrix();
                    t.HasMatrixWrite = false;
                }
            }
        }

        /// <summary>
        /// Undoes this tracker's position offset by subtracting exactly the local-space delta
        /// it applied last frame. Operating on our own delta (rather than restoring an absolute
        /// clean value behind a "did the game touch it" guard) preserves any movement the game
        /// made to the camera in between - e.g. weapon aim/ADS, which drives the camera transform
        /// every frame - and can never accumulate our offset into a runaway.
        /// </summary>
        public void RestorePositions()
        {
            for (int i = 0; i < _targets.Count; i++)
            {
                var t = _targets[i];
                if (!t.HasWrite || t.Camera == null) continue;

                t.Camera.transform.localPosition -= t.AppliedLocalDelta;
                t.HasWrite = false;
            }
        }

        /// <summary>
        /// Returns every tracked camera's view matrix to auto mode (derived from its
        /// transform), removing all visible head rotation.
        /// </summary>
        public void ResetMatrices()
        {
            for (int i = 0; i < _targets.Count; i++)
            {
                var t = _targets[i];
                t.HasMatrixWrite = false;
                if (t.Camera == null) continue;
                t.Camera.ResetWorldToCameraMatrix();
            }
        }

        /// <summary>
        /// Removes all visible head tracking immediately: position offsets undone and view
        /// matrices returned to auto mode.
        /// </summary>
        public void ResetAll()
        {
            RestorePositions();
            ResetMatrices();
        }

        /// <summary>
        /// Drops all tracked cameras after resetting them.
        /// </summary>
        public void Clear()
        {
            ResetAll();
            _targets.Clear();
        }

        private static Quaternion ComposeRotation(Quaternion baseRotation, float yaw, float pitch, float roll, bool worldSpaceYaw)
        {
            if (worldSpaceYaw)
            {
                Quaternion worldYaw = Quaternion.AngleAxis(yaw, Vector3.up);
                Quaternion localPitchRoll = Quaternion.Euler(pitch, 0f, roll);
                return worldYaw * baseRotation * localPitchRoll;
            }

            return baseRotation * Quaternion.Euler(pitch, yaw, roll);
        }

        /// <summary>
        /// Builds a Unity view matrix (right-handed, Z-flipped) from a world rotation and
        /// camera position, matching Unity's worldToCameraMatrix convention.
        /// </summary>
        private static Matrix4x4 BuildViewMatrix(Quaternion rotation, Vector3 position)
        {
            Matrix4x4 m = Matrix4x4.Rotate(Quaternion.Inverse(rotation)) * Matrix4x4.Translate(-position);
            m.m20 = -m.m20;
            m.m21 = -m.m21;
            m.m22 = -m.m22;
            m.m23 = -m.m23;
            return m;
        }

        private static string HierarchyPath(Transform t)
        {
            var path = new System.Text.StringBuilder(t.name);
            var parent = t.parent;
            int depth = 0;
            while (parent != null && depth < 10)
            {
                path.Insert(0, parent.name + "/");
                parent = parent.parent;
                depth++;
            }
            return path.ToString();
        }
    }
}
