using System;
using System.Reflection;
using Xunit;

namespace CameraUnlock.Core.Unity.Tests
{
    /// <summary>
    /// Covers <c>SplitInjectionCameraTracker</c>'s camera-name filter.
    ///
    /// This file ships to IL2CPP mods as source (csharp/il2cpp/*.props) rather than in any
    /// assembly, so nothing here compiled it until it was linked into this project - and it
    /// had misclassified real camera names in both directions with nothing to catch it. The
    /// filter is what decides whether a camera gets head tracking at all, and both failure
    /// directions are silent: a missed exclusion applies tracking to a HUD camera, a false
    /// exclusion drops tracking on a real one with only a log line.
    ///
    /// MatchExcludedToken is private, so it is reached by reflection. That is deliberate -
    /// widening the shipped type's visibility purely for a test would change the surface
    /// every consuming mod compiles.
    /// </summary>
    public class CameraNameTokenTests
    {
        private static readonly MethodInfo Match = typeof(CameraUnlock.Core.Unity.Il2Cpp.SplitInjectionCameraTracker)
            .GetMethod("MatchExcludedToken", BindingFlags.NonPublic | BindingFlags.Static);

        private static bool IsExcluded(string name)
        {
            Assert.NotNull(Match);
            return Match.Invoke(null, new object[] { name }) != null;
        }

        [Theory]
        // Plain separated and camel-cased forms, which the character rules always handled.
        [InlineData("UICamera")]
        [InlineData("ui_camera")]
        [InlineData("UI Camera")]
        [InlineData("hudUI")]
        [InlineData("InventoryCamera")]
        [InlineData("OverlayCam")]
        // Run-together lowercase, which needs the tail-word rule: there is no separator and
        // no camel hump for the boundary test to see.
        [InlineData("uicam")]
        [InlineData("uicamera")]
        [InlineData("uicanvas")]
        [InlineData("uiroot")]
        [InlineData("uioverlay")]
        [InlineData("uilayer")]
        [InlineData("uiview")]
        [InlineData("inventorycam")]
        public void ExcludesUiCameras(string name)
        {
            Assert.True(IsExcluded(name), $"expected '{name}' to be excluded");
        }

        [Theory]
        // Real camera names that must keep tracking.
        [InlineData("MainCamera")]
        [InlineData("PlayerCamera")]
        [InlineData("FirstPersonCamera")]
        [InlineData("WeaponCam")]
        [InlineData("cam")]
        [InlineData("Camera")]
        // An uppercase run does not start a token, so the "ui" inside "GUI" is not a match.
        [InlineData("GUICamera")]
        [InlineData("GUI_Camera")]
        // A lowercase letter is not a separator. These carry "ui" mid-word and are ordinary
        // names - Yui, Rui, Sui and Gui are all common romanisations, and a per-character
        // camera called "yuicamera" silently lost tracking.
        [InlineData("yuicamera")]
        [InlineData("yuicam")]
        [InlineData("ruicam")]
        [InlineData("suicam")]
        [InlineData("YuiCam")]
        [InlineData("EquiviewCamera")]
        [InlineData("guidancecam")]
        [InlineData("Equipment")]
        // The tail word has to run to a boundary, so a word merely starting with one does
        // not close the token. Otherwise "cam" swallows camp/camo/campaign/camshaft.
        [InlineData("uicamp")]
        [InlineData("uicamshaft")]
        [InlineData("uirooted")]
        public void KeepsRealCameras(string name)
        {
            Assert.False(IsExcluded(name), $"expected '{name}' to be tracked");
        }

        [Fact]
        public void MatchIsCaseInsensitive()
        {
            Assert.True(IsExcluded("UICAMERA"));
            Assert.True(IsExcluded("uiCAM"));
        }

        [Fact]
        public void ReportsWhichTokenMatched()
        {
            Assert.Equal("ui", Match.Invoke(null, new object[] { "UICamera" }));
            Assert.Equal("inventory", Match.Invoke(null, new object[] { "InventoryCam" }));
            Assert.Equal("overlay", Match.Invoke(null, new object[] { "OverlayCam" }));
        }

        [Theory]
        [InlineData("")]
        [InlineData("u")]
        [InlineData("ui")]
        public void HandlesShortAndEmptyNames(string name)
        {
            // "ui" alone is a bare excluded token and must match; the point here is that
            // none of these index past the end.
            bool excluded = IsExcluded(name);
            Assert.Equal(name == "ui", excluded);
        }
    }
}
