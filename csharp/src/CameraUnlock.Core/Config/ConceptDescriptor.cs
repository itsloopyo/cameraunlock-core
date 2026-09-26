using System.Collections.ObjectModel;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// What a concept's field holds, which fixes its codec: Bool is <see cref="BoolCodec"/>,
    /// Integer <see cref="IntCodec"/>, Floating <see cref="FloatCodec"/> and Hotkey
    /// <see cref="HotkeyCodec"/>.
    /// </summary>
    public enum ConceptValueFamily
    {
        Bool = 0,
        Integer = 1,
        Floating = 2,
        Hotkey = 3,
    }

    /// <summary>
    /// One concept the canonical format writes, as data/config-schema.json describes it. Every
    /// instance is one of the fields of <see cref="ConfigConcepts"/>.
    /// </summary>
    public abstract class ConceptDescriptor
    {
        internal ConceptDescriptor(string id, string section, string key, ConceptValueFamily family,
            string[] fileComment,
#if NULLABLE_ENABLED
            string? canonicalDefault,
#else
            string canonicalDefault,
#endif
            string defaultText, bool global)
        {
            Id = id;
            Section = section;
            Key = key;
            Family = family;
            FileComment = new ReadOnlyCollection<string>(fileComment);
            CanonicalDefault = canonicalDefault;
            DefaultText = defaultText;
            Global = global;
        }

        /// <summary>The schema's id for the concept.</summary>
        public string Id { get; }

        /// <summary>The section a canonical file writes it in.</summary>
        public string Section { get; }

        /// <summary>The key a canonical file writes it as.</summary>
        public string Key { get; }

        public ConceptValueFamily Family { get; }

        /// <summary>The one or two lines written above the key.</summary>
        public ReadOnlyCollection<string> FileComment { get; }

        /// <summary>
        /// The value a canonical file starts with where it differs from the flat readers' default, as
        /// its codec writes it: a hotkey concept's binding list, and <c>true</c> for CollisionEnabled.
        /// Null for every other concept.
        /// </summary>
#if NULLABLE_ENABLED
        public string? CanonicalDefault { get; }
#else
        public string CanonicalDefault { get; }
#endif

        /// <summary>
        /// False for a concept that holds engine data, a number in the engine's own units or a
        /// channel it names (CollisionMargin, CollisionChannel): every game keeps its own default,
        /// Defaults.ini never carries it and <see cref="ConfigTable{TConfig}.PerGame"/> refuses it. True
        /// for every other concept, whose row follows Defaults.ini unless the table marks it PerGame.
        /// </summary>
        public bool Global { get; }

        // The schema's default as text the concept's codec reads: the canonical_default where there
        // is one, else the schema's value as written there.
        internal string DefaultText { get; }

        // What the concept's codec, with the schema's range, expected when it does not read the
        // text; null when it does.
#if NULLABLE_ENABLED
        internal abstract string? CodecError(byte[] text);
#else
        internal abstract string CodecError(byte[] text);
#endif
    }

    /// <summary>
    /// A concept whose field is a <typeparamref name="T"/>: bool, int, float, or string for a
    /// hotkey list. A config table's accessors for the concept take this type, so a field of
    /// another type does not compile.
    /// </summary>
    public sealed class ConceptDescriptor<T> : ConceptDescriptor
    {
        internal ConceptDescriptor(string id, string section, string key, ConceptValueFamily family,
            IValueCodec<T> codec, string[] fileComment,
#if NULLABLE_ENABLED
            string? canonicalDefault,
#else
            string canonicalDefault,
#endif
            string defaultText, bool global)
            : base(id, section, key, family, fileComment, canonicalDefault, defaultText, global)
        {
            Codec = codec;
        }

        // Carries the schema's range.
        internal IValueCodec<T> Codec { get; }

#if NULLABLE_ENABLED
        internal override string? CodecError(byte[] text)
#else
        internal override string CodecError(byte[] text)
#endif
        {
            T value;
#if NULLABLE_ENABLED
            string? error;
#else
            string error;
#endif
            return Codec.TryParse(text, out value, out error) ? null : error;
        }
    }
}
