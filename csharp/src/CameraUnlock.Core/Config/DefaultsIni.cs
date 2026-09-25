using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;
using CameraUnlock.Core.Input;
using CameraUnlock.Core.Tracking;

namespace CameraUnlock.Core.Config
{
    /// <summary>
    /// Defaults.ini, the file a concept row that is not PerGame takes its default from: core's
    /// table of every canonical concept, the bytes a new file holds, and the reader. The C++ twin
    /// is cameraunlock/config/defaults_ini.h, and data/fixtures/canonical-ini/global holds both to
    /// the same bytes and lines.
    /// </summary>
    internal static class DefaultsIni
    {
        private static readonly string[] Header =
        {
            "; CameraUnlock head tracking defaults, read by every head tracking mod that keeps its",
            "; settings in CameraUnlock.ini. A game uses the value here for each setting its",
            "; CameraUnlock.ini sets to default. A value in a game's CameraUnlock.ini changes that game",
            "; only. The mods never change this file.",
            "; Comments start with ; and go on their own line. Text after a value is part of the value.",
            "; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.",
            "; Only these key names are read here: A to Z, Alpha0 to Alpha9, F1 to F24, Keypad0 to Keypad9,",
            "; KeypadPeriod, KeypadDivide, KeypadMultiply, KeypadMinus, KeypadPlus, UpArrow, DownArrow,",
            "; LeftArrow, RightArrow, Insert, Delete, Home, End, PageUp, PageDown, Backspace, Tab, Return,",
            "; Space, Escape, Pause, Print, Menu, Numlock, CapsLock, ScrollLock, LeftShift, RightShift,",
            "; LeftControl, RightControl, LeftAlt, RightAlt, LeftWindows, RightWindows. A value holding any",
            "; other key makes every game use its built-in keys for that action.",
        };

        /// <summary>
        /// Core's global table: <see cref="HeadTrackingConfigTable"/> naming every canonical concept,
        /// whose defaults are the built-in values a new Defaults.ini holds.
        /// </summary>
        internal static ConfigTable<HeadTrackingConfigData> Table()
        {
            return HeadTrackingConfigTable.Create(ConfigConcepts.All);
        }

        /// <summary>
        /// The bytes a new Defaults.ini holds: the global table's defaults, the four hotkey lists at
        /// their canonical_default, every row written as its value (CollisionChannel too, since a
        /// commented line here would only ever mean the built-in), under Defaults.ini's own header.
        /// </summary>
        internal static byte[] Render()
        {
            ConfigTable<HeadTrackingConfigData> table = Table();
            return table.RenderValues(table.CreateDefaults(), Header);
        }

        /// <summary>
        /// Reads Defaults.ini's bytes. Pure, and nothing in the bytes makes it throw.
        /// <para>
        /// A file saved as UTF-16 or holding a NUL is unreadable. Any other file is read by the
        /// canonical reader, whatever its stamp: no [CameraUnlock], or a ConfigFormat missing, zero
        /// or not a number, draws nothing, and a newer ConfigFormat draws <see cref="DefaultsIniSnapshot.FormatLine"/>.
        /// Each canonical concept is found by its section and key, ASCII case-insensitively, the last
        /// occurrence winning; an alias, a key in another section and a key no concept has are not
        /// read and draw nothing. A value is refused when the concept's codec with the schema's range
        /// does not read it, <c>default</c> included, and a hotkey list also when an item's key is
        /// not one of the names with a Windows virtual-key code, which every mod reads. Then the
        /// tracking-mode pair: each of RotationEnabled and PositionEnabled is its accepted value, or
        /// the built-in when absent; when either is refused, or the two name no tracking mode, both
        /// are refused together.
        /// </para>
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="bytes"/> is null.</exception>
        internal static DefaultsIniSnapshot Read(byte[] bytes)
        {
            if (bytes == null) throw new ArgumentNullException("bytes");

            var values = new DefaultsIniValue[ConfigConcepts.All.Length];
            CanonicalIni doc = CanonicalIni.Parse(bytes);
            if (!doc.IsReadable)
            {
                for (int i = 0; i < values.Length; i++) values[i] = DefaultsIniValue.Absent;
                string why = doc.Status == CanonicalReadStatus.Utf16
                    ? "it is saved as UTF-16; save it as ANSI or UTF-8"
                    : "line " + Number(doc.UnreadableLine) + " holds a NUL byte";
                return new DefaultsIniSnapshot(why, null, values, false);
            }

#if NULLABLE_ENABLED
            string? formatLine = null;
#else
            string formatLine = null;
#endif
            foreach (CanonicalDiagnostic diagnostic in doc.Diagnostics)
            {
                if (diagnostic.Kind != CanonicalDiagnosticKind.ConfigFormatNewer) continue;
                formatLine = "Defaults.ini: line " + Number(diagnostic.Lines[0]) + ": " + Text(diagnostic.Key) + "="
                    + Text(diagnostic.Value) + " was written by a newer version of the mod. This version reads format "
                    + Number(CanonicalIni.ConfigFormat) + ".";
            }

            for (int i = 0; i < values.Length; i++) values[i] = ReadValue(doc, ConfigConcepts.All[i]);

            int rotation = Array.IndexOf(ConfigConcepts.All, ConfigConcepts.RotationEnabled);
            int position = Array.IndexOf(ConfigConcepts.All, ConfigConcepts.PositionEnabled);
#if NULLABLE_ENABLED
            string? pairReason = PairReason(values[rotation], values[position]);
#else
            string pairReason = PairReason(values[rotation], values[position]);
#endif
            if (pairReason != null)
            {
                foreach (int i in new[] { rotation, position })
                {
                    DefaultsIniValue held = values[i];
                    if (held.State == DefaultsIniValueState.Absent) continue;
                    values[i] = new DefaultsIniValue(DefaultsIniValueState.Refused, held.Line, held.Section, held.Key, held.Value,
                        pairReason);
                }
            }
            return new DefaultsIniSnapshot(null, formatLine, values, pairReason != null);
        }

