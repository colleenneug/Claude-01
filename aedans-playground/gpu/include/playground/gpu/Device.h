#pragma once
// =============================================================================
//  playground/gpu/Device.h — STEP 4: the logical device and its queues.
//
//  WHAT IT IS
//    The physical device is the GPU; the logical device (VkDevice) is this
//    program's session with it. Almost every later Vulkan object — buffers,
//    images, pipelines, command pools — is created from the VkDevice.
//
//  WHAT create() DOES
//    1. Asks for one queue from each distinct queue family it needs. On most
//       GPUs graphics and present are the same family, so that is one queue;
//       asking for the same family twice is a validation error.
//    2. Enables exactly the extensions and features that device selection
//       checked for (both come from Requirements.h).
//    3. Creates the device and fetches the queue handles.
//
//  QUEUES
//    A queue is where command buffers are submitted. The graphics queue runs
//    draws and compute; the present queue hands finished images to the
//    window. When they come from the same family they are the same VkQueue.
//    More queues (async compute, transfer) wait until a profile shows a gap.
//
//  DESTRUCTION
//    Waits for the GPU to finish everything, then destroys the device. Every
//    object created from the device must be destroyed before it.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/gpu/PhysicalDevice.h"

#include <vulkan/vulkan.h>

namespace playground::gpu {

class Device {
 public:
  // Errc::GpuFailure with the driver's reason if vkCreateDevice fails.
  [[nodiscard]] static Result<Device> create(const PhysicalDeviceInfo& physicalDevice);

  Device(Device&& other) noexcept;
  Device& operator=(Device&& other) noexcept;
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  ~Device();

  [[nodiscard]] VkDevice handle() const noexcept { return device_; }
  [[nodiscard]] VkQueue graphicsQueue() const noexcept { return graphicsQueue_; }
  [[nodiscard]] VkQueue presentQueue() const noexcept { return presentQueue_; }
  [[nodiscard]] const QueueFamilies& queueFamilies() const noexcept { return families_; }

  // Blocks until the GPU is idle. Needed before destroying anything the GPU
  // might still be using, and before recreating the swapchain.
  [[nodiscard]] Status waitIdle() const;

 private:
  Device() = default;
  void destroy() noexcept;

  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue graphicsQueue_ = VK_NULL_HANDLE;
  VkQueue presentQueue_ = VK_NULL_HANDLE;
  QueueFamilies families_{};
};

}  // namespace playground::gpu
