using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>What <see cref="ConfigTable{TConfig}.Render"/> writes above the settings.</summary>
    public sealed class RenderHeader
    {
        /// <param name="displayName">The game's name as data/games.json spells it, in printable
        /// ASCII, since everything the renderer writes is ASCII. Render checks it.</param>
        /// <exception cref="ArgumentNullException"><paramref name="displayName"/> is null.</exception>
        public RenderHeader(string displayName)
        {
            if (displayName == null) throw new ArgumentNullException("displayName");
            DisplayName = displayName;
        }

        public string DisplayName { get; }
    }
}
