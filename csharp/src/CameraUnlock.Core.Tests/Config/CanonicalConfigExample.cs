#if NETCOREAPP
#nullable disable
#pragma warning disable CA1416
#endif
using System;
using System.IO;
using System.Text;
using CameraUnlock.Core.Config;
using CameraUnlock.Core.Input;

namespace CameraUnlock.Core.Tests.Config
{
    /// <summary>
    /// The C# example docs/canonical-config.md shows, run for real. scripts/check-doc-examples.mjs
    /// holds the document's C# blocks to this file line for line, so an example that stops compiling
    /// or behaving as the document says fails here. The table renders
    /// data/fixtures/canonical-ini/example/CameraUnlock.ini, as the C++ example's does. The same
    /// source runs under xunit on net8.0 (CanonicalConfigExampleTests) and in the
    /// CameraUnlock.Core.FrameworkTests console on .NET Framework 3.5 and 4.7.2, so the example is
    /// C# 7.3 that compiles on net35.
    /// </summary>
    internal static class CanonicalConfigExample
    {
        public sealed class ModConfig : HeadTrackingConfigData
        {
            public bool WriteLog { get; set; }
        }

        public static ConfigTable<ModConfig> ModConfigTable()
        {
            return HeadTrackingConfigTable.Create<ModConfig>(ConfigConcepts.UdpPort, ConfigConcepts.EnableOnStartup,
                    ConfigConcepts.WorldSpaceYaw, ConfigConcepts.RotationEnabled, ConfigConcepts.PositionEnabled,
                    ConfigConcepts.ToggleKey, ConfigConcepts.CycleTrackingModeKey, ConfigConcepts.YawModeKey)
                .Select(ConfigConcepts.WorldSpaceYaw).Writable()
                .Select(ConfigConcepts.RotationEnabled).Writable()
                .Select(ConfigConcepts.PositionEnabled).Writable()
                .Local("Logging", "WriteLog", c => c.WriteLog, (c, v) => c.WriteLog = v, new BoolCodec(),
                    "true: write HeadTracking.log beside the game's executable.");
        }

        /// <summary>Throws when the table's defaults do not render as example/CameraUnlock.ini.</summary>
        public static void RunRender(string root)
        {
            ConfigTable<ModConfig> table = ModConfigTable();
            var defaults = new ModConfig();
            table.Apply(CanonicalIni.Parse(new byte[0]), defaults);
            byte[] rendered = table.Render(defaults, new RenderHeader("Example Game"));
            if (Encoding.ASCII.GetString(rendered) != Expected(root))
            {
                throw new InvalidOperationException("rendered bytes differ:\n" + Encoding.ASCII.GetString(rendered));
            }
        }

        /// <summary>
        /// Throws unless the owner creates the file on the first Load, saves the yaw toggle as one
        /// changed line, and the next launch reads the saved value.
        /// </summary>
        public static void RunOwner(string root, string dir)
        {
            var owner = new ConfigOwner<ModConfig>(new ConfigOwnerOptions<ModConfig>
            {
                Path = Path.Combine(dir, "CameraUnlock.ini"),
                Table = ModConfigTable(),
                Header = new RenderHeader("Example Game"),
            });

            ConfigLoadResult<ModConfig> loaded = owner.Load();
            ModConfig config = loaded.Config;
            bool parsed = KeyBindings.TryParse(config.ToggleKeyName, out var toggle, out var error);

            ConfigSaveResult saved = owner.Save(c => c.WorldSpaceYaw = false);

            Expect(loaded.Status == ConfigLoadStatus.Created, "the first Load creates the file, not " + loaded.Status);
            Expect(parsed && toggle.Length == 2, "ToggleKey reads as two bindings: " + error);
            Expect(saved.Status == ConfigSaveStatus.Saved, "the yaw toggle saves, not " + saved.Status + ": " + saved.Reason);
            string after = Expected(root).Replace("WorldSpaceYaw=true", "WorldSpaceYaw=false");
            Expect(File.ReadAllText(Path.Combine(dir, "CameraUnlock.ini"), Encoding.ASCII) == after,
                "the save changes the WorldSpaceYaw line and nothing else");

            var next = new ConfigOwner<ModConfig>(new ConfigOwnerOptions<ModConfig>
            {
                Path = Path.Combine(dir, "CameraUnlock.ini"),
                Table = ModConfigTable(),
                Header = new RenderHeader("Example Game"),
            });
            ConfigLoadResult<ModConfig> reread = next.Load();
            Expect(reread.Status == ConfigLoadStatus.Canonical && !reread.Config.WorldSpaceYaw && !reread.Config.WriteLog,
                "the next launch reads the saved value");
        }

        private static string Expected(string root)
        {
            return File.ReadAllText(Path.Combine(Path.Combine(root, "example"), "CameraUnlock.ini"), Encoding.ASCII);
        }

        private static void Expect(bool condition, string what)
        {
            if (!condition) throw new InvalidOperationException(what);
        }
    }
}
