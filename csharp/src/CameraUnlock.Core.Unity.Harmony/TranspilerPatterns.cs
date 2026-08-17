using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;

namespace CameraUnlock.Core.Unity.Harmony
{
    /// <summary>
    /// Common IL transpiler patterns for head tracking mods.
    /// Provides utilities for replacing camera direction lookups with custom aim directions.
    /// </summary>
    public static class TranspilerPatterns
    {
        /// <summary>
        /// Replaces all occurrences of "field.transform.forward" with a static method call.
        /// This is the most common pattern for aim decoupling - games often use
        /// m_Camera.transform.forward or similar to get the aim direction.
        ///
        /// The pattern looks for:
        ///   ldarg.0 (or ldarg.N)
        ///   ldfld [cameraField]
        ///   callvirt UnityEngine.Component::get_transform()
        ///   callvirt UnityEngine.Transform::get_forward()
        ///
        /// And replaces it with:
        ///   call [replacementMethod]
        /// </summary>
        /// <param name="instructions">The original instructions.</param>
        /// <param name="cameraField">The FieldInfo for the camera field (e.g., m_Camera, m_CameraMain).</param>
        /// <param name="replacementMethod">The static method that returns Vector3 aim direction.</param>
        /// <returns>Modified instructions with replacements applied.</returns>
        public static IEnumerable<CodeInstruction> ReplaceCameraForwardWithMethod(
            IEnumerable<CodeInstruction> instructions,
            FieldInfo cameraField,
            MethodInfo replacementMethod)
        {
            if (cameraField == null)
            {
                throw new ArgumentNullException(nameof(cameraField));
            }

            if (replacementMethod == null)
            {
                throw new ArgumentNullException(nameof(replacementMethod));
            }

            var getTransform = AccessTools.PropertyGetter(typeof(UnityEngine.Component), "transform");
            var getForward = AccessTools.PropertyGetter(typeof(UnityEngine.Transform), "forward");

            var codes = new List<CodeInstruction>(instructions);

            // Look for the 4-instruction pattern
            for (int i = 0; i < codes.Count - 3; i++)
            {
                if (IsLoadArgInstruction(codes[i]) &&
                    codes[i + 1].opcode == OpCodes.Ldfld && Equals(codes[i + 1].operand, cameraField) &&
                    codes[i + 2].opcode == OpCodes.Callvirt && Equals(codes[i + 2].operand, getTransform) &&
                    codes[i + 3].opcode == OpCodes.Callvirt && Equals(codes[i + 3].operand, getForward))
                {
                    // Replace the 4 instructions with a single call
                    ReplaceWithCall(codes[i], replacementMethod);
                    NopOut(codes[i + 1]);
                    NopOut(codes[i + 2]);
                    NopOut(codes[i + 3]);
                }
            }

            return codes;
        }

        /// <summary>
        /// Replaces all occurrences of "Camera.main.transform.forward" with a static method call.
        /// This pattern is used when the code accesses Camera.main directly instead of a field.
        ///
        /// The pattern looks for:
        ///   call UnityEngine.Camera::get_main()
        ///   callvirt UnityEngine.Component::get_transform()
        ///   callvirt UnityEngine.Transform::get_forward()
        ///
        /// And replaces it with:
        ///   call [replacementMethod]
        /// </summary>
        public static IEnumerable<CodeInstruction> ReplaceCameraMainForwardWithMethod(
            IEnumerable<CodeInstruction> instructions,
            MethodInfo replacementMethod)
        {
            if (replacementMethod == null)
            {
                throw new ArgumentNullException(nameof(replacementMethod));
            }

            var getCameraMain = AccessTools.PropertyGetter(typeof(UnityEngine.Camera), "main");
            var getTransform = AccessTools.PropertyGetter(typeof(UnityEngine.Component), "transform");
            var getForward = AccessTools.PropertyGetter(typeof(UnityEngine.Transform), "forward");

            var codes = new List<CodeInstruction>(instructions);

            // Look for the 3-instruction pattern
            for (int i = 0; i < codes.Count - 2; i++)
            {
                if (codes[i].opcode == OpCodes.Call && Equals(codes[i].operand, getCameraMain) &&
                    codes[i + 1].opcode == OpCodes.Callvirt && Equals(codes[i + 1].operand, getTransform) &&
                    codes[i + 2].opcode == OpCodes.Callvirt && Equals(codes[i + 2].operand, getForward))
                {
                    ReplaceWithCall(codes[i], replacementMethod);
                    NopOut(codes[i + 1]);
                    NopOut(codes[i + 2]);
                }
            }

            return codes;
        }

