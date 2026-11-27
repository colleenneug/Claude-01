// Tests for the glTF 2.0 loader: what decodeGltf reads, what it makes when a
// file leaves it out, how it flattens the node tree, its errors, and the
// AssetManager path (decode on a worker, upload through a renderer, release
// on unload). No GPU: a recording fake stands in for the renderer, so these
// run everywhere, and under ThreadSanitizer (label "threads").
//
// Test files are written here, in memory: JSON with buffers as base64 data
// URIs (or a .glb built byte by byte), which keeps each test's data next to
// its checks.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/core/JobSystem.h"
#include "playground/engine/Assets.h"
#include "playground/engine/Gltf.h"

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <thread>
#include <vector>

using namespace playground;
using engine::ModelData;

namespace {

// --- Building test files --------------------------------------------------------

std::string base64(std::span<const std::byte> bytes) {
  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  usize i = 0;
  for (; i + 2 < bytes.size(); i += 3) {
    const u32 n = (std::to_integer<u32>(bytes[i]) << 16) | (std::to_integer<u32>(bytes[i + 1]) << 8) |
                  std::to_integer<u32>(bytes[i + 2]);
    for (int k = 3; k >= 0; --k) out += kAlphabet[(n >> (6 * k)) & 63];
  }
  if (const usize rest = bytes.size() - i; rest > 0) {
    u32 n = std::to_integer<u32>(bytes[i]) << 16;
    if (rest == 2) n |= std::to_integer<u32>(bytes[i + 1]) << 8;
    out += kAlphabet[(n >> 18) & 63];
    out += kAlphabet[(n >> 12) & 63];
    out += rest == 2 ? kAlphabet[(n >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

std::vector<std::byte> fromBase64(std::string_view text) {
  std::vector<std::byte> out;
  u32 bits = 0;
  int count = 0;
  for (const char c : text) {
    const char* const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const char* at = std::strchr(alphabet, c);
    if (c == '=' || at == nullptr) break;
    bits = (bits << 6) | static_cast<u32>(at - alphabet);
    count += 6;
    if (count >= 8) {
      count -= 8;
      out.push_back(std::byte{static_cast<u8>(bits >> count)});
    }
  }
  return out;
}

// One binary buffer, filled piece by piece; each piece 4-byte aligned, as
// glTF requires for float data.
struct BinaryBuffer {
  std::vector<std::byte> bytes;

  template <class T>
  usize add(const std::vector<T>& values) {
    while (bytes.size() % 4 != 0) bytes.push_back(std::byte{0});
    const usize offset = bytes.size();
    const auto raw = std::as_bytes(std::span(values));
    bytes.insert(bytes.end(), raw.begin(), raw.end());
    return offset;
  }
  [[nodiscard]] std::string dataUri() const { return "data:application/octet-stream;base64," + base64(bytes); }
};

// Replaces every "@KEY@" in `text`. JSON is full of braces, so std::format
// would need every one doubled; plain substitution keeps the files readable.
std::string fill(std::string text, std::initializer_list<std::pair<std::string, std::string>> values) {
  for (const auto& [key, value] : values) {
    const std::string marker = "@" + key + "@";
    for (usize at = text.find(marker); at != std::string::npos; at = text.find(marker, at + value.size())) {
      text.replace(at, marker.size(), value);
    }
  }
  return text;
}

Result<ModelData> decode(const std::string& text, const std::filesystem::path& path = "test.gltf") {
  return engine::decodeGltf(std::as_bytes(std::span(text)), path);
}

ModelData decodeOk(const std::string& text) {
  auto model = decode(text);
  REQUIRE_MESSAGE(model.has_value(), (model ? "" : model.error().message));
  return std::move(*model);
}

bool near(f32 a, f32 b, f32 tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }
bool near(glm::vec3 a, glm::vec3 b, f32 tolerance = 1e-4f) {
  return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

// A unit quad facing +z with every attribute, used by several tests:
// positions, normals, uvs (top-left = (0, 0)), u16 indices.
struct QuadBuffers {
  BinaryBuffer buffer;
  usize positions = 0, normals = 0, uvs = 0, indices = 0;
};
QuadBuffers quadBuffers(bool mirrorU = false) {
  QuadBuffers q;
  q.positions = q.buffer.add(std::vector<f32>{-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0});
  q.normals = q.buffer.add(std::vector<f32>{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1});
  q.uvs = mirrorU ? q.buffer.add(std::vector<f32>{1, 1, 0, 1, 0, 0, 1, 0})
                  : q.buffer.add(std::vector<f32>{0, 1, 1, 1, 1, 0, 0, 0});
  q.indices = q.buffer.add(std::vector<u16>{0, 1, 2, 0, 2, 3});
  return q;
}

// The quad as glTF, without tangents, with an optional material block.
std::string quadGltf(const QuadBuffers& q, const std::string& materials = "[]", int material = -1) {
  return fill(R"({
    "asset": {"version": "2.0"},
    "buffers": [{"byteLength": @LENGTH@, "uri": "@URI@"}],
    "bufferViews": [
      {"buffer": 0, "byteOffset": @POS@, "byteLength": 48},
      {"buffer": 0, "byteOffset": @NRM@, "byteLength": 48},
      {"buffer": 0, "byteOffset": @UV@, "byteLength": 32},
      {"buffer": 0, "byteOffset": @IDX@, "byteLength": 12}],
    "accessors": [
      {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [-1,-1,0], "max": [1,1,0]},
      {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
      {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
      {"bufferView": 3, "componentType": 5123, "count": 6, "type": "SCALAR"}],
    "materials": @MATERIALS@,
    "meshes": [{"name": "quad", "primitives": [
      {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3 @MATERIAL@}]}],
    "nodes": [{"mesh": 0}],
    "scenes": [{"nodes": [0]}]
  })",
              {{"LENGTH", std::to_string(q.buffer.bytes.size())},
               {"URI", q.buffer.dataUri()},
               {"POS", std::to_string(q.positions)},
               {"NRM", std::to_string(q.normals)},
               {"UV", std::to_string(q.uvs)},
               {"IDX", std::to_string(q.indices)},
               {"MATERIALS", materials},
               {"MATERIAL", material >= 0 ? ", \"material\": " + std::to_string(material) : ""}});
}

// A 2x2 RGB PNG (no alpha channel): red, green / blue, white.
constexpr const char* kPng2x2 =
    "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAAEklEQVR4nGP4z8DAAMIM/4EAAB/uBfsL2WiLAAAAAElFTkSuQmCC";

}  // namespace

// --- What is read --------------------------------------------------------------------

TEST_CASE("a triangle with only positions gets indices, flat normals, a tangent and the default material") {
  BinaryBuffer b;
  const usize at = b.add(std::vector<f32>{0, 0, 0, 1, 0, 0, 0, 1, 0});
  const ModelData model = decodeOk(fill(R"({
    "asset": {"version": "2.0"},
    "buffers": [{"byteLength": 36, "uri": "@URI@"}],
    "bufferViews": [{"buffer": 0, "byteOffset": @AT@, "byteLength": 36}],
    "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
    "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0
  })", {{"URI", b.dataUri()}, {"AT", std::to_string(at)}}));

  REQUIRE(model.primitives.size() == 1);
  const render::MeshData& mesh = model.primitives[0].mesh;
  CHECK(model.primitives[0].name == "mesh0/0");
  REQUIRE(mesh.vertices.size() == 3);
  CHECK(mesh.indices == std::vector<u32>{0, 1, 2});
  for (const render::Vertex& v : mesh.vertices) {
    CHECK(near(v.normal, {0, 0, 1}));  // the face normal: counter-clockwise, towards +z
    CHECK(near(glm::length(glm::vec3(v.tangent)), 1.0f));
    CHECK(near(glm::dot(glm::vec3(v.tangent), v.normal), 0.0f));  // any direction in the surface
    CHECK(v.uv == glm::vec2(0.0f));
  }
  REQUIRE(model.materials.size() == 1);  // just glTF's default
  CHECK(model.materials[0].name == "default");
  CHECK(model.materials[0].metallicFactor == 1.0f);
  CHECK(model.primitives[0].material == 0);
  REQUIRE(model.draws.size() == 1);
  CHECK(model.draws[0].transform == glm::mat4(1.0f));
  CHECK(near(model.bounds.min, {0, 0, 0}));
  CHECK(near(model.bounds.max, {1, 1, 0}));
}

TEST_CASE("given attributes are kept: positions, normals (normalised), uvs, 16-bit indices") {
  const ModelData model = decodeOk(quadGltf(quadBuffers()));
  REQUIRE(model.primitives.size() == 1);
  const render::MeshData& mesh = model.primitives[0].mesh;
  CHECK(model.primitives[0].name == "quad/0");
  REQUIRE(mesh.vertices.size() == 4);  // shared corners stay shared: normals were given
  CHECK(mesh.indices == std::vector<u32>{0, 1, 2, 0, 2, 3});
  CHECK(near(mesh.vertices[2].position, {1, 1, 0}));
  CHECK(mesh.vertices[3].uv == glm::vec2(0.0f, 0.0f));
  CHECK(mesh.vertices[0].uv == glm::vec2(0.0f, 1.0f));
  CHECK(near(mesh.vertices[1].normal, {0, 0, 1}));
}

TEST_CASE("missing tangents follow the texture: u along +x gives (1, 0, 0, +1); a mirrored u flips w") {
  SUBCASE("standard") {
    const ModelData model = decodeOk(quadGltf(quadBuffers()));
    for (const render::Vertex& v : model.primitives[0].mesh.vertices) {
      CHECK(near(glm::vec3(v.tangent), {1, 0, 0}));
      CHECK(v.tangent.w == 1.0f);  // bitangent = cross(n, t) = +y: "up the image", as normal maps mean
    }
  }
  SUBCASE("mirrored") {
    const ModelData model = decodeOk(quadGltf(quadBuffers(/*mirrorU=*/true)));
    for (const render::Vertex& v : model.primitives[0].mesh.vertices) {
      CHECK(near(glm::vec3(v.tangent), {-1, 0, 0}));
      CHECK(v.tangent.w == -1.0f);
    }
  }
}

TEST_CASE("given tangents are used as they are, sign included") {
  BinaryBuffer b;
  const usize pos = b.add(std::vector<f32>{0, 0, 0, 1, 0, 0, 0, 1, 0});
  const usize nrm = b.add(std::vector<f32>{0, 0, 1, 0, 0, 1, 0, 0, 1});
  const usize tan = b.add(std::vector<f32>{0, 2, 0, -1, 0, 2, 0, -1, 0, 2, 0, -1});
  const ModelData model = decodeOk(fill(R"({
    "asset": {"version": "2.0"},
    "buffers": [{"byteLength": @LEN@, "uri": "@URI@"}],
    "bufferViews": [{"buffer": 0, "byteOffset": @POS@, "byteLength": 36},
                    {"buffer": 0, "byteOffset": @NRM@, "byteLength": 36},
                    {"buffer": 0, "byteOffset": @TAN@, "byteLength": 48}],
    "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
                  {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
                  {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4"}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TANGENT": 2}}]}],
    "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}]
  })", {{"LEN", std::to_string(b.bytes.size())}, {"URI", b.dataUri()}, {"POS", std::to_string(pos)},
        {"NRM", std::to_string(nrm)}, {"TAN", std::to_string(tan)}}));
  for (const render::Vertex& v : model.primitives[0].mesh.vertices) {
    CHECK(near(glm::vec3(v.tangent), {0, 1, 0}));  // normalised
    CHECK(v.tangent.w == -1.0f);
  }
}

