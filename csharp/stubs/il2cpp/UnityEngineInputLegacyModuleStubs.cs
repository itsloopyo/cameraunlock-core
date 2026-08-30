// Reference stub for the Il2CppInterop-generated UnityEngine.InputLegacyModule. Compiled
// into an assembly named UnityEngine.InputLegacyModule by
// csharp/stubs/il2cpp/UnityEngine.InputLegacyModule/Stubs.UnityEngine.InputLegacyModule.csproj.
//
// Separate from the CoreModule stub beside it because the shipped interop proxies really do
// declare UnityEngine.Input in UnityEngine.InputLegacyModule.dll, and the compiler bakes
// that assembly identity into every call site. See UnityEngineCoreModuleStubs.cs for the
// rules this file follows.

using System;

namespace UnityEngine
{
    public class Input : Il2CppSystem.Object
    {
        public Input(IntPtr pointer) : base(pointer) { }

        public static bool GetKey(KeyCode key) => default;
        public static bool GetKeyDown(KeyCode key) => default;
    }
}
