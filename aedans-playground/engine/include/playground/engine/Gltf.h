#pragma once
// =============================================================================
//  playground/engine/Gltf.h — glTF 2.0 models, through the AssetManager.
//
//  glTF ("GL Transmission Format") is the standard file format for 3D
//  models: Blender, Maya and most tools export it. A model is JSON (.gltf)
//  plus binary buffers and images, or all of it in one binary file (.glb).
//
//  LOADING, IN THE ASSET MANAGER'S TWO HALVES (engine/Assets.h)
//
//    decode    worker thread: decodeGltf() parses the file (tinygltf),
//              decodes its PNG/JPEG images to RGBA (stb_image), and turns
//              every triangle primitive into a render::MeshData: positions,
//              normals, tangents and texture coordinates. It flattens the
//              node tree into a list of draws, each with its world
//              transform. No GPU, so it is also what the tests check.
//
//    finalize  main thread: uploadModel() creates one GPU mesh per
//              primitive and one texture per image (base colour as sRGB,
//              normal and metallic-roughness maps as plain data), and makes
//              the materials point at them.
//
//      assets.registerType<engine::Model>(engine::gltfLoader(renderer));
//      auto helmet = assets.load<engine::Model>("models/helmet.glb");
//      ... each frame, once Ready:
//      if (const Model* model = assets.get(helmet)) model->extract(transform, frame);
//
//  WHAT IS READ
//    Triangles (indexed or not; 8-, 16- or 32-bit indices), POSITION,
//    NORMAL, TANGENT, TEXCOORD_0 (float or normalised integers), the node
//    tree (matrices or translation/rotation/scale) of the default scene, and
//    metallic-roughness materials: base colour, metallic, roughness and
//    normal textures and factors, double-sided.
//  MADE WHEN MISSING, as the glTF specification says:
//    no normals   flat normals (each triangle gets its own corners), and
//                 any tangents in the file are ignored;
//    no tangents  computed from the positions, normals and texture
//                 coordinates (per-triangle, averaged per vertex: close to,
//                 but not exactly, the MikkTSpace tangents of the spec);
//    no indices   0, 1, 2, ...;  no texture coordinates: (0, 0).
//  NOT SUPPORTED (yet): skins, morph targets, animation, cameras, lights,
//    vertex colours, second UV sets, per-texture samplers (one renderer-wide
//    sampler is used), alpha blending and masking (drawn opaque, with a
//    warning), emissive, and any extension a file marks as REQUIRED (Draco,
//    meshopt, KTX2/Basis textures...): those files fail with Errc::Unsupported
//    naming the extension. Points, lines and strips are skipped with a warning.
//
//  LIFETIME: a Model owns its GPU meshes and textures and destroys them (via
//  the renderer) when it is unloaded. So every Model must go before the
//  renderer does: call AssetManager::clear() before shutting the renderer down.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/engine/Assets.h"
#include "playground/render/FrameData.h"
#include "playground/render/Renderer.h"
#include "playground/render/Resources.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace playground::engine {

// Axis-aligned bounding box, world (model) units. Empty when min > max.
struct Bounds {
  glm::vec3 min{1e30f};
  glm::vec3 max{-1e30f};

  [[nodiscard]] bool empty() const noexcept { return min.x > max.x; }
  [[nodiscard]] glm::vec3 centre() const noexcept { return 0.5f * (min + max); }
  [[nodiscard]] glm::vec3 size() const noexcept { return empty() ? glm::vec3(0.0f) : max - min; }
  void add(glm::vec3 point) noexcept;
};

// A model on the CPU: what decodeGltf makes, on a worker thread.
struct ModelData {
  // A material as the file describes it; images by index into `images`
  // (-1: none, the renderer's default applies).
  struct MaterialData {
    std::string name;
    glm::vec4 baseColourFactor{1.0f};
    f32 metallicFactor = 1.0f;
    f32 roughnessFactor = 1.0f;
    f32 normalScale = 1.0f;
    bool doubleSided = false;
    i32 baseColourImage = -1;
    i32 normalImage = -1;
    i32 metallicRoughnessImage = -1;
  };
  // One glTF primitive: a mesh with one material.
  struct Primitive {
    std::string name;                // "<mesh name>/<primitive number>"
    render::MeshData mesh;
    u32 material = 0;                // into `materials`; the last one is glTF's default material
    Bounds bounds;                   // of its own positions
  };
  // One primitive drawn at one place. A mesh used by two nodes is drawn twice.
  struct Draw {
    u32 primitive = 0;
    glm::mat4 transform{1.0f};  // model -> world: the product of the node and all its parents
  };

  std::vector<render::ImageData> images;  // RGBA8, decoded
  std::vector<MaterialData> materials;    // the file's, then one default
  std::vector<Primitive> primitives;
  std::vector<Draw> draws;
  Bounds bounds;  // of every draw, transformed
};

// Parses glTF 2.0: .glb if `file` starts with the binary magic "glTF",
// .gltf (JSON) otherwise. External buffers and images are read relative to
// `path`'s folder. Errc::InvalidArgument for a file that is not valid glTF
// (the message names the file and the problem), Errc::Unsupported for a
// required extension we do not have, Errc::IoFailure for a missing buffer or
// image file. Thread-safe: runs on worker threads.
[[nodiscard]] Result<ModelData> decodeGltf(std::span<const std::byte> file, const std::filesystem::path& path);

// A model on the GPU: meshes, textures and materials, owned. Move-only.
class Model {
 public:
  struct Draw {
    render::MeshHandle mesh;
    u32 material = 0;
    glm::mat4 transform{1.0f};
  };

  Model() = default;
  Model(Model&& other) noexcept;
  Model& operator=(Model&& other) noexcept;
  Model(const Model&) = delete;
  Model& operator=(const Model&) = delete;
  ~Model();  // destroys its meshes and textures in the renderer

  // Adds every draw to `frame`, placed by `transform` (model -> world).
  void extract(const glm::mat4& transform, render::FrameData& frame) const;

  [[nodiscard]] std::span<const Draw> draws() const noexcept { return draws_; }
  [[nodiscard]] std::span<const render::Material> materials() const noexcept { return materials_; }
  [[nodiscard]] usize meshCount() const noexcept { return meshes_.size(); }
  [[nodiscard]] usize textureCount() const noexcept { return textures_.size(); }
  [[nodiscard]] const Bounds& bounds() const noexcept { return bounds_; }

 private:
  friend Result<Model> uploadModel(render::Renderer& renderer, ModelData&& data, std::string_view name);
  void release() noexcept;

  render::Renderer* renderer_ = nullptr;
  std::vector<render::MeshHandle> meshes_;
  std::vector<render::TextureHandle> textures_;
  std::vector<render::Material> materials_;
  std::vector<Draw> draws_;
  Bounds bounds_;
};

// The finalize half: uploads `data` through `renderer` (main thread). On
// failure, whatever was already uploaded is destroyed again.
[[nodiscard]] Result<Model> uploadModel(render::Renderer& renderer, ModelData&& data, std::string_view name);

// decodeGltf + uploadModel, for AssetManager::registerType<Model>. The
// renderer must outlive every Model loaded with it.
[[nodiscard]] AssetLoader<Model, ModelData> gltfLoader(render::Renderer& renderer);

}  // namespace playground::engine
