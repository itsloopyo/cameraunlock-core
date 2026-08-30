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

// The base chain is part of the contract too, and it was flattened here: Graphic derived
// from Behaviour and Image/RawImage/Text from Graphic, skipping UIBehaviour and
// MaskableGraphic. Every shipped UnityEngine.UI.dll checked, from Unity 5.3 (Crawl,
// Painscreek) to Unity 6 (Valheim), declares the same five links:
//   UIBehaviour : MonoBehaviour  (namespace UnityEngine.EventSystems, in UnityEngine.UI.dll)
//   Graphic : UIBehaviour        (abstract)
//   MaskableGraphic : Graphic    (abstract)
//   Image / RawImage / Text : MaskableGraphic
// The flattening bound at run, because the real Image is still a Behaviour through the
// longer chain, but a mod that needs MaskableGraphic.maskable or anything on MonoBehaviour
// could not compile against it.

// Every shipped UnityEngine.UI.dll checked carries AssemblyVersion 1.0.0.0, where
// UnityEngine.dll and the module assemblies carry 0.0.0.0. The attribute lives here
// rather than in a csproj because both build paths set GenerateAssemblyInfo=false, and
// with that off an <AssemblyVersion> property emits nothing and csc falls back to
// 0.0.0.0. Neither assembly is strong-named, so Mono binds either way; naming the real
// version means nothing here rests on that.
[assembly: System.Reflection.AssemblyVersion("1.0.0.0")]

namespace UnityEngine.EventSystems {
    public abstract class UIBehaviour : UnityEngine.MonoBehaviour {
        protected UIBehaviour() { }
    }
}

namespace UnityEngine.UI {
    public abstract class Graphic : UnityEngine.EventSystems.UIBehaviour {
        public UnityEngine.Color color { get; set; }
        public bool raycastTarget { get; set; }
        public UnityEngine.RectTransform rectTransform { get; }
        public UnityEngine.Canvas canvas { get; }
        public virtual void SetNativeSize() { }
    }
    public abstract class MaskableGraphic : Graphic {
        public bool maskable { get; set; }
    }
    public class Image : MaskableGraphic {
        public UnityEngine.Sprite sprite { get; set; }
        public Type type { get; set; }
        public bool fillCenter { get; set; }
        public enum Type { Simple, Sliced, Tiled, Filled }
    }
    public class RawImage : MaskableGraphic {
        public UnityEngine.Texture texture { get; set; }
        public UnityEngine.Rect uvRect { get; set; }
    }
    public class Text : MaskableGraphic {
        public string text { get; set; }
        public UnityEngine.Font font { get; set; }
        public int fontSize { get; set; }
        public UnityEngine.TextAnchor alignment { get; set; }
    }
}
