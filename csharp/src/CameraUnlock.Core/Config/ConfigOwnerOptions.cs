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
        /// <summary>
        /// The config file, as an absolute path: <c>CameraUnlock.ini</c>, in the folder that holds
        /// the game's legacy file where it has one (<c>BepInEx\config\CameraUnlock.ini</c> for a
        /// BepInEx plugin). Required.
        /// </summary>
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
        /// before the canonical format. Needs <see cref="LegacySourcePath"/>, the one file it
        /// reads.
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
        /// The game's legacy file, as an absolute path, normally in the same folder as
        /// <see cref="Path"/>: <c>HeadTracking.ini</c> beside <c>CameraUnlock.ini</c>, or a BepInEx
        /// plugin's <c>BepInEx\config\&lt;GUID&gt;.cfg</c> beside
        /// <c>BepInEx\config\CameraUnlock.ini</c>. Required with <see cref="Import"/>, refused
        /// without it, and refused when it names <see cref="Path"/>. Only while no file exists at
        /// <see cref="Path"/> does the import read this file, and the owner then creates
        /// <see cref="Path"/> from what it gives. Once <see cref="Path"/> exists this file is not
        /// read again. It is never written, renamed, deleted or copied.
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
        /// Where Defaults.ini is: <see cref="DefaultsFile.PerUser"/> in a mod, and
        /// <see cref="DefaultsFile.At"/> with a scratch path in every test. A helper that builds the
        /// options for both takes it as a parameter, so a test never reaches the player's own file.
        /// Required.
        /// </summary>
#if NULLABLE_ENABLED
        public DefaultsFile? Defaults { get; set; }
#else
        public DefaultsFile Defaults { get; set; }
#endif

        /// <summary>
        /// Shows the player a one-line message, e.g. through the game's toast, when settings are
        /// not imported, cannot be read or are not saved. Optional. It runs after the owner has
        /// released its lock.
        /// </summary>
#if NULLABLE_ENABLED
        public Action<string>? StatusSink { get; set; }
#else
        public Action<string> StatusSink { get; set; }
#endif
    }
}
