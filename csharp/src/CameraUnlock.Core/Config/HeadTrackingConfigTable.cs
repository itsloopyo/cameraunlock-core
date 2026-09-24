using System;
using CameraUnlock.Core.Data;
using CameraUnlock.Core.Effects;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Core's config table over <see cref="HeadTrackingConfigData"/>, or over a class derived from
    /// it that adds the game's own fields for its local rows. The caller names the concepts its
    /// game implements, core binds each, and the game appends its local rows and modifiers. Naming
    /// them keeps a file to what the game binds (a game with no carried light has no [Light]), and
    /// a concept core adds later reaches a game's file only once the game names it. The C++ twin
    /// is cameraunlock::config::HeadTrackingConfigTable.
    /// <para>
    /// LocalSmoothing and RemoteSmoothing write the property and recompose
    /// <see cref="HeadTrackingConfigData.Position"/>'s smoothing pair from both, as
    /// <see cref="HeadTrackingConfigData.ApplyValues"/> does. PositionLimitY never sets
    /// PositionLimitYDown: each key is read on its own. The position limits and the light rows
    /// replace <see cref="HeadTrackingConfigData.Position"/> and
    /// <see cref="HeadTrackingConfigData.Light"/> with a copy carrying the new value, so a
    /// <see cref="HeadFollowLightSettings"/> instance another config shares is never changed.
    /// CollisionChannel is an Engine row: the channel is data about the game, so at its default it
    /// is written as a comment.
    /// </para>
    /// <para>
    /// The defaults instance is a new config with the three hotkey lists at the schema's
    /// canonical_default (<c>End, Ctrl+Shift+Y</c>, <c>PageUp, Ctrl+Shift+G</c>,
    /// <c>PageDown, Ctrl+Shift+H</c>); the properties' own initialisers, which
    /// <see cref="HeadTrackingConfigData.LoadFromFile"/> uses, stay single keys.
    /// </para>
    /// </summary>
    public static class HeadTrackingConfigTable
    {
        /// <summary>A table over <see cref="HeadTrackingConfigData"/> binding the named concepts.</summary>
        /// <exception cref="ArgumentNullException"><paramref name="implemented"/> or one of its
        /// items is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="implemented"/> is empty or names a
        /// concept twice.</exception>
        public static ConfigTable<HeadTrackingConfigData> Create(params ConceptDescriptor[] implemented)
        {
            return Create<HeadTrackingConfigData>(implemented);
        }

        /// <summary>
        /// A table over <typeparamref name="TConfig"/> binding the named concepts, for a game that
        /// appends local rows on its own fields.
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="implemented"/> or one of its
        /// items is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="implemented"/> is empty or names a
        /// concept twice.</exception>
        public static ConfigTable<TConfig> Create<TConfig>(params ConceptDescriptor[] implemented)
            where TConfig : HeadTrackingConfigData, new()
        {
            if (implemented == null) throw new ArgumentNullException("implemented");
            if (implemented.Length == 0)
            {
                throw new ArgumentException("HeadTrackingConfigTable needs the concepts the game implements", "implemented");
            }
            for (int i = 0; i < implemented.Length; i++)
            {
                if (implemented[i] == null) throw new ArgumentNullException("implemented", "item " + i + " is null");
                if (Array.IndexOf(implemented, implemented[i], 0, i) >= 0)
                {
                    throw new ArgumentException("HeadTrackingConfigTable names " + implemented[i].Id + " twice", "implemented");
                }
            }

            var table = new ConfigTable<TConfig>(() => WithCanonicalHotkeys(new TConfig()));
            foreach (ConceptDescriptor concept in implemented) Bind(table, concept);
            return table;
        }

        private static TConfig WithCanonicalHotkeys<TConfig>(TConfig config) where TConfig : HeadTrackingConfigData
        {
            config.ToggleKeyName = CanonicalDefault(ConfigConcepts.ToggleKey);
            config.CycleTrackingModeKeyName = CanonicalDefault(ConfigConcepts.CycleTrackingModeKey);
            config.YawModeKeyName = CanonicalDefault(ConfigConcepts.YawModeKey);
            return config;
        }

        private static string CanonicalDefault(ConceptDescriptor<string> concept)
        {
            var value = concept.CanonicalDefault;
            if (value == null) throw new InvalidOperationException(concept.Id + " has no canonical_default in data/config-schema.json");
            return value;
        }

        private static void Bind<TConfig>(ConfigTable<TConfig> table, ConceptDescriptor concept)
            where TConfig : HeadTrackingConfigData
        {
            switch (concept.Id)
            {
                case nameof(ConfigConcepts.UdpPort):
                    table.Concept(ConfigConcepts.UdpPort, c => c.UdpPort, (c, v) => c.UdpPort = v);
                    return;
                case nameof(ConfigConcepts.EnableOnStartup):
                    table.Concept(ConfigConcepts.EnableOnStartup, c => c.EnableOnStartup, (c, v) => c.EnableOnStartup = v);
                    return;
                case nameof(ConfigConcepts.LocalSmoothing):
                    table.Concept(ConfigConcepts.LocalSmoothing, c => c.LocalSmoothing, (c, v) =>
                    {
                        c.LocalSmoothing = v;
                        c.Position = c.Position.WithSmoothing(c.LocalSmoothing, c.RemoteSmoothing);
                    });
                    return;
                case nameof(ConfigConcepts.RemoteSmoothing):
                    table.Concept(ConfigConcepts.RemoteSmoothing, c => c.RemoteSmoothing, (c, v) =>
                    {
                        c.RemoteSmoothing = v;
                        c.Position = c.Position.WithSmoothing(c.LocalSmoothing, c.RemoteSmoothing);
                    });
                    return;
                case nameof(ConfigConcepts.WorldSpaceYaw):
                    table.Concept(ConfigConcepts.WorldSpaceYaw, c => c.WorldSpaceYaw, (c, v) => c.WorldSpaceYaw = v);
                    return;
                case nameof(ConfigConcepts.AimDecoupling):
                    table.Concept(ConfigConcepts.AimDecoupling, c => c.AimDecouplingEnabled, (c, v) => c.AimDecouplingEnabled = v);
                    return;
                case nameof(ConfigConcepts.RotationEnabled):
                    table.Concept(ConfigConcepts.RotationEnabled, c => c.RotationEnabled, (c, v) => c.RotationEnabled = v);
                    return;
                case nameof(ConfigConcepts.DataFreshnessMs):
                    table.Concept(ConfigConcepts.DataFreshnessMs, c => c.DataFreshnessMs, (c, v) => c.DataFreshnessMs = v);
                    return;
                case nameof(ConfigConcepts.PositionEnabled):
                    table.Concept(ConfigConcepts.PositionEnabled, c => c.PositionEnabled, (c, v) => c.PositionEnabled = v);
                    return;
                case nameof(ConfigConcepts.PositionAllowed):
                    table.Concept(ConfigConcepts.PositionAllowed, c => c.PositionAllowed, (c, v) => c.PositionAllowed = v);
                    return;
                case nameof(ConfigConcepts.PositionLimitX):
                    table.Concept(ConfigConcepts.PositionLimitX, c => c.Position.LimitX, (c, v) =>
                    {
                        PositionSettings p = c.Position;
                        c.Position = Limits(p, v, p.LimitY, p.LimitYDown, p.LimitZ, p.LimitZBack);
                    });
                    return;
                case nameof(ConfigConcepts.PositionLimitY):
                    table.Concept(ConfigConcepts.PositionLimitY, c => c.Position.LimitY, (c, v) =>
                    {
                        PositionSettings p = c.Position;
                        c.Position = Limits(p, p.LimitX, v, p.LimitYDown, p.LimitZ, p.LimitZBack);
                    });
                    return;
                case nameof(ConfigConcepts.PositionLimitYDown):
                    table.Concept(ConfigConcepts.PositionLimitYDown, c => c.Position.LimitYDown, (c, v) =>
                    {
                        PositionSettings p = c.Position;
                        c.Position = Limits(p, p.LimitX, p.LimitY, v, p.LimitZ, p.LimitZBack);
                    });
                    return;
                case nameof(ConfigConcepts.PositionLimitZ):
                    table.Concept(ConfigConcepts.PositionLimitZ, c => c.Position.LimitZ, (c, v) =>
                    {
                        PositionSettings p = c.Position;
                        c.Position = Limits(p, p.LimitX, p.LimitY, p.LimitYDown, v, p.LimitZBack);
                    });
                    return;
                case nameof(ConfigConcepts.PositionLimitZBack):
                    table.Concept(ConfigConcepts.PositionLimitZBack, c => c.Position.LimitZBack, (c, v) =>
                    {
                        PositionSettings p = c.Position;
                        c.Position = Limits(p, p.LimitX, p.LimitY, p.LimitYDown, p.LimitZ, v);
                    });
                    return;
                case nameof(ConfigConcepts.CollisionEnabled):
                    table.Concept(ConfigConcepts.CollisionEnabled, c => c.CollisionEnabled, (c, v) => c.CollisionEnabled = v);
                    return;
                case nameof(ConfigConcepts.CollisionMargin):
                    table.Concept(ConfigConcepts.CollisionMargin, c => c.CollisionMargin, (c, v) => c.CollisionMargin = v);
                    return;
                case nameof(ConfigConcepts.CollisionChannel):
                    table.Concept(ConfigConcepts.CollisionChannel, c => c.CollisionChannel, (c, v) => c.CollisionChannel = v)
                        .Engine();
                    return;
                case nameof(ConfigConcepts.CollisionReleaseSmoothing):
                    table.Concept(ConfigConcepts.CollisionReleaseSmoothing, c => c.CollisionReleaseSmoothing,
                        (c, v) => c.CollisionReleaseSmoothing = v);
                    return;
                case nameof(ConfigConcepts.TrackerPivotForward):
                    table.Concept(ConfigConcepts.TrackerPivotForward, c => c.TrackerPivotForward, (c, v) => c.TrackerPivotForward = v);
                    return;
                case nameof(ConfigConcepts.TrackerPivotUp):
                    table.Concept(ConfigConcepts.TrackerPivotUp, c => c.TrackerPivotUp, (c, v) => c.TrackerPivotUp = v);
                    return;
                case nameof(ConfigConcepts.ToggleKey):
                    table.Concept(ConfigConcepts.ToggleKey, c => c.ToggleKeyName, (c, v) => c.ToggleKeyName = v);
                    return;
                case nameof(ConfigConcepts.CycleTrackingModeKey):
                    table.Concept(ConfigConcepts.CycleTrackingModeKey, c => c.CycleTrackingModeKeyName,
                        (c, v) => c.CycleTrackingModeKeyName = v);
                    return;
                case nameof(ConfigConcepts.YawModeKey):
                    table.Concept(ConfigConcepts.YawModeKey, c => c.YawModeKeyName, (c, v) => c.YawModeKeyName = v);
                    return;
                case nameof(ConfigConcepts.LightFollowsHead):
                    table.Concept(ConfigConcepts.LightFollowsHead, c => c.Light.FollowsHead,
                        (c, v) => c.Light = new HeadFollowLightSettings { FollowsHead = v, Multiplier = c.Light.Multiplier });
                    return;
                case nameof(ConfigConcepts.LightMultiplier):
                    table.Concept(ConfigConcepts.LightMultiplier, c => c.Light.Multiplier,
                        (c, v) => c.Light = new HeadFollowLightSettings { FollowsHead = c.Light.FollowsHead, Multiplier = v });
                    return;
            }
            // Reached by a concept the schema gained after this switch was written.
            throw new InvalidOperationException("HeadTrackingConfigTable has no binding for " + concept.Id);
        }

        private static PositionSettings Limits(PositionSettings p, float limitX, float limitY, float limitYDown,
            float limitZ, float limitZBack)
        {
            return new PositionSettings(
                p.SensitivityX, p.SensitivityY, p.SensitivityZ,
                limitX, limitY, limitYDown, limitZ, limitZBack,
                p.LocalSmoothing, p.RemoteSmoothing,
                p.InvertX, p.InvertY, p.InvertZ);
        }
    }
}
