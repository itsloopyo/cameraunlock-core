// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <cameraunlock/unreal/ue_call.h>
#include <cameraunlock/unreal/ue_math.h>
#include <cameraunlock/unreal/ue_reflection.h>
#include <cameraunlock/unreal/ue_screen.h>

// Moves the game's own crosshair onto the aim, for a game whose crosshair is a
// UMG widget the consumer names (CrosshairWidgets). A game that draws its
// crosshair at the middle of the screen is right only while the view is the
// aim; once the head turns the view, the mark has to go where the shot will.
// The move is the engine's own UWidget::SetRenderTranslation, found by
// reflection and called through ProcessEvent, so there is one mark on screen,
// no graphics API is touched and no per-build address is needed.
//
// A render translation is in the units of the widget's parent, which every
// scale between it and the viewport stretches, not only the DPI scale: Voices
// of the Void's HUD sits under a scale of its own, and DPI alone put its
// crosshair 23% short (voices-of-the-void-headtracking src/reticle.cpp:131-139).
// So each widget's viewport pixels per unit are measured off the painted
// geometry of its nearest sized ancestor, through SlateBlueprintLibrary::LocalToViewport,
// and a widget is not moved until it has been measured, which it is within a
// tenth of a second of Slate first painting its parent. UmgReticle
// (umg_reticle.h) scales by the DPI scale alone and has that defect; this
// class is its replacement.
//
// In split screen each local player's crosshair is the one whose user widget
// that player's controller owns (UserWidget:GetOwningPlayer). It is moved by
// that player's own head pose, projected through that player's view, and held
// inside that player's part of the viewport.
//
// Windows x64 only: it calls into the game.
namespace cameraunlock::unreal {

// Which widgets are the crosshair. Either every live object of class `holder`
// keeps a crosshair widget in its object property `property`, or every live
// widget of a class in `widgets` (any UMG Widget when both are empty) is one.
// Class and name patterns match by NameMatches.
struct CrosshairWidgets {
    std::string holder;
    std::string property;
    std::vector<std::string> widgets;
    // Object name patterns, for a crosshair that is a plain Image or panel
    // known only by its name in the widget tree; with no `widgets` the class
    // must still be a UMG Widget (a texture can share the name).
    std::vector<std::string> names;
    // A class pattern one of the widget's outers must match: the HUD that
    // holds it.
    std::string within;
};

class UmgCrosshairMover {
public:
    static constexpr int kMaxPlayers = 4;

    // `processEvent` is UObject::ProcessEvent (FindProcessEvent). `refl` and
    // the Objects it reads must outlive the mover. Throws when UMG's functions
    // are not loaded.
    UmgCrosshairMover(const Reflection& refl, std::uintptr_t processEvent, const CrosshairWidgets& widgets);

    // Game thread, once a frame for each player (0 to kMaxPlayers - 1) whose
    // view takes a head pose, after the view is decided: places `player`'s
    // crosshairs, drawn for `controller`, where `aim` lands in that player's
    // view, measured from where `ahead` (a point on the drawn view's own axis)
    // lands. A crosshair laid out at rest sits where the game's view axis
    // meets the screen, which is not the middle when the game's projection is
    // off centre: The Outer Worlds' axis lands 46.8 px below the middle at
    // 1280x720, with its crosshair and shots.
    void Update(int player, std::uintptr_t controller, const FVector& aim, const FVector& ahead);

    // Game thread, every frame each player's view is drawn, posed or not:
    // which controller is that player's, and that the player is on screen.
    void See(int player, std::uintptr_t controller);

    // Game thread, a frame with no head pose on `player`'s view: every crosshair
    // of that player's this mover placed off its layout and still holding that
    // place goes back to it, once, and is then left to the game until Update
    // runs again for that player.
    void Release(int player);

private:
    struct Widget {
        std::uintptr_t object = 0;
        std::int32_t index = -1;
        double x = 0, y = 0;  // the translation last set, in the widget's own units
        // Viewport pixels per unit of render translation, 0 until measured.
        double perX = 0, perY = 0;
        std::uint64_t measuredAt = 0;  // when it was last measured or tried
        std::uint64_t foundAt = 0;
        double reported = 0;  // the scale last written to the log
        bool saidUnmeasured = false;
        bool saidUnmovable = false;
        bool set = false;     // whether this mover has set it yet
        bool retaken = false; // whether the game was seen putting it back, said once
        bool saidUnread = false;  // whether its translation was found unreadable, said once
        // The controller owning the user widget it is or is inside; 0 for
        // none. Read in split screen only, once each sweep pass.
        std::uintptr_t owner = 0;
        bool ownerRead = false;
        bool ownerSaid = false;
    };

