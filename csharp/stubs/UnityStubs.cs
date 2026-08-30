// The canonical Unity reference stubs, shared by cameraunlock-core and every mod that
// builds without a game installed. Compiled into UnityEngine.dll by
// csharp/stubs/build-unity-stubs.ps1. Fifteen repos each carried a drifted copy of this
// file before it lived here.
//
// Three rules the drift kept breaking, all of them invisible until the code runs in a game:
//
// 1. ONE ASSEMBLY, NAMED UnityEngine. Modern Unity splits the engine across
//    UnityEngine.CoreModule, UnityEngine.PhysicsModule and friends, and ships
//    UnityEngine.dll as a facade whose TypeForwardedTo entries point at them. A mod
//    compiled against this stub emits [UnityEngine]UnityEngine.Camera, which the facade
//    resolves. Compiling against split module stubs instead emits
//    [UnityEngine.CoreModule]UnityEngine.MonoBehaviour, which does not exist on the older
//    Unity versions half the fleet targets, and the plugin fails to JIT at Awake() with no
//    build-time warning. The module assemblies must therefore stay EMPTY: a type declared
//    both here and in a module stub is CS0433 at every use site.
//
// 2. uGUI IS NOT A MODULE. UnityEngine.dll carries no forwarder for the UnityEngine.UI
//    namespace, so Graphic/Image/RawImage/Text must be compiled into UnityEngine.UI.dll.
//    They live in UnityUIStubs.cs beside this file and must stay there. Declaring them
//    here emits [UnityEngine]UnityEngine.UI.Image and throws TypeLoadException at runtime.
//
// 3. SHAPE IS PART OF THE CONTRACT. The compiler bakes the member KIND into every
//    reference, and nothing catches a mismatch until it runs:
//      - a property declared as a field emits ldfld/ldsfld and throws MissingFieldException
//        against the shipped assembly. Rect.x/y/width/height are the classic case, which is
//        why they are properties over private backing fields here.
//      - a field must be declared on the type that really declares it; a field reference
//        binds to its declaring type. Texture.filterMode sits on Texture, not Texture2D.
//      - optional parameters are baked in at the call site, so a stub has to reproduce a
//        method's full parameter list and defaults.
//      - a const is inlined into the calling assembly, so its VALUE is part of the
//        contract. Physics.DefaultRaycastLayers below is read from a shipped
//        UnityEngine.PhysicsModule.dll, not guessed.
//      - an enum MEMBER is a const too. TextureFormat and RuntimePlatform were both
//        written here as bare 0..n sequences and both were wrong in every member; the
//        built CameraUnlock.Core.Unity.dll shipped ldc.i4.4 for TextureFormat.ARGB32,
//        which is RGBA32 in every Unity that ships. Read the numbers out of the enum,
//        never renumber them, and give every member an explicit value so a later edit
//        cannot shift the ones below it.
//      - class vs struct is in every signature blob. LayerMask and Mathf are structs in
//        the shipped engine. A stub class emits `call class LayerMask op_Implicit(int32)`
//        and fails to bind at run.
//
// 4. A TYPE THE GAME DOES NOT DECLARE IS WORSE THAN A MISSING ONE. It compiles, and then
//    the reference fails to resolve inside the game. Half the fleet targets a Unity that
//    predates parts of this file, so anything added here has to be checked against the
//    ExportedType tables of the OLDEST shipped game that will compile against it, not
//    against the newest. Verified absences are marked at each declaration below; the two
//    that no TargetFramework can express are:
//      - the UnityEngine.Rendering SRP types are absent from Tacoma (Unity 2017.2), which
//        builds net472 like every modern game.
//      - MotionVectorGenerationMode is absent from Dorfromantik's shipped CoreModule.
//        Managed code stripping removes an unused type from a module assembly even on a
//        current Unity, so "the engine version has it" is not the same as "the game
//        ships it".
//
// LangVersion is 7.3, because net35 mods (pre-2017.3 Unity) cannot go higher and one file
// has to serve every target. That rules out auto-properties on a struct that the
// constructor does not assign, which is why the derived Rect and Bounds members are
// computed rather than auto.
//
// Game-specific types do NOT belong here. They go in a separate <Game>Stubs.cs compiled
// into an assembly named exactly as the game ships it - see valheim-headtracking's
// ValheimStubs.cs and build-unity-stubs.ps1's -ExtraAssembly.
namespace UnityEngine {
    public class Object {
        public static void Destroy(Object obj) { }
        public static void DestroyImmediate(Object obj) { }
        public static void DontDestroyOnLoad(Object target) { }
        public string name { get; set; }
        public HideFlags hideFlags { get; set; }
        public int GetInstanceID() => 0;
        public static implicit operator bool(Object exists) => exists != null;
        public static bool operator ==(Object x, Object y) => ReferenceEquals(x, y);
        public static bool operator !=(Object x, Object y) => !ReferenceEquals(x, y);
        public override bool Equals(object other) => ReferenceEquals(this, other);
        public override int GetHashCode() => base.GetHashCode();
#if !NET35
        public static T[] FindObjectsByType<T>(FindObjectsSortMode sortMode) where T : Object => new T[0];
#endif
        public static T FindObjectOfType<T>() where T : Object => default;
        public static T FindObjectOfType<T>(bool includeInactive) where T : Object => default;
        public static T[] FindObjectsOfType<T>() where T : Object => new T[0];
        public static T[] FindObjectsOfType<T>(bool includeInactive) where T : Object => new T[0];
        public static Object FindObjectOfType(System.Type type) => default;
        public static Object[] FindObjectsOfType(System.Type type) => new Object[0];
    }
    public class ScriptableObject : Object { }
    public class Component : Object {
        public Transform transform { get; }
        public GameObject gameObject { get; }
        public T GetComponent<T>() => default;
        public Component GetComponent(System.Type type) => default;
        public T GetComponentInChildren<T>() => default;
        public T GetComponentInChildren<T>(bool includeInactive) => default;
        public T GetComponentInParent<T>() => default;
        public T GetComponentInParent<T>(bool includeInactive) => default;
        public T[] GetComponentsInChildren<T>() => new T[0];
        public T[] GetComponentsInChildren<T>(bool includeInactive) => new T[0];
        public T[] GetComponentsInParent<T>() => new T[0];
        public T[] GetComponentsInParent<T>(bool includeInactive) => new T[0];
    }
    public class Behaviour : Component {
        public bool enabled { get; set; }
        public bool isActiveAndEnabled { get; }
    }
    public class MonoBehaviour : Behaviour {
        public bool useGUILayout { get; set; }
        public Coroutine StartCoroutine(System.Collections.IEnumerator routine) => default;
        public void StopCoroutine(Coroutine routine) { }
        public void StopAllCoroutines() { }
    }
    public class YieldInstruction { }
    public class Coroutine : YieldInstruction { }
    public class WaitForSeconds : YieldInstruction { public WaitForSeconds(float seconds) { } }
#if !NET35
    // Unity 2022.2 and later. ABSENT from Painscreek, Crawl, Tacoma, Subnautica, Outer Wilds
    // and Hardspace Shipbreaker; only Dorfromantik and Valheim of the eight games checked
    // declare it. Object.FindObjectsByType is gated with it, since a mod that calls it on
    // an older game gets MissingMethodException whether or not the enum resolves.
    public enum FindObjectsSortMode { None = 0, InstanceID = 1 }
#endif
    public class Transform : Component {
        public Transform parent { get; set; }
        public Quaternion localRotation { get; set; }
        public Quaternion rotation { get; set; }
        public Vector3 position { get; set; }
        public Vector3 localPosition { get; set; }
        public Vector3 eulerAngles { get; set; }
        public Vector3 localEulerAngles { get; set; }
        public Vector3 forward { get; }
        public Vector3 up { get; }
        public Vector3 right { get; }
        public Vector3 localScale { get; set; }
        public Vector3 lossyScale { get; }
        public int childCount { get; }
        public Matrix4x4 localToWorldMatrix { get; }
        public Matrix4x4 worldToLocalMatrix { get; }
        public Transform GetChild(int index) => default;
        public Transform Find(string n) => default;
        public void SetParent(Transform parent) { }
        public void SetParent(Transform parent, bool worldPositionStays) { }
        public Vector3 TransformDirection(Vector3 direction) => default;
        public Vector3 InverseTransformDirection(Vector3 direction) => default;
        public Vector3 TransformPoint(Vector3 position) => default;
        public Vector3 InverseTransformPoint(Vector3 position) => default;
    }
    public class GameObject : Object {
        public GameObject() { }
        public GameObject(string name) { }
        public Transform transform { get; }
        public bool activeSelf { get; }
        public bool activeInHierarchy { get; }
        public T GetComponent<T>() => default;
        public Component GetComponent(System.Type type) => default;
        public T GetComponentInChildren<T>() => default;
        public T GetComponentInParent<T>() => default;
        public T[] GetComponents<T>() => default;
        public T[] GetComponentsInChildren<T>() => default;
        public T AddComponent<T>() where T : Component => default;
        public Component AddComponent(System.Type componentType) => default;
        public void SetActive(bool value) { }
        public static GameObject Find(string name) => default;
        public static GameObject FindWithTag(string tag) => default;
    }
    [System.Flags]
    public enum HideFlags { None = 0, HideInHierarchy = 1, HideInInspector = 2, DontSaveInEditor = 4, NotEditable = 8, DontSaveInBuild = 16, DontUnloadUnusedAsset = 32, DontSave = 52, HideAndDontSave = 61 }
    public class Camera : Behaviour {
        public delegate void CameraCallback(Camera cam);
        public static CameraCallback onPreCull;
        public static CameraCallback onPreRender;
        public static CameraCallback onPostRender;
        public static Camera main { get; }
        public static Camera current { get; }
        public static Camera[] allCameras { get; }
        public static int allCamerasCount { get; }
        public float fieldOfView { get; set; }
        public float aspect { get; set; }
        public float nearClipPlane { get; set; }
        public float farClipPlane { get; set; }
        public RenderTexture targetTexture { get; set; }
        public Matrix4x4 worldToCameraMatrix { get; set; }
        public Matrix4x4 cameraToWorldMatrix { get; }
        public Matrix4x4 projectionMatrix { get; set; }
        public CameraClearFlags clearFlags { get; set; }
        public Color backgroundColor { get; set; }
        public int cullingMask { get; set; }
        public float depth { get; set; }
        public Rect rect { get; set; }
        public int targetDisplay { get; set; }
        public bool stereoEnabled { get; }
        public Vector3 WorldToScreenPoint(Vector3 position) => default;
        public Vector3 ScreenToWorldPoint(Vector3 position) => default;
        public Ray ScreenPointToRay(Vector3 pos) => default;
        public void Render() { }
        public void ResetWorldToCameraMatrix() { }
        public void ResetProjectionMatrix() { }
    }
    public enum CameraClearFlags { Skybox = 1, Color = 2, SolidColor = 2, Depth = 3, Nothing = 4 }
    // VR and Reflection arrived in Unity 5.4; Painscreek and Crawl declare only the first
    // three. An enum member is a literal at the use site, so naming one costs nothing at
    // run - the comparison simply never matches on those games.
    public enum CameraType { Game = 1, SceneView = 2, Preview = 4, VR = 8, Reflection = 16 }
    public class RenderTexture : Object { }
    public struct Matrix4x4 {
        public float m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33;
        public static Matrix4x4 identity => default;
        public Vector4 GetColumn(int index) => default;
        public void SetColumn(int index, Vector4 column) { }
        public Vector4 GetRow(int index) => default;
        public void SetRow(int index, Vector4 row) { }
        public static Matrix4x4 TRS(Vector3 pos, Quaternion q, Vector3 s) => default;
        public static Matrix4x4 Perspective(float fov, float aspect, float zNear, float zFar) => default;
        public static Matrix4x4 Rotate(Quaternion q) => default;
        public static Matrix4x4 Translate(Vector3 v) => default;
        public static Matrix4x4 Scale(Vector3 v) => default;
        public static Matrix4x4 operator *(Matrix4x4 lhs, Matrix4x4 rhs) => default;
        public Matrix4x4 inverse => default;
        public static Vector4 operator *(Matrix4x4 lhs, Vector4 vector) => default;
        public static bool operator ==(Matrix4x4 lhs, Matrix4x4 rhs) => false;
        public static bool operator !=(Matrix4x4 lhs, Matrix4x4 rhs) => true;
        public override bool Equals(object other) => false;
        public override int GetHashCode() => 0;
        public Vector3 MultiplyPoint(Vector3 point) => default;
        public Vector3 MultiplyPoint3x4(Vector3 point) => default;
        public Vector3 MultiplyVector(Vector3 vector) => default;
        public Quaternion rotation => default;
        public Vector3 lossyScale => default;
        public float this[int row, int column] { get => 0; set { } }
    }
    public struct Vector4 {
        public float x, y, z, w;
        public Vector4(float x, float y, float z, float w) { this.x = x; this.y = y; this.z = z; this.w = w; }
    }
    public struct Quaternion {
        public float x, y, z, w;
        private Vector3 _eulerAngles;
        public Quaternion(float x, float y, float z, float w) { this.x = x; this.y = y; this.z = z; this.w = w; _eulerAngles = default; }
        public Vector3 eulerAngles { get => _eulerAngles; set => _eulerAngles = value; }
        public static Quaternion identity => new Quaternion(0, 0, 0, 1);
        public static Quaternion Euler(float x, float y, float z) => default;
        public static Quaternion Euler(Vector3 euler) => default;
        public static Quaternion AngleAxis(float angle, Vector3 axis) => default;
        public static Quaternion LookRotation(Vector3 forward) => default;
        public static Quaternion LookRotation(Vector3 forward, Vector3 upwards) => default;
        public static Quaternion Slerp(Quaternion a, Quaternion b, float t) => default;
        public static Quaternion Lerp(Quaternion a, Quaternion b, float t) => default;
        public static Quaternion SlerpUnclamped(Quaternion a, Quaternion b, float t) => default;
        public static Quaternion Inverse(Quaternion rotation) => default;
        public static float Angle(Quaternion a, Quaternion b) => 0;
        public static Quaternion operator *(Quaternion lhs, Quaternion rhs) => default;
        public static Vector3 operator *(Quaternion rotation, Vector3 point) => default;
        public static bool operator ==(Quaternion lhs, Quaternion rhs) => false;
        public static bool operator !=(Quaternion lhs, Quaternion rhs) => true;
        public override bool Equals(object other) => false;
        public override int GetHashCode() => 0;
    }
    public struct Vector3 {
        public float x, y, z;
        public Vector3(float x, float y, float z) { this.x = x; this.y = y; this.z = z; }
        public float magnitude => default;
        public float sqrMagnitude => default;
        public Vector3 normalized => default;
        public static Vector3 zero => new Vector3(0, 0, 0);
        public static Vector3 one => new Vector3(1, 1, 1);
        public static Vector3 up => new Vector3(0, 1, 0);
        public static Vector3 down => new Vector3(0, -1, 0);
        public static Vector3 forward => new Vector3(0, 0, 1);
        public static Vector3 back => new Vector3(0, 0, -1);
        public static Vector3 left => new Vector3(-1, 0, 0);
        public static Vector3 right => new Vector3(1, 0, 0);
        public static Vector3 Lerp(Vector3 a, Vector3 b, float t) => default;
        public static float Dot(Vector3 lhs, Vector3 rhs) => 0;
        public static Vector3 Cross(Vector3 lhs, Vector3 rhs) => default;
        public static float Distance(Vector3 a, Vector3 b) => 0;
        public static float Angle(Vector3 from, Vector3 to) => 0;
        public static Vector3 Scale(Vector3 a, Vector3 b) => new Vector3(a.x * b.x, a.y * b.y, a.z * b.z);
        public static Vector3 operator +(Vector3 a, Vector3 b) => default;
        public static Vector3 operator -(Vector3 a, Vector3 b) => default;
        public static Vector3 operator -(Vector3 a) => new Vector3(-a.x, -a.y, -a.z);
        public static Vector3 operator *(Vector3 a, float d) => default;
        public static Vector3 operator *(float d, Vector3 a) => default;
        public static Vector3 operator /(Vector3 a, float d) => default;
        public static bool operator ==(Vector3 lhs, Vector3 rhs) => false;
        public static bool operator !=(Vector3 lhs, Vector3 rhs) => true;
        public override bool Equals(object other) => false;
        public override int GetHashCode() => 0;
    }
    public struct Vector2 {
        public float x, y;
        public Vector2(float x, float y) { this.x = x; this.y = y; }
        public static Vector2 zero => new Vector2(0, 0);
        public static Vector2 one => new Vector2(1, 1);
        public static Vector2 Lerp(Vector2 a, Vector2 b, float t) => default;
        public static Vector2 operator +(Vector2 a, Vector2 b) => default;
        public static Vector2 operator -(Vector2 a, Vector2 b) => default;
        public static Vector2 operator *(Vector2 a, float d) => default;
        public static Vector2 operator *(float d, Vector2 a) => default;
        public static Vector2 operator /(Vector2 a, float d) => default;
        public static implicit operator Vector3(Vector2 v) => new Vector3(v.x, v.y, 0);
        public static implicit operator Vector2(Vector3 v) => new Vector2(v.x, v.y);
    }
    public struct Ray {
        public Vector3 origin { get; set; }
        public Vector3 direction { get; set; }
        public Ray(Vector3 origin, Vector3 direction) { this.origin = origin; this.direction = direction; }
    }
    public static class Time {
        public static float time { get; }
        public static float deltaTime { get; }
        public static float unscaledTime { get; }
        public static float unscaledDeltaTime { get; }
        public static float fixedDeltaTime { get; set; }
        public static float timeScale { get; set; }
        public static int frameCount { get; }
        public static float realtimeSinceStartup { get; }
    }
    public static class Input {
        public static bool GetKeyDown(KeyCode key) => false;
        public static bool GetKeyUp(KeyCode key) => false;
        public static bool GetKey(KeyCode key) => false;
        public static bool GetMouseButton(int button) => false;
        public static bool GetMouseButtonDown(int button) => false;
        public static bool GetMouseButtonUp(int button) => false;
        public static float GetAxis(string axisName) => 0;
        public static float GetAxisRaw(string axisName) => 0;
        public static Vector3 mousePosition { get; }
        public static bool anyKey { get; }
        public static bool anyKeyDown { get; }
    }
    public enum KeyCode {
        None = 0, Backspace = 8, Tab = 9, Clear = 12, Return = 13, Pause = 19, Escape = 27, Space = 32,
        Alpha0 = 48, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,
        A = 97, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        Delete = 127, Keypad0 = 256, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
        F1 = 282, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,
        UpArrow = 273, DownArrow = 274, RightArrow = 275, LeftArrow = 276,
        Insert = 277, Home = 278, End = 279, PageUp = 280, PageDown = 281,
        RightShift = 303, LeftShift = 304, RightControl = 305, LeftControl = 306,
        RightAlt = 307, LeftAlt = 308, Mouse0 = 323, Mouse1, Mouse2
    }
    public static class Cursor {
        public static CursorLockMode lockState { get; set; }
        public static bool visible { get; set; }
    }
    public enum CursorLockMode { None, Locked, Confined }
    // A struct in the shipped engine, like LayerMask. Every member is static and no
    // signature anywhere mentions the type, so the old `static class` bound correctly;
    // it is a struct here so the file has one rule for class-vs-struct rather than two.
    public struct Mathf {
        public const float PI = 3.14159274f;
        public const float Deg2Rad = 0.0174532924f;
        public const float Rad2Deg = 57.29578f;
        public static float Abs(float f) => System.Math.Abs(f);
        public static int Abs(int value) => System.Math.Abs(value);
        public static float Min(float a, float b) => a < b ? a : b;
        public static int Min(int a, int b) => a < b ? a : b;
        public static float Max(float a, float b) => a > b ? a : b;
        public static int Max(int a, int b) => a > b ? a : b;
        public static float Clamp(float value, float min, float max) => value < min ? min : value > max ? max : value;
        public static int Clamp(int value, int min, int max) => value < min ? min : value > max ? max : value;
        public static float Clamp01(float value) => Clamp(value, 0, 1);
        public static float Lerp(float a, float b, float t) => a + (b - a) * Clamp01(t);
        public static float LerpUnclamped(float a, float b, float t) => a + (b - a) * t;
        public static float InverseLerp(float a, float b, float value) => (value - a) / (b - a);
        public static float Sin(float f) => (float)System.Math.Sin(f);
        public static float Cos(float f) => (float)System.Math.Cos(f);
        public static float Tan(float f) => (float)System.Math.Tan(f);
        public static float Asin(float f) => (float)System.Math.Asin(f);
        public static float Acos(float f) => (float)System.Math.Acos(f);
        public static float Atan(float f) => (float)System.Math.Atan(f);
        public static float Atan2(float y, float x) => (float)System.Math.Atan2(y, x);
        public static float Sqrt(float f) => (float)System.Math.Sqrt(f);
        public static float Floor(float f) => (float)System.Math.Floor(f);
        public static float Ceil(float f) => (float)System.Math.Ceiling(f);
        public static float Round(float f) => (float)System.Math.Round(f);
        public static int FloorToInt(float f) => (int)System.Math.Floor(f);
        public static int CeilToInt(float f) => (int)System.Math.Ceiling(f);
        public static int RoundToInt(float f) => (int)System.Math.Round(f);
        public static float Pow(float f, float p) => (float)System.Math.Pow(f, p);
        public static float Exp(float power) => (float)System.Math.Exp(power);
        public static float SmoothStep(float from, float to, float t) { t = Clamp01(t); return from + (to - from) * t * t * (3f - 2f * t); }
    }
    public static class Debug {
        public static void Log(object message) { }
        public static void LogWarning(object message) { }
        public static void LogError(object message) { }
        public static void LogFormat(string format, params object[] args) { }
        public static void LogWarningFormat(string format, params object[] args) { }
        public static void LogErrorFormat(string format, params object[] args) { }
        public static void DrawLine(Vector3 start, Vector3 end) { }
        public static void DrawRay(Vector3 start, Vector3 dir) { }
    }
    public static class GUIUtility {
        public static int keyboardControl { get; set; }
        public static int hotControl { get; set; }
    }
    public class GUIStyle {
        public GUIStyle() { }
        public GUIStyle(GUIStyle other) { }
        public int fontSize { get; set; }
        public TextAnchor alignment { get; set; }
        public GUIStyleState normal { get; set; }
        public Font font { get; set; }
        public bool wordWrap { get; set; }
        public FontStyle fontStyle { get; set; }
        public Vector2 CalcSize(GUIContent content) => default;
    }
    public enum FontStyle { Normal, Bold, Italic, BoldAndItalic }
    public class GUIStyleState {
        public Color textColor { get; set; }
        public Texture2D background { get; set; }
    }
    public class Font : Object { }
    public enum TextAnchor { UpperLeft, UpperCenter, UpperRight, MiddleLeft, MiddleCenter, MiddleRight, LowerLeft, LowerCenter, LowerRight }
    public static class GUI {
        public static Color color { get; set; }
        public static Color backgroundColor { get; set; }
        public static Color contentColor { get; set; }
        public static Matrix4x4 matrix { get; set; }
        public static GUISkin skin { get; set; }
        public static void Label(Rect position, string text) { }
        public static void Label(Rect position, string text, GUIStyle style) { }
        public static void Label(Rect position, GUIContent content) { }
        public static void Label(Rect position, GUIContent content, GUIStyle style) { }
        public static void Box(Rect position, string text) { }
        public static void Box(Rect position, string text, GUIStyle style) { }
        public static void Box(Rect position, GUIContent content) { }
        public static void Box(Rect position, GUIContent content, GUIStyle style) { }
        public static bool Button(Rect position, string text) => false;
        public static bool Button(Rect position, string text, GUIStyle style) => false;
        public static void DrawTexture(Rect position, Texture image) { }
        public static void DrawTexture(Rect position, Texture image, ScaleMode scaleMode) { }
        public static void BeginGroup(Rect position) { }
        public static void EndGroup() { }
    }
    public class GUIContent {
        // A static FIELD in the real engine; a property here emits call get_none.
        public static GUIContent none = new GUIContent();
        public string text { get; set; }
        public Texture image { get; set; }
        public GUIContent() { }
        public GUIContent(string text) { this.text = text; }
    }
    public class GUISkin : Object {
        public GUIStyle label { get; }
        public GUIStyle button { get; }
        public GUIStyle box { get; }
    }
    public class Texture : Object {
        public int width { get; }
        public int height { get; }
        // Declared on Texture in the real engine, not on Texture2D.
        public FilterMode filterMode { get; set; }
    }
    public class Sprite : Object {
        public Texture2D texture { get; }
        public Rect rect { get; }
    }
    public class Texture2D : Texture {
        public Texture2D(int width, int height) { }
        public Texture2D(int width, int height, TextureFormat format, bool mipChain) { }
        public void SetPixel(int x, int y, Color color) { }
        public void SetPixels(Color[] colors) { }
        public Color GetPixel(int x, int y) => default;
        public Color[] GetPixels() => new Color[0];
        public void Apply() { }
        public static Texture2D whiteTexture { get; }
    }
    // Read out of the shipped enum, unanimous across eight games from Unity 5.3 (Crawl,
    // Painscreek) to Unity 6 (Valheim). The numbering is NOT sequential and never was: the
    // bare 0..8 that stood here shipped ARGB32 as 4, which the engine reads as RGBA32, and
    // would have baked RGB24 as ARGB4444 and DXT5 as RGB565.
    public enum TextureFormat {
        Alpha8 = 1, ARGB4444 = 2, RGB24 = 3, RGBA32 = 4, ARGB32 = 5, RGB565 = 7,
        R16 = 9, DXT1 = 10, DXT5 = 12
    }
    public enum FilterMode { Point, Bilinear, Trilinear }
    public enum ScaleMode { StretchToFill, ScaleAndCrop, ScaleToFit }
    // x/y/width/height are PROPERTIES in the real UnityEngine, not fields.
    // Declaring them as fields makes the compiler emit ldfld, which throws
    // MissingFieldException against the shipped assembly.
    public struct Rect {
        private float _x, _y, _width, _height;
        public Rect(float x, float y, float width, float height) { _x = x; _y = y; _width = width; _height = height; }
        public float x { get => _x; set => _x = value; }
        public float y { get => _y; set => _y = value; }
        public float width { get => _width; set => _width = value; }
        public float height { get => _height; set => _height = value; }
        public float xMin { get => _x; set => _x = value; }
        public float xMax { get => _x + _width; set => _width = value - _x; }
        public float yMin { get => _y; set => _y = value; }
        public float yMax { get => _y + _height; set => _height = value - _y; }
        public Vector2 center { get => new Vector2(_x + _width / 2, _y + _height / 2); set { _x = value.x - _width / 2; _y = value.y - _height / 2; } }
        public Vector2 size { get => new Vector2(_width, _height); set { _width = value.x; _height = value.y; } }
        public bool Contains(Vector2 point) => false;
    }
    public struct Color {
        public float r, g, b, a;
        public Color(float r, float g, float b) { this.r = r; this.g = g; this.b = b; this.a = 1; }
        public Color(float r, float g, float b, float a) { this.r = r; this.g = g; this.b = b; this.a = a; }
        public static Color white => new Color(1, 1, 1, 1);
        public static Color black => new Color(0, 0, 0, 1);
        public static Color red => new Color(1, 0, 0, 1);
        public static Color green => new Color(0, 1, 0, 1);
        public static Color blue => new Color(0, 0, 1, 1);
        public static Color yellow => new Color(1, 1, 0, 1);
        public static Color cyan => new Color(0, 1, 1, 1);
        public static Color magenta => new Color(1, 0, 1, 1);
        public static Color gray => new Color(0.5f, 0.5f, 0.5f, 1);
        public static Color grey => gray;
        public static Color clear => new Color(0, 0, 0, 0);
        public static Color Lerp(Color a, Color b, float t) => default;
        public static bool operator ==(Color lhs, Color rhs) => lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b && lhs.a == rhs.a;
        public static bool operator !=(Color lhs, Color rhs) => !(lhs == rhs);
        public override bool Equals(object other) => false;
        public override int GetHashCode() => 0;
    }
    public struct Color32 {
        public byte r, g, b, a;
        public Color32(byte r, byte g, byte b, byte a) { this.r = r; this.g = g; this.b = b; this.a = a; }
        public static implicit operator Color(Color32 c) => new Color(c.r / 255f, c.g / 255f, c.b / 255f, c.a / 255f);
        public static implicit operator Color32(Color c) => new Color32((byte)(c.r * 255), (byte)(c.g * 255), (byte)(c.b * 255), (byte)(c.a * 255));
    }
    public class Material : Object {
        public Material(Shader shader) { }
        public Material(Material source) { }
        public Color color { get; set; }
        public Shader shader { get; set; }
        public void SetFloat(string name, float value) { }
        public void SetColor(string name, Color value) { }
        public void SetTexture(string name, Texture value) { }
    }
    public class Shader : Object { public static Shader Find(string name) => default; }
    public static class Screen {
        public static int width { get; }
        public static int height { get; }
        public static float dpi { get; }
        public static Resolution currentResolution { get; }
        public static bool fullScreen { get; set; }
    }
    public static class QualitySettings {
        public static int vSyncCount { get; set; }
        public static int GetQualityLevel() => 0;
        public static void SetQualityLevel(int index) { }
        public static void SetQualityLevel(int index, bool applyExpensiveChanges) { }
        public static string[] names { get; }
    }
    public struct Resolution { public int width { get; set; } public int height { get; set; } public int refreshRate { get; set; } }
    public static class Application {
        public static string dataPath { get; }
        public static string persistentDataPath { get; }
        public static string streamingAssetsPath { get; }
        public static RuntimePlatform platform { get; }
        public static bool isPlaying { get; }
        public static bool isEditor { get; }
        public static string productName { get; }
        public static string version { get; }
        public static int targetFrameRate { get; set; }
        public static bool runInBackground { get; set; }
        public static void Quit() { }
    }
    // Same eight games, same unanimity. The bare 0..7 that stood here made
    // `Application.platform == RuntimePlatform.WindowsPlayer` compile to a comparison
    // against 1, which is OSXPlayer, so the Windows branch never ran on Windows.
    public enum RuntimePlatform {
        OSXEditor = 0, OSXPlayer = 1, WindowsPlayer = 2, WindowsEditor = 7,
        IPhonePlayer = 8, Android = 11, LinuxPlayer = 13, WebGLPlayer = 17
    }
    public static class PlayerPrefs {
        public static void SetFloat(string key, float value) { }
        public static float GetFloat(string key, float defaultValue = 0) => defaultValue;
        public static void SetInt(string key, int value) { }
        public static int GetInt(string key, int defaultValue = 0) => defaultValue;
        public static void SetString(string key, string value) { }
        public static string GetString(string key, string defaultValue = "") => defaultValue;
        public static bool HasKey(string key) => false;
        public static void DeleteKey(string key) { }
        public static void Save() { }
    }
    public class Rigidbody : Component {
        public Vector3 velocity { get; set; }
        public Vector3 angularVelocity { get; set; }
        public bool isKinematic { get; set; }
        public bool useGravity { get; set; }
    }
    public class Collider : Component {
        public bool enabled { get; set; }
        public bool isTrigger { get; set; }
    }
    public struct RaycastHit {
        public Vector3 point { get; }
        public Vector3 normal { get; }
        public float distance { get; }
        public Collider collider { get; }
        public Transform transform { get; }
    }
    public enum QueryTriggerInteraction { UseGlobal = 0, Ignore = 1, Collide = 2 }
    public static class Physics {
        // Read out of the Constant table of two shipped UnityEngine.PhysicsModule.dll
        // (Unity 2021.3.45 mono and an IL2CPP interop assembly): IgnoreRaycastLayer = 4,
        // DefaultRaycastLayers = -5, AllLayers = -1. These are consts, so the literal is
        // inlined into every mod that reads one. Four of the fifteen copies of this file
        // had DefaultRaycastLayers = ~0 (-1), which is AllLayers - their aim raycasts hit
        // colliders on the Ignore Raycast layer.
        public const int IgnoreRaycastLayer = 1 << 2;
        public const int DefaultRaycastLayers = ~(1 << 2);
        public const int AllLayers = ~0;
        public static bool Raycast(Ray ray, out RaycastHit hitInfo) { hitInfo = default; return false; }
        public static bool Raycast(Ray ray, out RaycastHit hitInfo, float maxDistance) { hitInfo = default; return false; }
        public static bool Raycast(Ray ray, out RaycastHit hitInfo, float maxDistance, int layerMask) { hitInfo = default; return false; }
        public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hitInfo, float maxDistance) { hitInfo = default; return false; }
        public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hitInfo, float maxDistance, int layerMask) { hitInfo = default; return false; }
        public static bool Raycast(Vector3 origin, Vector3 direction, out RaycastHit hitInfo, float maxDistance, int layerMask, QueryTriggerInteraction queryTriggerInteraction) { hitInfo = default; return false; }
        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance) => false;
        public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, int layerMask) => false;
    }
    public class Canvas : Behaviour {
        public static event WillRenderCanvases willRenderCanvases;
        public delegate void WillRenderCanvases();
        public RenderMode renderMode { get; set; }
        public Camera worldCamera { get; set; }
        public float scaleFactor { get; set; }
    }
    public enum RenderMode { ScreenSpaceOverlay, ScreenSpaceCamera, WorldSpace }
    public class RectTransform : Transform {
        public Vector2 anchoredPosition { get; set; }
        public Vector2 sizeDelta { get; set; }
        public Vector2 anchorMin { get; set; }
        public Vector2 anchorMax { get; set; }
        public Vector2 pivot { get; set; }
        public Rect rect { get; }
    }
    public class CanvasGroup : Behaviour {
        public float alpha { get; set; }
        public bool interactable { get; set; }
        public bool blocksRaycasts { get; set; }
    }
    public static class Resources {
        public static T Load<T>(string path) where T : Object => default;
        public static Object Load(string path) => default;
        public static T[] FindObjectsOfTypeAll<T>() where T : Object => new T[0];
        public static Object[] FindObjectsOfTypeAll(System.Type type) => new Object[0];
    }
    // A STRUCT in the shipped engine, verified on Unity 5.3 and Unity 6. Declared as a class
    // here, `LayerMask m = 8;` emitted `call class LayerMask op_Implicit(int32)` and the
    // reference did not bind at run.
    public struct LayerMask {
        public int value { get; set; }
        public static int NameToLayer(string layerName) => 0;
        public static string LayerToName(int layer) => "";
        public static implicit operator int(LayerMask mask) => mask.value;
        public static implicit operator LayerMask(int intVal) => new LayerMask { value = intVal };
    }
    public static class GL {
        public static void PushMatrix() { }
        public static void PopMatrix() { }
        public static void LoadPixelMatrix() { }
        public static void LoadOrtho() { }
        public static void Begin(int mode) { }
        public static void End() { }
        public static void Vertex3(float x, float y, float z) { }
        public static void Color(Color c) { }
        public const int LINES = 1;
        public const int TRIANGLES = 4;
        public const int QUADS = 7;
    }

    public class Animator : Behaviour {
        public float GetFloat(string name) => 0;
        public float GetFloat(int id) => 0;
        public void SetFloat(string name, float value) { }
        public void SetFloat(int id, float value) { }
        public bool GetBool(string name) => false;
        public void SetBool(string name, bool value) { }
        public int GetInteger(string name) => 0;
        public void SetInteger(string name, int value) { }
        public void SetTrigger(string name) { }
        public static int StringToHash(string name) => 0;
    }
    public static class ColorUtility {
        public static bool TryParseHtmlString(string htmlString, out Color color) { color = default; return false; }
    }
