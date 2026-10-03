using System;
using CameraUnlock.Core.Data;

namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Keeps head tracking's effect on the picture the same size whatever the game does with
    /// its field of view. C# twin of cameraunlock/camera/zoom_compensation.h, and the two must
    /// keep the same behaviour case for case.
    /// <para>
    /// A narrow field of view magnifies everything in the frame, head tracking included: the
    /// head turns ten degrees, the camera turns ten degrees, and the picture moves further by
    /// the ratio between the two fields of view. Scaling the pose by that ratio holds its
    /// SCREEN displacement at what it would have been at the base field of view. It is exactly
    /// 1.0 when nothing is zoomed. Nothing here is user-configurable.
    /// </para>
    /// <para>
    /// Yaw, pitch and a lean across the view translate the image across the frame, so all three
    /// scale. Roll rotates it about the view axis by the same angle at every field of view, so
    /// roll is left alone. A lean along the view moves nothing across the frame, it brings the
    /// scene closer, so it is left alone too: scaled, it would cut short how far the player can
    /// lean in. <see cref="ScaleLeanForZoom"/> makes the split.
    /// </para>
    /// </summary>
    public static class ZoomCompensation
    {
        private const double DegToRad = System.Math.PI / 180.0;

        /// <summary>
        /// The factor a translation across the view (a sideways or vertical lean) scales by,
        /// given the field of view being rendered
        /// now and the game's un-zoomed one, both as tan(fov/2) in the same axis.
        /// <para>
        /// The same axis is the whole of the difficulty: pairing a vertical accessor with a
        /// horizontal setting is off by a constant that nothing catches, so the whole of normal
        /// play runs at a fixed fraction of the pose. Prove the pairing by checking that this
        /// returns 1.0 when the game is not zoomed.
        /// </para>
        /// <para>
        /// Both must be finite and positive. They come out of the game, so that is the mod's
        /// boundary check to make, and a mod that cannot read the live field of view applies
        /// no compensation rather than a guessed one.
        /// </para>
        /// </summary>
        public static float FovZoomFactor(float tanHalfFov, float tanHalfFovBase)
        {
            return tanHalfFov / tanHalfFovBase;
        }

        /// <summary>
        /// An angle in degrees, rescaled so it displaces the image by as much as the original
        /// angle did at the base field of view. The image displacement of an angle goes as
        /// tan(angle) / tan(fov/2), so tan(out) = tan(in) * factor holds it exactly; for the
        /// small angles a head reaches it is indistinguishable from multiplying.
        /// <paramref name="factor"/> must be positive and <paramref name="angleDeg"/> within
        /// +/-90, which every pose a neck produces is.
        /// </summary>
        public static float ScaleAngleForZoom(float angleDeg, float factor)
        {
            return (float)(System.Math.Atan(System.Math.Tan(angleDeg * DegToRad) * factor) / DegToRad);
        }

        /// <summary>
        /// A lean with the part perpendicular to the view axis scaled by
        /// <paramref name="factor"/> and the part along it left as it is.
        /// <paramref name="viewAxis"/> is the direction the camera looks along, unit length, in
        /// the frame <paramref name="lean"/> is in: (0, 0, 1) for a lean in the camera's own
        /// axes where z is the view axis, the camera's forward vector for a lean in world space.
        /// Its sign makes no difference.
        /// </summary>
        public static Vec3 ScaleLeanForZoom(Vec3 lean, Vec3 viewAxis, float factor)
        {
            Vec3 along = viewAxis * Vec3.Dot(lean, viewAxis);
            return along + (lean - along) * factor;
        }
    }
}