        /// <summary>
        /// The log line for a refused value that a game would take from Defaults.ini, e.g.
        /// <c>Defaults.ini: line 12: [Hotkeys] ToggleKey=Mouse4 is not read (Mouse4 is not one of the
        /// key names this file takes), so the built-in End, Ctrl+Shift+Y is used.</c>
        /// </summary>
        /// <param name="builtIn">The game's built-in value for the row, as its codec writes it.</param>
        /// <exception cref="ArgumentNullException">An argument is null.</exception>
        /// <exception cref="ArgumentException"><paramref name="value"/> is not refused.</exception>
        internal static string RefusedLine(DefaultsIniValue value, string builtIn)
        {
            if (value == null) throw new ArgumentNullException("value");
            if (builtIn == null) throw new ArgumentNullException("builtIn");
            if (value.State != DefaultsIniValueState.Refused)
            {
                throw new ArgumentException("the value is " + value.State + ", and only a refused value has a line", "value");
            }
            return "Defaults.ini: line " + Number(value.Line) + ": " + Setting(value) + " is not read (" + value.Reason
                + "), so the built-in " + builtIn + " is used.";
        }

        /// <summary>
        /// The one log line for a refused tracking-mode pair, naming each of the two lines the file
        /// holds, e.g. <c>Defaults.ini: lines 2 and 4: [General] RotationEnabled=false and
        /// [Position] PositionEnabled=false are not read (both false is not a tracking mode), so the
        /// built-in RotationEnabled=true and PositionEnabled=true are used.</c>
        /// </summary>
        /// <exception cref="ArgumentNullException">An argument is null.</exception>
        /// <exception cref="ArgumentException">The snapshot's pair is not refused.</exception>
        internal static string PairLine(DefaultsIniSnapshot snapshot, string rotationBuiltIn, string positionBuiltIn)
        {
            if (snapshot == null) throw new ArgumentNullException("snapshot");
            if (rotationBuiltIn == null) throw new ArgumentNullException("rotationBuiltIn");
            if (positionBuiltIn == null) throw new ArgumentNullException("positionBuiltIn");
            if (!snapshot.PairRefused) throw new ArgumentException("the snapshot's tracking-mode pair is not refused", "snapshot");

            var held = new List<DefaultsIniValue>();
            foreach (ConceptDescriptor concept in new ConceptDescriptor[] { ConfigConcepts.RotationEnabled, ConfigConcepts.PositionEnabled })
            {
                DefaultsIniValue value = snapshot.Value(concept);
                if (value.State != DefaultsIniValueState.Absent) held.Add(value);
            }
            if (held.Count == 2 && held[1].Line < held[0].Line) held.Reverse();

            string text = held.Count == 1
                ? "line " + Number(held[0].Line) + ": " + Setting(held[0]) + " is not read"
                : "lines " + Number(held[0].Line) + " and " + Number(held[1].Line) + ": " + Setting(held[0]) + " and "
                    + Setting(held[1]) + " are not read";
            return "Defaults.ini: " + text + " (" + held[0].Reason + "), so the built-in RotationEnabled=" + rotationBuiltIn
                + " and PositionEnabled=" + positionBuiltIn + " are used.";
        }

