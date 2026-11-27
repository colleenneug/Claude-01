#pragma once
// =============================================================================
//  render/src/Mesh.h — a mesh in GPU memory (internal to the render layer).
//
//  Two DEVICE-LOCAL buffers, the GPU's own fast memory, filled once through
//  a staging copy (gpu::createDeviceLocalBuffer):
//    vertices  render::Vertex[], read by the vertex shader's inputs;
//    indices   u32[], three per triangle, so shared corners are stored once.
//  Drawn with vkCmdBindVertexBuffers + vkCmdBindIndexBuffer + vkCmdDrawIndexed.
//
//  Outside the render layer a mesh is a render::MeshHandle; the renderer owns
//  the Mesh (VulkanRenderer::createMesh).
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/gpu/Buffer.h"
#include "playground/gpu/Context.h"
#include "playground/render/Resources.h"

#include <string_view>

namespace playground::render {

class Mesh {
 public:
  // Errc::InvalidArgument for no vertices, no triangles, an index count
  // that is not a multiple of 3, or an index past the last vertex (which
  // would make the GPU read outside the buffer). Waits for the upload.
  [[nodiscard]] static Result<Mesh> create(const gpu::Context& context, const MeshData& data,
                                           std::string_view debugName);

  [[nodiscard]] VkBuffer vertexBuffer() const noexcept { return vertices_.handle(); }
  [[nodiscard]] VkBuffer indexBuffer() const noexcept { return indices_.handle(); }
  [[nodiscard]] u32 vertexCount() const noexcept { return vertexCount_; }
  [[nodiscard]] u32 indexCount() const noexcept { return indexCount_; }

 private:
  gpu::Buffer vertices_;
  gpu::Buffer indices_;
  u32 vertexCount_ = 0;
  u32 indexCount_ = 0;
};

}  // namespace playground::render
