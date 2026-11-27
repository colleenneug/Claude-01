#pragma once
// =============================================================================
//  playground/render/Resources.h — meshes, textures and materials, as plain data.
//
//  No Vulkan here (see render/Renderer.h). The engine describes geometry and
//  pixels with these types, hands them to Renderer::createMesh /
//  createTexture once, and gets back a HANDLE: a small number that names the
//  GPU copy. Each frame it then says "draw mesh M with material X at
//  transform T" (MeshInstance, in FrameData) without touching GPU memory.
//
//  HANDLES are an index plus a generation, like ecs::Entity and
//  engine::AssetHandle: after destroyMesh/destroyTexture the old handle is
//  stale, and the renderer ignores it (a stale mesh is not drawn; a stale
//  texture reads as the default) instead of reading freed memory.
//
//  COORDINATES: as FrameData: world units, right-handed, +y up. glTF uses
//  the same convention, so its data needs no conversion. Triangles are
//  counter-clockwise when seen from their front (glTF's rule); back faces
//  are culled.
// =============================================================================

#include "playground/core/Types.h"

#include <cstddef>
#include <limits>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace playground::render {

// One vertex: what a glTF mesh provides (the loader fills in normals and
// tangents when a file leaves them out).
struct Vertex {
  glm::vec3 position{0.0f};
  glm::vec3 normal{0.0f, 0.0f, 1.0f};
  // xyz: the direction the texture's u (x) axis runs across the surface;
  // w: +1 or -1, which way the v axis runs (bitangent = cross(n, t) * w).
  // Together with the normal they turn a normal map's texel into a world
  // direction.
  glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
  glm::vec2 uv{0.0f};  // texture coordinates; (0, 0) is the image's top-left
};
static_assert(sizeof(Vertex) == 48, "the mesh pipeline's vertex layout expects 48 bytes");

// A triangle list: every three indices make a triangle.
struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<u32> indices;
};

// RGBA, 8 bits per channel, top row first: width * height * 4 bytes.
struct ImageData {
  u32 width = 0;
  u32 height = 0;
  std::vector<std::byte> rgba;
};

// How a texture's numbers are read. Getting this wrong is the classic
// washed-out (or too dark) look:
//   Colour  stored as sRGB (what image editors save); the GPU converts to
//           linear light when sampling. For base colour.
//   Data    stored as plain numbers. For normal maps and metallic-roughness.
enum class TextureUsage : u8 { Colour, Data };

template <class Tag>
struct ResourceHandle {
  static constexpr u32 kInvalidIndex = std::numeric_limits<u32>::max();
  u32 index = kInvalidIndex;
  u32 generation = 0;

  [[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalidIndex; }
  friend constexpr bool operator==(const ResourceHandle&, const ResourceHandle&) = default;
};
using MeshHandle = ResourceHandle<struct MeshTag>;
using TextureHandle = ResourceHandle<struct TextureTag>;

// glTF's metallic-roughness material (the PBR model most tools export).
// A texture left empty reads as its default: white for base colour and
// metallic-roughness (so the factors apply unchanged), "straight out of the
// surface" for the normal map.
struct Material {
  glm::vec4 baseColourFactor{1.0f};  // linear RGBA, multiplies the base colour texture
  f32 metallicFactor = 1.0f;         // 0 = plastic, wood, stone...; 1 = metal
  f32 roughnessFactor = 1.0f;        // 0 = mirror-smooth; 1 = fully matte
  f32 normalScale = 1.0f;            // how strongly the normal map bends the surface
  bool doubleSided = false;          // draw back faces too (leaves, cloth, thin walls); lit from both sides
  TextureHandle baseColour;
  TextureHandle normal;
  TextureHandle metallicRoughness;  // glTF packing: roughness in green, metallic in blue
};

// One mesh to draw this frame.
struct MeshInstance {
  MeshHandle mesh;
  Material material;
  glm::mat4 transform{1.0f};  // model -> world
};

// Sunlight: parallel rays from far away.
struct DirectionalLight {
  glm::vec3 direction{-0.4f, -1.0f, -0.6f};  // the way the light TRAVELS (towards the ground)
  glm::vec3 colour{1.0f, 0.96f, 0.9f};       // linear RGB
  f32 intensity = 3.0f;
};

}  // namespace playground::render
