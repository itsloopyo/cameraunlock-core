// The canonical uGUI reference stubs, compiled into UnityEngine.UI.dll by
// csharp/stubs/build-unity-stubs.ps1. Shared by cameraunlock-core and every mod that
// builds without a game installed.
//
// These live in their own assembly and must stay there. The shipped
// UnityEngine.dll is a facade that type-forwards every engine MODULE type
// (Camera, Canvas, RectTransform, ...), so a stub that declares those in
// UnityEngine.dll still resolves at runtime. uGUI is a package, not a module:
// the real UnityEngine.dll carries no forwarder for the UnityEngine.UI
// namespace at all. Declaring Image/Graphic/Text in the UnityEngine stub emits
// typerefs to [UnityEngine]UnityEngine.UI.Image, which nothing can resolve, and
// every method that touches one throws TypeLoadException at runtime.

namespace UnityEngine.UI {
    public abstract class Graphic : UnityEngine.Behaviour {
        public UnityEngine.Color color { get; set; }
        public bool raycastTarget { get; set; }
        public UnityEngine.RectTransform rectTransform { get; }
        public UnityEngine.Canvas canvas { get; }
        public virtual void SetNativeSize() { }
    }
    public class Image : Graphic {
        public UnityEngine.Sprite sprite { get; set; }
        public Type type { get; set; }
        public bool fillCenter { get; set; }
        public enum Type { Simple, Sliced, Tiled, Filled }
    }
    public class RawImage : Graphic {
        public UnityEngine.Texture texture { get; set; }
        public UnityEngine.Rect uvRect { get; set; }
    }
    public class Text : Graphic {
        public string text { get; set; }
        public UnityEngine.Font font { get; set; }
        public int fontSize { get; set; }
        public UnityEngine.TextAnchor alignment { get; set; }
    }
}
