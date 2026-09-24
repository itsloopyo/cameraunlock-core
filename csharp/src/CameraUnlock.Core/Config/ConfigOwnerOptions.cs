using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// What a <see cref="ConfigOwner{TConfig}"/> is built from. Fill it by property, never
    /// positionally, so a property added later changes no game's code.
    /// <see cref="ConfigOwner{TConfig}"/> copies the values when it is built.
    /// </summary>
    public sealed class ConfigOwnerOptions<TConfig> where TConfig : class
    {
        /// <summary>The config file, as an absolute path. Required.</summary>
#if NULLABLE_ENABLED
        public string? Path { get; set; }
#else
        public string Path { get; set; }
#endif

        /// <summary>The game's table. Required.</summary>
#if NULLABLE_ENABLED
        public ConfigTable<TConfig>? Table { get; set; }
#else
        public ConfigTable<TConfig> Table { get; set; }
#endif

        /// <summary>
        /// The game's frozen legacy import, or null for a game that never published a build
        /// reading a pre-canonical file. With an import, a file with no [CameraUnlock] stamp is a
        /// legacy file: it is converted once, never edited.
        /// <para>
        /// A BepInEx import binds the plugin's frozen definitions on the plugin's own
        /// <c>Config</c>. BepInEx's ConfigFile already read the .cfg in its constructor, before
        /// the owner held the file, so the import sets <c>Config.SaveOnConfigSet = false</c>, then
        /// calls <c>Config.Reload()</c>, then binds. Without the Reload its values come from a
        /// read the owner's snapshot does not cover, and a program that changed the file in
        /// between would go unnoticed. With SaveOnConfigSet off, no Bind writes the .cfg.
        /// </para>
        /// </summary>
#if NULLABLE_ENABLED
        public LegacyImport<TConfig>? Import { get; set; }
#else
        public LegacyImport<TConfig> Import { get; set; }
#endif

        /// <summary>
        /// The pre-canonical file when it is not the file at <see cref="Path"/>, as an absolute
        /// path: a BepInEx plugin's <c>&lt;GUID&gt;.cfg</c> beside its <c>&lt;GUID&gt;.ini</c>. Needs
        /// <see cref="Import"/>. When <see cref="Path"/> is absent and this file exists, the import
        /// reads this file and the owner creates <see cref="Path"/> from it; this file is never
        /// written and no copy of it is made. The import reads only this file, so a file at
        /// <see cref="Path"/> is always read as canonical, and one missing its stamp gets it at the
        /// next save.
        /// <para>
        /// Null: the legacy file is the one at <see cref="Path"/>, and it is converted in place,
        /// keeping its bytes in <c>&lt;Path&gt;.pre-canonical</c> (the first conversion) and
        /// <c>&lt;Path&gt;.pre-canonical.last</c> (a later one whose input differs from the first).
        /// </para>
        /// </summary>
#if NULLABLE_ENABLED
        public string? LegacySourcePath { get; set; }
#else
        public string LegacySourcePath { get; set; }
#endif

        /// <summary>What the renderer writes above the settings. Required.</summary>
#if NULLABLE_ENABLED
        public RenderHeader? Header { get; set; }
#else
        public RenderHeader Header { get; set; }
#endif

        /// <summary>
        /// Shows the player a one-line message, e.g. through the game's toast, when settings are
        /// not converted, cannot be read or are not saved. Optional. It runs after the owner has
        /// released its lock.
        /// </summary>
#if NULLABLE_ENABLED
        public Action<string>? StatusSink { get; set; }
#else
        public Action<string> StatusSink { get; set; }
#endif
    }
}