TEST_CASE("interleaved vertices (a byte stride), normalised 16-bit uvs and 8-bit indices are read correctly") {
  // Per vertex: position (3 floats, 12 bytes) then uv (2 x u16, 4 bytes) = 16 bytes.
  BinaryBuffer b;
  std::vector<std::byte> interleaved;
  const auto put = [&](const auto& value) {
    const auto raw = std::as_bytes(std::span(&value, 1));
    interleaved.insert(interleaved.end(), raw.begin(), raw.end());
  };
  const std::array<std::array<f32, 3>, 3> positions{{{0, 0, 0}, {2, 0, 0}, {0, 2, 0}}};
  const std::array<std::array<u16, 2>, 3> uvs{{{0, 0}, {65535, 0}, {0, 32768}}};
  for (usize i = 0; i < 3; ++i) {
    put(positions[i]);
    put(uvs[i]);
  }
  const usize vertices = b.add(interleaved);
  const usize indices = b.add(std::vector<u8>{0, 1, 2, 0});  // 3 used; padded to 4 bytes
  const ModelData model = decodeOk(fill(R"({
    "asset": {"version": "2.0"},
    "buffers": [{"byteLength": @LEN@, "uri": "@URI@"}],
    "bufferViews": [{"buffer": 0, "byteOffset": @VTX@, "byteLength": 48, "byteStride": 16},
                    {"buffer": 0, "byteOffset": @IDX@, "byteLength": 3}],
    "accessors": [
      {"bufferView": 0, "byteOffset": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
      {"bufferView": 0, "byteOffset": 12, "componentType": 5123, "normalized": true, "count": 3, "type": "VEC2"},
      {"bufferView": 1, "componentType": 5121, "count": 3, "type": "SCALAR"}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1}, "indices": 2}]}],
    "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}]
  })", {{"LEN", std::to_string(b.bytes.size())}, {"URI", b.dataUri()}, {"VTX", std::to_string(vertices)},
        {"IDX", std::to_string(indices)}}));
  // No normals, so the triangle was split for flat normals: vertex i is index i's corner.
  const render::MeshData& mesh = model.primitives[0].mesh;
  REQUIRE(mesh.vertices.size() == 3);
  CHECK(near(mesh.vertices[1].position, {2, 0, 0}));
  CHECK(near(mesh.vertices[2].position, {0, 2, 0}));
  CHECK(near(mesh.vertices[1].uv.x, 1.0f));       // 65535 / 65535
  CHECK(near(mesh.vertices[2].uv.y, 0.50001f));   // 32768 / 65535
}

