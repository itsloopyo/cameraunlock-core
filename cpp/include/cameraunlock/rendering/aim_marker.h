#pragma once

// The mark that says where the rounds are going, for any overlay backend.
//
// AdsMode::Marker exists for games with no aim indicator the mod can move while
// the sights are up - the sight picture is the weapon's own irons, holo or
// optic, drawn on the gun, and a scope's reticle is only honest while the eye
// sits exactly on the optic, which is precisely what head tracking breaks. With
// tracking live through an aim there is then nothing on screen saying where the
// shot lands, and the mod owes the player one.
//
// This is the drawing half only. The projection is NOT here and must never be
// re-derived here: the caller hands over the same clean-aim screen position its
// existing reticle compensation already computes, because two projections drift
// apart and only one of them can be right.
//
// What it owns:
//   - the fixed marker style (small, high contrast, translucent - the mode is
//     the on/off switch and nothing here earns a config entry),
//   - the publish/consume handoff between the mod's render hook and Present,
//   - a staleness cut-off, so a marker cannot sit frozen on a loading screen,
//   - a lazy one-shot install on a worker thread, so a mod only ever patches the
//     swap chain for a player who has actually asked for the marker.
//
// It is written once against OverlayDrawList::DrawCross and parameterised on the
// backend, so a mod running on Direct3D 12 gets the same mark, in the same place,
// as one running on Direct3D 11. Use the aliases in aim_marker_dx11.h /
// aim_marker_dx12.h rather than naming the template.
//
// Ensure() answers false until the overlay is up, so a mode whose marker never
// came up behaves exactly like AdsMode::Tracked. That is the honest degradation:
// a marker that half-draws is worse than no marker.

#include "cameraunlock/rendering/overlay_draw_list.h"

namespace cameraunlock::rendering {

// Fixed style. A black cross behind a white one so the mark reads against snow
// and against a night sky without either being opaque.
struct AimMarkerStyle {
    float arm_pixels = 9.0f;
    float gap_pixels = 3.0f;
    float thickness_pixels = 2.0f;
    Rgba ink = 0xE6FFFFFF;      // AABBGGRR, white
    Rgba outline = 0x99000000;  // black, softer
};

// `Traits` binds one graphics backend:
//
//   using Overlay     = <the backend's overlay class>;
//   using DrawContext = <the backend's draw context>;
//   static void SetLogger(OverlayLogFn fn);
//
// See DX11AimMarkerTraits / DX12AimMarkerTraits.
template <typename Traits>
class AimMarker {
public:
    // Starts the install the first time it is called and returns false until the
    // overlay is drawing. Idempotent, and safe to call from a render frame: the
    // work - a window class, a probe device, a few MinHook detours - happens on a
    // worker thread, never inside the frame that asked.
    bool Ensure();

    bool Ready() const;

    // The aim in the drawn frame, x right, y up, -1..1. `visible` is the
    // caller's per-frame decision and is never latched here: an invalid
    // projection must publish false rather than leave the last position
    // standing, which would put a mark where the rounds are not going.
    void Publish(bool visible, float ndcX, float ndcY);

    // Singleton-by-design, like the overlay underneath it: there is one HUD per
    // process. Both of these write shared state without a lock, so set them from
    // the mod thread BEFORE Ensure() rather than from a frame.
    void SetStyle(const AimMarkerStyle& style);
    void SetLogger(OverlayLogFn fn);

    // How old a published aim may be before the marker stops drawing. Present
    // keeps running when the player's view stops being rendered - a loading
    // screen, a full-screen menu - and a marker left frozen on one of those is
    // pointing at nothing.
    static constexpr unsigned long long kStaleMs = 200;
};

#ifdef CAMERAUNLOCK_AIM_MARKER_IMPLEMENTATION

}  // namespace cameraunlock::rendering - re-opened after includes

#include <Windows.h>

#include <atomic>
#include <thread>

