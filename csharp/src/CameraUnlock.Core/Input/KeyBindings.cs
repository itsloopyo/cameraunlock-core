using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace CameraUnlock.Core.Input
{
    /// <summary>
    /// Reads and writes a hotkey value as Unity mods write it: <c>End</c>,
    /// <c>End, Ctrl+Shift+Y</c>, or empty for unbound. The C++ ParseKeyBindings and
    /// FormatKeyBindings read the same syntax with Windows virtual-key codes, and
    /// data/fixtures/canonical-ini/keys holds both to it.
    /// </summary>
    public static class KeyBindings
    {
        /// <summary>
        /// Reads a hotkey value. Pure, and nothing in the text makes it throw.
        /// <para>
        /// A value that is empty after trimming spaces and tabs is an empty list. Otherwise it
        /// is split at ',' into items, each trimmed and non-empty. An item is split at '+'
        /// into trimmed tokens: any of Ctrl, Shift and Alt, each at most once and in any
        /// order, then exactly one key. A key is a name from data/keys.json that has a Unity
        /// KeyCode value; a number is not read, because a KeyCode value is not a Windows
        /// virtual-key code and the same number would mean another key in a native mod.
        /// Names and modifiers read ASCII case-insensitively. A list naming the same binding
        /// twice is invalid.
        /// </para>
        /// </summary>
        /// <param name="bindings">The list read, empty on failure.</param>
        /// <param name="error">What was expected, or null when the value was read.</param>
        /// <exception cref="ArgumentNullException"><paramref name="text"/> is null.</exception>
#if NULLABLE_ENABLED
        public static bool TryParse(string text, out KeyBinding[] bindings, out string? error)
#else
        public static bool TryParse(string text, out KeyBinding[] bindings, out string error)
#endif
        {
            if (text == null) throw new ArgumentNullException("text");

            bindings = new KeyBinding[0];
            error = null;
            string value = Trim(text);
            if (value.Length == 0) return true;

            var read = new List<KeyBinding>();
            string[] items = text.Split(',');
            for (int n = 0; n < items.Length; n++)
            {
                string item = Trim(items[n]);
                if (item.Length == 0)
                {
                    error = "item " + (n + 1).ToString(CultureInfo.InvariantCulture) + " of " + Quote(value)
                        + " is empty: expected a key such as End or Ctrl+Shift+Y between commas";
                    return false;
                }

                string[] tokens = item.Split('+');
                KeyModifiers modifiers = KeyModifiers.None;
                for (int t = 0; t + 1 < tokens.Length; t++)
                {
                    int index = ModifierIndex(Trim(tokens[t]));
                    if (index < 0)
                    {
                        error = Quote(item) + ": expected Ctrl, Shift or Alt before each '+' and one key after the last";
                        return false;
                    }
                    var modifier = (KeyModifiers)(1 << index);
                    if ((modifiers & modifier) != 0)
                    {
                        error = Quote(item) + " names " + KeyNames.Modifiers[index].Name
                            + " twice: expected each modifier at most once";
                        return false;
                    }
                    modifiers |= modifier;
                }

                string key = Trim(tokens[tokens.Length - 1]);
                if (key.Length == 0)
                {
                    error = Quote(item) + ": expected Ctrl, Shift or Alt before each '+' and one key after the last";
                    return false;
                }
                if (ModifierIndex(key) >= 0)
                {
                    error = Quote(item) + " has no key: expected a key after the modifiers";
                    return false;
                }
                int code = ReadKey(key, out error);
                if (code == 0) return false;

                var binding = new KeyBinding(modifiers, code);
                if (read.Contains(binding))
                {
                    error = Quote(item) + " is listed twice: expected each binding once";
                    return false;
                }
                read.Add(binding);
            }

            bindings = read.ToArray();
            return true;
        }

        /// <summary>
        /// The canonical text of a hotkey list, as <see cref="TryParse"/> reads it back:
        /// modifiers as <c>Ctrl+Shift+Alt+</c> in that order, the key's name from
        /// data/keys.json, items joined by ", ". An empty list is "".
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="bindings"/> is null.</exception>
        /// <exception cref="ArgumentException">A code has no name in the table, or a binding is listed twice; neither reads back.</exception>
        public static string Format(IList<KeyBinding> bindings)
        {
            if (bindings == null) throw new ArgumentNullException("bindings");

            var text = new StringBuilder();
            for (int n = 0; n < bindings.Count; n++)
            {
                KeyBinding binding = bindings[n];
                for (int earlier = 0; earlier < n; earlier++)
                {
                    if (bindings[earlier] == binding)
                    {
                        throw new ArgumentException("binding " + (n + 1).ToString(CultureInfo.InvariantCulture)
                            + " repeats an earlier one", "bindings");
                    }
                }

                if (n > 0) text.Append(", ");
                for (int i = 0; i < KeyNames.Modifiers.Length; i++)
                {
                    if (((int)binding.Modifiers & (1 << i)) != 0) text.Append(KeyNames.Modifiers[i].Name).Append('+');
                }
                text.Append(NameOf(binding.UnityKeyCode));
            }
            return text.ToString();
        }

        private static string NameOf(int unityKeyCode)
        {
            foreach (KeyNames.Key key in KeyNames.Keys)
            {
                if (key.UnityKeyCode != 0 && key.UnityKeyCode == unityKeyCode) return key.Name;
            }
            throw new ArgumentException("Unity key code " + unityKeyCode.ToString(CultureInfo.InvariantCulture)
                + " has no name in data/keys.json", "bindings");
        }

        // The Unity code of one key token, or 0 with the error set.
#if NULLABLE_ENABLED
        private static int ReadKey(string token, out string? error)
#else
        private static int ReadKey(string token, out string error)
#endif
        {
            error = null;
            if (token.Length >= 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X'))
            {
                error = Quote(token) + " is a key code: expected a key name such as End or F9, "
                    + "because a Unity mod reads key names only";
                return 0;
            }

            int index = KeyIndex(token);
            if (index < 0)
            {
                error = Quote(token) + " is not a key name: expected a name such as End, F9 or A";
                return 0;
            }
            if (KeyNames.Keys[index].UnityKeyCode == 0)
            {
                error = Quote(token) + " has no Unity key code: expected a key a Unity game can report";
                return 0;
            }
            return KeyNames.Keys[index].UnityKeyCode;
        }

        private static int KeyIndex(string token)
        {
            for (int i = 0; i < KeyNames.Keys.Length; i++)
            {
                if (EqualsAsciiIgnoreCase(token, KeyNames.Keys[i].Name)) return i;
            }
            foreach (KeyNames.Alias alias in KeyNames.Aliases)
            {
                if (EqualsAsciiIgnoreCase(token, alias.Name)) return alias.KeyIndex;
            }
            return -1;
        }

        // The index into KeyNames.Modifiers of the modifier a token names, or -1. Its flag is 1 << index.
        private static int ModifierIndex(string token)
        {
            for (int i = 0; i < KeyNames.Modifiers.Length; i++)
            {
                if (EqualsAsciiIgnoreCase(token, KeyNames.Modifiers[i].Name)) return i;
            }
            return -1;
        }

        // ASCII A-Z only. Full Unicode folding would read U+212A KELVIN SIGN as 'k', which the
        // C++ reader does not.
        private static bool EqualsAsciiIgnoreCase(string a, string b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++)
            {
                if (FoldAscii(a[i]) != FoldAscii(b[i])) return false;
            }
            return true;
        }

        private static char FoldAscii(char c)
        {
            return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
        }

        private static string Trim(string text)
        {
            return text.Trim(' ', '\t');
        }

        private static string Quote(string text)
        {
            return "'" + text + "'";
        }
    }
}