TEST_CASE("the node tree is flattened: each draw carries its parents' transforms times its own") {
  BinaryBuffer b;
  const usize at = b.add(std::vector<f32>{0, 0, 0, 1, 0, 0, 0, 1, 0});
  const ModelData model = decodeOk(fill(R"({
    "asset": {"version": "2.0"},
    "buffers": [{"byteLength": 36, "uri": "@URI@"}],
    "bufferViews": [{"buffer": 0, "byteOffset": @AT@, "byteLength": 36}],
    "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
    "nodes": [
      {"name": "parent", "translation": [10, 0, 0], "scale": [2, 2, 2], "children": [1]},
      {"name": "child", "translation": [0, 5, 0], "rotation": [0, 0, 0.7071068, 0.7071068], "mesh": 0},
      {"name": "other", "matrix": [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,-3,1], "mesh": 0},
      {"name": "not in the scene", "mesh": 0}],
    "scenes": [{"nodes": [0, 2]}], "scene": 0
  })", {{"URI", b.dataUri()}, {"AT", std::to_string(at)}}));

  REQUIRE(model.draws.size() == 2);  // node 3 is not in the scene
  // Child: rotate 90 degrees about z, move up 5; then the parent scales by 2 and moves right 10.
  const glm::vec3 corner = glm::vec3(model.draws[0].transform * glm::vec4(1, 0, 0, 1));
  CHECK(near(corner, {10, 12, 0}));
  // The matrix node: column-major, translation in the last column.
  CHECK(near(glm::vec3(model.draws[1].transform * glm::vec4(0, 0, 0, 1)), {0, 0, -3}));
  CHECK(model.draws[0].primitive == model.draws[1].primitive);  // one mesh, drawn twice
  CHECK(near(model.bounds.max, {10, 12, 0}));
  CHECK(near(model.bounds.min.z, -3.0f));
}

