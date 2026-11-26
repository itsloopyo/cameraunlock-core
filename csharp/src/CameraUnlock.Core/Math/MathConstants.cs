namespace CameraUnlock.Core.Math
{
    /// <summary>
    /// Common mathematical constants used throughout the library.
    /// </summary>
    public static class MathConstants
    {
        /// <summary>Pi (3.14159...).</summary>
        public const float Pi = 3.14159265358979323846f;

        /// <summary>Two times Pi (6.28318...).</summary>
        public const float TwoPi = Pi * 2f;

        /// <summary>Conversion factor from degrees to radians.</summary>
        public const float DegToRad = Pi / 180f;

        /// <summary>Conversion factor from radians to degrees.</summary>
        public const float RadToDeg = 180f / Pi;
    }
}