#if !NET35
    // ABSENT from Painscreek and Crawl (pre-2017.3) and from Dorfromantik, whose shipped
    // CoreModule declares neither this enum nor Renderer.motionVectorGenerationMode -
    // managed code stripping drops an unused type even on a current Unity. Reach it by
    // reflection on a game that may have been stripped.
    public enum MotionVectorGenerationMode { Camera = 0, Object = 1, ForceNoMotion = 2 }
#endif
    public class Renderer : Component {
        public UnityEngine.Rendering.ShadowCastingMode shadowCastingMode { get; set; }
#if !NET35
        public MotionVectorGenerationMode motionVectorGenerationMode { get; set; }
#endif
        public bool enabled { get; set; }
        public Material material { get; set; }
        public Material[] materials { get; set; }
        public Material sharedMaterial { get; set; }
        public Bounds bounds { get; }
    }
    public class MeshRenderer : Renderer { }
    public class SkinnedMeshRenderer : Renderer { public bool updateWhenOffscreen { get; set; } }
    public class MeshFilter : Component { public Mesh mesh { get; set; } public Mesh sharedMesh { get; set; } }
    public class Mesh : Object { public Bounds bounds { get; set; } }
    public struct Bounds {
        private Vector3 _center, _size;
        public Bounds(Vector3 center, Vector3 size) { _center = center; _size = size; }
        public Vector3 center { get => _center; set => _center = value; }
        public Vector3 size { get => _size; set => _size = value; }
        public Vector3 extents { get => _size * 0.5f; set => _size = value * 2f; }
        public Vector3 min { get => _center - _size * 0.5f; }
        public Vector3 max { get => _center + _size * 0.5f; }
        public bool Contains(Vector3 point) => false;
        public void Encapsulate(Vector3 point) { }
        public void Encapsulate(Bounds bounds) { }
    }