TEST_CASE("materials: factors, texture slots, double-sided, and images decoded to RGBA") {
  const std::string materials = fill(R"([{
      "name": "painted",
      "pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.25, 1, 1], "metallicFactor": 0.2, "roughnessFactor": 0.7,
                               "baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 0}},
      "normalTexture": {"index": 0, "scale": 0.5},
      "doubleSided": true}])", {});
  std::string gltf = quadGltf(quadBuffers(), materials, 0);
  // Add the image and the texture that points at it.
  gltf.insert(gltf.rfind('}'), fill(R"(, "images": [{"uri": "data:image/png;base64,@PNG@"}],
                                       "textures": [{"source": 0}])", {{"PNG", kPng2x2}}));
  const ModelData model = decodeOk(gltf);

  REQUIRE(model.materials.size() == 2);  // the file's, then the default
  const ModelData::MaterialData& m = model.materials[0];
  CHECK(m.name == "painted");
  CHECK(m.baseColourFactor == glm::vec4(0.5f, 0.25f, 1.0f, 1.0f));
  CHECK(near(m.metallicFactor, 0.2f));
  CHECK(near(m.roughnessFactor, 0.7f));
  CHECK(near(m.normalScale, 0.5f));
  CHECK(m.doubleSided);
  CHECK(m.baseColourImage == 0);
  CHECK(m.normalImage == 0);
  CHECK(m.metallicRoughnessImage == 0);
  CHECK(model.primitives[0].material == 0);

  REQUIRE(model.images.size() == 1);
  const render::ImageData& image = model.images[0];
  CHECK(image.width == 2);
  CHECK(image.height == 2);
  REQUIRE(image.rgba.size() == 16);  // RGB in the file, RGBA here
  const auto pixel = [&](usize i) {
    return std::array<int, 4>{std::to_integer<int>(image.rgba[i * 4]), std::to_integer<int>(image.rgba[i * 4 + 1]),
                              std::to_integer<int>(image.rgba[i * 4 + 2]), std::to_integer<int>(image.rgba[i * 4 + 3])};
  };
  CHECK(pixel(0) == std::array<int, 4>{255, 0, 0, 255});
  CHECK(pixel(1) == std::array<int, 4>{0, 255, 0, 255});
  CHECK(pixel(2) == std::array<int, 4>{0, 0, 255, 255});
  CHECK(pixel(3) == std::array<int, 4>{255, 255, 255, 255});
}

