namespace CameraUnlock.Core.Data
{
    /// <summary>
    /// Configuration for the neck model pivot simulation.
    /// The neck model computes eye position offset from head rotation,
    /// simulating that the head rotates around the neck rather than the eye center.
    /// </summary>
    public struct NeckModelSettings
    {
        /// <summary>Whether the neck model is enabled.</summary>
        public bool Enabled { get; }

        /// <summary>Distance from neck pivot to eyes, in meters (vertical component).</summary>
        public float NeckHeight { get; }

        /// <summary>Distance from neck pivot to eyes, in meters (forward component).</summary>
        public float NeckForward { get; }

        /// <summary>Vector from neck pivot to eyes.</summary>
        public Vec3 NeckToEyes => new Vec3(0f, NeckHeight, NeckForward);

        /// <summary>Default: enabled, height=0.10m, forward=0.08m.</summary>
        public static NeckModelSettings Default => new NeckModelSettings(true, 0.10f, 0.08f);

        /// <summary>Disabled neck model.</summary>
        public static NeckModelSettings Disabled => new NeckModelSettings(false, 0f, 0f);

        public NeckModelSettings(bool enabled, float neckHeight, float neckForward)
        {
            Enabled = enabled;
            NeckHeight = neckHeight;
            NeckForward = neckForward;
        }
    }
}
