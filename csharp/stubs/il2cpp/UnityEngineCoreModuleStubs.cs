// Reference stubs for the Il2CppInterop-generated UnityEngine.CoreModule that IL2CPP mods
// compile against. Compiled into an assembly named UnityEngine.CoreModule by
// csharp/stubs/il2cpp/UnityEngine.CoreModule/Stubs.UnityEngine.CoreModule.csproj.
//
// THESE ARE NOT ../UnityStubs.cs AND THE TWO MUST NEVER BE MIXED. UnityStubs.cs describes
// Mono Unity, where every engine type lives in (or is forwarded from) UnityEngine.dll and
// the module assemblies are built EMPTY. Under IL2CPP there is no Mono UnityEngine.dll:
// BepInEx 6 runs Il2CppInterop, which writes one proxy assembly per real engine module into
// BepInEx/interop, and a mod's references genuinely read
// [UnityEngine.CoreModule]UnityEngine.Camera. So this file carries types, and the
// assemblies it is compiled into are named for the modules that really declare them.
//
// The proxy shape differs from Mono Unity in ways that are not cosmetic, which is why
// unityengine.modules off NuGet (real Unity reference assemblies) cannot serve here:
//
//   - every engine class derives from Il2CppSystem.Object and so from Il2CppObjectBase,
//     and carries an IntPtr constructor. An injected MonoBehaviour subclass must chain to
//     it (`public MyBehaviour(IntPtr ptr) : base(ptr) { }`) or ClassInjector cannot build
//     it.
//   - array-returning members return Il2CppReferenceArray<T>, not T[].
//   - members that take a System.Type in Mono take an Il2CppSystem.Type here.
//
// Shape is part of the contract, the same rule ../UnityStubs.cs spells out: the compiler
// bakes the member kind, the declaring type and the parameter list into every reference,
// and nothing catches a mismatch until the plugin runs inside the game. Every signature
// below was read off a shipped BepInEx/interop/UnityEngine.CoreModule.dll (Sons of the
// Forest, Unity 2022.2) with Cecil rather than written from memory. In particular
// Matrix4x4.m20..m23 and Vector3.x/y/z are FIELDS, while Rect-style members elsewhere are
// properties; getting that backwards emits ldfld and throws MissingFieldException.
//
// AssemblyVersion 0.0.0.0 because the interop assemblies really carry 0.0.0.0.
//
// Only what cameraunlock-core's csharp/il2cpp sources and the IL2CPP mods compile against
// is stubbed. This is not a Unity reimplementation, and a member added here that the real
// proxy does not declare is worse than a missing one: it compiles and then throws at run.

using System;
using Il2CppInterop.Runtime.InteropTypes.Arrays;

namespace UnityEngine
{
    public class Object : Il2CppSystem.Object
    {
        public Object() { }
        public Object(IntPtr pointer) : base(pointer) { }

        public string name { get; set; }
        public HideFlags hideFlags { get; set; }

        public static void DontDestroyOnLoad(Object target) { }

        public static bool operator ==(Object x, Object y) => ReferenceEquals(x, y);
        public static bool operator !=(Object x, Object y) => !ReferenceEquals(x, y);
    }

    public class Component : Object
    {
        public Component() { }
        public Component(IntPtr pointer) : base(pointer) { }

        public Transform transform => default;
    }

    public class Behaviour : Component
    {
        public Behaviour() { }
        public Behaviour(IntPtr pointer) : base(pointer) { }

        public bool enabled { get; set; }
    }

    public class MonoBehaviour : Behaviour
    {
        public MonoBehaviour() { }
        public MonoBehaviour(IntPtr pointer) : base(pointer) { }
    }

    public sealed class GameObject : Object
    {
        public GameObject() { }
        public GameObject(string name) { }
        public GameObject(IntPtr pointer) : base(pointer) { }

        public T AddComponent<T>() where T : Component => default;
    }

    public class Transform : Component
    {
        public Transform() { }
        public Transform(IntPtr pointer) : base(pointer) { }

        public Vector3 position { get; set; }
        public Vector3 localPosition { get; set; }
        public Quaternion rotation { get; set; }
        public Transform parent { get; set; }
    }

    public class Texture : Object
    {
        public Texture() { }
        public Texture(IntPtr pointer) : base(pointer) { }
    }

