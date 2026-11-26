// Test files intentionally pass null to test exception handling
#pragma warning disable CS8625 // Cannot convert null literal to non-nullable reference type

using Xunit;
using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Tests.Data
{
    public class CoordinateTransformerTests
    {
        [Fact]
        public void Constructor_Default_IdentityMapping()
        {
            var transformer = new CoordinateTransformer();

            Assert.Equal(SourceAxis.Yaw, transformer.YawMapping.Source);
            Assert.Equal(SourceAxis.Pitch, transformer.PitchMapping.Source);
            Assert.Equal(SourceAxis.Roll, transformer.RollMapping.Source);
        }

        [Fact]
        public void Constructor_Default_NoInversion()
        {
            var transformer = new CoordinateTransformer();

            Assert.False(transformer.YawMapping.Invert);
            Assert.False(transformer.PitchMapping.Invert);
            Assert.False(transformer.RollMapping.Invert);
        }

        [Fact]
        public void Transform_WithNoChanges_ReturnsOriginal()
        {
            var transformer = new CoordinateTransformer();
            var pose = new TrackingPose(10f, 20f, 30f, 12345);

            TrackingPose result = transformer.Transform(pose);

            Assert.Equal(10f, result.Yaw, precision: 3);
            Assert.Equal(20f, result.Pitch, precision: 3);
            Assert.Equal(30f, result.Roll, precision: 3);
        }

        [Fact]
        public void Transform_PreservesTimestamp()
        {
            var transformer = new CoordinateTransformer();
            var pose = new TrackingPose(10f, 20f, 30f, 99999);

            TrackingPose result = transformer.Transform(pose);

            Assert.Equal(99999, result.TimestampTicks);
        }

        [Fact]
        public void Transform_WithInvertYaw_NegatesYaw()
        {
            var transformer = CoordinateTransformer.CreateFromInversions(true, false, false);
            var pose = new TrackingPose(10f, 20f, 30f, 12345);

            TrackingPose result = transformer.Transform(pose);

            Assert.Equal(-10f, result.Yaw, precision: 3);
            Assert.Equal(20f, result.Pitch, precision: 3);
            Assert.Equal(30f, result.Roll, precision: 3);
        }

        [Fact]
        public void Transform_WithInvertPitch_NegatesPitch()
        {
            var transformer = CoordinateTransformer.CreateFromInversions(false, true, false);
            var pose = new TrackingPose(10f, 20f, 30f, 12345);

            TrackingPose result = transformer.Transform(pose);

            Assert.Equal(10f, result.Yaw, precision: 3);
            Assert.Equal(-20f, result.Pitch, precision: 3);
            Assert.Equal(30f, result.Roll, precision: 3);
        }

        [Fact]
        public void Transform_WithInvertRoll_NegatesRoll()
        {
            var transformer = CoordinateTransformer.CreateFromInversions(false, false, true);
            var pose = new TrackingPose(10f, 20f, 30f, 12345);

            TrackingPose result = transformer.Transform(pose);

            Assert.Equal(10f, result.Yaw, precision: 3);
            Assert.Equal(20f, result.Pitch, precision: 3);
            Assert.Equal(-30f, result.Roll, precision: 3);
        }

        [Fact]
        public void Transform_CombinedInversions_AppliesAll()
        {
            var transformer = CoordinateTransformer.CreateFromInversions(true, true, true);
            var pose = new TrackingPose(10f, 20f, 30f, 12345);

            TrackingPose result = transformer.Transform(pose);

            Assert.Equal(-10f, result.Yaw, precision: 3);
            Assert.Equal(-20f, result.Pitch, precision: 3);
            Assert.Equal(-30f, result.Roll, precision: 3);
        }

        [Fact]
        public void Transform_WithYawPitchSwap_SwapsValues()
        {
            var transformer = new CoordinateTransformer(
                new AxisMapping(SourceAxis.Pitch, false),  // Yaw reads from Pitch
                new AxisMapping(SourceAxis.Yaw, false),    // Pitch reads from Yaw
                new AxisMapping(SourceAxis.Roll, false));  // Roll stays same
            var pose = new TrackingPose(10f, 20f, 30f, 12345);

            TrackingPose result = transformer.Transform(pose);

            Assert.Equal(20f, result.Yaw, precision: 3);  // Was pitch
            Assert.Equal(10f, result.Pitch, precision: 3); // Was yaw
            Assert.Equal(30f, result.Roll, precision: 3);
        }

        [Fact]
        public void Transform_FloatOverload_TransformsValues()
        {
            var transformer = CoordinateTransformer.CreateFromInversions(true, false, false);

            transformer.Transform(10f, 20f, 30f, out float outYaw, out float outPitch, out float outRoll);

            Assert.Equal(-10f, outYaw, precision: 3);
            Assert.Equal(20f, outPitch, precision: 3);
            Assert.Equal(30f, outRoll, precision: 3);
        }

        [Fact]
        public void CreateOpenTrackToUnity_InvertsPitch()
        {
            var transformer = CoordinateTransformer.CreateOpenTrackToUnity();

            Assert.False(transformer.YawMapping.Invert);
            Assert.True(transformer.PitchMapping.Invert);  // Pitch is inverted for Unity
            Assert.False(transformer.RollMapping.Invert);
        }

        [Fact]
        public void CreatePassthrough_NoInversion()
        {
            var transformer = CoordinateTransformer.CreatePassthrough();

            Assert.False(transformer.YawMapping.Invert);
            Assert.False(transformer.PitchMapping.Invert);
            Assert.False(transformer.RollMapping.Invert);
        }

        [Fact]
        public void CreatePassthrough_IdentityMapping()
        {
            var transformer = CoordinateTransformer.CreatePassthrough();

            Assert.Equal(SourceAxis.Yaw, transformer.YawMapping.Source);
            Assert.Equal(SourceAxis.Pitch, transformer.PitchMapping.Source);
            Assert.Equal(SourceAxis.Roll, transformer.RollMapping.Source);
        }

        [Fact]
        public void AxisMapping_Equality_SameValues()
        {
            var a = new AxisMapping(SourceAxis.Yaw, true);
            var b = new AxisMapping(SourceAxis.Yaw, true);

            Assert.True(a.Equals(b));
            Assert.True(a == b);
            Assert.Equal(a.GetHashCode(), b.GetHashCode());
        }

        [Fact]
        public void AxisMapping_Equality_DifferentSource()
        {
            var a = new AxisMapping(SourceAxis.Yaw, true);
            var b = new AxisMapping(SourceAxis.Pitch, true);

            Assert.False(a.Equals(b));
            Assert.True(a != b);
        }

        [Fact]
        public void AxisMapping_Equality_DifferentInvert()
        {
            var a = new AxisMapping(SourceAxis.Yaw, true);
            var b = new AxisMapping(SourceAxis.Yaw, false);

            Assert.False(a.Equals(b));
            Assert.True(a != b);
        }

        [Fact]
        public void AxisMapping_Equals_WithObjectType()
        {
            var a = new AxisMapping(SourceAxis.Yaw, true);
            object b = new AxisMapping(SourceAxis.Yaw, true);

            Assert.True(a.Equals(b));
        }

        [Fact]
        public void AxisMapping_Equals_WithNull()
        {
            var a = new AxisMapping(SourceAxis.Yaw, true);

            Assert.False(a.Equals(null));
        }

        [Fact]
        public void AxisMapping_Equals_WithDifferentType()
        {
            var a = new AxisMapping(SourceAxis.Yaw, true);

            Assert.False(a.Equals("string"));
        }
    }
}
