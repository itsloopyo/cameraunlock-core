using System;
using System.Runtime.CompilerServices;

namespace CameraUnlock.Core.Data
{
    /// <summary>
    /// Transforms rotation data between coordinate systems (e.g., OpenTrack to Unity).
    /// Supports axis remapping and inversion for flexible coordinate transformation.
    ///
    /// OpenTrack coordinate system (right-handed, NED-like):
    ///   - Yaw: Rotation around vertical axis (positive = turn right when viewed from above)
    ///   - Pitch: Rotation around lateral axis (positive = look down)
    ///   - Roll: Rotation around longitudinal axis (positive = tilt right ear toward shoulder)
    ///
    /// Unity coordinate system (left-handed):
    ///   - Y rotation: Yaw (positive = turn right)
    ///   - X rotation: Pitch (positive = look down in local space, but typically inverted)
    ///   - Z rotation: Roll (negative = tilt right in Unity's default setup)
    /// </summary>
    public sealed class CoordinateTransformer
    {
        /// <summary>
        /// Axis mapping for yaw output.
        /// </summary>
        public AxisMapping YawMapping { get; set; }

        /// <summary>
        /// Axis mapping for pitch output.
        /// </summary>
        public AxisMapping PitchMapping { get; set; }

        /// <summary>
        /// Axis mapping for roll output.
        /// </summary>
        public AxisMapping RollMapping { get; set; }

        /// <summary>
        /// Creates a transformer with default mappings (identity, no inversion).
        /// </summary>
        public CoordinateTransformer()
        {
            YawMapping = new AxisMapping(SourceAxis.Yaw, false);
            PitchMapping = new AxisMapping(SourceAxis.Pitch, false);
            RollMapping = new AxisMapping(SourceAxis.Roll, false);
        }

        /// <summary>
        /// Creates a transformer with specified mappings.
        /// </summary>
        public CoordinateTransformer(AxisMapping yaw, AxisMapping pitch, AxisMapping roll)
        {
            YawMapping = yaw;
            PitchMapping = pitch;
            RollMapping = roll;
        }

        /// <summary>
        /// Transform a tracking pose using the configured axis mappings.
        /// </summary>
        /// <param name="pose">Raw tracking pose to transform.</param>
        /// <returns>Transformed tracking pose.</returns>
        public TrackingPose Transform(TrackingPose pose)
        {
            return new TrackingPose(
                ApplyMapping(pose, YawMapping),
                ApplyMapping(pose, PitchMapping),
                ApplyMapping(pose, RollMapping),
                pose.TimestampTicks
            );
        }

        /// <summary>
        /// Transform individual rotation values.
        /// </summary>
        /// <param name="yaw">Raw yaw value.</param>
        /// <param name="pitch">Raw pitch value.</param>
        /// <param name="roll">Raw roll value.</param>
        /// <param name="outYaw">Transformed yaw output.</param>
        /// <param name="outPitch">Transformed pitch output.</param>
        /// <param name="outRoll">Transformed roll output.</param>
        public void Transform(float yaw, float pitch, float roll, out float outYaw, out float outPitch, out float outRoll)
        {
            outYaw = ApplyMapping(yaw, pitch, roll, YawMapping);
            outPitch = ApplyMapping(yaw, pitch, roll, PitchMapping);
            outRoll = ApplyMapping(yaw, pitch, roll, RollMapping);
        }

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        private static float ApplyMapping(TrackingPose pose, AxisMapping mapping)
        {
            return ApplyMapping(pose.Yaw, pose.Pitch, pose.Roll, mapping);
        }

#if !NET35 && !NET40
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
#endif
        private static float ApplyMapping(float yaw, float pitch, float roll, AxisMapping mapping)
        {
            float value;
            switch (mapping.Source)
            {
                case SourceAxis.Yaw:
                    value = yaw;
                    break;
                case SourceAxis.Pitch:
                    value = pitch;
                    break;
                case SourceAxis.Roll:
                    value = roll;
                    break;
                default:
                    throw new InvalidOperationException("Unknown source axis: " + mapping.Source);
            }

            if (mapping.Invert)
            {
                value = -value;
            }

            return value;
        }

        /// <summary>
        /// Creates a transformer with default OpenTrack to Unity mapping.
        /// Inverts pitch by default (OpenTrack pitch down = Unity pitch up).
        /// </summary>
        public static CoordinateTransformer CreateOpenTrackToUnity()
        {
            return new CoordinateTransformer(
                new AxisMapping(SourceAxis.Yaw, false),
                new AxisMapping(SourceAxis.Pitch, true),  // Invert pitch for Unity
                new AxisMapping(SourceAxis.Roll, false)
            );
        }

        /// <summary>
        /// Creates a transformer with no transformations (passthrough).
        /// </summary>
        public static CoordinateTransformer CreatePassthrough()
        {
            return new CoordinateTransformer();
        }

        /// <summary>
        /// Creates a transformer from inversion flags.
        /// </summary>
        /// <param name="invertYaw">Invert yaw axis.</param>
        /// <param name="invertPitch">Invert pitch axis.</param>
        /// <param name="invertRoll">Invert roll axis.</param>
        public static CoordinateTransformer CreateFromInversions(bool invertYaw, bool invertPitch, bool invertRoll)
        {
            return new CoordinateTransformer(
                new AxisMapping(SourceAxis.Yaw, invertYaw),
                new AxisMapping(SourceAxis.Pitch, invertPitch),
                new AxisMapping(SourceAxis.Roll, invertRoll)
            );
        }
    }

    /// <summary>
    /// Source axis for coordinate transformation.
    /// </summary>
    public enum SourceAxis
    {
        /// <summary>Yaw rotation (horizontal turn).</summary>
        Yaw,
        /// <summary>Pitch rotation (vertical nod).</summary>
        Pitch,
        /// <summary>Roll rotation (head tilt).</summary>
        Roll
    }

    /// <summary>
    /// Configuration for mapping a source axis to a target rotation component.
    /// </summary>
    public struct AxisMapping : IEquatable<AxisMapping>
    {
        /// <summary>Which source axis to read from.</summary>
        public SourceAxis Source { get; }

        /// <summary>Whether to invert the value (multiply by -1).</summary>
        public bool Invert { get; }

        public AxisMapping(SourceAxis source, bool invert)
        {
            Source = source;
            Invert = invert;
        }

        public bool Equals(AxisMapping other)
        {
            return Source == other.Source && Invert == other.Invert;
        }

        public override bool Equals(object obj)
        {
            if (obj is AxisMapping)
            {
                return Equals((AxisMapping)obj);
            }
            return false;
        }

        public override int GetHashCode()
        {
            unchecked
            {
                return ((int)Source * 397) ^ Invert.GetHashCode();
            }
        }

        public static bool operator ==(AxisMapping left, AxisMapping right) => left.Equals(right);
        public static bool operator !=(AxisMapping left, AxisMapping right) => !left.Equals(right);
    }
}
