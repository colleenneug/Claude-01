#include "playground/gpu/Device.h"

#include "Requirements.h"
#include "playground/core/Log.h"
#include "playground/gpu/VulkanCheck.h"

#include <utility>
#include <vector>

namespace playground::gpu {

using log::Category;

Result<Device> Device::create(const PhysicalDeviceInfo& physicalDevice) {
  const QueueFamilies& families = physicalDevice.queues;
  if (physicalDevice.handle == VK_NULL_HANDLE || !families.complete()) {
    return fail(Errc::InvalidArgument, "a logical device needs a selected GPU with graphics and present queues");
  }

  log::info(Category::Gpu, "creating logical device on {}", physicalDevice.name);

  // --- 1. Queues: one per distinct family --------------------------------
  // Priority only matters between queues of the same family; 1.0 is "normal".
  const float priority = 1.0f;
  std::vector<VkDeviceQueueCreateInfo> queueInfos;
  queueInfos.push_back({
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = families.graphics,
      .queueCount = 1,
      .pQueuePriorities = &priority,
  });
  if (!families.shared()) {
    queueInfos.push_back({
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = families.present,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    });
  }

  // --- 2. Extensions and features: exactly what selection checked --------
  detail::FeatureChain features;
  detail::requestEngineFeatures(features);
  features.core.features.samplerAnisotropy = physicalDevice.samplerAnisotropy ? VK_TRUE : VK_FALSE;  // optional

  const VkDeviceCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &features.core,  // VkPhysicalDeviceFeatures2 -> 1.2 -> 1.3 features
      .queueCreateInfoCount = static_cast<u32>(queueInfos.size()),
      .pQueueCreateInfos = queueInfos.data(),
      .enabledExtensionCount = static_cast<u32>(detail::kRequiredDeviceExtensions.size()),
      .ppEnabledExtensionNames = detail::kRequiredDeviceExtensions.data(),
      .pEnabledFeatures = nullptr,  // must be null when VkPhysicalDeviceFeatures2 is in pNext
  };

  // --- 3. Create, then fetch the queues -----------------------------------
  Device device;
  if (Status s = check(vkCreateDevice(physicalDevice.handle, &createInfo, nullptr, &device.device_), "vkCreateDevice");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  device.families_ = families;
  vkGetDeviceQueue(device.device_, families.graphics, 0, &device.graphicsQueue_);
  vkGetDeviceQueue(device.device_, families.present, 0, &device.presentQueue_);

  if (device.graphicsQueue_ == VK_NULL_HANDLE || device.presentQueue_ == VK_NULL_HANDLE) {
    return fail(Errc::GpuFailure, "vkGetDeviceQueue returned no queue (graphics family {}, present family {})",
                families.graphics, families.present);
  }

  log::info(Category::Gpu, "logical device created: extensions {}; features dynamicRendering, synchronization2, "
                           "timelineSemaphore, descriptorIndexing (update after bind), bufferDeviceAddress{}",
            detail::kRequiredDeviceExtensions[0], physicalDevice.samplerAnisotropy ? ", samplerAnisotropy" : "");
  if (families.shared()) {
    log::info(Category::Gpu, "graphics queue ready: family {}, also used for present", families.graphics);
  } else {
    log::info(Category::Gpu, "graphics queue ready: family {}; separate present queue: family {}", families.graphics,
              families.present);
  }
  return device;
}

Device::Device(Device&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      graphicsQueue_(std::exchange(other.graphicsQueue_, VK_NULL_HANDLE)),
      presentQueue_(std::exchange(other.presentQueue_, VK_NULL_HANDLE)),
      families_(other.families_) {}

Device& Device::operator=(Device&& other) noexcept {
  if (this != &other) {
    destroy();
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    graphicsQueue_ = std::exchange(other.graphicsQueue_, VK_NULL_HANDLE);
    presentQueue_ = std::exchange(other.presentQueue_, VK_NULL_HANDLE);
    families_ = other.families_;
  }
  return *this;
}

Device::~Device() { destroy(); }

Status Device::waitIdle() const {
  if (device_ == VK_NULL_HANDLE) return {};
  return check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
}

void Device::destroy() noexcept {
  if (device_ == VK_NULL_HANDLE) return;
  // The GPU may still be running work that uses this device. A failure here
  // (device lost) is logged; the device is destroyed regardless.
  if (Status s = check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle"); !s) {
    log::error(Category::Gpu, "{}", s.error().message);
  }
  vkDestroyDevice(device_, nullptr);
  device_ = VK_NULL_HANDLE;
  graphicsQueue_ = VK_NULL_HANDLE;
  presentQueue_ = VK_NULL_HANDLE;
  log::info(Category::Gpu, "logical device destroyed");
}

}  // namespace playground::gpu
