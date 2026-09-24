using System;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// A key a frozen legacy reader reads. Section and key compare ASCII case-insensitively. The
    /// C++ twin is <c>cameraunlock::config::LegacyKey</c>.
    /// </summary>
    public sealed class LegacyKey
    {
        /// <param name="section">The section, or "" for a reader that ignores sections, so the key
        /// is read in every section.</param>
        /// <param name="key">The key.</param>
        /// <exception cref="ArgumentNullException">An argument is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="key"/> is empty.</exception>
        public LegacyKey(string section, string key)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (key.Length == 0) throw new ArgumentException("a legacy key needs a name", "key");
            Section = section;
            Key = key;
        }

        public string Section { get; }

        public string Key { get; }
    }
}
