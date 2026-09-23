using System;
using System.Collections.ObjectModel;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// The outcome of <see cref="IniEditor.Edit"/>: the edited document, or a typed refusal.
    /// </summary>
    public sealed class IniEditResult
    {
        private readonly byte[] _bytes;

#if NULLABLE_ENABLED
        internal IniEditResult(IniEditRefusal refusal, byte[] bytes, string? section, string? key, int[] lines)
#else
        internal IniEditResult(IniEditRefusal refusal, byte[] bytes, string section, string key, int[] lines)
#endif
        {
            Refusal = refusal;
            _bytes = bytes;
            Section = section;
            Key = key;
            Lines = new ReadOnlyCollection<int>(lines);
        }

        public IniEditRefusal Refusal { get; }

        public bool Succeeded
        {
            get { return Refusal == IniEditRefusal.None; }
        }

        /// <summary>
        /// The edited document.
        /// </summary>
        /// <exception cref="InvalidOperationException">The edit was refused.</exception>
        public byte[] Bytes
        {
            get
            {
                if (!Succeeded)
                {
                    throw new InvalidOperationException(
                        "The INI edit was refused (" + Refusal + "), so there is no document to take.");
                }
                return _bytes;
            }
        }

        /// <summary>
        /// The refused edit's section, as the caller spelled it. Null for a refusal of the
        /// whole document, and on success.
        /// </summary>
#if NULLABLE_ENABLED
        public string? Section { get; }
#else
        public string Section { get; }
#endif

        /// <summary>
        /// The refused edit's key, as the caller spelled it. Null for a refusal of the whole
        /// document, and on success.
        /// </summary>
#if NULLABLE_ENABLED
        public string? Key { get; }
#else
        public string Key { get; }
#endif

        /// <summary>
        /// 1-based line numbers the refusal is about: every occurrence for a duplicate, the
        /// line holding the first offending byte for an encoding refusal. Empty otherwise.
        /// </summary>
        public ReadOnlyCollection<int> Lines { get; }
    }
}
