using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// The rows of one game's canonical config file, each bound to a property or field of
    /// <typeparamref name="TConfig"/>: <see cref="Apply"/> reads a parsed file into a config and
    /// <see cref="Render"/> writes a config as a canonical file. The C++ twin is
    /// cameraunlock::config::ConfigTable, and data/fixtures/canonical-ini/table holds both to the
    /// same bytes.
    /// <para>
    /// Concept rows take their section, key, codec, range and comment from
    /// <see cref="ConfigConcepts"/>; local rows name their own. A row's default is its value in the
    /// defaults instance. Modifiers apply to the last row added, or to the concept row
    /// <see cref="Select"/> names.
    /// </para>
    /// <para>
    /// Every check throws from the call that breaks it, ArgumentException for what the row
    /// declares: two rows with one key name anywhere in the file (ASCII case-insensitive), counting
    /// the ConfigFormat key core writes in [CameraUnlock]; a local section or key that is not
    /// PascalCase ASCII letters and digits; a local row in [CameraUnlock], or in a schema section
    /// that holds no canonical concept ([Sensitivity], [Inversion], [Reticle]) or that a
    /// non_canonical_keys group lists ([Deadzone]), or in a section
    /// spelled like a schema section or an earlier local section with other letter case; a local key
    /// that is a concept's key or alias, canonical, non-canonical or retired, under the schema's
    /// normalisation (<see cref="ConfigKeySchema.Resolve"/>), or a spelling the schema's
    /// non_canonical_keys lists; a local row with no comment that
    /// follows no local row of its section; a default its row cannot write; RotationEnabled and
    /// PositionEnabled both defaulting to false. <see cref="EnumCodec{TEnum}"/> already refuses a token that is
    /// not PascalCase. A modifier used on a row it does not apply to throws
    /// InvalidOperationException. A row that fails a check is not added.
    /// </para>
    /// </summary>
    /// <typeparam name="TConfig">A class, since a setter on a struct writes to a copy.</typeparam>
    public sealed class ConfigTable<TConfig> where TConfig : class
    {
        private const string StampSection = "CameraUnlock";
        private static readonly byte[] StampSectionBytes = Encoding.ASCII.GetBytes(StampSection);
        private static readonly byte[] Empty = new byte[0];
        private static readonly byte[] Crlf = { (byte)'\r', (byte)'\n' };

        private readonly Func<TConfig> defaults;
        private readonly TConfig checkDefaults;
        private readonly List<Row> rows = new List<Row>();
        private int last = -1;

        /// <param name="defaults">Makes a new config holding the defaults. It is called when the
        /// table is built and again by every Apply and Render, and must return a new instance with
        /// the same values each time.</param>
        /// <exception cref="ArgumentNullException"><paramref name="defaults"/> is null.</exception>
        /// <exception cref="InvalidOperationException"><paramref name="defaults"/> returned null.</exception>
        public ConfigTable(Func<TConfig> defaults)
        {
            if (defaults == null) throw new ArgumentNullException("defaults");
            this.defaults = defaults;
            checkDefaults = NewDefaults();
        }

        /// <summary>
        /// A concept row. The accessors' type is the concept's, so a field of another type does
        /// not compile.
        /// </summary>
        /// <exception cref="ArgumentNullException">An argument is null.</exception>
        /// <exception cref="ArgumentException">A check above fails.</exception>
        public ConfigTable<TConfig> Concept<T>(ConceptDescriptor<T> concept, Func<TConfig, T> get, Action<TConfig, T> set)
        {
            if (concept == null) throw new ArgumentNullException("concept");
            if (get == null) throw new ArgumentNullException("get");
            if (set == null) throw new ArgumentNullException("set");

            var row = new CodecRow<T>(concept.Section, concept.Key, ToArray(concept.FileComment), concept,
                concept.Family == ConceptValueFamily.Hotkey, concept.Codec, get, set);
            CheckUniqueKey(row);
            ConceptDescriptor descriptor = concept;
            if (descriptor == ConfigConcepts.RotationEnabled || descriptor == ConfigConcepts.PositionEnabled)
            {
                ConceptDescriptor other = descriptor == ConfigConcepts.RotationEnabled
                    ? (ConceptDescriptor)ConfigConcepts.PositionEnabled
                    : ConfigConcepts.RotationEnabled;
                foreach (Row earlier in rows)
                {
                    if (earlier.Concept == other && earlier.IsFalse(checkDefaults) && row.IsFalse(checkDefaults))
                    {
                        throw new ArgumentException(
                            "RotationEnabled and PositionEnabled both default to false, which is not a tracking mode", "concept");
                    }
                }
            }
            Add(row);
            return this;
        }

        /// <summary>
        /// A game-local row. The comment is written above the row; '\n' separates its lines. An
        /// empty comment is allowed when an earlier local row of the section is written above this
        /// one, whose comment covers both.
        /// </summary>
        /// <exception cref="ArgumentNullException">An argument is null.</exception>
        /// <exception cref="ArgumentException">A check above fails.</exception>
        public ConfigTable<TConfig> Local<T>(string section, string key, Func<TConfig, T> get, Action<TConfig, T> set,
            IValueCodec<T> codec, string comment)
        {
            if (section == null) throw new ArgumentNullException("section");
            if (key == null) throw new ArgumentNullException("key");
            if (get == null) throw new ArgumentNullException("get");
            if (set == null) throw new ArgumentNullException("set");
            if (codec == null) throw new ArgumentNullException("codec");
            if (comment == null) throw new ArgumentNullException("comment");

            string name = "[" + section + "] " + key;
            var row = new CodecRow<T>(section, key, CommentLines(comment, name), null, codec is HotkeyCodec, codec, get, set);
            CheckLocalRow(row);
            Add(row);
            return this;
        }

        /// <summary>
        /// Replaces the schema's comment on a concept row, where the game's unit or behaviour
        /// differs. '\n' separates lines; the text may not be empty.
        /// </summary>
        /// <exception cref="InvalidOperationException">There is no row, or it is a local row.</exception>
        /// <exception cref="ArgumentException">The text breaks the comment rules.</exception>
        public ConfigTable<TConfig> Comment(string text)
        {
            Row row = Last("Comment");
            if (row.Concept == null)
            {
                throw new InvalidOperationException(row.Name + " is a local row, which carries its comment in Local");
            }
            if (text == null) throw new ArgumentNullException("text");
            string[] lines = CommentLines(text, row.Name);
            if (lines.Length == 0) throw new ArgumentException(row.Name + " needs a comment", "text");
            row.Comment = lines;
            return this;
        }

        /// <summary>
        /// An inclusive range for an int, float or double local row: a value outside it is invalid,
        /// never clamped. A concept row has the schema's range.
        /// </summary>
        /// <exception cref="InvalidOperationException">There is no row, or it is a concept row or
        /// not an int, float or double row.</exception>
        /// <exception cref="ArgumentException">The bounds do not suit the row, or exclude its default.</exception>
        public ConfigTable<TConfig> Range(double min, double max)
        {
            Row row = Last("Range");
            if (row.Concept != null)
            {
                throw new InvalidOperationException(row.Name + " is a concept row, which has the schema's range");
            }
            Row ranged;
            try
            {
                ranged = row.WithRange(min, max);
            }
            catch (ArgumentException e)
            {
                throw new ArgumentException(row.Name + ": " + e.Message, e);
            }
            CheckDefault(ranged);
            rows[last] = ranged;
            return this;
        }

        /// <summary>
        /// Marks the row as data about the game rather than a taste: at its default it is written
        /// as a comment, <c>; Key=value</c>, so a later build's corrected default reaches everyone
        /// who never set it.
        /// </summary>
        /// <exception cref="InvalidOperationException">There is no row.</exception>
        public ConfigTable<TConfig> Engine()
        {
            Last("Engine").Engine = true;
            return this;
        }

        /// <summary>Marks the row as one the config owner's Save may change.</summary>
        /// <exception cref="InvalidOperationException">There is no row.</exception>
        public ConfigTable<TConfig> Writable()
        {
            Last("Writable").Writable = true;
            return this;
        }

        /// <summary>
        /// Makes the concept's row the one the next modifier applies to, for a table another
        /// method built.
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="concept"/> is null.</exception>
        /// <exception cref="InvalidOperationException">The table has no row for the concept.</exception>
        public ConfigTable<TConfig> Select(ConceptDescriptor concept)
        {
            if (concept == null) throw new ArgumentNullException("concept");
            for (int i = 0; i < rows.Count; i++)
            {
                if (rows[i].Concept == concept)
                {
                    last = i;
                    return this;
                }
            }
            throw new InvalidOperationException("the table has no row for " + concept.Id);
        }

        /// <summary>
        /// Reads a parsed canonical file into the table's rows of <paramref name="config"/>; members
        /// no row binds are left as they are. Every row starts from its default, so a key the file
        /// leaves out reads as the default with no diagnostic. A value its codec does not read keeps
        /// the default and draws <see cref="CanonicalDiagnosticKind.InvalidValue"/>. A section the
        /// table has no row in draws one UnknownSection, a key no row of a read section names one
        /// UnknownKey, and none is drawn in [CameraUnlock]. A key that names a row of the table in
        /// another section, or a concept row by an alias, draws MisplacedKey naming the row; one that
        /// names a retired concept draws RetiredKey, and one that names a concept the canonical format
        /// does not write, or is a spelling the schema's non_canonical_keys lists (a deadzone, a
        /// response curve), draws NonCanonicalConcept with the schema's reason; all three in any
        /// section. Any other key in a section a non_canonical_keys group lists ([Sensitivity],
        /// [Inversion], [Deadzone]) draws NonCanonicalConcept with that group's reason, beside the
        /// section's UnknownSection.
        /// No key takes its value from another. When the table binds RotationEnabled and
        /// PositionEnabled and both read false, both take their defaults and one NoTrackingMode names
        /// the lines that set them.
        /// </summary>
        /// <exception cref="ArgumentNullException">An argument is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="doc"/> is not readable.</exception>
        public ApplyReport Apply(CanonicalIni doc, TConfig config)
        {
            if (doc == null) throw new ArgumentNullException("doc");
            if (config == null) throw new ArgumentNullException("config");
            if (!doc.IsReadable)
            {
                throw new ArgumentException("Apply needs a readable document, and this one is " + doc.Status, "doc");
            }

            TConfig fresh = NewDefaults();
            foreach (Row row in rows) row.Assign(config, fresh);

            var diagnostics = new List<CanonicalDiagnostic>();
            var readFrom = new int[rows.Count];
            foreach (CanonicalSection section in doc.Sections)
            {
                if (CanonicalIni.EqualsAsciiIgnoreCase(section.Name, StampSectionBytes)) continue;
                bool known = false;
                foreach (Row row in rows)
                {
                    if (CanonicalIni.EqualsAsciiIgnoreCase(section.Name, row.SectionBytes)) known = true;
                }
                if (!known)
                {
                    diagnostics.Add(new CanonicalDiagnostic(CanonicalDiagnosticKind.UnknownSection, new[] { section.Line },
                        section.Name, Empty, Empty));
                }
                foreach (CanonicalValue value in section.Values)
                {
                    int found = -1;
                    for (int i = 0; known && i < rows.Count; i++)
                    {
                        if (CanonicalIni.EqualsAsciiIgnoreCase(section.Name, rows[i].SectionBytes)
                            && CanonicalIni.EqualsAsciiIgnoreCase(value.Key, rows[i].KeyBytes))
                        {
                            found = i;
                        }
                    }
                    if (found < 0)
                    {
                        ReportUnread(section, value, known, diagnostics);
                        continue;
                    }
#if NULLABLE_ENABLED
                    string? error = rows[found].Apply(value.Value, config);
#else
                    string error = rows[found].Apply(value.Value, config);
#endif
                    if (error == null)
                    {
                        readFrom[found] = value.Line;
                    }
                    else
                    {
                        diagnostics.Add(new CanonicalDiagnostic(CanonicalDiagnosticKind.InvalidValue, new[] { value.Line },
                            section.Name, value.Key, value.Value, error));
                    }
                }
            }

            int rotation = IndexOf(ConfigConcepts.RotationEnabled);
            int position = IndexOf(ConfigConcepts.PositionEnabled);
            if (rotation >= 0 && position >= 0 && rows[rotation].IsFalse(config) && rows[position].IsFalse(config))
            {
                var lines = new List<int>();
                if (readFrom[rotation] != 0) lines.Add(readFrom[rotation]);
                if (readFrom[position] != 0) lines.Add(readFrom[position]);
                lines.Sort();
                rows[rotation].Assign(config, fresh);
                rows[position].Assign(config, fresh);
                diagnostics.Add(new CanonicalDiagnostic(CanonicalDiagnosticKind.NoTrackingMode, lines.ToArray(), Empty,
                    Empty, Empty));
            }

            return new ApplyReport(Sorted(diagnostics));
        }

        /// <summary>
        /// Writes <paramref name="values"/> as a canonical file: the header comments, [CameraUnlock]
        /// with ConfigFormat, the schema sections the table has rows in, in the schema's order, each
        /// with its concept rows in the schema's concepts order and then its local rows in table
        /// order, then the local sections in table order. Each row is its comment lines as
        /// <c>; text</c>, then <c>Key=value</c>, or <c>; Key=value</c> for an Engine row holding its
        /// default. A blank line separates sections. CRLF line endings with a final CRLF, no byte
        /// order mark.
        /// </summary>
        /// <exception cref="ArgumentNullException">An argument is null.</exception>
        /// <exception cref="ArgumentException">A value its codec cannot write, naming the row; or a
        /// display name that is empty, has a leading or trailing space, or holds a character outside
        /// printable ASCII.</exception>
        public byte[] Render(TConfig values, RenderHeader header)
        {
            if (values == null) throw new ArgumentNullException("values");
            if (header == null) throw new ArgumentNullException("header");
            string name = header.DisplayName;
            if (name.Length == 0 || !IsPrintableAscii(name) || name[0] == ' ' || name[name.Length - 1] == ' ')
            {
                throw new ArgumentException("display name '" + name
                    + "' is not printable ASCII without a leading or trailing space", "header");
            }

            TConfig fresh = NewDefaults();
            var output = new List<byte>();
            Line(output, "; " + name + " head tracking settings.");
            Line(output, "; Comments start with ; and go on their own line. Text after a value is part of the value.");
            bool hotkeys = false;
            foreach (Row row in rows) hotkeys |= row.Hotkey;
            if (hotkeys)
            {
                Line(output, "; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; "
                    + "leave empty for none.");
            }
            Line(output, string.Empty);
            Line(output, "[" + StampSection + "]");
            Line(output, "; Written by the mod. Leave this section in place.");
            Line(output, CanonicalIni.FormatKeyText + "=" + CanonicalIni.ConfigFormat.ToString(CultureInfo.InvariantCulture));

            foreach (string section in ConfigConcepts.Sections)
            {
                var order = new List<Row>();
                foreach (ConceptDescriptor concept in ConfigConcepts.All)
                {
                    foreach (Row row in rows)
                    {
                        if (row.Concept == concept && row.Section == section) order.Add(row);
                    }
                }
                foreach (Row row in rows)
                {
                    if (row.Concept == null && row.Section == section) order.Add(row);
                }
                if (order.Count == 0) continue;
                Line(output, string.Empty);
                Line(output, "[" + section + "]");
                foreach (Row row in order) AppendRow(output, row, values, fresh);
            }

            var localSections = new List<string>();
            foreach (Row row in rows)
            {
                if (Array.IndexOf(ConfigConcepts.Sections, row.Section) < 0 && !localSections.Contains(row.Section))
                {
                    localSections.Add(row.Section);
                }
            }
            foreach (string section in localSections)
            {
                Line(output, string.Empty);
                Line(output, "[" + section + "]");
                foreach (Row row in rows)
                {
                    if (row.Section == section) AppendRow(output, row, values, fresh);
                }
            }
            return output.ToArray();
        }

        internal TConfig CreateDefaults()
        {
            return NewDefaults();
        }

        internal int RowCount
        {
            get { return rows.Count; }
        }

        internal string RowName(int row)
        {
            return rows[row].Name;
        }

        internal string RowSection(int row)
        {
            return rows[row].Section;
        }

        internal string RowKey(int row)
        {
            return rows[row].Key;
        }

        internal bool RowWritable(int row)
        {
            return rows[row].Writable;
        }

        internal bool RowEqual(int row, TConfig a, TConfig b)
        {
            return rows[row].Equal(a, b);
        }

        /// <exception cref="ArgumentException">The row's codec cannot write the value.</exception>
        internal byte[] RowRender(int row, TConfig config)
        {
            return rows[row].Render(config);
        }

        /// <summary>The row's value as a message shows it: its canonical text, or the value as .NET
        /// prints it when the codec cannot write it.</summary>
        internal string RowValueText(int row, TConfig config)
        {
            try
            {
                return Encoding.UTF8.GetString(rows[row].Render(config));
            }
            catch (ArgumentException)
            {
                return rows[row].Display(config);
            }
        }

        internal int RowOf(ConceptDescriptor concept)
        {
            return IndexOf(concept);
        }

        private TConfig NewDefaults()
        {
            TConfig made = defaults();
            if (made == null) throw new InvalidOperationException("the defaults factory returned null");
            return made;
        }

        private void Add(Row row)
        {
            CheckDefault(row);
            rows.Add(row);
            last = rows.Count - 1;
        }

        private void CheckDefault(Row row)
        {
            try
            {
                row.Render(checkDefaults);
            }
            catch (ArgumentException e)
            {
                throw new ArgumentException(row.Name + " has a default it cannot write: " + e.Message, e);
            }
        }

        private Row Last(string modifier)
        {
            if (last < 0) throw new InvalidOperationException(modifier + " needs a row: add or Select one first");
            return rows[last];
        }

        private int IndexOf(ConceptDescriptor concept)
        {
            for (int i = 0; i < rows.Count; i++)
            {
                if (rows[i].Concept == concept) return i;
            }
            return -1;
        }

        private void CheckUniqueKey(Row row)
        {
            foreach (Row earlier in rows)
            {
                if (CanonicalIni.EqualsAsciiIgnoreCase(earlier.KeyBytes, row.KeyBytes))
                {
                    throw new ArgumentException(row.Name + ": " + earlier.Name
                        + " already has that key, and a key name is used once in the file");
                }
            }
        }

        private void CheckLocalRow(Row row)
        {
            if (!IsPascalCase(row.Section))
            {
                throw new ArgumentException(row.Name + ": the section name is not PascalCase ASCII letters and digits", "section");
            }
            if (!IsPascalCase(row.Key))
            {
                throw new ArgumentException(row.Name + ": the key is not PascalCase ASCII letters and digits", "key");
            }
            if (CanonicalIni.EqualsAsciiIgnoreCase(row.SectionBytes, StampSectionBytes))
            {
                throw new ArgumentException(row.Name + ": [CameraUnlock] belongs to core, so a game-local row goes elsewhere",
                    "section");
            }
            foreach (string section in ConfigConcepts.Sections)
            {
                if (!EqualsAsciiIgnoreCase(row.Section, section)) continue;
                if (row.Section != section)
                {
                    throw new ArgumentException(row.Name + ": the schema spells this section [" + section + "]", "section");
                }
                bool canonical = false;
                foreach (ConceptDescriptor concept in ConfigConcepts.All)
                {
                    if (concept.Section == section) canonical = true;
                }
                if (!canonical)
                {
                    throw new ArgumentException(row.Name + ": [" + section
                        + "] holds none of the settings a canonical file writes, so it has no rows", "section");
                }
            }
            foreach (KeyValuePair<string, string> other in ConfigConcepts.NonCanonicalSectionReasons)
            {
                if (EqualsAsciiIgnoreCase(row.Section, other.Key))
                {
                    throw new ArgumentException(row.Name + ": [" + row.Section + "] holds only settings a canonical file "
                        + "does not carry, so it has no rows: " + other.Value, "section");
                }
            }
            foreach (Row earlier in rows)
            {
                if (EqualsAsciiIgnoreCase(earlier.Section, row.Section) && earlier.Section != row.Section)
                {
                    throw new ArgumentException(row.Name + ": " + earlier.Name + " spells this section [" + earlier.Section + "]",
                        "section");
                }
            }
#if NULLABLE_ENABLED
            string? canonicalKey = ConfigKeySchema.Resolve(row.Key);
#else
            string canonicalKey = ConfigKeySchema.Resolve(row.Key);
#endif
            if (canonicalKey != null)
            {
                throw new ArgumentException(row.Name + ": " + row.Key + " is the key or an alias of the schema concept '"
                    + canonicalKey + "', so a game-local row cannot use it", "key");
            }
            string nonCanonicalReason;
            if (ConfigConcepts.NonCanonicalKeyReasons.TryGetValue(ConfigKeySchema.Normalize(row.Key), out nonCanonicalReason))
            {
                throw new ArgumentException(row.Name + ": " + row.Key + " names a setting a canonical file does not carry, so a "
                    + "game-local row cannot use it: " + nonCanonicalReason, "key");
            }
            if (CanonicalIni.EqualsAsciiIgnoreCase(row.KeyBytes, CanonicalIni.FormatKey))
            {
                throw new ArgumentException(row.Name + ": [CameraUnlock] ConfigFormat already has that key, and a key name is "
                    + "used once in the file", "key");
            }
            CheckUniqueKey(row);
            if (row.Comment.Length == 0)
            {
                bool covered = false;
                foreach (Row earlier in rows)
                {
                    if (earlier.Concept == null && earlier.Section == row.Section) covered = true;
                }
                if (!covered)
                {
                    throw new ArgumentException(row.Name + " needs a comment: only a row written below a game-local row of its "
                        + "section may share that row's comment", "comment");
                }
            }
        }

        // The row a key names outside its own section or spelling: a row's key in any section, or
        // a concept row's key or alias. Every key name is used once in a file, so there is at most one.
#if NULLABLE_ENABLED
        private Row? MisplacedRow(byte[] key, string? canonical)
#else
        private Row MisplacedRow(byte[] key, string canonical)
#endif
        {
            foreach (Row row in rows)
            {
                if (CanonicalIni.EqualsAsciiIgnoreCase(row.KeyBytes, key)) return row;
                if (canonical != null && row.Concept != null && ConfigKeySchema.Normalize(row.Key) == canonical) return row;
            }
            return null;
        }

        private void ReportUnread(CanonicalSection section, CanonicalValue value, bool reportUnknown,
            List<CanonicalDiagnostic> diagnostics)
        {
#if NULLABLE_ENABLED
            string? canonical = ConfigKeySchema.Resolve(Encoding.UTF8.GetString(value.Key));
            string? reason = null;
            Row? misplaced = MisplacedRow(value.Key, canonical);
#else
            string canonical = ConfigKeySchema.Resolve(Encoding.UTF8.GetString(value.Key));
            string reason = null;
            Row misplaced = MisplacedRow(value.Key, canonical);
#endif
            if (misplaced != null)
            {
                diagnostics.Add(new CanonicalDiagnostic(CanonicalDiagnosticKind.MisplacedKey, new[] { value.Line },
                    section.Name, value.Key, value.Value, misplaced.Name));
                return;
            }
            if (canonical != null && ConfigKeySchema.IsRetired(canonical))
            {
                diagnostics.Add(new CanonicalDiagnostic(CanonicalDiagnosticKind.RetiredKey, new[] { value.Line }, section.Name,
                    value.Key, value.Value));
                return;
            }
            bool nonCanonical = canonical != null
                ? ConfigConcepts.NonCanonicalReasons.TryGetValue(canonical, out reason)
                : ConfigConcepts.NonCanonicalKeyReasons.TryGetValue(ConfigKeySchema.Normalize(Encoding.UTF8.GetString(value.Key)), out reason);
            if (!nonCanonical)
            {
                foreach (KeyValuePair<string, string> other in ConfigConcepts.NonCanonicalSectionReasons)
                {
                    if (!CodecText.EqualsAsciiIgnoreCase(section.Name, other.Key)) continue;
                    reason = other.Value;
                    nonCanonical = true;
                }
            }
            if (nonCanonical)
            {
                diagnostics.Add(new CanonicalDiagnostic(CanonicalDiagnosticKind.NonCanonicalConcept, new[] { value.Line },
                    section.Name, value.Key, value.Value, reason));
                return;
            }
            if (reportUnknown)
            {
                diagnostics.Add(new CanonicalDiagnostic(CanonicalDiagnosticKind.UnknownKey, new[] { value.Line }, section.Name,
                    value.Key, value.Value));
            }
        }

        // Stable: List<T>.Sort is not, so the index each diagnostic was added at breaks ties.
        private static CanonicalDiagnostic[] Sorted(List<CanonicalDiagnostic> diagnostics)
        {
            var order = new List<KeyValuePair<int, CanonicalDiagnostic>>();
            for (int i = 0; i < diagnostics.Count; i++)
            {
                order.Add(new KeyValuePair<int, CanonicalDiagnostic>(i, diagnostics[i]));
            }
            order.Sort((a, b) =>
            {
                int byLine = a.Value.Lines[0].CompareTo(b.Value.Lines[0]);
                if (byLine != 0) return byLine;
                int byKind = ((int)a.Value.Kind).CompareTo((int)b.Value.Kind);
                return byKind != 0 ? byKind : a.Key.CompareTo(b.Key);
            });
            var sorted = new CanonicalDiagnostic[order.Count];
            for (int i = 0; i < order.Count; i++) sorted[i] = order[i].Value;
            return sorted;
        }

        private static void AppendRow(List<byte> output, Row row, TConfig values, TConfig fresh)
        {
            foreach (string line in row.Comment) Line(output, "; " + line);
            byte[] value;
            try
            {
                value = row.Render(values);
            }
            catch (ArgumentException e)
            {
                throw new ArgumentException(row.Name + ": " + e.Message, "values", e);
            }
            if (row.Engine && row.Equal(values, fresh)) output.AddRange(Encoding.ASCII.GetBytes("; "));
            output.AddRange(row.KeyBytes);
            output.Add((byte)'=');
            output.AddRange(value);
            output.AddRange(Crlf);
        }

        private static void Line(List<byte> output, string ascii)
        {
            output.AddRange(Encoding.ASCII.GetBytes(ascii));
            output.AddRange(Crlf);
        }

        private static string[] CommentLines(string text, string row)
        {
            if (text.Length == 0) return new string[0];
            string[] lines = text.Split('\n');
            for (int i = 0; i < lines.Length; i++)
            {
                string number = (i + 1).ToString(CultureInfo.InvariantCulture);
                string line = lines[i];
                if (line.Length == 0) throw new ArgumentException(row + ": comment line " + number + " is empty", "comment");
                if (!IsPrintableAscii(line))
                {
                    throw new ArgumentException(row + ": comment line " + number
                        + " holds a character outside printable ASCII, and a canonical file is ASCII", "comment");
                }
                if (line[0] == ' ' || line[line.Length - 1] == ' ')
                {
                    throw new ArgumentException(row + ": comment line " + number + " starts or ends with a space", "comment");
                }
            }
            return lines;
        }

        private static string[] ToArray(IList<string> lines)
        {
            var array = new string[lines.Count];
            lines.CopyTo(array, 0);
            return array;
        }

        private static bool IsPrintableAscii(string text)
        {
            foreach (char c in text)
            {
                if (c < 0x20 || c > 0x7E) return false;
            }
            return true;
        }

        private static bool IsPascalCase(string name)
        {
            if (name.Length == 0 || name[0] < 'A' || name[0] > 'Z') return false;
            foreach (char c in name)
            {
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
            }
            return true;
        }

        private static bool EqualsAsciiIgnoreCase(string a, string b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++)
            {
                if (CodecText.FoldAscii(a[i]) != CodecText.FoldAscii(b[i])) return false;
            }
            return true;
        }

        private abstract class Row
        {
            protected Row(string section, string key, string[] comment,
#if NULLABLE_ENABLED
                ConceptDescriptor? concept,
#else
                ConceptDescriptor concept,
#endif
                bool hotkey)
            {
                Section = section;
                Key = key;
                SectionBytes = Encoding.ASCII.GetBytes(section);
                KeyBytes = Encoding.ASCII.GetBytes(key);
                Comment = comment;
                Concept = concept;
                Hotkey = hotkey;
            }

            public string Section { get; }

            public string Key { get; }

            public byte[] SectionBytes { get; }

            public byte[] KeyBytes { get; }

            public string[] Comment { get; set; }

#if NULLABLE_ENABLED
            public ConceptDescriptor? Concept { get; }
#else
            public ConceptDescriptor Concept { get; }
#endif

            public bool Hotkey { get; }

            public bool Engine { get; set; }

            public bool Writable { get; set; }

            public string Name
            {
                get { return "[" + Section + "] " + Key; }
            }

            // The codec's error, or null when the text was read into the config.
#if NULLABLE_ENABLED
            public abstract string? Apply(byte[] text, TConfig config);
#else
            public abstract string Apply(byte[] text, TConfig config);
#endif

            public abstract byte[] Render(TConfig config);

            public abstract string Display(TConfig config);

            public abstract bool Equal(TConfig a, TConfig b);

            public abstract void Assign(TConfig to, TConfig from);

            public abstract bool IsFalse(TConfig config);

            public abstract Row WithRange(double min, double max);
        }

        private sealed class CodecRow<T> : Row
        {
            // Range on an int row takes whole numbers no larger in size than 2^53, which a double
            // holds exactly.
            private const double LargestExactWhole = 9007199254740992.0;

            private readonly IValueCodec<T> codec;
            private readonly Func<TConfig, T> get;
            private readonly Action<TConfig, T> set;

            public CodecRow(string section, string key, string[] comment,
#if NULLABLE_ENABLED
                ConceptDescriptor? concept,
#else
                ConceptDescriptor concept,
#endif
                bool hotkey, IValueCodec<T> codec, Func<TConfig, T> get, Action<TConfig, T> set)
                : base(section, key, comment, concept, hotkey)
            {
                this.codec = codec;
                this.get = get;
                this.set = set;
            }

#if NULLABLE_ENABLED
            public override string? Apply(byte[] text, TConfig config)
#else
            public override string Apply(byte[] text, TConfig config)
#endif
            {
                T value;
#if NULLABLE_ENABLED
                string? error;
#else
                string error;
#endif
                if (!codec.TryParse(text, out value, out error)) return error;
                set(config, value);
                return null;
            }

            public override byte[] Render(TConfig config)
            {
                return codec.Render(get(config));
            }

            public override string Display(TConfig config)
            {
                T held = get(config);
                if (held == null) return "null";
                object value = held;
                var items = value as System.Collections.IEnumerable;
                if (items != null && !(value is string))
                {
                    var parts = new List<string>();
                    foreach (object item in items) parts.Add(Convert.ToString(item, CultureInfo.InvariantCulture));
                    return string.Join(", ", parts.ToArray());
                }
                return Convert.ToString(value, CultureInfo.InvariantCulture);
            }

            public override bool Equal(TConfig a, TConfig b)
            {
                return codec.Equal(get(a), get(b));
            }

            public override void Assign(TConfig to, TConfig from)
            {
                set(to, get(from));
            }

            public override bool IsFalse(TConfig config)
            {
                if (get(config) is bool flag) return !flag;
                throw new InvalidOperationException(Name + " does not hold a bool");
            }

            public override Row WithRange(double min, double max)
            {
                object ranged;
                if (codec is IntCodec)
                {
                    bool representable = min >= -LargestExactWhole && min <= LargestExactWhole
                        && max >= -LargestExactWhole && max <= LargestExactWhole;
                    if (!representable || min != System.Math.Floor(min) || max != System.Math.Floor(max)
                        || min < int.MinValue || max > int.MaxValue)
                    {
                        throw new ArgumentException("Range(" + min.ToString(CultureInfo.InvariantCulture) + ", "
                            + max.ToString(CultureInfo.InvariantCulture) + ") is not two whole numbers the field's type holds");
                    }
                    ranged = new IntCodec((int)min, (int)max);
                }
                else if (codec is FloatCodec)
                {
                    ranged = new FloatCodec((float)min, (float)max);
                }
                else if (codec is DoubleCodec)
                {
                    ranged = new DoubleCodec(min, max);
                }
                else
                {
                    throw new InvalidOperationException(Name + ": Range applies to an int, float or double row");
                }
                var row = new CodecRow<T>(Section, Key, Comment, Concept, Hotkey, (IValueCodec<T>)ranged, get, set);
                row.Engine = Engine;
                row.Writable = Writable;
                return row;
            }
        }
    }
}
