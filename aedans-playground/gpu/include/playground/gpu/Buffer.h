#pragma once
// =============================================================================
//  playground/gpu/Buffer.h — a block of GPU memory, as an RAII object.
//
//  TWO KINDS, BECAUSE THE HARDWARE HAS TWO KINDS OF MEMORY
//
//    HostVisible   The CPU can write it directly. Created mapped and kept
//                  mapped for its whole life ("persistent mapping"): writing
//                  is a plain memcpy, with no map/unmap per frame. On a
//                  discrete GPU this memory is slower for the GPU to read,
//                  and the CPU should write it front to back and never read
//                  it back (it is often uncached). Right for data rewritten
//                  every frame, like the sprite list.
//
//    DeviceLocal   The GPU's own memory: fastest for the GPU, invisible to
//                  the CPU. Filled by writing a temporary HostVisible
//                  "staging" buffer and having the GPU copy it across
//                  (createDeviceLocalBuffer). Right for data written once,
//                  like meshes.
//
//    HostReadback  Mapped memory the CPU READS: cached, so reading is fast.
//                  For results coming back from the GPU (screenshots, tests).
//                  Call invalidate() before reading.
//
//  Shaders can read a buffer through its 64-bit GPU address
//  (deviceAddress()) when it was created with
//  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT. That is how the sprite shader
//  finds its data, with no descriptor sets.
//
//  Destroy every Buffer before the Allocator (gpu::Context) it came from.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/gpu/Allocator.h"

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace playground::gpu {

class Context;

enum class MemoryKind : u8 { HostVisible, DeviceLocal, HostReadback };

struct BufferDesc {
  u64 sizeBytes = 0;
  VkBufferUsageFlags usage = 0;
  MemoryKind memory = MemoryKind::DeviceLocal;
  std::string_view debugName;  // shown in leak reports; set it
};

class Buffer {
 public:
  // Errc::InvalidArgument for a zero size or no usage; Errc::GpuFailure if
  // the allocation fails (the message says how big and what for).
  [[nodiscard]] static Result<Buffer> create(const Allocator& allocator, const BufferDesc& desc);

  Buffer() = default;  // empty: owns nothing
  Buffer(Buffer&& other) noexcept;
  Buffer& operator=(Buffer&& other) noexcept;
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  ~Buffer();

  [[nodiscard]] VkBuffer handle() const noexcept { return buffer_; }
  [[nodiscard]] u64 size() const noexcept { return size_; }
  [[nodiscard]] bool isHostVisible() const noexcept { return mapped_ != nullptr; }
  [[nodiscard]] explicit operator bool() const noexcept { return buffer_ != VK_NULL_HANDLE; }

  // The persistent mapping of a HostVisible or HostReadback buffer; empty
  // for DeviceLocal.
  [[nodiscard]] std::span<std::byte> mapped() const noexcept;

  // Copies bytes into a HostVisible buffer at `offset`, then flushes.
  [[nodiscard]] Status write(std::span<const std::byte> bytes, u64 offset = 0);

  // Makes CPU writes through mapped() visible to the GPU. A no-op on memory
  // that is host-coherent (most of it); required on memory that is not.
  [[nodiscard]] Status flush(u64 offset = 0, u64 bytes = VK_WHOLE_SIZE);

  // Makes GPU writes visible to CPU reads through mapped(). Call after the
  // GPU work that wrote the buffer has finished. A no-op on coherent memory.
  [[nodiscard]] Status invalidate(u64 offset = 0, u64 bytes = VK_WHOLE_SIZE);

  // The buffer's GPU address, for shaders (needs SHADER_DEVICE_ADDRESS usage).
  [[nodiscard]] VkDeviceAddress deviceAddress() const noexcept { return address_; }

 private:
  void destroy() noexcept;

  VmaAllocator allocator_ = VK_NULL_HANDLE;
  VkBuffer buffer_ = VK_NULL_HANDLE;
  VmaAllocation allocation_ = VK_NULL_HANDLE;
  std::byte* mapped_ = nullptr;
  u64 size_ = 0;
  VkDeviceAddress address_ = 0;
};

// Records commands with `record`, submits them to the graphics queue and
// waits until the GPU has run them. For loading and tests, not per frame:
// it stalls the CPU until the GPU is done.
[[nodiscard]] Status submitAndWait(const Context& context, const std::function<void(VkCommandBuffer)>& record);

// GPU copy of `bytes` bytes from src to dst (both need the matching
// TRANSFER usage). Waits for completion.
[[nodiscard]] Status copyBuffer(const Context& context, const Buffer& src, const Buffer& dst, u64 bytes);

// A DeviceLocal buffer holding `data`, uploaded through a staging buffer.
// TRANSFER_DST is added to `usage` automatically. Waits for completion.
[[nodiscard]] Result<Buffer> createDeviceLocalBuffer(const Context& context, std::span<const std::byte> data,
                                                     VkBufferUsageFlags usage, std::string_view debugName);

}  // namespace playground::gpu
