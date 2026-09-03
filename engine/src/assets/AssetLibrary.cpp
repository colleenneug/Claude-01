#include "forge/assets/AssetLibrary.hpp"

#include "forge/core/Log.hpp"
#include "forge/script/ScriptGraph.hpp"

namespace forge {

AssetLibrary::AssetLibrary() {
    // The placeholder is deliberately loud: a magenta box is impossible
    // to mistake for content you meant to see.
    placeholderMesh_ = Mesh::makeBox(Vec3(1.0f));
    placeholderMesh_->name = "__missing";
    placeholderMaterial_ = std::make_unique<Material>();
    placeholderMaterial_->name = "__missing";
    placeholderMaterial_->baseColor = Color::fromHex("#ff00ff");
    placeholderMaterial_->roughness = 0.9f;
}

AssetLibrary::~AssetLibrary() = default;

Mesh* AssetLibrary::addMesh(std::unique_ptr<Mesh> m) {
    if (!m || m->name.empty()) return nullptr;
    Mesh* raw = m.get();
    meshes_[m->name] = std::move(m);
    ++revision_;
    return raw;
}

Material* AssetLibrary::addMaterial(std::unique_ptr<Material> m) {
    if (!m || m->name.empty()) return nullptr;
    Material* raw = m.get();
    materials_[m->name] = std::move(m);
    ++revision_;
    return raw;
}

Texture* AssetLibrary::addTexture(std::unique_ptr<Texture> t) {
    if (!t || t->name.empty()) return nullptr;
    if (t->pixels.empty()) t->rebuild();
    Texture* raw = t.get();
    textures_[t->name] = std::move(t);
    ++revision_;
    return raw;
}

ScriptGraph* AssetLibrary::addScript(std::unique_ptr<ScriptGraph> s) {
    if (!s || s->name.empty()) return nullptr;
    ScriptGraph* raw = s.get();
    scripts_[s->name] = std::move(s);
    ++revision_;
    return raw;
}

template <typename Map>
static typename Map::mapped_type::element_type* lookup(const Map& map, const std::string& name) {
    auto it = map.find(name);
    return it == map.end() ? nullptr : it->second.get();
}

Mesh* AssetLibrary::mesh(const std::string& n) const { return lookup(meshes_, n); }
Material* AssetLibrary::material(const std::string& n) const { return lookup(materials_, n); }
Texture* AssetLibrary::texture(const std::string& n) const { return lookup(textures_, n); }
ScriptGraph* AssetLibrary::script(const std::string& n) const { return lookup(scripts_, n); }

Mesh* AssetLibrary::meshOrDefault(const std::string& n) const {
    Mesh* m = mesh(n);
    return m ? m : placeholderMesh_.get();
}

Material* AssetLibrary::materialOrDefault(const std::string& n) const {
    Material* m = material(n);
    return m ? m : placeholderMaterial_.get();
}

bool AssetLibrary::removeMesh(const std::string& n) { ++revision_; return meshes_.erase(n) > 0; }
bool AssetLibrary::removeMaterial(const std::string& n) { ++revision_; return materials_.erase(n) > 0; }
bool AssetLibrary::removeTexture(const std::string& n) { ++revision_; return textures_.erase(n) > 0; }
bool AssetLibrary::removeScript(const std::string& n) { ++revision_; return scripts_.erase(n) > 0; }

bool AssetLibrary::renameAsset(const std::string& kind, const std::string& from, const std::string& to) {
    if (to.empty() || from == to) return false;
    auto move = [&](auto& map, auto setName) {
        auto it = map.find(from);
        if (it == map.end() || map.count(to)) return false;
        auto node = std::move(it->second);
        map.erase(it);
        setName(node.get());
        map[to] = std::move(node);
        ++revision_;
        return true;
    };
    if (kind == "mesh") return move(meshes_, [&](Mesh* m) { m->name = to; });
    if (kind == "material") return move(materials_, [&](Material* m) { m->name = to; });
    if (kind == "texture") return move(textures_, [&](Texture* t) { t->name = to; });
    if (kind == "script") return move(scripts_, [&](ScriptGraph* s) { s->name = to; });
    return false;
}

template <typename Map>
static std::vector<std::string> keysOf(const Map& map) {
    std::vector<std::string> out;
    out.reserve(map.size());
    for (const auto& kv : map) out.push_back(kv.first);
    return out;
}

std::vector<std::string> AssetLibrary::meshNames() const { return keysOf(meshes_); }
std::vector<std::string> AssetLibrary::materialNames() const { return keysOf(materials_); }
std::vector<std::string> AssetLibrary::textureNames() const { return keysOf(textures_); }
std::vector<std::string> AssetLibrary::scriptNames() const { return keysOf(scripts_); }

void AssetLibrary::createStarterContent() {
    // ---- primitives ----
    auto shape = [&](const char* name, ShapeKind kind, ShapeParams p) {
        addMesh(Mesh::makeShape(name, kind, p));
    };
    ShapeParams p;
    shape("Cube", ShapeKind::Box, p);
    p.size = {1, 1, 1}; p.radius = 0.5f; p.segments = 24; p.rings = 16;
    shape("Sphere", ShapeKind::Sphere, p);
    p.height = 1.0f;
    shape("Cylinder", ShapeKind::Cylinder, p);
    shape("Cone", ShapeKind::Cone, p);
    p.height = 2.0f;
    shape("Capsule", ShapeKind::Capsule, p);
    p = ShapeParams{};
    p.size = {10, 1, 10}; p.segments = 8;
    shape("Plane", ShapeKind::Plane, p);
    p = ShapeParams{};
    p.radius = 0.5f; p.innerRadius = 0.18f; p.segments = 32; p.rings = 16;
    shape("Torus", ShapeKind::Torus, p);
    p = ShapeParams{};
    shape("Wedge", ShapeKind::Wedge, p);
    shape("Pyramid", ShapeKind::Pyramid, p);
    p.size = {2, 2, 4}; p.steps = 8;
    shape("Stairs", ShapeKind::Stairs, p);
    p = ShapeParams{};
    p.size = {3, 3, 1}; p.radius = 1.1f; p.segments = 16;
    shape("Arch", ShapeKind::Arch, p);
    p = ShapeParams{};
    p.radius = 0.5f; p.innerRadius = 0.35f; p.segments = 32;
    shape("Ring", ShapeKind::Ring, p);

    // ---- textures ----
    auto tex = [&](const char* name, TextureKind kind, const char* a, const char* b, float scale) {
        auto t = std::make_unique<Texture>();
        t->name = name;
        t->kind = kind;
        t->colorA = Color::fromHex(a);
        t->colorB = Color::fromHex(b);
        t->scale = scale;
        t->rebuild();
        addTexture(std::move(t));
    };
    tex("CheckerLight", TextureKind::Checker, "#d8d8d8", "#9aa0a8", 8.0f);
    tex("GridBlue", TextureKind::Grid, "#2c3440", "#5fa8d3", 16.0f);
    tex("BrickRed", TextureKind::Bricks, "#8c4a3a", "#d9cbb8", 10.0f);
    tex("ConcreteNoise", TextureKind::Noise, "#8d8d8a", "#b4b3ae", 6.0f);

    // ---- materials ----
    auto mat = [&](const char* name, const char* hex, float metal, float rough,
                   const char* texture = "", float emissive = 0.0f) {
        auto m = std::make_unique<Material>();
        m->name = name;
        m->baseColor = Color::fromHex(hex);
        m->metallic = metal;
        m->roughness = rough;
        m->texture = texture;
        if (emissive > 0.0f) {
            m->emissive = Color::fromHex(hex);
            m->emissiveStrength = emissive;
        }
        addMaterial(std::move(m));
    };
    mat("Default", "#b8bcc4", 0.0f, 0.65f);
    mat("Checker", "#ffffff", 0.0f, 0.7f, "CheckerLight");
    mat("Grid", "#ffffff", 0.05f, 0.55f, "GridBlue");
    mat("Brick", "#ffffff", 0.0f, 0.85f, "BrickRed");
    mat("Concrete", "#ffffff", 0.0f, 0.9f, "ConcreteNoise");
    mat("Steel", "#c9ced6", 0.9f, 0.28f);
    mat("Gold", "#d4a441", 1.0f, 0.22f);
    mat("Rubber", "#2b2f36", 0.0f, 0.95f);
    mat("Grass", "#5d8a4a", 0.0f, 0.9f);
    mat("Water", "#3f7fa8", 0.1f, 0.12f);
    mat("GlowCyan", "#4fe3ff", 0.0f, 0.4f, "", 4.0f);
    mat("GlowAmber", "#ffb454", 0.0f, 0.4f, "", 4.0f);
    FORGE_LOG("Starter content: %zu meshes, %zu materials, %zu textures",
              meshes_.size(), materials_.size(), textures_.size());
}

Json AssetLibrary::toJson() const {
    Json j = Json::object();
    Json ms = Json::array(), mats = Json::array(), texs = Json::array(), scr = Json::array();
    for (const auto& kv : meshes_) ms.push(kv.second->toJson());
    for (const auto& kv : materials_) mats.push(kv.second->toJson());
    for (const auto& kv : textures_) texs.push(kv.second->toJson());
    for (const auto& kv : scripts_) scr.push(kv.second->toJson());
    j.set("meshes", ms);
    j.set("materials", mats);
    j.set("textures", texs);
    j.set("scripts", scr);
    return j;
}

void AssetLibrary::loadJson(const Json& j) {
    // Textures before materials: a material resolves its texture by name
    // when the renderer builds it, so the order only matters for the
    // warning, but keeping it makes a partial load easier to read.
    for (size_t i = 0; i < j["textures"].size(); ++i) addTexture(Texture::fromJson(j["textures"][i]));
    for (size_t i = 0; i < j["meshes"].size(); ++i) addMesh(Mesh::fromJson(j["meshes"][i]));
    for (size_t i = 0; i < j["materials"].size(); ++i) addMaterial(Material::fromJson(j["materials"][i]));
    for (size_t i = 0; i < j["scripts"].size(); ++i) addScript(ScriptGraph::fromJson(j["scripts"][i]));
    ++revision_;
}

bool AssetLibrary::save(const std::string& path) const { return toJson().saveFile(path); }

bool AssetLibrary::load(const std::string& path) {
    std::string err;
    Json j = Json::loadFile(path, &err);
    if (!err.empty()) { FORGE_ERROR("assets: %s", err.c_str()); return false; }
    loadJson(j);
    return true;
}

} // namespace forge
