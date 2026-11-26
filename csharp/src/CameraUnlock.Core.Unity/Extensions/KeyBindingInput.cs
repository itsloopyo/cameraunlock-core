using System;
using System.Collections.Generic;
using CameraUnlock.Core.Input;
using UnityEngine;

namespace CameraUnlock.Core.Unity.Extensions
{
    /// <summary>
    /// Checks a hotkey list read by <see cref="KeyBindings.TryParse"/> against Unity's input.
    /// Every binding is an ordinary item of the list, chords included.
    /// </summary>
    public static class KeyBindingInput
    {
        internal delegate bool KeyQuery(int unityKeyCode);

        private static readonly KeyQuery WentDown = code => UnityEngine.Input.GetKeyDown((KeyCode)code);
        private static readonly KeyQuery IsHeld = code => UnityEngine.Input.GetKey((KeyCode)code);

        /// <summary>
        /// True on the frame a binding's key goes down while the binding allows it. A binding
        /// with modifiers fires while every modifier it names is held, either side. One without
        /// does not fire while Ctrl and Shift are both held, so Ctrl+Shift+&lt;key&gt; reaches
        /// only a binding that names the chord. The same rule as the C++ RegisterKeyBindings.
        /// </summary>
        /// <exception cref="ArgumentNullException"><paramref name="bindings"/> is null.</exception>
        public static bool IsTriggered(IList<KeyBinding> bindings)
        {
            return IsTriggered(bindings, WentDown, IsHeld);
        }

        internal static bool IsTriggered(IList<KeyBinding> bindings, KeyQuery wentDown, KeyQuery isHeld)
        {
            if (bindings == null) throw new ArgumentNullException("bindings");

            for (int i = 0; i < bindings.Count; i++)
            {
                KeyBinding binding = bindings[i];
                if (!wentDown(binding.UnityKeyCode)) continue;

                KeyModifiers held = KeyModifiers.None;
                if (isHeld((int)KeyCode.LeftControl) || isHeld((int)KeyCode.RightControl)) held |= KeyModifiers.Ctrl;
                if (isHeld((int)KeyCode.LeftShift) || isHeld((int)KeyCode.RightShift)) held |= KeyModifiers.Shift;
                if (isHeld((int)KeyCode.LeftAlt) || isHeld((int)KeyCode.RightAlt)) held |= KeyModifiers.Alt;

                if (binding.Modifiers == KeyModifiers.None)
                {
                    const KeyModifiers chord = KeyModifiers.Ctrl | KeyModifiers.Shift;
                    if ((held & chord) != chord) return true;
                }
                else if ((held & binding.Modifiers) == binding.Modifiers)
                {
                    return true;
                }
            }
            return false;
        }
    }
}
