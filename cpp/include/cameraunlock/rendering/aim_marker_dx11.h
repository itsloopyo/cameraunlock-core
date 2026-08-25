#pragma once

// The mark that says where the rounds are going, for DX11 games.
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
// Requires the DX11 overlay, so exactly one translation unit in the consuming
// mod must define CAMERAUNLOCK_DX11_OVERLAY_IMPLEMENTATION and
// CAMERAUNLOCK_AIM_MARKER_DX11_IMPLEMENTATION before including this, and MinHook
// must already be initialised by the time the install runs.
//
// Usage, once per rendered frame, from wherever the mod already knows the aim:
//
//     bool visible = false;
//     if (aiming && mode == AdsMode::Marker && marker.Ensure()) visible = !offScreen;
//     marker.Publish(visible, ndcX, ndcY);
//
// Ensure() answers false until the overlay is up, so a mode whose marker never
// came up behaves exactly like AdsMode::Tracked. That is the honest degradation:
// a marker that half-draws is worse than no marker.

#include "cameraunlock/rendering/dx11_overlay.h"

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

class AimMarkerDX11 {
public:
    // Starts the install the first time it is called and returns false until the
    // overlay is drawing. Idempotent, and safe to call from a render frame: the
    // work - a window class, a probe device, two MinHook detours - happens on a
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
    void SetLogger(DX11LogFn fn);

    // How old a published aim may be before the marker stops drawing. Present
    // keeps running when the player's view stops being rendered - a loading
    // screen, a full-screen menu - and a marker left frozen on one of those is
    // pointing at nothing.
    static constexpr unsigned long long kStaleMs = 200;
};

#ifdef CAMERAUNLOCK_AIM_MARKER_DX11_IMPLEMENTATION

}  // namespace cameraunlock::rendering - re-opened after includes

#include <Windows.h>

#include <atomic>
#include <thread>

namespace cameraunlock::rendering {

namespace detail {

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
    DX11Overlay* overlay = nullptr;
};

inline AimMarkerState& MarkerState() {
    static AimMarkerState s;
    return s;
}

inline void DrawMarker(DX11DrawContext& dc) {
    auto& s = MarkerState();
    if (!s.visible.load(std::memory_order_acquire)) return;
    if (GetTickCount64() - s.stampMs.load(std::memory_order_acquire)
            > AimMarkerDX11::kStaleMs) {
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

inline void InstallMarkerOverlay() {
    auto& s = MarkerState();
    s.overlay = new DX11Overlay();
    s.overlay->SetRenderCallback(&DrawMarker);
    if (!s.overlay->Install()) return;
    s.ready.store(true, std::memory_order_release);
}

}  // namespace detail

inline bool AimMarkerDX11::Ensure() {
    auto& s = detail::MarkerState();
    if (s.ready.load(std::memory_order_acquire)) return true;
    if (!s.installStarted.exchange(true, std::memory_order_acq_rel)) {
        std::thread(&detail::InstallMarkerOverlay).detach();
    }
    return false;
}

inline bool AimMarkerDX11::Ready() const {
    return detail::MarkerState().ready.load(std::memory_order_acquire);
}

inline void AimMarkerDX11::Publish(bool visible, float ndcX, float ndcY) {
    auto& s = detail::MarkerState();
    s.ndcX.store(ndcX, std::memory_order_relaxed);
    s.ndcY.store(ndcY, std::memory_order_relaxed);
    s.stampMs.store(GetTickCount64(), std::memory_order_release);
    s.visible.store(visible, std::memory_order_release);
}

inline void AimMarkerDX11::SetStyle(const AimMarkerStyle& style) {
    detail::MarkerState().style = style;
}

inline void AimMarkerDX11::SetLogger(DX11LogFn fn) { SetDX11OverlayLogger(fn); }

#endif  // CAMERAUNLOCK_AIM_MARKER_DX11_IMPLEMENTATION

}  // namespace cameraunlock::rendering
