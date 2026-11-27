#include "playground/gpu/Allocator.h"

#include "playground/core/Log.h"
#include "playground/gpu/Instance.h"
#include "playground/gpu/VulkanCheck.h"

#include <utility>

namespace playground::gpu {

using log::Category;

Result<Allocator> Allocator::create(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
  if (instance == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE) {
    return fail(Errc::InvalidArgument, "the memory allocator needs a live instance, GPU and device");
  }

  const VmaAllocatorCreateInfo info{
      // Buffers may be read by shaders through their GPU address
      // (bufferDeviceAddress is a required device feature, Requirements.h).
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = physicalDevice,
      .device = device,
      .instance = instance,
      .vulkanApiVersion = kRequiredApiVersion,
  };

  Allocator allocator;
  allocator.device_ = device;
  if (Status s = check(vmaCreateAllocator(&info, &allocator.allocator_), "vmaCreateAllocator"); !s) {
    return tl::unexpected(std::move(s.error()));
  }
  log::info(Category::Gpu, "GPU memory allocator ready (Vulkan Memory Allocator 3.3)");
  return allocator;
}

Allocator::Allocator(Allocator&& other) noexcept
    : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)), device_(std::exchange(other.device_, VK_NULL_HANDLE)) {}

Allocator& Allocator::operator=(Allocator&& other) noexcept {
  if (this != &other) {
    destroy();
    allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
  }
  return *this;
}

Allocator::~Allocator() { destroy(); }

AllocatorStats Allocator::stats() const noexcept {
  if (allocator_ == VK_NULL_HANDLE) return {};
  VmaTotalStatistics total{};
  vmaCalculateStatistics(allocator_, &total);
  return {
      .allocationCount = total.total.statistics.allocationCount,
      .allocationBytes = total.total.statistics.allocationBytes,
      .blockBytes = total.total.statistics.blockBytes,
  };
}

void Allocator::destroy() noexcept {
  if (allocator_ == VK_NULL_HANDLE) return;
  // A buffer still alive here is a leak: it outlived the memory it lives in.
  const AllocatorStats leftover = stats();
  if (leftover.allocationCount > 0) {
    log::error(Category::Gpu, "GPU memory leak: {} allocation(s), {} bytes, still alive when the allocator was destroyed",
               leftover.allocationCount, leftover.allocationBytes);
  }
  vmaDestroyAllocator(allocator_);
  allocator_ = VK_NULL_HANDLE;
  log::info(Category::Gpu, "GPU memory allocator destroyed");
}

}  // namespace playground::gpu
