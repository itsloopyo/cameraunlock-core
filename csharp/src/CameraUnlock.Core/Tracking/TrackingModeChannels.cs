namespace CameraUnlock.Core.Tracking
{
    /// <summary>
    /// How a <see cref="TrackingMode"/> is stored in a config: <c>[General] RotationEnabled</c>
    /// and <c>[Position] PositionEnabled</c>. The mapping is data/pipeline-conformance.json's
    /// <c>preference_modes.tracking_mode</c>, and the tests hold these methods to it.
    /// </summary>
    public static class TrackingModeChannels
    {
        /// <summary>
        /// The pair a mode is saved as. Selecting a mode writes both keys together.
        /// </summary>
        public static void Encode(TrackingMode mode, out bool rotationEnabled, out bool positionEnabled)
        {
            rotationEnabled = mode != TrackingMode.PositionOnly;
            positionEnabled = mode != TrackingMode.RotationOnly;
        }

        /// <summary>
        /// The mode a stored pair names, or null when no mode writes that pair.
        /// <para>
        /// false/false is one such pair. It is reported, not mapped onto a mode: what an
        /// unrepresentable config means is the caller's decision, and a caller that saved a
        /// repaired mode back would overwrite what the user wrote.
        /// </para>
        /// </summary>
        public static TrackingMode? Decode(bool rotationEnabled, bool positionEnabled)
        {
            if (rotationEnabled && positionEnabled) return TrackingMode.RotationAndPosition;
            if (rotationEnabled) return TrackingMode.RotationOnly;
            if (positionEnabled) return TrackingMode.PositionOnly;
            return null;
        }
    }
}