namespace cameraunlock::rendering {

namespace detail {

template <typename Traits>
struct AimMarkerState {
    // Written by the mod's render hook, read in Present. Those are the same
    // thread in every engine we have shipped against, but they are different
    // call stacks reached through different hooks, so the handful of bytes
    // between them are atomics rather than an assumption.
    std::atomic<bool> visible{false};
    std::atomic<float> ndcX{0.0f};
    std::atomic<float> ndcY{0.0f};
    std::atomic<unsigned long long> stampMs{0};

    std::atomic<bool> installStarted{false};
    std::atomic<bool> ready{false};

    AimMarkerStyle style;

    // Leaked, never destroyed. A destructor at process exit would tear down
    // MinHook detours under the loader lock, on threads ExitProcess has already
    // killed - the same reason every mod in the fleet leaks its plugin.
    typename Traits::Overlay* overlay = nullptr;
};

template <typename Traits>
inline AimMarkerState<Traits>& MarkerState() {
    static AimMarkerState<Traits> s;
    return s;
}

template <typename Traits>
inline void DrawMarker(typename Traits::DrawContext& dc) {
    auto& s = MarkerState<Traits>();
    if (!s.visible.load(std::memory_order_acquire)) return;
    if (GetTickCount64() - s.stampMs.load(std::memory_order_acquire)
            > AimMarker<Traits>::kStaleMs) {
        return;
    }

    const float ndcX = s.ndcX.load(std::memory_order_relaxed);
    const float ndcY = s.ndcY.load(std::memory_order_relaxed);
    // NDC y is up, the back buffer's is down.
    const float px = (ndcX * 0.5f + 0.5f) * dc.Width();
    const float py = (0.5f - ndcY * 0.5f) * dc.Height();

    const AimMarkerStyle& st = s.style;
    dc.DrawCross(px, py, st.arm_pixels + 1.0f, st.outline,
                 st.thickness_pixels + 2.0f, st.gap_pixels - 1.0f);
    dc.DrawCross(px, py, st.arm_pixels, st.ink, st.thickness_pixels, st.gap_pixels);
}

template <typename Traits>
inline void InstallMarkerOverlay() {
    auto& s = MarkerState<Traits>();
    s.overlay = new typename Traits::Overlay();
    s.overlay->SetRenderCallback(&DrawMarker<Traits>);
    if (!s.overlay->Install()) return;
    s.ready.store(true, std::memory_order_release);
}

}  // namespace detail

template <typename Traits>
inline bool AimMarker<Traits>::Ensure() {
    auto& s = detail::MarkerState<Traits>();
    if (s.ready.load(std::memory_order_acquire)) return true;
    if (!s.installStarted.exchange(true, std::memory_order_acq_rel)) {
        std::thread(&detail::InstallMarkerOverlay<Traits>).detach();
    }
    return false;
}

template <typename Traits>
inline bool AimMarker<Traits>::Ready() const {
    return detail::MarkerState<Traits>().ready.load(std::memory_order_acquire);
}

template <typename Traits>
inline void AimMarker<Traits>::Publish(bool visible, float ndcX, float ndcY) {
    auto& s = detail::MarkerState<Traits>();
    s.ndcX.store(ndcX, std::memory_order_relaxed);
    s.ndcY.store(ndcY, std::memory_order_relaxed);
    s.stampMs.store(GetTickCount64(), std::memory_order_release);
    s.visible.store(visible, std::memory_order_release);
}

template <typename Traits>
inline void AimMarker<Traits>::SetStyle(const AimMarkerStyle& style) {
    detail::MarkerState<Traits>().style = style;
}

template <typename Traits>
inline void AimMarker<Traits>::SetLogger(OverlayLogFn fn) {
    Traits::SetLogger(fn);
}

#endif  // CAMERAUNLOCK_AIM_MARKER_IMPLEMENTATION

}  // namespace cameraunlock::rendering
