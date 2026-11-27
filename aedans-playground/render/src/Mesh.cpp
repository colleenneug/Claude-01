#include "Mesh.h"

#include <algorithm>
#include <limits>
#include <span>
#include <string>
#include <utility>

namespace playground::render {

Result<Mesh> Mesh::create(const gpu::Context& context, const MeshData& data, std::string_view debugName) {
  if (data.vertices.empty() || data.indices.empty()) {
    return fail(Errc::InvalidArgument, "mesh \"{}\" has {} vertices and {} indices; it needs both", debugName,
                data.vertices.size(), data.indices.size());
  }
  if (data.indices.size() % 3 != 0) {
    return fail(Errc::InvalidArgument, "mesh \"{}\": {} indices is not a whole number of triangles", debugName,
                data.indices.size());
  }
  if (data.vertices.size() > std::numeric_limits<u32>::max() || data.indices.size() > std::numeric_limits<u32>::max()) {
    return fail(Errc::InvalidArgument, "mesh \"{}\" is too large for 32-bit indices", debugName);
  }
  // A bad index would make the vertex shader read past the buffer: on a
  // real GPU, garbage or a device loss. One pass over the indices here
  // turns that into an error message.
  const u32 largest = *std::max_element(data.indices.begin(), data.indices.end());
  if (largest >= data.vertices.size()) {
    return fail(Errc::InvalidArgument, "mesh \"{}\": index {} points past its last vertex ({})", debugName, largest,
                data.vertices.size() - 1);
  }

  Mesh mesh;
  const std::string vertexName = std::string(debugName) + " (vertices)";
  auto vertices = gpu::createDeviceLocalBuffer(context, std::as_bytes(std::span(data.vertices)),
                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexName);
  if (!vertices) return tl::unexpected(std::move(vertices.error()));
  const std::string indexName = std::string(debugName) + " (indices)";
  auto indices = gpu::createDeviceLocalBuffer(context, std::as_bytes(std::span(data.indices)),
                                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexName);
  if (!indices) return tl::unexpected(std::move(indices.error()));

  mesh.vertices_ = std::move(*vertices);
  mesh.indices_ = std::move(*indices);
  mesh.vertexCount_ = static_cast<u32>(data.vertices.size());
  mesh.indexCount_ = static_cast<u32>(data.indices.size());
  for (const Vertex& v : data.vertices) mesh.bounds_.add(v.position);  // for fitting the shadow map
  return mesh;
}

}  // namespace playground::render
