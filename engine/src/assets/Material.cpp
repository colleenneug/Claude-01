#include "forge/assets/Material.hpp"

#include <algorithm>

namespace forge {

const char* Texture::kindName(TextureKind k) {
    switch (k) {
        case TextureKind::Solid: return "Solid";
        case TextureKind::Checker: return "Checker";
        case TextureKind::Grid: return "Grid";
        case TextureKind::Noise: return "Noise";
        case TextureKind::Bricks: return "Bricks";
        case TextureKind::Gradient: return "Gradient";
        case TextureKind::Dots: return "Dots";
    }
    return "Checker";
}

TextureKind Texture::kindFromName(const std::string& s) {
    for (int i = 0; i <= (int)TextureKind::Dots; ++i)
        if (s == kindName((TextureKind)i)) return (TextureKind)i;
    return TextureKind::Checker;
}

void Texture::rebuild() {
    width = std::max(1, std::min(width, 1024));
    height = std::max(1, std::min(height, 1024));
    pixels.assign((size_t)width * (size_t)height, colorA);

    const float sx = std::max(0.01f, scale);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float u = ((float)x + 0.5f) / (float)width;
            const float v = ((float)y + 0.5f) / (float)height;
            float t = 0.0f;   // 0 selects colorA, 1 selects colorB

            switch (kind) {
                case TextureKind::Solid:
                    t = 0.0f;
                    break;
                case TextureKind::Checker: {
                    int cx = (int)std::floor(u * sx), cy = (int)std::floor(v * sx);
                    t = ((cx + cy) & 1) ? 1.0f : 0.0f;
                    break;
                }
                case TextureKind::Grid: {
                    float fx = u * sx - std::floor(u * sx);
                    float fy = v * sx - std::floor(v * sx);
                    const float lineWidth = 0.06f;
                    t = (fx < lineWidth || fy < lineWidth) ? 1.0f : 0.0f;
                    break;
                }
                case TextureKind::Noise:
                    t = fbm(u * sx, v * sx, 5, 2.0f, 0.5f, seed);
                    break;
                case TextureKind::Bricks: {
                    // Every other course is offset by half a brick.
                    float row = v * sx;
                    int course = (int)std::floor(row);
                    float offset = (course & 1) ? 0.5f : 0.0f;
                    float col = u * sx * 0.5f + offset;
                    float fx = col - std::floor(col);
                    float fy = row - std::floor(row);
                    const float mortar = 0.05f;
                    bool isMortar = fx < mortar || fy < mortar * 2.0f;
                    // A little per-brick variation keeps a wall from
                    // reading as a repeated stamp.
                    float shade = valueNoise((float)(int)std::floor(col), (float)course, seed) * 0.35f;
                    t = isMortar ? 1.0f : shade;
                    break;
                }
                case TextureKind::Gradient:
                    t = v;
                    break;
                case TextureKind::Dots: {
                    float fx = u * sx - std::floor(u * sx) - 0.5f;
                    float fy = v * sx - std::floor(v * sx) - 0.5f;
                    t = std::sqrt(fx * fx + fy * fy) < 0.3f ? 1.0f : 0.0f;
                    break;
                }
            }

            t = saturate((t - 0.5f) * contrast + 0.5f);
            pixels[(size_t)y * (size_t)width + (size_t)x] = Color::lerp(colorA, colorB, t);
        }
    }
}

Color Texture::texel(int x, int y) const {
    if (pixels.empty()) return colorA;
    // Wrap, so a UV outside [0,1] tiles rather than clamping to a smear.
    x = ((x % width) + width) % width;
    y = ((y % height) + height) % height;
    return pixels[(size_t)y * (size_t)width + (size_t)x];
}

Color Texture::sample(float u, float v) const {
    if (pixels.empty()) return colorA;
    float fx = u * (float)width - 0.5f;
    float fy = v * (float)height - 0.5f;
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    float tx = fx - (float)x0, ty = fy - (float)y0;
    Color c00 = texel(x0, y0), c10 = texel(x0 + 1, y0);
    Color c01 = texel(x0, y0 + 1), c11 = texel(x0 + 1, y0 + 1);
    return Color::lerp(Color::lerp(c00, c10, tx), Color::lerp(c01, c11, tx), ty);
}

Json Texture::toJson() const {
    Json j = Json::object();
    j.set("name", name);
    j.set("kind", kindName(kind));
    j.set("width", width);
    j.set("height", height);
    j.set("colorA", Json::fromColor(colorA));
    j.set("colorB", Json::fromColor(colorB));
    j.set("scale", scale);
    j.set("contrast", contrast);
    j.set("seed", (int)seed);
    return j;
}

std::unique_ptr<Texture> Texture::fromJson(const Json& j) {
    auto t = std::make_unique<Texture>();
    t->name = j["name"].asString("Texture");
    t->kind = kindFromName(j["kind"].asString("Checker"));
    t->width = j["width"].asInt(128);
    t->height = j["height"].asInt(128);
    t->colorA = j["colorA"].asColor(t->colorA);
    t->colorB = j["colorB"].asColor(t->colorB);
    t->scale = j["scale"].asFloat(8.0f);
    t->contrast = j["contrast"].asFloat(1.0f);
    t->seed = (uint32_t)j["seed"].asInt(1);
    t->rebuild();
    return t;
}

Json Material::toJson() const {
    Json j = Json::object();
    j.set("name", name);
    j.set("baseColor", Json::fromColor(baseColor));
    j.set("metallic", metallic);
    j.set("roughness", roughness);
    j.set("emissive", Json::fromColor(emissive));
    j.set("emissiveStrength", emissiveStrength);
    j.set("opacity", opacity);
    if (!texture.empty()) j.set("texture", texture);
    Json ts = Json::array();
    ts.push(textureScale.x);
    ts.push(textureScale.y);
    j.set("textureScale", ts);
    j.set("twoSided", twoSided);
    j.set("castsShadow", castsShadow);
    j.set("unlit", unlit);
    return j;
}

std::unique_ptr<Material> Material::fromJson(const Json& j) {
    auto m = std::make_unique<Material>();
    m->name = j["name"].asString("Material");
    m->baseColor = j["baseColor"].asColor(m->baseColor);
    m->metallic = j["metallic"].asFloat(0.0f);
    m->roughness = j["roughness"].asFloat(0.65f);
    m->emissive = j["emissive"].asColor(m->emissive);
    m->emissiveStrength = j["emissiveStrength"].asFloat(0.0f);
    m->opacity = j["opacity"].asFloat(1.0f);
    m->texture = j["texture"].asString("");
    if (j["textureScale"].size() >= 2)
        m->textureScale = {j["textureScale"][0].asFloat(1), j["textureScale"][1].asFloat(1)};
    m->twoSided = j["twoSided"].asBool(false);
    m->castsShadow = j["castsShadow"].asBool(true);
    m->unlit = j["unlit"].asBool(false);
    return m;
}

} // namespace forge