    void Sweep();
    // A pass over the whole array is done: what it found is what is moved.
    void EndPass();
    // Whether `object` has one of the names asked for, or none are asked for.
    bool Named(std::uintptr_t object) const;
    // The widget the `property` of `holder`, an object of `cls`, points at; 0 for none.
    std::uintptr_t Held(std::uintptr_t holder, std::uintptr_t cls);
    // Whether an outer of `object` is of the `within` class.
    bool Within(std::uintptr_t object) const;
    bool ClassMatches(std::uintptr_t cls);
    // Reads w.owner unless it was this sweep pass.
    void ReadOwner(Widget& w);
    int PlayerOf(const Widget& w) const;
    void Move(Widget& w, double x, double y);
    // Back to its layout (0,0) unless the game has moved it since, then left to the game.
    void PutBack(Widget& w);
    // Measures w.perX and w.perY off the widget's own painted geometry, its
    // own render scale and turn taken back out: an unmeasured one ten times a
    // second, a measured one again once a second, in case its HUD is rescaled.
    // A geometry Slate has not painted leaves it as it was.
    void Measure(std::uintptr_t controller, Widget& w);
    // One of SlateBlueprintLibrary's local-coordinate conversions on a geometry.
    std::array<double, 2> Probe(const ScriptFunction& fn, const char* out, std::uintptr_t controller,
                                const std::uint8_t* geometry, double x, double y) const;
    // The widget's RenderTransform.Translation as it holds it now, which the
    // game's own code may have written since this mover last did.
    bool Translation(std::uintptr_t widget, double& x, double& y) const;

    const Objects& objects_;
    const Reflection& refl_;
    CrosshairWidgets reticle_;
    std::vector<std::string> patterns_;  // the holder class, or the widget classes
    ScriptFunction setTranslation_;
    ScriptFunction viewportScale_;
    Screen screen_;
    ScriptFunction cachedGeometry_;
    ScriptFunction localToViewport_;
    ScriptFunction localToAbsolute_;
    ScriptFunction owningPlayer_;
    std::uintptr_t userWidgetClass_ = 0;
    std::uintptr_t slateLibrary_ = 0;
    std::size_t translationReal_ = 4;
    std::size_t bound_ = 0;  // widgets named in the log so far

    std::vector<Widget> widgets_;
    std::vector<Widget> sweeping_;
    std::int32_t next_ = 0;  // the slot the sweep in progress reads next
    std::uint64_t passEndedAt_ = 0;
    std::size_t reported_ = static_cast<std::size_t>(-1);
    // Each player's controller while that player draws views, 0 once it has
    // drawn none for kSplitWithinMs, and when each last drew one, from See.
    std::array<std::uintptr_t, kMaxPlayers> onScreen_{};
    std::array<std::uintptr_t, kMaxPlayers> controllers_{};
    std::array<double, kMaxPlayers> seenAtMs_{};
    // Whether a player after the first has drawn a view lately: split screen.
    bool split_ = false;
    // Where each player's aim lands, in viewport pixels off where the drawn view's axis lands.
    std::array<std::array<double, 2>, kMaxPlayers> last_{};
    bool saidOffCentre_ = false;
    std::map<std::uintptr_t, bool> classMatches_;
    std::map<std::uintptr_t, std::optional<std::int32_t>> propertyOffsets_;
    std::uintptr_t widgetClass_ = 0;
    // Game thread: the classes the holder's property held that are no widget's, each said once.
    std::set<std::uintptr_t> notWidgets_;
    // UWidget::RenderTransform's members, as reflection lays FWidgetTransform
    // and FVector2D out: each real's offset in the widget, and whether it is a double.
    struct Real {
        std::int32_t at = 0;
        bool wide = false;
    };
    std::array<Real, 2> translation_{}, scale_{}, shear_{};
    Real angle_;
    double Read(std::uintptr_t widget, const Real& r) const;
};

}  // namespace cameraunlock::unreal
