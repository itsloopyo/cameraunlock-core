// Reference stub for the Il2CppInterop-generated UnityEngine.VideoModule. Compiled into an
// assembly named UnityEngine.VideoModule by
// csharp/stubs/il2cpp/UnityEngine.VideoModule/Stubs.UnityEngine.VideoModule.csproj.
//
// Consumed by csharp/il2cpp/FastBootBehaviour.cs, which disables splash VideoPlayers. See
// UnityEngineCoreModuleStubs.cs for the rules this file follows.

using System;

namespace UnityEngine.Video
{
    public sealed class VideoPlayer : Behaviour
    {
        public VideoPlayer() { }
        public VideoPlayer(IntPtr pointer) : base(pointer) { }

        public bool playOnAwake { get; set; }
        public bool isPlaying => default;

        public void Stop() { }
    }
}