TEST_CASE("a .glb (binary glTF) gives the same model as the .gltf") {
  const QuadBuffers q = quadBuffers();
  // The same JSON, but the buffer has no uri: it is the .glb's BIN chunk.
  std::string json = quadGltf(q);
  const usize uriAt = json.find(", \"uri\"");
  json.erase(uriAt, json.find('}', uriAt) - uriAt);
  while (json.size() % 4 != 0) json += ' ';
  std::vector<std::byte> bin = q.buffer.bytes;
  while (bin.size() % 4 != 0) bin.push_back(std::byte{0});

  std::vector<std::byte> glb;
  const auto u32le = [&](u32 v) {
    for (int i = 0; i < 4; ++i) glb.push_back(std::byte{static_cast<u8>(v >> (8 * i))});
  };
  glb.insert(glb.end(), {std::byte{'g'}, std::byte{'l'}, std::byte{'T'}, std::byte{'F'}});
  u32le(2);                                                                 // version
  u32le(static_cast<u32>(12 + 8 + json.size() + 8 + bin.size()));          // total length
  u32le(static_cast<u32>(json.size()));
  u32le(0x4E4F534A);                                                        // "JSON"
  for (const char c : json) glb.push_back(std::byte{static_cast<u8>(c)});
  u32le(static_cast<u32>(bin.size()));
  u32le(0x004E4942);                                                        // "BIN\0"
  glb.insert(glb.end(), bin.begin(), bin.end());

  auto fromGlb = engine::decodeGltf(glb, "test.glb");
  REQUIRE_MESSAGE(fromGlb.has_value(), (fromGlb ? "" : fromGlb.error().message));
  const ModelData fromGltf = decodeOk(quadGltf(q));
  REQUIRE(fromGlb->primitives.size() == 1);
  CHECK(fromGlb->primitives[0].mesh.indices == fromGltf.primitives[0].mesh.indices);
  REQUIRE(fromGlb->primitives[0].mesh.vertices.size() == 4);
  for (usize i = 0; i < 4; ++i) {
    CHECK(fromGlb->primitives[0].mesh.vertices[i].position == fromGltf.primitives[0].mesh.vertices[i].position);
    CHECK(fromGlb->primitives[0].mesh.vertices[i].uv == fromGltf.primitives[0].mesh.vertices[i].uv);
  }
}

// --- Errors ------------------------------------------------------------------------

