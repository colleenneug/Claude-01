#pragma once
// =============================================================================
//  playground/gpu/DeviceHandle.h — RAII for small Vulkan objects.
//
//  Almost every Vulkan object created from a device is destroyed by a call of
//  the same shape:
//
//      vkDestroyX(VkDevice device, VkX object, const VkAllocationCallbacks* allocator);
//
//  so one template, given the handle type and its destroy function, covers
//  semaphores, fences, image views, shader modules, pipelines and the rest.
//  Each object then cleans itself up exactly once, in the right place, and
//  cannot be copied (two owners would destroy it twice).
//
//      gpu::Fence fence(device, rawFence);   // destroyed when `fence` dies
//
//  The device must outlive every handle made from it. The renderer gets that
//  by declaring its gpu::Context before everything else.
// =============================================================================

#include <utility>

#include <vulkan/vulkan.h>

namespace playground::gpu {

template <class Handle, auto Destroy>
class DeviceHandle {
 public:
  DeviceHandle() noexcept = default;
  DeviceHandle(VkDevice device, Handle handle) noexcept : device_(device), handle_(handle) {}

  DeviceHandle(DeviceHandle&& other) noexcept
      : device_(other.device_), handle_(std::exchange(other.handle_, VK_NULL_HANDLE)) {}

  DeviceHandle& operator=(DeviceHandle&& other) noexcept {
    if (this != &other) {
      reset();
      device_ = other.device_;
      handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);
    }
    return *this;
  }

  DeviceHandle(const DeviceHandle&) = delete;
  DeviceHandle& operator=(const DeviceHandle&) = delete;
  ~DeviceHandle() { reset(); }

  void reset() noexcept {
    if (handle_ != VK_NULL_HANDLE) {
      Destroy(device_, handle_, nullptr);
      handle_ = VK_NULL_HANDLE;
    }
  }

  [[nodiscard]] Handle get() const noexcept { return handle_; }
  [[nodiscard]] explicit operator bool() const noexcept { return handle_ != VK_NULL_HANDLE; }

 private:
  VkDevice device_ = VK_NULL_HANDLE;
  Handle handle_ = VK_NULL_HANDLE;
};

using Semaphore = DeviceHandle<VkSemaphore, &vkDestroySemaphore>;
using Fence = DeviceHandle<VkFence, &vkDestroyFence>;
using ImageView = DeviceHandle<VkImageView, &vkDestroyImageView>;
using ShaderModule = DeviceHandle<VkShaderModule, &vkDestroyShaderModule>;
using PipelineLayout = DeviceHandle<VkPipelineLayout, &vkDestroyPipelineLayout>;
using Pipeline = DeviceHandle<VkPipeline, &vkDestroyPipeline>;
using CommandPool = DeviceHandle<VkCommandPool, &vkDestroyCommandPool>;
using DescriptorSetLayout = DeviceHandle<VkDescriptorSetLayout, &vkDestroyDescriptorSetLayout>;
using DescriptorPool = DeviceHandle<VkDescriptorPool, &vkDestroyDescriptorPool>;

}  // namespace playground::gpu
