// ============================================================
//  Materials and textures.
//
//  A material is a physically based surface description: base colour,
//  metalness, roughness, emissive. That set is what the renderer's
//  shading model consumes, and what the details panel exposes.
//
//  Textures are generated, like meshes: a kind and its parameters
//  rather than an image file, so a project carries no binary assets and
//  a checker or a brick pattern costs a line of JSON.
// ============================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "forge/core/Json.hpp"
#include "forge/math/Math.hpp"

namespace forge {

enum class TextureKind { Solid, Checker, Grid, Noise, Bricks, Gradient, Dots };

class Texture {
public:
    std::string name;
    TextureKind kind = TextureKind::Checker;
    int width = 128, height = 128;
    Color colorA = Color::fromHex("#ffffff");
    Color colorB = Color::fromHex("#808080");
    float scale = 8.0f;
    float contrast = 1.0f;
    uint32_t seed = 1;

    // Linear RGBA, row-major from the top.
    std::vector<Color> pixels;

    void rebuild();
    // Bilinear sample with wrapping, which is what the rasteriser calls.
    Color sample(float u, float v) const;
    Color texel(int x, int y) const;

    Json toJson() const;
    static std::unique_ptr<Texture> fromJson(const Json& j);
    static const char* kindName(TextureKind k);
    static TextureKind kindFromName(const std::string& s);
};

class Material {
public:
    std::string name;
    Color baseColor = Color::fromHex("#b8bcc4");
    float metallic = 0.0f;
    float roughness = 0.65f;
    Color emissive{0, 0, 0, 1};
    float emissiveStrength = 0.0f;
    float opacity = 1.0f;
    std::string texture;          // texture asset name, or empty
    Vec2 textureScale{1, 1};
    bool twoSided = false;
    bool castsShadow = true;
    bool unlit = false;

    Json toJson() const;
    static std::unique_ptr<Material> fromJson(const Json& j);
};

} // namespace forge