TEST_CASE("broken files fail with a message naming the file and the problem") {
  SUBCASE("not JSON") {
    const auto bad = decode("this is not glTF", "models/broken.gltf");
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == Errc::InvalidArgument);
    CHECK(bad.error().message.find("models/broken.gltf") != std::string::npos);
  }
  SUBCASE("a required extension we do not have") {
    std::string gltf = quadGltf(quadBuffers());
    gltf.insert(gltf.rfind('}'), R"(, "extensionsUsed": ["KHR_draco_mesh_compression"],
                                      "extensionsRequired": ["KHR_draco_mesh_compression"])");
    const auto bad = decode(gltf);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == Errc::Unsupported);
    CHECK(bad.error().message.find("KHR_draco_mesh_compression") != std::string::npos);
  }
  SUBCASE("an accessor reaching past its buffer") {
    std::string gltf = quadGltf(quadBuffers());
    const usize at = gltf.find("\"count\": 4");
    gltf.replace(at, 10, "\"count\": 400");
    const auto bad = decode(gltf);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == Errc::InvalidArgument);
    INFO(bad.error().message);
    CHECK(bad.error().message.find("400 elements of 12 bytes (stride 12) do not fit") != std::string::npos);
  }
  SUBCASE("an index past the last vertex") {
    QuadBuffers q = quadBuffers();
    std::vector<u16> indices{0, 1, 2, 0, 2, 9};
    std::memcpy(q.buffer.bytes.data() + q.indices, indices.data(), 12);
    const auto bad = decode(quadGltf(q));
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("index 9 points past its 4 vertices") != std::string::npos);
  }
  SUBCASE("a node that is its own ancestor") {
    std::string gltf = quadGltf(quadBuffers());
    const usize at = gltf.find(R"("nodes": [{"mesh": 0}])");
    gltf.replace(at, 22, R"("nodes": [{"mesh": 0, "children": [1]}, {"children": [0]}])");
    const auto bad = decode(gltf);
    REQUIRE_FALSE(bad.has_value());
    INFO(bad.error().message);
    CHECK(bad.error().message.find("the node tree has a cycle") != std::string::npos);
  }
  SUBCASE("nothing but points") {
    std::string gltf = quadGltf(quadBuffers());
    const usize at = gltf.find("\"indices\": 3");
    gltf.replace(at, 12, "\"indices\": 3, \"mode\": 0");
    const auto bad = decode(gltf);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("no triangles to draw") != std::string::npos);
  }
}

// --- Through the asset manager --------------------------------------------------------

namespace {

// Records what a model asks the renderer for. No GPU.
class RecordingRenderer final : public render::Renderer {
 public:
  [[nodiscard]] std::string_view name() const noexcept override { return "recording"; }
  [[nodiscard]] Status onFramebufferResized(platform::Extent2D) override { return {}; }
  [[nodiscard]] Status renderFrame(const FrameTime&, const render::FrameData&) override { return {}; }

  Result<render::MeshHandle> createMesh(const render::MeshData& mesh, std::string_view) override {
    if (failMeshNumber > 0 && ++meshCalls == failMeshNumber) return fail(Errc::GpuFailure, "out of GPU memory (test)");
    triangles += mesh.indices.size() / 3;
    return render::MeshHandle{.index = nextMesh++, .generation = 0};
  }
  Result<render::TextureHandle> createTexture(const render::ImageData& image, render::TextureUsage usage,
                                              std::string_view) override {
    usages.push_back(usage);
    CHECK(image.rgba.size() == usize{image.width} * image.height * 4);
    return render::TextureHandle{.index = nextTexture++, .generation = 0};
  }
  void destroyMesh(render::MeshHandle) noexcept override { ++meshesDestroyed; }
  void destroyTexture(render::TextureHandle) noexcept override { ++texturesDestroyed; }

  u32 nextMesh = 0;
  u32 nextTexture = 0;
  usize triangles = 0;
  std::vector<render::TextureUsage> usages;
  int meshesDestroyed = 0;
  int texturesDestroyed = 0;
  int failMeshNumber = 0;  // > 0: that createMesh call fails
  int meshCalls = 0;
};

// A folder with a .gltf whose buffer and image are separate files.
std::filesystem::path writeModelFolder(const std::string& name) {
  const auto folder = std::filesystem::temp_directory_path() / "playground_gltf_tests" / name;
  std::filesystem::remove_all(folder);
  std::filesystem::create_directories(folder / "textures");
  const QuadBuffers q = quadBuffers();
  std::ofstream(folder / "quad.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(q.buffer.bytes.data()), static_cast<std::streamsize>(q.buffer.bytes.size()));
  const std::vector<std::byte> png = fromBase64(kPng2x2);
  std::ofstream(folder / "textures" / "grid.png", std::ios::binary)
      .write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));

  std::string gltf = quadGltf(q, R"([{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                                                              "metallicRoughnessTexture": {"index": 0}},
                                      "normalTexture": {"index": 0}}])",
                              0);
  const usize uriAt = gltf.find("\"uri\": \"") + 8;
  gltf.replace(uriAt, gltf.find('"', uriAt) - uriAt, "quad.bin");
  // Two primitives: the quad with its material, and again with none (the default).
  const usize primitive = gltf.find("{\"attributes\"");
  const std::string second = R"({"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3}, )";
  gltf.insert(primitive, second);
  gltf.insert(gltf.rfind('}'), R"(, "images": [{"uri": "textures/grid.png"}], "textures": [{"source": 0}])");
  std::ofstream(folder / "quad.gltf") << gltf;
  return folder;
}

}  // namespace

