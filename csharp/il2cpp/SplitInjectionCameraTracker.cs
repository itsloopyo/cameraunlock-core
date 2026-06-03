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
        }

        private readonly List<TrackedCamera> _targets = new List<TrackedCamera>();

        /// <summary>Frames between target-set rebuilds.</summary>
        public int RefreshInterval { get; set; } = 60;

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
            bool due = _targets.Count == 0 || Time.frameCount % RefreshInterval == 0;
            if (!due) return;

            var all = Camera.allCameras;
            var fresh = new List<Camera>();
            for (int i = 0; i < all.Length; i++)
            {
                Camera cam = all[i];
                if (cam == null) continue;
                if (cam.cameraType != CameraType.Game) continue;
                if (cam.targetTexture != null) continue;

                string lower = cam.name.ToLowerInvariant();
                if (lower.Contains("ui") || lower.Contains("inventory") || lower.Contains("overlay")) continue;

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
                if (_targets[i].Camera == null) continue;
                _targets[i].Camera.ResetWorldToCameraMatrix();
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
