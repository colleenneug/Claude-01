// ============================================================
//  What the renderer is handed each frame.
//
//  The scene is gathered from the world once and then rendered — the
//  renderer never walks the actor graph itself. That separation is what
//  lets the same frame be drawn by a different backend, rendered from a
//  second camera for a shadow map, or built by hand in a test.
// ============================================================
#pragma once

#include <vector>

#include "forge/math/Math.hpp"

namespace forge {

class Mesh;
class Material;
class Texture;
class World;
class Actor;

struct DrawItem {
    const Mesh* mesh = nullptr;
    const Material* material = nullptr;
    const Texture* texture = nullptr;
    Mat4 transform;
    Mat4 normalBasis;      // rotation only, for transforming normals
    Color tint{1, 1, 1, 1};
    Box worldBounds;
    bool castShadow = true;
    bool selected = false;   // the editor outlines these
    const Actor* actor = nullptr;
};

enum class LightKind { Directional, Point, Spot };

struct LightItem {
    LightKind kind = LightKind::Point;
    Vec3 position{0, 0, 0};
    Vec3 direction{0, -1, 0};
    Color color{1, 1, 1, 1};
    float intensity = 10.0f;
    float radius = 12.0f;
    float innerCos = 0.9f;
    float outerCos = 0.8f;
};

struct RenderView {
    Mat4 view;
    Mat4 projection;
    Vec3 cameraPosition{0, 0, 0};
    float nearClip = 0.1f;
    float farClip = 500.0f;
};

// Everything that is not geometry: sky, fog, tone mapping.
struct RenderEnvironment {
    Color background = Color::fromHex("#8fb3d9");
    Color skyColor = Color::fromHex("#b9d4f2");
    Color groundColor = Color::fromHex("#4a4238");
    float ambientIntensity = 0.75f;
    bool fogEnabled = true;
    Color fogColor = Color::fromHex("#9dbcd8");
    float fogDensity = 0.006f;
    float exposure = 1.0f;
    float vignette = 0.3f;
    bool shadows = true;
};

struct RenderScene {
    std::vector<DrawItem> items;
    std::vector<LightItem> lights;
    RenderEnvironment environment;
    // The sun is kept apart from the light list: it is the only light
    // that casts the shadow map, and it has no position to attenuate from.
    LightItem sun;
    bool hasSun = true;

    void clear() {
        items.clear();
        lights.clear();
        hasSun = false;
    }

    // Walks the world and fills this in. `editorView` keeps actors marked
    // hidden-in-game visible, which is what the editor wants.
    static RenderScene collect(const World& world, bool editorView = false,
                               const Actor* selected = nullptr);
};

} // namespace forge