TEST_CASE("through the AssetManager: decoded on a worker, uploaded on update(), released on unload") {
  const auto folder = writeModelFolder("asset");
  JobSystem jobs(2);
  engine::AssetManager assets(jobs, folder);
  RecordingRenderer renderer;
  assets.registerType<engine::Model>(engine::gltfLoader(renderer));

  const auto handle = assets.load<engine::Model>("quad.gltf");
  while (!assets.idle()) {
    (void)assets.update();
    std::this_thread::yield();
  }
  REQUIRE_MESSAGE(assets.state(handle) == engine::AssetState::Ready,
                  (assets.error(handle) ? assets.error(handle)->message : std::string("not ready")));
  const engine::Model* model = assets.get(handle);
  REQUIRE(model != nullptr);
  CHECK(model->meshCount() == 2);
  CHECK(renderer.triangles == 4);
  // One image, used as base colour (sRGB) and as data (normal and
  // metallic-roughness share one upload): two textures.
  CHECK(model->textureCount() == 2);
  REQUIRE(renderer.usages.size() == 2);
  CHECK(renderer.usages[0] == render::TextureUsage::Colour);
  CHECK(renderer.usages[1] == render::TextureUsage::Data);
  REQUIRE(model->draws().size() == 2);

  render::FrameData frame;
  model->extract(glm::mat4(2.0f), frame);
  REQUIRE(frame.meshes.size() == 2);
  // The material the primitive names, with its textures; the other gets the default (no textures).
  const auto withMaterial = std::find_if(frame.meshes.begin(), frame.meshes.end(),
                                         [](const render::MeshInstance& m) { return m.material.baseColour.valid(); });
  REQUIRE(withMaterial != frame.meshes.end());
  CHECK(withMaterial->material.normal == withMaterial->material.metallicRoughness);
  CHECK(withMaterial->material.normal != withMaterial->material.baseColour);
  CHECK(withMaterial->transform == glm::mat4(2.0f));

  assets.unload(handle);
  CHECK(renderer.meshesDestroyed == 2);
  CHECK(renderer.texturesDestroyed == 2);
}

TEST_CASE("a failed upload releases what it had already created; a missing file is an I/O failure") {
  const auto folder = writeModelFolder("failure");
  JobSystem jobs(2);
  engine::AssetManager assets(jobs, folder);
  RecordingRenderer renderer;
  renderer.failMeshNumber = 2;  // the second mesh cannot be made
  assets.registerType<engine::Model>(engine::gltfLoader(renderer));

  const auto broken = assets.load<engine::Model>("quad.gltf");
  const auto missing = assets.load<engine::Model>("nope.gltf");
  while (!assets.idle()) {
    (void)assets.update();
    std::this_thread::yield();
  }
  CHECK(assets.state(broken) == engine::AssetState::Failed);
  REQUIRE(assets.error(broken) != nullptr);
  CHECK(assets.error(broken)->code == Errc::GpuFailure);
  CHECK(renderer.meshesDestroyed == 1);    // the first mesh
  CHECK(renderer.texturesDestroyed == 2);  // and both textures
  CHECK(assets.state(missing) == engine::AssetState::Failed);
  CHECK(assets.error(missing)->code == Errc::IoFailure);
}

TEST_CASE("clear() unloads every model, so they can go before the renderer") {
  const auto folder = writeModelFolder("clear");
  JobSystem jobs(2);
  RecordingRenderer renderer;
  engine::AssetManager assets(jobs, folder);
  assets.registerType<engine::Model>(engine::gltfLoader(renderer));
  const auto handle = assets.load<engine::Model>("quad.gltf");
  assets.waitForDecodes();
  (void)assets.update(100);
  REQUIRE(assets.get(handle) != nullptr);

  const auto pending = assets.load<engine::Model>("quad.gltf");  // cached: same handle
  CHECK(pending == handle);
  assets.clear();
  CHECK(renderer.meshesDestroyed == 2);
  CHECK(renderer.texturesDestroyed == 2);
  CHECK(assets.state(handle) == engine::AssetState::Invalid);
  CHECK(assets.idle());
  // And the manager still works afterwards.
  const auto again = assets.load<engine::Model>("quad.gltf");
  CHECK(again != handle);
}
