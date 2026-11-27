// =============================================================================
//  Gltf.cpp — glTF 2.0 into ModelData (worker thread), ModelData into a
//  Model (main thread). See engine/Gltf.h for what is and is not supported.
//
//  READING AN ATTRIBUTE ("accessor")
//    glTF stores vertex data in BUFFERS (raw bytes), sliced by BUFFER VIEWS,
//    read through ACCESSORS: "count elements of type VEC3 of FLOAT, starting
//    at byte offset o, one every `stride` bytes". Integers may be
//    "normalised" (a byte 0..255 means 0..1). readFloats() handles all of
//    that and checks every range against the buffer's real size first, so a
//    damaged file is an error message, not a read past the end of memory.
// =============================================================================

#include "playground/engine/Gltf.h"

#include "playground/core/Log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <utility>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>

#include <stb_image.h>   // declarations only; the code is in GltfImplementation.cpp
#include <tiny_gltf.h>

namespace playground::engine {

using log::Category;

void Bounds::add(glm::vec3 point) noexcept {
  min = glm::min(min, point);
  max = glm::max(max, point);
}

namespace {

// --- Images ----------------------------------------------------------------------

// tinygltf's hook for image bytes (embedded, in a buffer, or a file). Always
// decodes to RGBA, 8 bits per channel, whatever the file has: the renderer
// takes one pixel format. Runs on the worker thread that decodes the model;
// stb_image keeps its error message per thread.
bool decodeImage(tinygltf::Image* image, const int index, std::string* error, std::string* /*warning*/,
                 int /*requestedWidth*/, int /*requestedHeight*/, const unsigned char* bytes, int size,
                 void* /*user*/) {
  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(bytes, size, &width, &height, &channels, 4);
  if (pixels == nullptr) {
    if (error != nullptr) {
      *error += std::format("image {} (\"{}\") could not be decoded: {}\n", index, image->name, stbi_failure_reason());
    }
    return false;
  }
  image->width = width;
  image->height = height;
  image->component = 4;
  image->bits = 8;
  image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
  image->image.assign(pixels, pixels + static_cast<usize>(width) * static_cast<usize>(height) * 4);
  stbi_image_free(pixels);
  return true;
}

// --- Accessors -------------------------------------------------------------------

struct Strided {
  const unsigned char* data = nullptr;
  usize stride = 0;
  usize count = 0;
};

// Where an accessor's elements are, after checking they lie inside its
// buffer view and the view inside its buffer.
Result<Strided> locate(const tinygltf::Model& model, const tinygltf::Accessor& accessor, usize elementSize,
                       std::string_view what) {
  if (accessor.bufferView < 0 || static_cast<usize>(accessor.bufferView) >= model.bufferViews.size()) {
    return fail(Errc::InvalidArgument, "{}: accessor has no valid buffer view", what);
  }
  const tinygltf::BufferView& view = model.bufferViews[static_cast<usize>(accessor.bufferView)];
  if (view.buffer < 0 || static_cast<usize>(view.buffer) >= model.buffers.size()) {
    return fail(Errc::InvalidArgument, "{}: buffer view {} has no valid buffer", what, accessor.bufferView);
  }
  const tinygltf::Buffer& buffer = model.buffers[static_cast<usize>(view.buffer)];
  const u64 viewEnd = u64{view.byteOffset} + view.byteLength;
  if (viewEnd > buffer.data.size()) {
    return fail(Errc::InvalidArgument, "{}: buffer view {} ends at byte {}, past its buffer's {} bytes", what,
                accessor.bufferView, viewEnd, buffer.data.size());
  }
  const usize stride = view.byteStride != 0 ? view.byteStride : elementSize;
  if (stride < elementSize) {
    return fail(Errc::InvalidArgument, "{}: stride {} is smaller than an element ({} bytes)", what, stride, elementSize);
  }
  const u64 begin = u64{view.byteOffset} + accessor.byteOffset;
  if (accessor.count > 0) {
    // (count - 1) * stride + elementSize, without overflowing on a nonsense count.
    const u64 room = viewEnd >= begin ? viewEnd - begin : 0;
    if (room < elementSize || (accessor.count - 1) > (room - elementSize) / stride) {
      return fail(Errc::InvalidArgument, "{}: {} elements of {} bytes (stride {}) do not fit in buffer view {}", what,
                  accessor.count, elementSize, stride, accessor.bufferView);
    }
  }
  return Strided{.data = buffer.data.data() + begin, .stride = stride, .count = accessor.count};
}

const tinygltf::Accessor* findAccessor(const tinygltf::Model& model, int index) {
  return index >= 0 && static_cast<usize>(index) < model.accessors.size() ? &model.accessors[static_cast<usize>(index)]
                                                                          : nullptr;
}

f32 readComponent(const unsigned char* p, int componentType, bool normalised) {
  switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_FLOAT: {
      f32 v = 0.0f;
      std::memcpy(&v, p, sizeof v);
      return v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
      const f32 v = static_cast<f32>(*p);
      return normalised ? v / 255.0f : v;
    }
    case TINYGLTF_COMPONENT_TYPE_BYTE: {
      i8 raw = 0;
      std::memcpy(&raw, p, 1);
      const f32 v = static_cast<f32>(raw);
      return normalised ? std::max(v / 127.0f, -1.0f) : v;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
      u16 raw = 0;
      std::memcpy(&raw, p, 2);
      const f32 v = static_cast<f32>(raw);
      return normalised ? v / 65535.0f : v;
    }
    case TINYGLTF_COMPONENT_TYPE_SHORT: {
      i16 raw = 0;
      std::memcpy(&raw, p, 2);
      const f32 v = static_cast<f32>(raw);
      return normalised ? std::max(v / 32767.0f, -1.0f) : v;
    }
    default:
      return 0.0f;  // rejected by readFloats before it gets here
  }
}

// Accessor `index` as `count` elements of N floats.
template <usize N>
Result<std::vector<std::array<f32, N>>> readFloats(const tinygltf::Model& model, int index, std::string_view what) {
  const tinygltf::Accessor* accessor = findAccessor(model, index);
  if (accessor == nullptr) return fail(Errc::InvalidArgument, "{}: no accessor {}", what, index);
  if (accessor->sparse.isSparse) return fail(Errc::Unsupported, "{}: sparse accessors are not supported", what);
  if (tinygltf::GetNumComponentsInType(static_cast<u32>(accessor->type)) != static_cast<int>(N)) {
    return fail(Errc::InvalidArgument, "{}: expected {} components per element", what, N);
  }
  const int componentSize = tinygltf::GetComponentSizeInBytes(static_cast<u32>(accessor->componentType));
  if (componentSize <= 0 || accessor->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT ||
      accessor->componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE) {
    return fail(Errc::InvalidArgument, "{}: component type {} cannot hold vertex data", what, accessor->componentType);
  }
  auto located = locate(model, *accessor, static_cast<usize>(componentSize) * N, what);
  if (!located) return tl::unexpected(std::move(located.error()));

  std::vector<std::array<f32, N>> values(located->count);
  for (usize i = 0; i < located->count; ++i) {
    const unsigned char* element = located->data + i * located->stride;
    for (usize c = 0; c < N; ++c) {
      values[i][c] = readComponent(element + c * static_cast<usize>(componentSize), accessor->componentType,
                                   accessor->normalized);
    }
  }
  return values;
}

Result<std::vector<u32>> readIndices(const tinygltf::Model& model, int index, std::string_view what) {
  const tinygltf::Accessor* accessor = findAccessor(model, index);
  if (accessor == nullptr) return fail(Errc::InvalidArgument, "{}: no index accessor {}", what, index);
  if (accessor->sparse.isSparse) return fail(Errc::Unsupported, "{}: sparse accessors are not supported", what);
  if (accessor->type != TINYGLTF_TYPE_SCALAR) return fail(Errc::InvalidArgument, "{}: indices must be scalars", what);
  usize size = 0;
  switch (accessor->componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: size = 1; break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: size = 2; break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: size = 4; break;
    default: return fail(Errc::InvalidArgument, "{}: indices of component type {}", what, accessor->componentType);
  }
  auto located = locate(model, *accessor, size, what);
  if (!located) return tl::unexpected(std::move(located.error()));
  std::vector<u32> indices(located->count);
  for (usize i = 0; i < located->count; ++i) {
    const unsigned char* p = located->data + i * located->stride;
    if (size == 1) {
      indices[i] = *p;
    } else if (size == 2) {
      u16 v = 0;
      std::memcpy(&v, p, 2);
      indices[i] = v;
    } else {
      std::memcpy(&indices[i], p, 4);
    }
  }
  return indices;
}

// --- Normals and tangents --------------------------------------------------------

glm::vec3 safeNormalise(glm::vec3 v, glm::vec3 fallback) {
  const f32 length = glm::length(v);
  return length > 1e-12f ? v / length : fallback;
}

// Any unit vector at right angles to n.
glm::vec3 perpendicular(glm::vec3 n) {
  const glm::vec3 other = std::fabs(n.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
  return glm::normalize(glm::cross(other, n));
}

// Flat normals, as glTF requires when a primitive has none: every triangle
// gets its own three vertices (shared corners are split), all with the
// triangle's face normal.
void makeFlatNormals(render::MeshData& mesh) {
  std::vector<render::Vertex> split;
  split.reserve(mesh.indices.size());
  for (usize t = 0; t + 2 < mesh.indices.size(); t += 3) {
    render::Vertex a = mesh.vertices[mesh.indices[t]];
    render::Vertex b = mesh.vertices[mesh.indices[t + 1]];
    render::Vertex c = mesh.vertices[mesh.indices[t + 2]];
    const glm::vec3 n = safeNormalise(glm::cross(b.position - a.position, c.position - a.position), {0.0f, 0.0f, 1.0f});
    a.normal = b.normal = c.normal = n;
    split.push_back(a);
    split.push_back(b);
    split.push_back(c);
  }
  mesh.vertices = std::move(split);
  mesh.indices.resize(mesh.vertices.size());
  std::iota(mesh.indices.begin(), mesh.indices.end(), 0u);
}

// Tangents from texture coordinates. For each triangle, the directions in
// which u and v grow across its surface; summed per vertex, then made
// perpendicular to the normal (Lengyel's method).
//
// glTF's v runs DOWN the image while a normal map's green (+y) means "up
// the image", so the bitangent the map expects is the direction v SHRINKS:
// hence the -v below. w records whether that bitangent agrees with
// cross(normal, tangent) (+1) or the texture is mirrored (-1).
void makeTangents(render::MeshData& mesh) {
  std::vector<glm::vec3> tangents(mesh.vertices.size(), glm::vec3(0.0f));
  std::vector<glm::vec3> bitangents(mesh.vertices.size(), glm::vec3(0.0f));
  for (usize t = 0; t + 2 < mesh.indices.size(); t += 3) {
    const u32 i0 = mesh.indices[t];
    const u32 i1 = mesh.indices[t + 1];
    const u32 i2 = mesh.indices[t + 2];
    const render::Vertex& a = mesh.vertices[i0];
    const render::Vertex& b = mesh.vertices[i1];
    const render::Vertex& c = mesh.vertices[i2];
    const glm::vec3 e1 = b.position - a.position;
    const glm::vec3 e2 = c.position - a.position;
    const glm::vec2 d1{b.uv.x - a.uv.x, -(b.uv.y - a.uv.y)};
    const glm::vec2 d2{c.uv.x - a.uv.x, -(c.uv.y - a.uv.y)};
    const f32 det = d1.x * d2.y - d2.x * d1.y;
    if (std::fabs(det) < 1e-12f) continue;  // no texture stretch to follow (all corners at one uv)
    const f32 r = 1.0f / det;
    const glm::vec3 tangent = (e1 * d2.y - e2 * d1.y) * r;
    const glm::vec3 bitangent = (e2 * d1.x - e1 * d2.x) * r;
    for (const u32 i : {i0, i1, i2}) {
      tangents[i] += tangent;
      bitangents[i] += bitangent;
    }
  }
  for (usize i = 0; i < mesh.vertices.size(); ++i) {
    render::Vertex& v = mesh.vertices[i];
    glm::vec3 t = tangents[i] - v.normal * glm::dot(v.normal, tangents[i]);
    t = glm::length(t) > 1e-12f ? glm::normalize(t) : perpendicular(v.normal);
    const f32 w = glm::dot(glm::cross(v.normal, t), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
    v.tangent = glm::vec4(t, w);
  }
}

// --- Nodes -----------------------------------------------------------------------

glm::mat4 localTransform(const tinygltf::Node& node) {
  if (node.matrix.size() == 16) {
    std::array<f32, 16> m{};
    for (usize i = 0; i < 16; ++i) m[i] = static_cast<f32>(node.matrix[i]);
    return glm::make_mat4(m.data());  // column-major, as glTF stores it
  }
  glm::mat4 result(1.0f);
  if (node.translation.size() == 3) {
    result = glm::translate(result, {static_cast<f32>(node.translation[0]), static_cast<f32>(node.translation[1]),
                                     static_cast<f32>(node.translation[2])});
  }
  if (node.rotation.size() == 4) {  // glTF: x, y, z, w; glm::quat's constructor: w, x, y, z
    const glm::quat q(static_cast<f32>(node.rotation[3]), static_cast<f32>(node.rotation[0]),
                      static_cast<f32>(node.rotation[1]), static_cast<f32>(node.rotation[2]));
    result *= glm::mat4_cast(glm::normalize(q));
  }
  if (node.scale.size() == 3) {
    result = glm::scale(result, {static_cast<f32>(node.scale[0]), static_cast<f32>(node.scale[1]),
                                 static_cast<f32>(node.scale[2])});
  }
  return result;  // T * R * S
}

class Decoder {
 public:
  Decoder(const tinygltf::Model& gltf, std::string fileName) : gltf_(gltf), file_(std::move(fileName)) {}

  Result<ModelData> run() {
    if (Status s = images(); !s) return tl::unexpected(std::move(s.error()));
    if (Status s = materials(); !s) return tl::unexpected(std::move(s.error()));
    if (Status s = meshes(); !s) return tl::unexpected(std::move(s.error()));
    if (Status s = scene(); !s) return tl::unexpected(std::move(s.error()));
    if (out_.draws.empty()) {
      return fail(Errc::InvalidArgument, "glTF \"{}\" has no triangles to draw", file_);
    }
    return std::move(out_);
  }

 private:
  Status images() {
    for (const tinygltf::Image& image : gltf_.images) {
      render::ImageData data;
      if (image.width > 0 && image.height > 0 && !image.image.empty()) {
        data.width = static_cast<u32>(image.width);
        data.height = static_cast<u32>(image.height);
        data.rgba.resize(image.image.size());
        std::memcpy(data.rgba.data(), image.image.data(), image.image.size());
      } else {
        log::warn(Category::Engine, "glTF \"{}\": image \"{}\" has no pixels (missing file?); its textures use defaults",
                  file_, image.uri.empty() ? image.name : image.uri);
      }
      out_.images.push_back(std::move(data));
    }
    return {};
  }

  // A material's texture slot -> an image index, or -1 for none.
  i32 imageOf(int textureIndex, int texCoord, std::string_view material, std::string_view slot) {
    if (textureIndex < 0) return -1;
    if (static_cast<usize>(textureIndex) >= gltf_.textures.size()) {
      log::warn(Category::Engine, "glTF \"{}\": material \"{}\" {} names texture {}, which does not exist", file_,
                material, slot, textureIndex);
      return -1;
    }
    const int source = gltf_.textures[static_cast<usize>(textureIndex)].source;
    if (source < 0 || static_cast<usize>(source) >= out_.images.size() ||
        out_.images[static_cast<usize>(source)].rgba.empty()) {
      return -1;
    }
    if (texCoord != 0) {
      log::warn(Category::Engine, "glTF \"{}\": material \"{}\" {} uses texture coordinate set {}; only set 0 is read",
                file_, material, slot, texCoord);
    }
    return source;
  }

  Status materials() {
    for (const tinygltf::Material& m : gltf_.materials) {
      ModelData::MaterialData material{.name = m.name, .doubleSided = m.doubleSided};
      const auto& pbr = m.pbrMetallicRoughness;
      if (pbr.baseColorFactor.size() == 4) {
        material.baseColourFactor = {static_cast<f32>(pbr.baseColorFactor[0]), static_cast<f32>(pbr.baseColorFactor[1]),
                                     static_cast<f32>(pbr.baseColorFactor[2]), static_cast<f32>(pbr.baseColorFactor[3])};
      }
      material.metallicFactor = static_cast<f32>(pbr.metallicFactor);
      material.roughnessFactor = static_cast<f32>(pbr.roughnessFactor);
      material.normalScale = static_cast<f32>(m.normalTexture.scale);
      material.baseColourImage = imageOf(pbr.baseColorTexture.index, pbr.baseColorTexture.texCoord, m.name, "base colour");
      material.metallicRoughnessImage = imageOf(pbr.metallicRoughnessTexture.index,
                                                pbr.metallicRoughnessTexture.texCoord, m.name, "metallic-roughness");
      material.normalImage = imageOf(m.normalTexture.index, m.normalTexture.texCoord, m.name, "normal");
      if (!m.alphaMode.empty() && m.alphaMode != "OPAQUE") {
        log::warn(Category::Engine, "glTF \"{}\": material \"{}\" has alpha mode {}; drawn opaque for now", file_,
                  m.name, m.alphaMode);
      }
      out_.materials.push_back(std::move(material));
    }
    // glTF's default material, for primitives that name none: white,
    // metallic 1, roughness 1.
    out_.materials.push_back({.name = "default"});
    return {};
  }

  Status meshes() {
    meshPrimitives_.resize(gltf_.meshes.size());
    for (usize m = 0; m < gltf_.meshes.size(); ++m) {
      const tinygltf::Mesh& mesh = gltf_.meshes[m];
      for (usize p = 0; p < mesh.primitives.size(); ++p) {
        const std::string name = std::format("{}/{}", mesh.name.empty() ? std::format("mesh{}", m) : mesh.name, p);
        auto primitive = convert(mesh.primitives[p], name);
        if (!primitive) return tl::unexpected(std::move(primitive.error()));
        if (!*primitive) continue;  // skipped, with a warning
        meshPrimitives_[m].push_back(static_cast<u32>(out_.primitives.size()));
        out_.primitives.push_back(std::move(**primitive));
      }
    }
    return {};
  }

  // A primitive as a MeshData; an empty optional if it is skipped.
  Result<std::optional<ModelData::Primitive>> convert(const tinygltf::Primitive& primitive, const std::string& name) {
    const std::string what = std::format("glTF \"{}\" primitive {}", file_, name);
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1) {
      log::warn(Category::Engine, "{}: drawing mode {} is not triangles; skipped", what, primitive.mode);
      return std::optional<ModelData::Primitive>{};
    }
    const auto attribute = [&](const char* key) {
      const auto it = primitive.attributes.find(key);
      return it == primitive.attributes.end() ? -1 : it->second;
    };
    if (attribute("POSITION") < 0) {
      log::warn(Category::Engine, "{}: no POSITION attribute; skipped", what);
      return std::optional<ModelData::Primitive>{};
    }

    auto positions = readFloats<3>(gltf_, attribute("POSITION"), what + " POSITION");
    if (!positions) return tl::unexpected(std::move(positions.error()));
    const usize count = positions->size();
    if (count == 0) return std::optional<ModelData::Primitive>{};

    ModelData::Primitive out{.name = name};
    render::MeshData& mesh = out.mesh;
    mesh.vertices.resize(count);
    for (usize i = 0; i < count; ++i) {
      mesh.vertices[i].position = {(*positions)[i][0], (*positions)[i][1], (*positions)[i][2]};
      out.bounds.add(mesh.vertices[i].position);
    }
    // Optional attributes must have one element per vertex.
    const auto sameCount = [&](usize n, const char* key) -> Status {
      if (n == count) return {};
      return fail(Errc::InvalidArgument, "{}: {} has {} elements for {} positions", what, key, n, count);
    };
    const bool hasNormals = attribute("NORMAL") >= 0;
    if (hasNormals) {
      auto normals = readFloats<3>(gltf_, attribute("NORMAL"), what + " NORMAL");
      if (!normals) return tl::unexpected(std::move(normals.error()));
      if (Status s = sameCount(normals->size(), "NORMAL"); !s) return tl::unexpected(std::move(s.error()));
      for (usize i = 0; i < count; ++i) {
        mesh.vertices[i].normal = safeNormalise({(*normals)[i][0], (*normals)[i][1], (*normals)[i][2]}, {0, 0, 1});
      }
    }
    // Tangents only count with normals: without them, glTF says to ignore them.
    const bool hasTangents = hasNormals && attribute("TANGENT") >= 0;
    if (hasTangents) {
      auto tangents = readFloats<4>(gltf_, attribute("TANGENT"), what + " TANGENT");
      if (!tangents) return tl::unexpected(std::move(tangents.error()));
      if (Status s = sameCount(tangents->size(), "TANGENT"); !s) return tl::unexpected(std::move(s.error()));
      for (usize i = 0; i < count; ++i) {
        const auto& t = (*tangents)[i];
        mesh.vertices[i].tangent = {safeNormalise({t[0], t[1], t[2]}, perpendicular(mesh.vertices[i].normal)),
                                    t[3] < 0.0f ? -1.0f : 1.0f};
      }
    }
    if (attribute("TEXCOORD_0") >= 0) {
      auto uvs = readFloats<2>(gltf_, attribute("TEXCOORD_0"), what + " TEXCOORD_0");
      if (!uvs) return tl::unexpected(std::move(uvs.error()));
      if (Status s = sameCount(uvs->size(), "TEXCOORD_0"); !s) return tl::unexpected(std::move(s.error()));
      for (usize i = 0; i < count; ++i) mesh.vertices[i].uv = {(*uvs)[i][0], (*uvs)[i][1]};
    }

    if (primitive.indices >= 0) {
      auto indices = readIndices(gltf_, primitive.indices, what + " indices");
      if (!indices) return tl::unexpected(std::move(indices.error()));
      mesh.indices = std::move(*indices);
      for (const u32 index : mesh.indices) {
        if (index >= count) {
          return fail(Errc::InvalidArgument, "{}: index {} points past its {} vertices", what, index, count);
        }
      }
    } else {
      mesh.indices.resize(count);
      std::iota(mesh.indices.begin(), mesh.indices.end(), 0u);
    }
    if (mesh.indices.size() % 3 != 0) {
      return fail(Errc::InvalidArgument, "{}: {} indices is not a whole number of triangles", what,
                  mesh.indices.size());
    }
    if (mesh.indices.empty()) return std::optional<ModelData::Primitive>{};

    if (!hasNormals) makeFlatNormals(mesh);
    if (!hasTangents) makeTangents(mesh);

    if (primitive.material >= 0) {
      if (static_cast<usize>(primitive.material) >= gltf_.materials.size()) {
        return fail(Errc::InvalidArgument, "{}: material {} does not exist", what, primitive.material);
      }
      out.material = static_cast<u32>(primitive.material);
    } else {
      out.material = static_cast<u32>(out_.materials.size() - 1);  // the default
    }
    return std::optional<ModelData::Primitive>{std::move(out)};
  }

  Status scene() {
    std::vector<int> roots;
    if (!gltf_.scenes.empty()) {
      const usize chosen = gltf_.defaultScene >= 0 ? static_cast<usize>(gltf_.defaultScene) : 0;
      if (chosen >= gltf_.scenes.size()) {
        return fail(Errc::InvalidArgument, "glTF \"{}\": default scene {} does not exist", file_, chosen);
      }
      roots = gltf_.scenes[chosen].nodes;
    } else {
      // No scene: every node that is nobody's child is a root.
      std::vector<bool> isChild(gltf_.nodes.size(), false);
      for (const tinygltf::Node& node : gltf_.nodes) {
        for (const int child : node.children) {
          if (child >= 0 && static_cast<usize>(child) < isChild.size()) isChild[static_cast<usize>(child)] = true;
        }
      }
      for (usize i = 0; i < gltf_.nodes.size(); ++i) {
        if (!isChild[i]) roots.push_back(static_cast<int>(i));
      }
    }
    if (gltf_.nodes.empty()) {
      // Meshes and no nodes at all: show each mesh once, where it is.
      for (const std::vector<u32>& primitives : meshPrimitives_) {
        for (const u32 p : primitives) addDraw(p, glm::mat4(1.0f));
      }
      return {};
    }
    for (const int root : roots) {
      if (Status s = visit(root, glm::mat4(1.0f), 0); !s) return s;
    }
    return {};
  }

  Status visit(int index, const glm::mat4& parent, usize depth) {
    if (index < 0 || static_cast<usize>(index) >= gltf_.nodes.size()) {
      return fail(Errc::InvalidArgument, "glTF \"{}\": node {} does not exist", file_, index);
    }
    if (depth > gltf_.nodes.size()) {  // deeper than there are nodes: a node is its own ancestor
      return fail(Errc::InvalidArgument, "glTF \"{}\": the node tree has a cycle (through node {})", file_, index);
    }
    const tinygltf::Node& node = gltf_.nodes[static_cast<usize>(index)];
    const glm::mat4 world = parent * localTransform(node);
    if (node.mesh >= 0) {
      if (static_cast<usize>(node.mesh) >= meshPrimitives_.size()) {
        return fail(Errc::InvalidArgument, "glTF \"{}\": node {} names mesh {}, which does not exist", file_, index,
                    node.mesh);
      }
      for (const u32 p : meshPrimitives_[static_cast<usize>(node.mesh)]) addDraw(p, world);
    }
    for (const int child : node.children) {
      if (Status s = visit(child, world, depth + 1); !s) return s;
    }
    return {};
  }

  void addDraw(u32 primitive, const glm::mat4& transform) {
    out_.draws.push_back({.primitive = primitive, .transform = transform});
    const Bounds& local = out_.primitives[primitive].bounds;
    for (int corner = 0; corner < 8; ++corner) {
      const glm::vec3 p{(corner & 1) ? local.max.x : local.min.x, (corner & 2) ? local.max.y : local.min.y,
                        (corner & 4) ? local.max.z : local.min.z};
      out_.bounds.add(glm::vec3(transform * glm::vec4(p, 1.0f)));
    }
  }

  const tinygltf::Model& gltf_;
  std::string file_;
  ModelData out_;
  std::vector<std::vector<u32>> meshPrimitives_;  // glTF mesh -> our primitive indices
};

}  // namespace

// --- Decode (worker thread) -----------------------------------------------------

Result<ModelData> decodeGltf(std::span<const std::byte> file, const std::filesystem::path& path) {
  const std::string name = path.generic_string();
  tinygltf::TinyGLTF loader;
  loader.SetImageLoader(&decodeImage, nullptr);
  tinygltf::Model gltf;
  std::string error;
  std::string warning;
  const std::string folder = path.parent_path().string();
  const auto* bytes = reinterpret_cast<const unsigned char*>(file.data());
  if (file.size() > std::numeric_limits<unsigned int>::max()) {
    return fail(Errc::InvalidArgument, "glTF \"{}\" is larger than 4 GiB", name);
  }
  const auto size = static_cast<unsigned int>(file.size());

  const bool binary = file.size() >= 4 && std::memcmp(file.data(), "glTF", 4) == 0;
  const bool ok = binary ? loader.LoadBinaryFromMemory(&gltf, &error, &warning, bytes, size, folder)
                         : loader.LoadASCIIFromString(&gltf, &error, &warning, reinterpret_cast<const char*>(bytes),
                                                      size, folder);
  if (!warning.empty()) log::warn(Category::Engine, "glTF \"{}\": {}", name, warning);
  if (!ok) {
    while (!error.empty() && (error.back() == '\n' || error.back() == ' ')) error.pop_back();
    // A buffer or image file that is not there is an I/O problem, not bad glTF.
    const bool missingFile = error.find("File not found") != std::string::npos ||
                             error.find("Failed to load external") != std::string::npos;
    return fail(missingFile ? Errc::IoFailure : Errc::InvalidArgument, "glTF \"{}\" could not be read: {}", name,
                error.empty() ? "unknown error" : error);
  }
  if (!gltf.extensionsRequired.empty()) {
    std::string list;
    for (const std::string& extension : gltf.extensionsRequired) list += (list.empty() ? "" : ", ") + extension;
    return fail(Errc::Unsupported, "glTF \"{}\" requires extensions this engine does not have: {}", name, list);
  }

  auto model = Decoder(gltf, name).run();
  if (!model) return model;
  usize triangles = 0;
  for (const ModelData::Primitive& p : model->primitives) triangles += p.mesh.indices.size() / 3;
  log::debug(Category::Engine, "glTF \"{}\": {} primitive(s), {} draw(s), {} triangles, {} image(s), {} material(s)",
             name, model->primitives.size(), model->draws.size(), triangles, model->images.size(),
             model->materials.size() - 1);
  return model;
}

// --- Model (main thread) ----------------------------------------------------------

Model::Model(Model&& other) noexcept
    : renderer_(std::exchange(other.renderer_, nullptr)),
      meshes_(std::move(other.meshes_)),
      textures_(std::move(other.textures_)),
      materials_(std::move(other.materials_)),
      draws_(std::move(other.draws_)),
      bounds_(other.bounds_) {}

Model& Model::operator=(Model&& other) noexcept {
  if (this != &other) {
    release();
    renderer_ = std::exchange(other.renderer_, nullptr);
    meshes_ = std::move(other.meshes_);
    textures_ = std::move(other.textures_);
    materials_ = std::move(other.materials_);
    draws_ = std::move(other.draws_);
    bounds_ = other.bounds_;
  }
  return *this;
}

Model::~Model() { release(); }

void Model::release() noexcept {
  if (renderer_ == nullptr) return;
  for (const render::MeshHandle mesh : meshes_) renderer_->destroyMesh(mesh);
  for (const render::TextureHandle texture : textures_) renderer_->destroyTexture(texture);
  meshes_.clear();
  textures_.clear();
  renderer_ = nullptr;
}

void Model::extract(const glm::mat4& transform, render::FrameData& frame) const {
  for (const Draw& draw : draws_) {
    frame.meshes.push_back({.mesh = draw.mesh, .material = materials_[draw.material],
                            .transform = transform * draw.transform});
  }
}

Result<Model> uploadModel(render::Renderer& renderer, ModelData&& data, std::string_view name) {
  Model model;  // on any early return, its destructor destroys what was already made
  model.renderer_ = &renderer;
  model.bounds_ = data.bounds;

  // One texture per (image, usage): the same image as base colour (sRGB)
  // and as data would need two. Made on first use; unused images are skipped.
  std::map<std::pair<i32, render::TextureUsage>, render::TextureHandle> made;
  const auto texture = [&](i32 image, render::TextureUsage usage) -> Result<render::TextureHandle> {
    if (image < 0) return render::TextureHandle{};  // none: the renderer's default
    if (const auto it = made.find({image, usage}); it != made.end()) return it->second;
    auto handle = renderer.createTexture(data.images[static_cast<usize>(image)], usage,
                                         std::format("{} image {}", name, image));
    if (!handle) return tl::unexpected(std::move(handle.error()));
    model.textures_.push_back(*handle);
    made.emplace(std::pair{image, usage}, *handle);
    return *handle;
  };

  for (const ModelData::MaterialData& m : data.materials) {
    auto baseColour = texture(m.baseColourImage, render::TextureUsage::Colour);
    if (!baseColour) return tl::unexpected(std::move(baseColour.error()));
    auto normal = texture(m.normalImage, render::TextureUsage::Data);
    if (!normal) return tl::unexpected(std::move(normal.error()));
    auto metallicRoughness = texture(m.metallicRoughnessImage, render::TextureUsage::Data);
    if (!metallicRoughness) return tl::unexpected(std::move(metallicRoughness.error()));
    model.materials_.push_back({.baseColourFactor = m.baseColourFactor,
                                .metallicFactor = m.metallicFactor,
                                .roughnessFactor = m.roughnessFactor,
                                .normalScale = m.normalScale,
                                .doubleSided = m.doubleSided,
                                .baseColour = *baseColour,
                                .normal = *normal,
                                .metallicRoughness = *metallicRoughness});
  }

  for (const ModelData::Primitive& primitive : data.primitives) {
    auto mesh = renderer.createMesh(primitive.mesh, std::format("{} {}", name, primitive.name));
    if (!mesh) return tl::unexpected(std::move(mesh.error()));
    model.meshes_.push_back(*mesh);
  }
  for (const ModelData::Draw& draw : data.draws) {
    model.draws_.push_back({.mesh = model.meshes_[draw.primitive],
                            .material = data.primitives[draw.primitive].material,
                            .transform = draw.transform});
  }
  log::info(Category::Engine, "model \"{}\" uploaded: {} mesh(es), {} texture(s), {} draw(s)", name,
            model.meshes_.size(), model.textures_.size(), model.draws_.size());
  return model;
}

AssetLoader<Model, ModelData> gltfLoader(render::Renderer& renderer) {
  return {
      .decode = [](std::span<const std::byte> file, const std::filesystem::path& path) { return decodeGltf(file, path); },
      .finalize = [&renderer](ModelData&& data, const std::filesystem::path& path) {
        return uploadModel(renderer, std::move(data), path.filename().generic_string());
      },
  };
}

}  // namespace playground::engine