#if !NET35
    // Unity 2017.3 and later. ABSENT from Painscreek and Crawl. Applying an attribute the
    // game does not declare fails to resolve when the type is loaded, so this one cannot
    // be reached by reflection as a fallback - a net35 mod goes without it.
    [System.AttributeUsage(System.AttributeTargets.Class)] public class DefaultExecutionOrder : System.Attribute { public DefaultExecutionOrder(int order) { } }
#endif
    [System.AttributeUsage(System.AttributeTargets.Field)] public class SerializeField : System.Attribute { }
    [System.AttributeUsage(System.AttributeTargets.Field)] public class HideInInspector : System.Attribute { }
    [System.AttributeUsage(System.AttributeTargets.Field)] public class HeaderAttribute : System.Attribute { public HeaderAttribute(string header) { } }
    [System.AttributeUsage(System.AttributeTargets.Field)] public class TooltipAttribute : System.Attribute { public TooltipAttribute(string tooltip) { } }
    [System.AttributeUsage(System.AttributeTargets.Field)] public class RangeAttribute : System.Attribute { public RangeAttribute(float min, float max) { } }
}
namespace UnityEngine.Rendering {
    public enum ShadowCastingMode { Off = 0, On = 1, TwoSided = 2, ShadowsOnly = 3 }
#if !NET35
    // Scriptable Render Pipelines arrived in Unity 2018. ABSENT from Painscreek and Crawl
    // (pre-2017.3, the net35 targets) and from Tacoma (Unity 2017.2), which builds net472
    // like every modern game - no TargetFramework can express that one, so a mod on a
    // 2017-era game must reach these by reflection the way RenderPipelineHelper does.
    //
    // RenderPipelineAsset is an abstract ScriptableObject, not an enum. A property's type
    // is part of its signature, so GraphicsSettings.currentRenderPipeline declared as
    // UnityEngine.Object emitted a getter returning Object and threw MissingMethodException
    // on a direct call.
    public abstract class RenderPipelineAsset : UnityEngine.ScriptableObject { }
    public static class GraphicsSettings { public static RenderPipelineAsset currentRenderPipeline { get; } }
    public abstract class RenderPipeline { }
    public struct ScriptableRenderContext { }
    public static class RenderPipelineManager {
        public static event System.Action<ScriptableRenderContext, UnityEngine.Camera> beginCameraRendering;
        public static event System.Action<ScriptableRenderContext, UnityEngine.Camera> endCameraRendering;
        public static event System.Action<ScriptableRenderContext, UnityEngine.Camera[]> beginFrameRendering;
        public static event System.Action<ScriptableRenderContext, UnityEngine.Camera[]> endFrameRendering;
    }
#endif
}
namespace UnityEngine.SceneManagement {
    public struct Scene {
        public string name { get; }
        public int buildIndex { get; }
        public bool isLoaded { get; }
        public bool IsValid() => false;
    }
    public enum LoadSceneMode { Single, Additive }
    public static class SceneManager {
        public static Scene GetActiveScene() => default;
        public static void LoadScene(string sceneName) { }
        public static void LoadScene(int sceneBuildIndex) { }
        public static event UnityEngine.Events.UnityAction<Scene, LoadSceneMode> sceneLoaded;
        public static event UnityEngine.Events.UnityAction<Scene, Scene> activeSceneChanged;
    }
}
namespace UnityEngine.Events {
    public class UnityEvent { public void AddListener(UnityAction call) { } public void RemoveListener(UnityAction call) { } public void Invoke() { } }
    public class UnityEvent<T0> { public void AddListener(UnityAction<T0> call) { } public void RemoveListener(UnityAction<T0> call) { } public void Invoke(T0 arg0) { } }
    public delegate void UnityAction();
    public delegate void UnityAction<T0>(T0 arg0);
    public delegate void UnityAction<T0, T1>(T0 arg0, T1 arg1);
}
