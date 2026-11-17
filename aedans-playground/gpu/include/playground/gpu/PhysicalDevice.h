#pragma once
// =============================================================================
//  playground/gpu/PhysicalDevice.h — STEP 3: choosing the GPU.
//
//  A VkPhysicalDevice is a GPU as the driver reports it. It is not created or
//  destroyed, only chosen, so this step is a function, not a class.
//
//  For every GPU found, selectPhysicalDevice() logs its name, type, Vulkan
//  version and driver, then checks it against the engine's requirements:
//
//    - Vulkan 1.3
//    - the device extensions in Requirements.h (VK_KHR_swapchain)
//    - the features in Requirements.h (dynamic rendering, synchronization2,
//      timeline semaphores, descriptor indexing, buffer device address)
//    - a queue family that can do graphics, and one that can present to the
//      window's surface (usually the same family)
//    - at least one surface format and present mode for that window
//
//  A GPU that fails is logged with EVERY reason it failed, not just the
//  first, so a report from a player's machine shows the whole picture.
//
//  Among the GPUs that pass, the score prefers discrete over integrated over
//  virtual over CPU (software) devices, then a single queue family for both
//  graphics and present. A software device (llvmpipe, SwiftShader) is
//  accepted but logged as a warning: it is right for tests, not for play.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"

#include <string>
#include <string_view>

#include <vulkan/vulkan.h>

namespace playground::gpu {

inline constexpr u32 kNoQueueFamily = ~0u;

struct QueueFamilies {
  u32 graphics = kNoQueueFamily;
  u32 present = kNoQueueFamily;

  [[nodiscard]] bool complete() const noexcept { return graphics != kNoQueueFamily && present != kNoQueueFamily; }
  [[nodiscard]] bool shared() const noexcept { return graphics == present; }
};

struct PhysicalDeviceInfo {
  VkPhysicalDevice handle = VK_NULL_HANDLE;
  std::string name;
  VkPhysicalDeviceType type = VK_PHYSICAL_DEVICE_TYPE_OTHER;
  u32 apiVersion = 0;
  std::string driver;  // e.g. "NVIDIA 560.35.03" or "llvmpipe Mesa 25.2.8"
  QueueFamilies queues;
};

// Errc::Unsupported if no GPU meets the requirements; the message summarises
// each GPU and why it was rejected. Errc::GpuFailure if a query call fails.
[[nodiscard]] Result<PhysicalDeviceInfo> selectPhysicalDevice(VkInstance instance, VkSurfaceKHR surface);

// "discrete GPU", "integrated GPU", "CPU (software)"...
[[nodiscard]] std::string_view describe(VkPhysicalDeviceType type) noexcept;

}  // namespace playground::gpu