    public class RenderTexture : Texture
    {
        public RenderTexture() { }
        public RenderTexture(IntPtr pointer) : base(pointer) { }
    }

    public sealed class Camera : Behaviour
    {
        public Camera() { }
        public Camera(IntPtr pointer) : base(pointer) { }

        public static Camera main => default;
        public static Il2CppReferenceArray<Camera> allCameras => default;

        public CameraType cameraType { get; set; }
        public RenderTexture targetTexture { get; set; }
        public float depth { get; set; }
        public float fieldOfView { get; set; }
        public Matrix4x4 worldToCameraMatrix { get; set; }

        public void ResetWorldToCameraMatrix() { }
    }

    public enum CameraType
    {
        Game = 1, SceneView = 2, Preview = 4, VR = 8, Reflection = 16
    }

    public enum HideFlags
    {
        None = 0, HideInHierarchy = 1, HideInInspector = 2, DontSaveInEditor = 4,
        NotEditable = 8, DontSaveInBuild = 16, DontUnloadUnusedAsset = 32,
        DontSave = 52, HideAndDontSave = 61
    }

    // A config-bound enum: the default a mod passes to ConfigFile.Bind is inlined into the
    // mod as its integer value, so these numbers are the contract. Read off the shipped
    // interop UnityEngine.CoreModule.dll.
    public enum KeyCode
    {
        None = 0, Backspace = 8, Tab = 9, Clear = 12, Return = 13, Pause = 19, Escape = 27, Space = 32,
        Alpha0 = 48, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,
        A = 97, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Delete = 127, Keypad0 = 256, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
        UpArrow = 273, DownArrow = 274, RightArrow = 275, LeftArrow = 276,
        Insert = 277, Home = 278, End = 279, PageUp = 280, PageDown = 281,
        F1 = 282, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,
        RightShift = 303, LeftShift = 304, RightControl = 305, LeftControl = 306,
        RightAlt = 307, LeftAlt = 308, Mouse0 = 323, Mouse1 = 324, Mouse2 = 325
    }

    public struct Matrix4x4
    {
        public float m00, m10, m20, m30, m01, m11, m21, m31, m02, m12, m22, m32, m03, m13, m23, m33;

        public static Matrix4x4 Rotate(Quaternion q) => default;
        public static Matrix4x4 Translate(Vector3 vector) => default;
        public static Matrix4x4 operator *(Matrix4x4 lhs, Matrix4x4 rhs) => default;
    }

    public struct Vector3
    {
        public float x, y, z;

        public Vector3(float x, float y, float z) { this.x = x; this.y = y; this.z = z; }

        public static Vector3 up => default;

        public static Vector3 operator +(Vector3 a, Vector3 b) => default;
        public static Vector3 operator -(Vector3 a, Vector3 b) => default;
        public static Vector3 operator -(Vector3 a) => default;
    }

    public struct Quaternion
    {
        public float x, y, z, w;

        public static Quaternion AngleAxis(float angle, Vector3 axis) => default;
        public static Quaternion Euler(float x, float y, float z) => default;
        public static Quaternion Inverse(Quaternion rotation) => default;

        public static Quaternion operator *(Quaternion lhs, Quaternion rhs) => default;
        public static Vector3 operator *(Quaternion rotation, Vector3 point) => default;
    }

    // Time, Resources and SceneManager are ordinary Il2CppSystem.Object-derived classes in
    // the interop proxies, not the static classes Mono Unity declares.
    public class Time : Il2CppSystem.Object
    {
        public Time(IntPtr pointer) : base(pointer) { }

        public static float deltaTime => default;
        public static int frameCount => default;
        public static float timeScale { get; set; }
    }

    public sealed class Resources : Il2CppSystem.Object
    {
        public Resources(IntPtr pointer) : base(pointer) { }

        public static Il2CppReferenceArray<Object> FindObjectsOfTypeAll(Il2CppSystem.Type type) => default;
    }
}

namespace UnityEngine.SceneManagement
{
    public struct Scene
    {
        public int m_Handle;

        public int handle => default;
        public string name { get => default; set { } }
    }

    public class SceneManager : Il2CppSystem.Object
    {
        public SceneManager(IntPtr pointer) : base(pointer) { }

        public static Scene GetActiveScene() => default;
    }
}
