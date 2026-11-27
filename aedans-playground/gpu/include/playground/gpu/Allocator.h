#pragma once
// =============================================================================
//  playground/gpu/Allocator.h — GPU memory, through the Vulkan Memory Allocator.
//
//  WHY NOT vkAllocateMemory DIRECTLY
//    Drivers allow only a few thousand separate allocations (often 4096), and
//    each one is slow. Real engines allocate large blocks and sub-allocate
//    buffers and images from them, choosing the right memory type for each
//    use. VMA does exactly that, and it is the standard way to do it; writing
//    our own would be a project of its own.
//
//  OWNERSHIP
//    gpu::Context owns the Allocator and declares it after the Device, so it
//    is destroyed before the device. Every gpu::Buffer must be destroyed
//    before the Allocator: at destruction the Allocator logs an error for
//    anything still allocated, and VMA itself asserts in debug builds.
//
//  Buffer device addresses are enabled (the device has the feature), so
//  buffers can be read by shaders through a 64-bit GPU pointer.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace playground::gpu {

struct AllocatorStats {
  u64 allocationCount = 0;  // live buffers/images
  u64 allocationBytes = 0;  // bytes they use
  u64 blockBytes = 0;       // bytes reserved from the driver (>= allocationBytes)
};

class Allocator {
 public:
  [[nodiscard]] static Result<Allocator> create(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);

  Allocator(Allocator&& other) noexcept;
  Allocator& operator=(Allocator&& other) noexcept;
  Allocator(const Allocator&) = delete;
  Allocator& operator=(const Allocator&) = delete;
  ~Allocator();

  [[nodiscard]] VmaAllocator handle() const noexcept { return allocator_; }
  [[nodiscard]] VkDevice device() const noexcept { return device_; }
  [[nodiscard]] AllocatorStats stats() const noexcept;

 private:
  Allocator() = default;
  void destroy() noexcept;

  VmaAllocator allocator_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
};

}  // namespace playground::gpu
