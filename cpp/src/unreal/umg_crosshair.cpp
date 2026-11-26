// SPDX-License-Identifier: LicenseRef-CameraUnlock-Core
#include <cameraunlock/unreal/umg_crosshair.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

#include <cameraunlock/logging/file_log.h>
#include <cameraunlock/unreal/crosshair_split.h>
#include <cameraunlock/unreal/ue_object_sweep.h>
#include <cameraunlock/unreal/ue_objects.h>
#include <cameraunlock/unreal/ue_params.h>
#include <cameraunlock/unreal/umg_translation_scale.h>

namespace cameraunlock::unreal {
namespace {

namespace log = ::cameraunlock::logging;

// Templates are never drawn: a class default object, an archetype, and a
// Blueprint's widget-tree template, which is not archetype-flagged but was
// loaded off disk, where a drawn widget is made at runtime
// (pacific-drive-headtracking's notes, "RF_WasLoaded"). A moved template
// would hand its offset to every HUD built from it.
bool Template(const Objects& o, std::uintptr_t obj) { return o.IsTemplate(obj) || (o.Flags(obj) & kWasLoaded); }

// How many outers up a widget is looked for inside the `within` class.
constexpr int kWithinDepth = 16;

// Below this many of the widget's own units a move is not worth a call.
constexpr double kStill = 0.05;

// A measured widget's scale is read again this often, for a HUD rescaled while
// it is up (the window resized, a UI scale setting changed).
constexpr std::uint64_t kMeasureAgainMs = 1000;
// One not painted yet is tried this often, and named in the log once it has
// gone this long without being painted.
constexpr std::uint64_t kMeasureRetryMs = 100;
constexpr std::uint64_t kUnmeasuredSayMs = 10000;
// The local points measured are this many units apart: wide enough that a
// pixel's rounding is far below the half percent a rescale is logged at.
constexpr double kProbeUnits = 1000;
// A scale outside this is a widget not laid out yet, not a scale.
constexpr double kMinPerUnit = 0.05, kMaxPerUnit = 20;
// A measured scale is logged again once it has moved by this share of itself:
// far below a pixel's move, far above the measurement's jitter.
constexpr double kRescaleShare = 0.005;
// The drawn view's axis is taken as landing off the middle of the viewport
// when it is more than this many pixels from it.
constexpr double kOffCentrePx = 0.5;
// A player who has drawn no view for this long is off the screen, and with
// no player after the first left on it the screen is not split.
constexpr double kSplitWithinMs = 1000;
// How many bound widgets are named in the log, so a consumer matching every
// Image of a HUD does not write a line for each.
constexpr std::size_t kNamedWidgets = 8;

std::string Joined(const std::vector<std::string>& all) {
    std::string out;
    for (const std::string& s : all) out += (out.empty() ? "" : ", ") + s;
    return out;
}

}  // namespace

UmgCrosshairMover::UmgCrosshairMover(const Reflection& refl, std::uintptr_t processEvent, const CrosshairWidgets& widgets)
    : objects_(refl.Objs()), refl_(refl), reticle_(widgets) {
    const std::uintptr_t pe = processEvent;
    setTranslation_ = ScriptFunction(refl, pe, "/Script/UMG.Widget:SetRenderTranslation");
    screen_ = Screen(refl, pe);
    viewportScale_ = ScriptFunction(refl, pe, "/Script/UMG.WidgetLayoutLibrary:GetViewportScale");
    cachedGeometry_ = ScriptFunction(refl, pe, "/Script/UMG.Widget:GetCachedGeometry");
    localToViewport_ = ScriptFunction(refl, pe, "/Script/UMG.SlateBlueprintLibrary:LocalToViewport");
    localToAbsolute_ = ScriptFunction(refl, pe, "/Script/UMG.SlateBlueprintLibrary:LocalToAbsolute");
    // UserWidget declares it in A Way Out's Unreal 4.17 and Widget in The Ascent's 4.27.
    const char* const userWidgetOwner = "/Script/UMG.UserWidget:GetOwningPlayer";
    owningPlayer_ = ScriptFunction(
        refl, pe, objects_.FindPath(userWidgetOwner) ? userWidgetOwner : "/Script/UMG.Widget:GetOwningPlayer");
    userWidgetClass_ = objects_.FindPath("/Script/UMG.UserWidget");
    if (!userWidgetClass_) throw std::runtime_error("the UMG UserWidget class is not loaded");
    widgetClass_ = objects_.FindPath("/Script/UMG.Widget");
    if (!widgetClass_) throw std::runtime_error("the UMG Widget class is not loaded");
    slateLibrary_ = objects_.FindPath("/Script/UMG.Default__SlateBlueprintLibrary");
    if (!slateLibrary_) throw std::runtime_error("the SlateBlueprintLibrary default object is not loaded");
    if (cachedGeometry_.Param("ReturnValue").size != localToViewport_.Param("Geometry").size ||
        cachedGeometry_.Param("ReturnValue").size != localToAbsolute_.Param("Geometry").size)
        throw std::runtime_error("Widget:GetCachedGeometry and SlateBlueprintLibrary:LocalToViewport disagree on FGeometry's size");
    translationReal_ = RealOf(setTranslation_, "Translation", 2);
    viewportScale_.Param("WorldContextObject", sizeof(std::uintptr_t));
    viewportScale_.Param("ReturnValue", sizeof(float));
    owningPlayer_.Param("ReturnValue", sizeof(std::uintptr_t));
    localToViewport_.Param("WorldContextObject", sizeof(std::uintptr_t));
    if (RealOf(localToViewport_, "LocalCoordinate", 2) != translationReal_ ||
        RealOf(localToViewport_, "PixelPosition", 2) != translationReal_ ||
        RealOf(localToAbsolute_, "LocalCoordinate", 2) != translationReal_ ||
        RealOf(localToAbsolute_, "ReturnValue", 2) != translationReal_)
        throw std::runtime_error("SlateBlueprintLibrary:LocalToViewport's Vector2Ds are not the size of a render translation");
    const auto renderTransform = refl.Find(widgetClass_, "RenderTransform");
    const std::uintptr_t widgetTransform = objects_.FindPath("/Script/SlateCore.WidgetTransform");
    const std::uintptr_t vector2D = objects_.FindPath("/Script/CoreUObject.Vector2D");
    if (!renderTransform || renderTransform->type != "StructProperty" || !widgetTransform ||
        refl.PropertyStruct(*renderTransform) != widgetTransform)
        throw std::runtime_error("UMG Widget has no RenderTransform of SlateCore's WidgetTransform");
    if (!vector2D) throw std::runtime_error("CoreUObject's Vector2D is not loaded");
    const auto real = [&](std::uintptr_t owner, const char* name, std::int32_t base) {
        const auto f = refl.Find(owner, name);
        if (!f || (f->type != "FloatProperty" && f->type != "DoubleProperty"))
            throw std::runtime_error(std::string(owner == vector2D ? "Vector2D" : "WidgetTransform") + " has no real " + name);
        return Real{base + f->offset, f->type == "DoubleProperty"};
    };
    const auto pair = [&](const char* member) {
        const auto f = refl.Find(widgetTransform, member);
        if (!f || f->type != "StructProperty" || refl.PropertyStruct(*f) != vector2D)
            throw std::runtime_error(std::string("WidgetTransform has no Vector2D ") + member);
        const std::int32_t base = renderTransform->offset + f->offset;
        return std::array<Real, 2>{real(vector2D, "X", base), real(vector2D, "Y", base)};
    };
    translation_ = pair("Translation");
    scale_ = pair("Scale");
    shear_ = pair("Shear");
    angle_ = real(widgetTransform, "Angle", renderTransform->offset);
    if (translation_[0].wide != (translationReal_ == 8))
        throw std::runtime_error("Widget:SetRenderTranslation's Translation is not the size of RenderTransform's");
    patterns_ = !reticle_.holder.empty() ? std::vector<std::string>{reticle_.holder}
              : !reticle_.widgets.empty()  ? reticle_.widgets
                                           : std::vector<std::string>{"Widget"};
    const std::string classes = Joined(patterns_), names = Joined(reticle_.names);
    if (!reticle_.holder.empty())
        log::Line("reticle: moving the widget in %s.%s", classes.c_str(), reticle_.property.c_str());
    else
        log::Line("reticle: moving every %s widget%s%s%s%s", classes.c_str(), names.empty() ? "" : " named ",
                  names.c_str(), reticle_.within.empty() ? "" : " inside a ", reticle_.within.c_str());
}

bool UmgCrosshairMover::ClassMatches(std::uintptr_t cls) {
    if (const auto it = classMatches_.find(cls); it != classMatches_.end()) return it->second;
    const bool match = ::cameraunlock::unreal::ClassMatches(objects_, refl_, cls, patterns_);
    classMatches_.emplace(cls, match);
    return match;
}

bool UmgCrosshairMover::Within(std::uintptr_t object) const {
    const Objects& o = objects_;
    int depth = 0;
    for (std::uintptr_t outer = o.Outer(object); outer && depth < kWithinDepth; outer = o.Outer(outer), ++depth)
        for (std::uintptr_t c = o.Class(outer); c; c = refl_.Super(c))
            if (NameMatches(reticle_.within, o.Name(c))) return true;
    return false;
}

bool UmgCrosshairMover::Named(std::uintptr_t object) const {
    if (reticle_.names.empty()) return true;
    const std::string name = objects_.Name(object);
    for (const std::string& n : reticle_.names)
        if (NameMatches(n, name)) return true;
    return false;
}

std::uintptr_t UmgCrosshairMover::Held(std::uintptr_t holder, std::uintptr_t cls) {
    const Objects& o = objects_;
    auto it = propertyOffsets_.find(cls);
    if (it == propertyOffsets_.end()) {
        const auto f = refl_.Find(cls, reticle_.property);
        const bool object = f && (f->type == "ObjectProperty" || f->type == "ObjectPtrProperty");
        it = propertyOffsets_.emplace(cls, object ? std::optional<std::int32_t>(f->offset) : std::nullopt).first;
        if (!f)
            log::Line("reticle: %s has no property %s", o.Name(cls).c_str(), reticle_.property.c_str());
        else if (!object)
            log::Line("reticle: %s.%s is a %s, not an object, so nothing is moved through it", o.Name(cls).c_str(),
                      reticle_.property.c_str(), f->type.c_str());
    }
    if (!it->second) return 0;
    const std::uintptr_t widget = o.mem->Ptr(holder + static_cast<std::uintptr_t>(*it->second));
    if (!widget) return 0;
    const std::uintptr_t held = o.Class(widget);
    if (!held) return 0;
    if (refl_.IsA(held, widgetClass_)) return widget;
    if (notWidgets_.insert(held).second)
        log::Line("reticle: %s.%s holds a %s, which is no widget, so it is not moved", o.Name(cls).c_str(),
                  reticle_.property.c_str(), o.Name(held).c_str());
    return 0;
}

void UmgCrosshairMover::CheckPlayer(int player) {
    if (player < 0 || player >= kMaxPlayers)
        throw std::out_of_range("crosshair player " + std::to_string(player) + " is outside 0 to " +
                                std::to_string(kMaxPlayers - 1));
}

int UmgCrosshairMover::PlayerOf(const Widget& w) const { return split_ ? split::OwningPlayer(w.owner, onScreen_) : 0; }

void UmgCrosshairMover::Sweep() {
    if (next_ == 0 && GetTickCount64() - passEndedAt_ < kSweepAgainMs) return;
    const Objects& o = objects_;
    const std::int32_t count = ObjectCount(*o.mem, o.array);
    const bool held = !reticle_.holder.empty();
    const std::int32_t end = next_ + kSweepSlice < count ? next_ + kSweepSlice : count;
    for (std::int32_t i = next_; i < end; ++i) {
        const std::uintptr_t obj = ObjectAt(*o.mem, o.array, i);
        if (!obj) continue;
        const std::uintptr_t cls = o.Class(obj);
        if (!cls || !ClassMatches(cls)) continue;
        if (Template(o, obj)) continue;
        if (!Named(obj)) continue;
        if (!reticle_.within.empty() && !Within(obj)) continue;
        const std::uintptr_t widget = held ? Held(obj, cls) : obj;
        if (!widget) continue;
        const std::int32_t index = o.mem->Or0<std::int32_t>(widget + kUObjectIndex);
        if (!Live(o, widget, index)) continue;
        // Every instance of a widget blueprint has the same class and object
        // name, so the path and flags are what tell the one on screen from a
        // torn-down HUD's copy when a crosshair does not follow the aim.
        if (bound_ < kNamedWidgets &&
            std::none_of(widgets_.begin(), widgets_.end(), [&](const Widget& k) { return k.object == widget; })) {
            ++bound_;
            log::Line("reticle: moving %s (flags 0x%x)", o.PathName(widget).c_str(),
                      o.Flags(widget));
        }
        sweeping_.push_back(Widget{widget, index});
    }
    next_ = end;
    if (next_ >= count) EndPass();
}

void UmgCrosshairMover::EndPass() {
    // A widget already being moved keeps what it was last given, as it stands
    // now: the pass took several frames, and a copy taken when its slice was
    // read is that many frames old.
    for (Widget& w : sweeping_)
        for (const Widget& k : widgets_)
            if (k.object == w.object && k.index == w.index) w = k;
    // One this pass no longer finds (its holder points at another, or it no
    // longer matches) goes back to its layout, or it would stay off centre
    // where nothing moves it again.
    for (Widget& k : widgets_) {
        bool kept = false;
        for (const Widget& w : sweeping_) kept = kept || (k.object == w.object && k.index == w.index);
        if (!kept && k.set && Live(objects_, k.object, k.index)) PutBack(k);
    }
    for (Widget& w : sweeping_) w.ownerRead = false;
    widgets_.swap(sweeping_);
    sweeping_.clear();
    next_ = 0;
    passEndedAt_ = GetTickCount64();
    if (widgets_.size() != reported_) {
        reported_ = widgets_.size();
        log::Line("reticle: %zu crosshair widget%s live", reported_, reported_ == 1 ? "" : "s");
    }
}

double UmgCrosshairMover::Read(std::uintptr_t widget, const Real& r) const {
    const std::uintptr_t at = widget + static_cast<std::uintptr_t>(r.at);
    return r.wide ? objects_.mem->Or0<double>(at) : static_cast<double>(objects_.mem->Or0<float>(at));
}

bool UmgCrosshairMover::Translation(std::uintptr_t widget, double& x, double& y) const {
    const memory::Memory& mem = *objects_.mem;
    const auto get = [&](const Real& r, double& out) {
        const std::uintptr_t at = widget + static_cast<std::uintptr_t>(r.at);
        if (r.wide) return mem.Get(at, out);
        float f = 0;
        if (!mem.Get(at, f)) return false;
        out = f;
        return true;
    };
    return get(translation_[0], x) && get(translation_[1], y);
}

void UmgCrosshairMover::Move(Widget& w, double x, double y) {
    // What the widget holds now, not what was last set: a game that lays its
    // crosshair out each frame writes the translation back, and a mover that
    // only compared against its own last value would never set it again.
    double heldX = 0, heldY = 0;
    const bool read = Translation(w.object, heldX, heldY);
    if (!read && !w.saidUnread) {
        w.saidUnread = true;
        log::Line("reticle: %s's render translation does not read, so a game writing over it is not seen",
                  objects_.PathName(w.object).c_str());
    }
    if (read && w.set && !w.retaken && (std::fabs(heldX - w.x) >= kStill || std::fabs(heldY - w.y) >= kStill)) {
        w.retaken = true;
        log::Line("reticle: the game put %s back to (%.1f, %.1f) after it was moved to (%.1f, %.1f); it is moved "
                  "again whenever that happens",
                  objects_.PathName(w.object).c_str(), heldX, heldY, w.x, w.y);
    }
    if (read && std::fabs(x - heldX) < kStill && std::fabs(y - heldY) < kStill) {
        w.x = x, w.y = y, w.set = true;
        return;
    }
    ScriptFunction::Frame f(setTranslation_);
    SetVector2D(f, "Translation", translationReal_, x, y);
    f.Call(w.object);
    w.x = x, w.y = y, w.set = true;
}

void UmgCrosshairMover::PutBack(Widget& w) {
    double heldX = 0, heldY = 0;
    // A widget the game has moved since is the game's already; one whose
    // translation does not read is put back regardless (Move logs it).
    const bool read = Translation(w.object, heldX, heldY);
    if (!read || (std::fabs(heldX - w.x) < kStill && std::fabs(heldY - w.y) < kStill)) Move(w, 0, 0);
    w.set = false;
}

void UmgCrosshairMover::Release(int player) {
    CheckPlayer(player);
    for (Widget& w : widgets_)
        if (w.set && PlayerOf(w) == player && Live(objects_, w.object, w.index)) PutBack(w);
}

void UmgCrosshairMover::See(int player, std::uintptr_t controller) {
    CheckPlayer(player);
    const double now = static_cast<double>(GetTickCount64());
    controllers_[static_cast<std::size_t>(player)] = controller;
    seenAtMs_[static_cast<std::size_t>(player)] = now;
    split_ = split::SplitScreen(seenAtMs_, now, kSplitWithinMs);
    for (std::size_t i = 0; i < onScreen_.size(); ++i)
        onScreen_[i] = seenAtMs_[i] > 0 && now - seenAtMs_[i] <= kSplitWithinMs ? controllers_[i] : 0;
}

void UmgCrosshairMover::ReadOwner(Widget& w) {
    if (w.ownerRead) return;
    w.ownerRead = true;
    const std::uintptr_t before = w.owner;
    const Objects& o = objects_;
    // A user widget laid inside another may have no owner of its own (Hazelight's
    // WBP_RotationWidget's StickLabel), so the walk goes on out to the
    // first user widget that has one.
    w.owner = 0;
    bool user = false;
    std::uintptr_t at = w.object;
    for (int depth = 0; at && !w.owner && depth < kWithinDepth; at = o.Outer(at), ++depth) {
        if (!refl_.IsA(o.Class(at), userWidgetClass_)) continue;
        user = true;
        ScriptFunction::Frame f(owningPlayer_);
        f.Call(at);
        w.owner = f.Get<std::uintptr_t>("ReturnValue");
    }
    if (w.ownerSaid && w.owner == before) return;
    w.ownerSaid = true;
    log::Line("reticle: %s is owned by %s, so player %d's head moves it", o.PathName(w.object).c_str(),
              w.owner ? o.PathName(w.owner).c_str() : user ? "no player controller" : "no user widget", PlayerOf(w) + 1);
}

std::array<double, 2> UmgCrosshairMover::Probe(const ScriptFunction& fn, const char* out, std::uintptr_t controller,
                                   const std::uint8_t* geometry, double x, double y) const {
    ScriptFunction::Frame f(fn);
    if (controller) f.Set("WorldContextObject", controller);
    f.SetBytes("Geometry", geometry, static_cast<std::size_t>(cachedGeometry_.Param("ReturnValue").size));
    SetVector2D(f, "LocalCoordinate", translationReal_, x, y);
    f.Call(slateLibrary_);
    return GetVector2D(f, out, translationReal_);
}

void UmgCrosshairMover::Measure(std::uintptr_t controller, Widget& w) {
    const std::uint64_t now = GetTickCount64();
    if (!w.foundAt) w.foundAt = now;
    if (now - w.measuredAt < (w.perX > 0 ? kMeasureAgainMs : kMeasureRetryMs)) return;
    w.measuredAt = now;
    const double scaleX = Read(w.object, scale_[0]), scaleY = Read(w.object, scale_[1]);
    const double shearX = Read(w.object, shear_[0]), shearY = Read(w.object, shear_[1]);
    const double angle = Read(w.object, angle_);
    // A shear of its own is not taken back out of the measurement (reticle_scale.h).
    if (shearX != 0 || shearY != 0 || std::fabs(scaleX) < kNoRenderScale || std::fabs(scaleY) < kNoRenderScale) {
        if (!w.saidUnmovable) {
            w.saidUnmovable = true;
            log::Line("reticle: %s has a render scale of %.4f x %.4f and a shear of %.4f x %.4f; only an unsheared one "
                      "with a scale is moved, so it is not",
                      objects_.PathName(w.object).c_str(), scaleX, scaleY, shearX, shearY);
        }
        w.perX = w.perY = 0;
        return;
    }
    ScriptFunction::Frame geometry(cachedGeometry_);
    geometry.Call(w.object);
    const std::uint8_t* g = geometry.Bytes("ReturnValue");
    // A geometry Slate never painted is the default one, laid at the desktop's
    // origin with unit scale; a painted crosshair is never exactly that.
    const auto a0 = Probe(localToAbsolute_, "ReturnValue", 0, g, 0, 0);
    const auto a1 = Probe(localToAbsolute_, "ReturnValue", 0, g, kProbeUnits, kProbeUnits);
    const bool painted = !(a0[0] == 0 && a0[1] == 0 && a1[0] == kProbeUnits && a1[1] == kProbeUnits);
    // The widget's own painted geometry carries every scale above it, panels
    // without a size of their own included (Voices of the Void lays its
    // crosshair in one scaled past the DPI scale: 0.823 pixels a unit against
    // a DPI scale of 0.666, voices-of-the-void-headtracking src/reticle.cpp),
    // and its own render scale and turn last, which are taken back out.
    double perX = 0, perY = 0;
    if (painted) {
        const auto p0 = Probe(localToViewport_, "PixelPosition", controller, g, 0, 0);
        const auto px = Probe(localToViewport_, "PixelPosition", controller, g, kProbeUnits, 0);
        const auto py = Probe(localToViewport_, "PixelPosition", controller, g, 0, kProbeUnits);
        const auto per = TranslationScale({px[0] - p0[0], px[1] - p0[1]}, {py[0] - p0[0], py[1] - p0[1]}, kProbeUnits,
                                          scaleX, scaleY, angle);
        perX = per[0], perY = per[1];
    }
    if (painted && perX > kMinPerUnit && perX < kMaxPerUnit && perY > kMinPerUnit && perY < kMaxPerUnit) {
        w.perX = perX, w.perY = perY;
        return;
    }
    if (!(w.perX > 0) && !w.saidUnmeasured && now - w.foundAt >= kUnmeasuredSayMs) {
        w.saidUnmeasured = true;
        if (painted)
            log::Line("reticle: %s is painted at %.4f x %.4f pixels a unit of its translation, which is not a scale, so it is "
                      "not moved",
                      objects_.PathName(w.object).c_str(), perX, perY);
        else
            log::Line("reticle: %s has not been painted in %llu s, so it is not moved until it is", objects_.PathName(w.object).c_str(),
                      static_cast<unsigned long long>(kUnmeasuredSayMs / 1000));
    }
}

void UmgCrosshairMover::Update(int player, std::uintptr_t controller, const FVector& aim, const FVector& ahead) {
    CheckPlayer(player);
    const std::size_t p = static_cast<std::size_t>(player);
    Sweep();
    if (widgets_.empty()) return;

    const std::array<double, 2> size = screen_.Size(controller);
    const double width = size[0], height = size[1];

    // An aim the head has turned out of view holds the mark at the frame's
    // edge rather than sending it back to the middle, where it would read as
    // the aim (deep-rock-galactic-headtracking src/view_hook.cpp:376-388).
    // In split screen that edge is the player's own part of the viewport.
    const auto at = screen_.Project(controller, aim);
    const auto rest = screen_.Project(controller, ahead);
    const auto restInPart = split_ ? screen_.Project(controller, ahead, true) : rest;
    if (at && rest && restInPart && width > 0 && height > 0) {
        const double halfW = width / 2, halfH = height / 2;
        const auto held = split::HeldIn(split::PlayerRect(*rest, *restInPart, size, split_), *at);
        last_[p] = {held[0] - (*rest)[0], held[1] - (*rest)[1]};
        if (!split_ && (std::fabs((*rest)[0] - halfW) > kOffCentrePx || std::fabs((*rest)[1] - halfH) > kOffCentrePx)) {
            if (!saidOffCentre_) {
                saidOffCentre_ = true;
                log::Line("reticle: the drawn view's axis lands at (%.1f, %.1f) of a %.0f x %.0f viewport, not the middle; "
                          "the crosshair is moved from there",
                          (*rest)[0], (*rest)[1], width, height);
            }
        }
    }
    for (Widget& w : widgets_) {
        if (!Live(objects_, w.object, w.index)) continue;
        if (split_) ReadOwner(w);
        if (PlayerOf(w) != player) continue;
        Measure(controller, w);
        if (!(w.perX > 0)) continue;
        if (std::fabs(w.perX - w.reported) > w.reported * kRescaleShare) {
            w.reported = w.perX;
            ScriptFunction::Frame scale(viewportScale_);
            scale.Set("WorldContextObject", controller);
            scale.Call(screen_.layoutLibrary);
            const double dpi = scale.Get<float>("ReturnValue");
            log::Line("reticle: %.4f x %.4f viewport pixels per unit of render translation, measured off %s (DPI scale says %.4f)",
                      w.perX, w.perY, objects_.Name(w.object).c_str(), dpi);
        }
        Move(w, last_[p][0] / w.perX, last_[p][1] / w.perY);
    }
}

}  // namespace cameraunlock::unreal
