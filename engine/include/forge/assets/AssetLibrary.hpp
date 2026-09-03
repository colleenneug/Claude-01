// ============================================================
//  The content browser's backing store.
//
//  One registry per project, holding meshes, materials, textures and
//  script graphs by name. Everything an actor references, it
//  references by name: a level file says "Cube" and "Brick", not a
//  pointer, so a level survives its content being rebuilt.
//
//  Missing content resolves to a visible placeholder rather than a
//  crash — a level that references an asset you deleted should load
//  and tell you, not refuse to open.
// ============================================================
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "forge/assets/Material.hpp"
#include "forge/assets/Mesh.hpp"

namespace forge {

class ScriptGraph;

class AssetLibrary {
public:
    AssetLibrary();
    ~AssetLibrary();

    // Loading the same name twice replaces it, so re-importing content
    // updates every actor that references it without touching the level.
    Mesh* addMesh(std::unique_ptr<Mesh> mesh);
    Material* addMaterial(std::unique_ptr<Material> material);
    Texture* addTexture(std::unique_ptr<Texture> texture);
    ScriptGraph* addScript(std::unique_ptr<ScriptGraph> script);

    Mesh* mesh(const std::string& name) const;
    Material* material(const std::string& name) const;
    Texture* texture(const std::string& name) const;
    ScriptGraph* script(const std::string& name) const;

    // Never null: falls back to the placeholder so a broken reference
    // shows up in the viewport instead of taking the frame down.
    Mesh* meshOrDefault(const std::string& name) const;
    Material* materialOrDefault(const std::string& name) const;

    bool removeMesh(const std::string& name);
    bool removeMaterial(const std::string& name);
    bool removeTexture(const std::string& name);
    bool removeScript(const std::string& name);
    bool renameAsset(const std::string& kind, const std::string& from, const std::string& to);

    std::vector<std::string> meshNames() const;
    std::vector<std::string> materialNames() const;
    std::vector<std::string> textureNames() const;
    std::vector<std::string> scriptNames() const;

    // The set every new project starts with: primitives, a handful of
    // surfaces, and the textures they use.
    void createStarterContent();

    Json toJson() const;
    void loadJson(const Json& j);
    bool save(const std::string& path) const;
    bool load(const std::string& path);

    // Bump whenever content changes, so the renderer knows to drop the
    // GPU buffers it built from the old version.
    uint64_t revision() const { return revision_; }

private:
    std::map<std::string, std::unique_ptr<Mesh>> meshes_;
    std::map<std::string, std::unique_ptr<Material>> materials_;
    std::map<std::string, std::unique_ptr<Texture>> textures_;
    std::map<std::string, std::unique_ptr<ScriptGraph>> scripts_;
    std::unique_ptr<Mesh> placeholderMesh_;
    std::unique_ptr<Material> placeholderMaterial_;
    uint64_t revision_ = 1;
};

} // namespace forge