        /// <summary>
        /// Replaces a property getter chain with a static method call.
        /// More general version that can match arbitrary property chains.
        /// </summary>
        /// <param name="instructions">The original instructions.</param>
        /// <param name="startField">The field at the start of the chain (can be null for static chains).</param>
        /// <param name="propertyGetters">The property getters in the chain.</param>
        /// <param name="replacementMethod">The static method to call instead.</param>
        /// <param name="includeLoadArg">Whether the pattern starts with ldarg instruction.</param>
        /// <returns>Modified instructions.</returns>
        public static IEnumerable<CodeInstruction> ReplacePropertyChain(
            IEnumerable<CodeInstruction> instructions,
            FieldInfo startField,
            MethodInfo[] propertyGetters,
            MethodInfo replacementMethod,
            bool includeLoadArg = true)
        {
            if (propertyGetters == null || propertyGetters.Length == 0)
            {
                throw new ArgumentException("Property getters array cannot be null or empty", nameof(propertyGetters));
            }

            if (replacementMethod == null)
            {
                throw new ArgumentNullException(nameof(replacementMethod));
            }

            var codes = new List<CodeInstruction>(instructions);

            // Calculate pattern length
            int patternLength = propertyGetters.Length;
            if (startField != null) patternLength++;
            if (includeLoadArg && startField != null) patternLength++;

            // Search for pattern
            for (int i = 0; i <= codes.Count - patternLength; i++)
            {
                int offset = 0;

                // Check ldarg if expected
                if (includeLoadArg && startField != null)
                {
                    if (!IsLoadArgInstruction(codes[i + offset]))
                    {
                        continue;
                    }
                    offset++;
                }

                // Check ldfld if expected
                if (startField != null)
                {
                    if (codes[i + offset].opcode != OpCodes.Ldfld ||
                        !Equals(codes[i + offset].operand, startField))
                    {
                        continue;
                    }
                    offset++;
                }

                // Check property getters
                bool match = true;
                for (int j = 0; j < propertyGetters.Length && match; j++)
                {
                    var code = codes[i + offset + j];
                    if ((code.opcode != OpCodes.Call && code.opcode != OpCodes.Callvirt) ||
                        !Equals(code.operand, propertyGetters[j]))
                    {
                        match = false;
                    }
                }

                if (match)
                {
                    // Replace first instruction with call, nop the rest
                    ReplaceWithCall(codes[i], replacementMethod);
                    for (int k = 1; k < patternLength; k++)
                    {
                        NopOut(codes[i + k]);
                    }
                }
            }

            return codes;
        }

        /// <summary>
        /// Counts how many times a pattern would be replaced.
        /// Useful for debugging/logging transpiler effects.
        /// </summary>
        public static int CountCameraForwardPatterns(
            IEnumerable<CodeInstruction> instructions,
            FieldInfo cameraField)
        {
            if (cameraField == null)
            {
                return 0;
            }

            var getTransform = AccessTools.PropertyGetter(typeof(UnityEngine.Component), "transform");
            var getForward = AccessTools.PropertyGetter(typeof(UnityEngine.Transform), "forward");

            var codes = new List<CodeInstruction>(instructions);
            int count = 0;

            for (int i = 0; i < codes.Count - 3; i++)
            {
                if (IsLoadArgInstruction(codes[i]) &&
                    codes[i + 1].opcode == OpCodes.Ldfld && Equals(codes[i + 1].operand, cameraField) &&
                    codes[i + 2].opcode == OpCodes.Callvirt && Equals(codes[i + 2].operand, getTransform) &&
                    codes[i + 3].opcode == OpCodes.Callvirt && Equals(codes[i + 3].operand, getForward))
                {
                    count++;
                }
            }

            return count;
        }

        /// <summary>
        /// Creates a standard aim direction getter method that checks if tracking is active.
        /// </summary>
        /// <param name="isTrackingActive">Function that returns whether tracking is active.</param>
        /// <param name="getAimDirection">Function that returns the aim direction when tracking is active.</param>
        /// <param name="getCameraForward">Function that returns Camera.main.transform.forward (fallback).</param>
        /// <returns>A Func that returns the appropriate aim direction.</returns>
        public static Func<UnityEngine.Vector3> CreateAimDirectionGetter(
            Func<bool> isTrackingActive,
            Func<UnityEngine.Vector3> getAimDirection,
            Func<UnityEngine.Vector3> getCameraForward = null)
        {
            if (isTrackingActive == null)
            {
                throw new ArgumentNullException(nameof(isTrackingActive));
            }

            if (getAimDirection == null)
            {
                throw new ArgumentNullException(nameof(getAimDirection));
            }

            return () =>
            {
                if (isTrackingActive())
                {
                    return getAimDirection();
                }

                if (getCameraForward != null)
                {
                    return getCameraForward();
                }

                var cam = UnityEngine.Camera.main;
                if (cam != null)
                {
                    return cam.transform.forward;
                }

                return UnityEngine.Vector3.forward;
            };
        }

        private static bool IsLoadArgInstruction(CodeInstruction instruction)
        {
            return instruction.opcode == OpCodes.Ldarg_0 ||
                   instruction.opcode == OpCodes.Ldarg_1 ||
                   instruction.opcode == OpCodes.Ldarg_2 ||
                   instruction.opcode == OpCodes.Ldarg_3 ||
                   instruction.opcode == OpCodes.Ldarg_S ||
                   instruction.opcode == OpCodes.Ldarg;
        }
    
        // Mutated in place rather than replaced. A fresh CodeInstruction drops the
        // original's labels and exception blocks, and Harmony's IL reader parks branch
        // targets on exactly the kind of instruction these patterns match (the first of a
        // statement). Losing one makes the ILGenerator fail to resolve the label, which
        // aborts the whole PatchAll - so every other patch in the mod, camera hook
        // included, silently never applies.
        private static void ReplaceWithCall(CodeInstruction instruction, MethodInfo replacementMethod)
        {
            instruction.opcode = OpCodes.Call;
            instruction.operand = replacementMethod;
        }

        private static void NopOut(CodeInstruction instruction)
        {
            instruction.opcode = OpCodes.Nop;
            instruction.operand = null;
        }
}
}