        private static DefaultsIniValue ReadValue(CanonicalIni doc, ConceptDescriptor concept)
        {
#if NULLABLE_ENABLED
            CanonicalSection? section = doc.FindSection(concept.Section);
            CanonicalValue? found = section == null ? null : section.Find(concept.Key);
#else
            CanonicalSection section = doc.FindSection(concept.Section);
            CanonicalValue found = section == null ? null : section.Find(concept.Key);
#endif
            if (section == null || found == null) return DefaultsIniValue.Absent;

#if NULLABLE_ENABLED
            string? error = concept.Family == ConceptValueFamily.Hotkey ? KeyNameError(found.Value) : null;
#else
            string error = concept.Family == ConceptValueFamily.Hotkey ? KeyNameError(found.Value) : null;
#endif
            if (error == null) error = concept.CodecError(found.Value);
            return error == null
                ? new DefaultsIniValue(DefaultsIniValueState.Accepted, found.Line, section.Name, found.Key, found.Value, string.Empty)
                : new DefaultsIniValue(DefaultsIniValueState.Refused, found.Line, section.Name, found.Key, found.Value, error);
        }

        // The first item whose key is not a name with a Windows virtual-key code, or null. An item
        // this cannot split into modifiers and a key is left to the codec, which names what it
        // expected.
#if NULLABLE_ENABLED
        private static string? KeyNameError(byte[] text)
#else
        private static string KeyNameError(byte[] text)
#endif
        {
            foreach (string item in Encoding.UTF8.GetString(text).Split(','))
            {
                string[] tokens = item.Split('+');
                string key = tokens[tokens.Length - 1].Trim(' ', '\t');
                if (key.Length == 0 || IsModifier(key) || IsVirtualKeyName(key)) continue;
                return key + " is not one of the key names this file takes";
            }
            return null;
        }

        private static bool IsModifier(string token)
        {
            foreach (KeyNames.Modifier modifier in KeyNames.Modifiers)
            {
                if (EqualsAsciiIgnoreCase(token, modifier.Name)) return true;
            }
            return false;
        }

        private static bool IsVirtualKeyName(string token)
        {
            foreach (KeyNames.Key key in KeyNames.Keys)
            {
                if (key.VirtualKey != 0 && EqualsAsciiIgnoreCase(token, key.Name)) return true;
            }
            return false;
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

        // Why the pair is refused, or null when it names a tracking mode.
#if NULLABLE_ENABLED
        private static string? PairReason(DefaultsIniValue rotation, DefaultsIniValue position)
#else
        private static string PairReason(DefaultsIniValue rotation, DefaultsIniValue position)
#endif
        {
            var refused = new List<string>();
            if (rotation.State == DefaultsIniValueState.Refused) refused.Add(Text(rotation.Key) + ": " + rotation.Reason);
            if (position.State == DefaultsIniValueState.Refused) refused.Add(Text(position.Key) + ": " + position.Reason);
            if (refused.Count > 0)
            {
                return string.Join("; ", refused.ToArray()) + ", and the two are read together as the tracking mode";
            }
            bool rotationEnabled = Flag(ConfigConcepts.RotationEnabled, rotation);
            bool positionEnabled = Flag(ConfigConcepts.PositionEnabled, position);
            return TrackingModeChannels.Decode(rotationEnabled, positionEnabled) == null ? "both false is not a tracking mode" : null;
        }

        // An accepted value as the codec reads it, or the built-in for an absent one.
        private static bool Flag(ConceptDescriptor<bool> concept, DefaultsIniValue value)
        {
            byte[] text = value.State == DefaultsIniValueState.Accepted ? value.Value : Encoding.ASCII.GetBytes(concept.DefaultText);
            bool flag;
#if NULLABLE_ENABLED
            string? error;
#else
            string error;
#endif
            if (!concept.Codec.TryParse(text, out flag, out error))
            {
                throw new InvalidOperationException(concept.Id + ": '" + Encoding.ASCII.GetString(text) + "' does not read: " + error);
            }
            return flag;
        }

        private static string Setting(DefaultsIniValue value)
        {
            return "[" + Text(value.Section) + "] " + Text(value.Key) + "=" + Text(value.Value);
        }

        private static string Text(byte[] bytes)
        {
            return Encoding.UTF8.GetString(bytes);
        }

        private static string Number(int n)
        {
            return n.ToString(CultureInfo.InvariantCulture);
        }
    }
}
