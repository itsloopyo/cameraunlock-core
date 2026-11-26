namespace CameraUnlock.Core.Processing
{
    /// <summary>
    /// Interface for modifying head tracking influence based on game state.
    /// Implementations can reduce or disable head tracking during specific conditions
    /// (dialogues, cutscenes, menus, etc.).
    /// </summary>
    public interface IInfluenceModifier
    {
        /// <summary>
        /// Gets the current influence multiplier.
        /// </summary>
        /// <returns>Value from 0 (disabled) to 1 (full tracking).</returns>
        float GetInfluence();

        /// <summary>
        /// Resets the modifier state.
        /// </summary>
        void Reset();
    }

    /// <summary>
    /// Composite influence modifier that combines multiple modifiers.
    /// The final influence is the product of all modifiers.
    /// </summary>
    public class CompositeInfluenceModifier : IInfluenceModifier
    {
        private readonly IInfluenceModifier[] _modifiers;

        /// <summary>
        /// Creates a composite modifier from multiple sources.
        /// </summary>
        /// <param name="modifiers">The modifiers to combine.</param>
        public CompositeInfluenceModifier(params IInfluenceModifier[] modifiers)
        {
            _modifiers = modifiers ?? new IInfluenceModifier[0];
        }

        /// <summary>
        /// Gets the combined influence (product of all modifiers).
        /// </summary>
        public float GetInfluence()
        {
            float influence = 1f;
            for (int i = 0; i < _modifiers.Length; i++)
            {
                influence *= _modifiers[i].GetInfluence();
                if (influence <= 0f)
                {
                    return 0f;
                }
            }
            return influence;
        }

        /// <summary>
        /// Resets all contained modifiers.
        /// </summary>
        public void Reset()
        {
            for (int i = 0; i < _modifiers.Length; i++)
            {
                _modifiers[i].Reset();
            }
        }
    }

    /// <summary>
    /// Simple toggle-based influence modifier.
    /// Returns 0 when disabled, 1 when enabled.
    /// </summary>
    public class ToggleInfluenceModifier : IInfluenceModifier
    {
        private bool _enabled = true;

        /// <summary>
        /// Gets or sets whether tracking is enabled.
        /// </summary>
        public bool Enabled
        {
            get => _enabled;
            set => _enabled = value;
        }

        /// <summary>
        /// Gets the influence (1 if enabled, 0 if disabled).
        /// </summary>
        public float GetInfluence() => _enabled ? 1f : 0f;

        /// <summary>
        /// Resets to enabled state.
        /// </summary>
        public void Reset() => _enabled = true;
    }
}
