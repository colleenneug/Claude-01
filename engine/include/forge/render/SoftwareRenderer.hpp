// ============================================================
//  The software rasteriser.
//
//  A complete renderer with no GPU and no dependencies. That is not a
//  fallback for its own sake: it means the engine builds and renders
//  identically on a workstation, a headless build machine and a test
//  run, and it means the renderer can be checked by asserting on
//  pixels rather than by looking at a window.
//
//  The pipeline, in order:
//
//    shadow pass    depth-only render from the sun into an orthographic
//                   map fitted around the camera
//    main pass      transform, clip against the near plane, rasterise
//                   with a depth buffer and perspective-correct
//                   interpolation, shade per pixel
//    shading        a physically based model: Lambert diffuse and GGX
//                   specular, sun plus point and spot lights, a
//                   hemisphere ambient term, then height fog
//    resolve        exposure, ACES tone mapping, vignette
//
//  Everything is linear until the framebuffer is converted for display.
// ============================================================
#pragma once

#include <memory>
#include <vector>

#include "forge/render/Framebuffer.hpp"
#include "forge/render/RenderScene.hpp"

namespace forge {

struct RenderStats {
    int trianglesSubmitted = 0;
    int trianglesDrawn = 0;
    int itemsCulled = 0;
    int pixelsShaded = 0;
    double milliseconds = 0.0;
};

class SoftwareRenderer {
public:
    SoftwareRenderer();
    ~SoftwareRenderer();

    void render(Framebuffer& target, const RenderScene& scene, const RenderView& view);

    // Depth read back for editor picking: the world-space point under a
    // pixel, which is what makes click-to-select agree with what is drawn.
    bool worldPositionAt(int x, int y, const RenderView& view, Vec3& out) const;
    float depthAt(int x, int y) const;

    const RenderStats& stats() const { return stats_; }

    // Quality knobs. Shadow resolution of 0 turns the shadow pass off.
    int shadowResolution = 1024;
    float shadowExtent = 40.0f;
    // In world units. Converting to depth units internally keeps the
    // bias meaningful when the map's extent changes -- expressed in
    // normalised depth it would silently scale with the range.
    float shadowBias = 0.02f;
    bool wireframe = false;
    // Draws every item's collision-free bounding box. An editor aid.
    bool showBounds = false;

private:
    struct ShadedVertex {
        Vec4 clip;
        Vec3 world;
        Vec3 normal;
        Vec2 uv;
    };

    struct Raster {
        int width = 0, height = 0;
        std::vector<float> depth;
    };

    void renderShadowMap(const RenderScene& scene, const RenderView& view);
    void drawItem(Framebuffer& target, const DrawItem& item, const RenderScene& scene,
                  const RenderView& view, const Mat4& viewProj);
    void rasterTriangle(Framebuffer& target, const ShadedVertex& a, const ShadedVertex& b,
                        const ShadedVertex& c, const DrawItem& item, const RenderScene& scene,
                        const RenderView& view);
    void drawLine3D(Framebuffer& target, const Mat4& viewProj, const Vec3& a, const Vec3& b,
                    const Color& color);
    Color shade(const Vec3& worldPos, const Vec3& normal, const Vec2& uv, const DrawItem& item,
                const RenderScene& scene, const RenderView& view) const;
    float sunShadowFactor(const Vec3& worldPos, const Vec3& normal) const;

    Framebuffer* target_ = nullptr;
    std::vector<float> depth_;
    int width_ = 0, height_ = 0;

    // Shadow map: an orthographic depth buffer in the sun's space.
    std::vector<float> shadowDepth_;
    Mat4 shadowMatrix_;
    float shadowDepthRange_ = 1.0f;
    bool shadowValid_ = false;

    RenderStats stats_;
};

} // namespace forge
