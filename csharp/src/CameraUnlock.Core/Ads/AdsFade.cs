namespace CameraUnlock.Core.Ads
{
    /// <summary>
    /// The shape of the transition into and out of aiming down sights. C# twin of
    /// cameraunlock/ads/ads_fade.h.
    /// <para>
    /// Head tracking and iron sights want different things from the camera. Tracking says
    /// the view is wherever you are looking; a sight picture says the view is down the
    /// barrel, because that is the only place the weapon's own reticle means anything. So
    /// the moment the sights start coming up the head pose comes off the camera and the
    /// frame settles onto the aim, which is where the reticle was already pointing, so the
    /// thing the player was about to shoot ends up in the middle of the screen.
    /// </para>
    /// <para>
    /// This class owns the SHAPE of that transition and nothing else. It returns a scale, 1
    /// at the hip and 0 with the sights up, and the caller decides what the scale blends
    /// between (see <see cref="AdsPoseBlend"/>):
    /// </para>
    /// <list type="bullet">
    /// <item><description><see cref="AdsMode.Paused"/> blends the head pose down to nothing
    /// and holds it there.</description></item>
    /// <item><description><see cref="AdsMode.Marker"/> and <see cref="AdsMode.Tracked"/>
    /// blend the absolute pose into the pose measured from the entry frame
    /// (<see cref="AdsEntryPose"/>), which is identity at that moment.</description></item>
    /// </list>
    /// <para>
    /// So all three modes make the same swing onto the aim, and differ only in what happens
    /// for the rest of the aim.
    /// </para>
    /// <para>
    /// It is a SUSPEND, not a reset. The pose keeps flowing through the pipeline with its
    /// smoothing state intact, so lowering the weapon eases the view back to where the head
    /// actually is. Resetting instead would swing the view back through the whole head
    /// angle on the way out, dozens of times a firefight. Reset stays right for menus,
    /// cinematics and the master toggle, which is what <see cref="Reset"/> is for.
    /// </para>
    /// <para>
    /// The tracker's centre is deliberately not moved by any of this. Head centre means
    /// "looking down the gun", always. Recentring on the sights coming up, the obvious
    /// first idea, makes the pose the player happened to hold when they pressed aim the new
    /// neutral, so they have to HOLD their head turned to keep looking where they shot, and
    /// every aim press walks the neutral further from where the head actually rests.
    /// </para>
    /// <para>
    /// Pure: no clock of its own, no logging, no game. <c>nowMs</c> comes from the caller,
    /// which is what lets the whole transition be driven frame by frame in a test.
    /// </para>
    /// </summary>
    public sealed class AdsFade
    {
        /// <summary>
        /// How long the transition takes when the sights start coming up. Short enough to
        /// be done before there is a sight picture to look through - a weapon's own raise
        /// animation is around a fifth of a second - and long enough that the view leans
        /// onto the gun rather than snapping to it.
        /// </summary>
        public static readonly ulong LowerMs = 150;

        /// <summary>
        /// And back when they drop. Longer, because nothing is waiting on it and a slower
        /// return is the more comfortable half.
        /// </summary>
        public static readonly ulong RaiseMs = 250;

        private enum State { Hip, Lowering, Aiming, Raising }

        // Below this the two ends of a leg are the same place and there is nothing to
        // travel.
        private const float Settled = 1e-6f;

        private State _state = State.Hip;
        private ulong _startMs;
        private ulong _durationMs = LowerMs;

        // The scale the current leg started from and is heading to. Held rather than
        // assumed, because a leg can start anywhere: see Update.
        private float _from = 1.0f;
        private float _target = 1.0f;

        /// <summary>
        /// Called once per rendered frame, before the head pose is applied.
        /// <paramref name="aiming"/> is the ADS state for this frame, polled rather than
        /// latched. Returns the scale to blend at: 1 at the hip, 0 with the sights up.
        /// </summary>
        public float Update(bool aiming, ulong nowMs)
        {
            bool turnDown = aiming && (_state == State.Hip || _state == State.Raising);
            bool turnUp = !aiming && (_state == State.Lowering || _state == State.Aiming);

            if (turnDown || turnUp)
            {
                // A reversal starts from WHERE THE TRANSITION IS, not from the end the
                // interrupted leg would have reached. Starting each leg at its own
                // endpoint steps the pose by however far the previous one had travelled,
                // and the worst case is the most common input there is: a tap of the aim
                // button releases a frame after it was pressed, so the pose is 99.99%
                // applied and the next frame removes all of it. That is the jolt this
                // class exists to remove, delivered by the class itself.
                float from = Current(nowMs);
                float target = turnDown ? 0.0f : 1.0f;
                float distance = target > from ? target - from : from - target;
                if (distance < Settled)
                {
                    _state = turnDown ? State.Aiming : State.Hip;
                    return target;
                }
                _state = turnDown ? State.Lowering : State.Raising;
                _from = from;
                _target = target;
                // Scaled by the distance left to travel, so an interrupted leg moves at
                // the same RATE as a whole one rather than taking the full time to cover
                // a fraction of the distance.
                float full = turnDown ? LowerMs : RaiseMs;
                _durationMs = (ulong)(full * distance);
                if (_durationMs == 0) _durationMs = 1;
                _startMs = nowMs;
            }

            float scale = Current(nowMs);
            if ((_state == State.Lowering || _state == State.Raising)
                && Elapsed(nowMs) >= _durationMs)
            {
                _state = _target == 0.0f ? State.Aiming : State.Hip;
            }
            return scale;
        }

        /// <summary>
        /// Drops back to hip state. Call wherever tracking is suppressed - menu, loading,
        /// cinematic, master toggle, tracker dropout - so the next aim starts clean.
        /// </summary>
        public void Reset()
        {
            _state = State.Hip;
            _from = 1.0f;
            _target = 1.0f;
        }

        // Clamped at zero rather than allowed to wrap. nowMs is the caller's clock, and an
        // unsigned subtraction across a clock that stepped backwards yields an enormous
        // elapsed, which settles the transition instantly - a snap, in the one place this
        // class exists to prevent one.
        private ulong Elapsed(ulong nowMs)
        {
            return nowMs > _startMs ? nowMs - _startMs : 0;
        }

        // Where the transition is right now, without advancing it.
        private float Current(ulong nowMs)
        {
            if (_state == State.Hip) return 1.0f;
            if (_state == State.Aiming) return 0.0f;
            ulong elapsed = Elapsed(nowMs);
            if (elapsed >= _durationMs) return _target;
            return _from + (_target - _from) * Ease(elapsed, _durationMs);
        }

        // Smoothstep, so the transition leaves and arrives at rest instead of starting and
        // stopping with a visible corner.
        private static float Ease(ulong elapsed, ulong duration)
        {
            float t = (float)elapsed / (float)duration;
            return t * t * (3.0f - 2.0f * t);
        }
    }
}
