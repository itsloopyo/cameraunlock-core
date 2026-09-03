#pragma once

// The 2D primitives an overlay draws, and nothing that knows which graphics API
// draws them.
//
// Every overlay in this directory accumulates the same thing: a triangle list in
// pixel space, top-left origin, packed colour per vertex. Only the upload and
// the draw call differ between D3D9, D3D11 and D3D12. Keeping the primitives
// here is what lets aim_marker.h be written once against `DrawCross` rather than
// once per API, and it is why a mod can move from one backend to another without
// its render callback changing shape.
//
// Free of Windows headers and of any D3D header on purpose, so it costs nothing
// to include from a header that is not the implementation TU.

#include <cmath>
#include <cstdint>
#include <vector>

namespace cameraunlock::rendering {

// 0xAABBGGRR. That is R8G8B8A8_UNORM read back as a little-endian uint32, which
// is the byte order all three backends declare their colour attribute in, so one
// literal means the same colour everywhere.
using Rgba = std::uint32_t;

// Pixel coords plus packed colour. Laid out to match the input element
// descriptions in the backends: 8 bytes of float2, then 4 bytes of UNORM colour.
struct OverlayVertex {
    float x;
    float y;
    Rgba  color;
};

// Optional diagnostic log sink, shared by every overlay.
using OverlayLogFn = void (*)(const char* msg);

// Accumulates primitives into a CPU-side vector; the overlay flushes them once
// per frame. Pixel space, top-left origin, y down - the backends do the NDC
// conversion in their vertex shader.
class OverlayDrawList {
public:
    OverlayDrawList(float w, float h) : m_width(w), m_height(h) {}

    float Width()  const { return m_width;  }
    float Height() const { return m_height; }

    void DrawRect(float x, float y, float w, float h, Rgba color) {
        // Wound one way only. Every backend disables culling, so the winding is
        // not load-bearing and does not have to agree with anyone's convention.
        const OverlayVertex v0{x,     y,     color};
        const OverlayVertex v1{x + w, y,     color};
        const OverlayVertex v2{x + w, y + h, color};
        const OverlayVertex v3{x,     y + h, color};
        m_triVerts.push_back(v0); m_triVerts.push_back(v1); m_triVerts.push_back(v2);
        m_triVerts.push_back(v0); m_triVerts.push_back(v2); m_triVerts.push_back(v3);
    }

    // A thin quad rather than a line primitive: line-list rasterisation is one
    // pixel wide on most adapters whatever thickness is asked for, so a marker
    // built on it disappears against a busy frame at high resolution.
    void DrawLine(float x1, float y1, float x2, float y2, Rgba color,
                  float thickness = 1.0f) {
        const float dx = x2 - x1, dy = y2 - y1;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-3f) return;
        const float nx = -dy / len, ny = dx / len;  // perpendicular
        const float t = thickness * 0.5f;
        const float ox = nx * t, oy = ny * t;

        const OverlayVertex a{x1 - ox, y1 - oy, color};
        const OverlayVertex b{x2 - ox, y2 - oy, color};
        const OverlayVertex c{x2 + ox, y2 + oy, color};
        const OverlayVertex d{x1 + ox, y1 + oy, color};
        m_triVerts.push_back(a); m_triVerts.push_back(b); m_triVerts.push_back(c);
        m_triVerts.push_back(a); m_triVerts.push_back(c); m_triVerts.push_back(d);
    }

    void DrawDot(float cx, float cy, float radius, Rgba color) {
        constexpr int kSegments = 16;
        constexpr float kTau = 6.28318530718f;
        const OverlayVertex centre{cx, cy, color};
        for (int i = 0; i < kSegments; ++i) {
            const float a0 = (kTau * i) / kSegments;
            const float a1 = (kTau * (i + 1)) / kSegments;
            const OverlayVertex p0{cx + std::cos(a0) * radius, cy + std::sin(a0) * radius, color};
            const OverlayVertex p1{cx + std::cos(a1) * radius, cy + std::sin(a1) * radius, color};
            m_triVerts.push_back(centre);
            m_triVerts.push_back(p0);
            m_triVerts.push_back(p1);
        }
    }

    // Crosshair: four segments centred at (cx, cy), each `arm` long, with a
    // central `gap` left empty so the thing being aimed at stays visible.
    void DrawCross(float cx, float cy, float arm, Rgba color,
                   float thickness = 1.0f, float gap = 0.0f) {
        if (arm <= gap) return;
        DrawLine(cx - arm, cy, cx - gap, cy, color, thickness);
        DrawLine(cx + gap, cy, cx + arm, cy, color, thickness);
        DrawLine(cx, cy - arm, cx, cy - gap, color, thickness);
        DrawLine(cx, cy + gap, cx, cy + arm, color, thickness);
    }

    const std::vector<OverlayVertex>& TriVerts() const { return m_triVerts; }

private:
    float m_width;
    float m_height;
    std::vector<OverlayVertex> m_triVerts;  // triangle list
};

// The HLSL every backend compiles. One copy, because a divergence between them
// is a marker that sits in a different place depending on the player's renderer,
// which is the one bug this whole file exists to make impossible.
//
// Pixel (0..W, 0..H) in, NDC out; passthrough colour.
inline const char* const kOverlayHLSL = R"(
cbuffer cb : register(b0) { float2 g_invHalfViewport; float2 _pad; };
struct VSIn  { float2 pos : POSITION; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR0; };
VSOut VSMain(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos.x * g_invHalfViewport.x - 1.0,
                   1.0 - i.pos.y * g_invHalfViewport.y, 0, 1);
    o.col = i.col;
    return o;
}
float4 PSMain(VSOut i) : SV_TARGET { return i.col; }
)";

}  // namespace cameraunlock::rendering
