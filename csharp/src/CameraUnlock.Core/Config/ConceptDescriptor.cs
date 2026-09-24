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
        internal ConceptDescriptor(int index, string id, string section, string key, ConceptValueFamily family,
            string[] fileComment,
#if NULLABLE_ENABLED
            string? canonicalDefault)
#else
            string canonicalDefault)
#endif
        {
            Index = index;
            Id = id;
            Section = section;
            Key = key;
            Family = family;
            FileComment = new ReadOnlyCollection<string>(fileComment);
            CanonicalDefault = canonicalDefault;
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

        /// <summary>The binding list a canonical file starts with, where the schema gives one; else null.</summary>
#if NULLABLE_ENABLED
        public string? CanonicalDefault { get; }
#else
        public string CanonicalDefault { get; }
#endif

        // The position in the schema's concepts array among the canonical concepts, which orders
        // a section's concept rows.
        internal int Index { get; }
    }

    /// <summary>
    /// A concept whose field is a <typeparamref name="T"/>: bool, int, float, or string for a
    /// hotkey list. A config table's accessors for the concept take this type, so a field of
    /// another type does not compile.
    /// </summary>
    public sealed class ConceptDescriptor<T> : ConceptDescriptor
    {
        internal ConceptDescriptor(int index, string id, string section, string key, ConceptValueFamily family,
            IValueCodec<T> codec, string[] fileComment,
#if NULLABLE_ENABLED
            string? canonicalDefault)
#else
            string canonicalDefault)
#endif
            : base(index, id, section, key, family, fileComment, canonicalDefault)
        {
            Codec = codec;
        }

        // Carries the schema's range.
        internal IValueCodec<T> Codec { get; }
    }
}
