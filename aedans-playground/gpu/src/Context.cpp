#include "playground/gpu/Context.h"

#include "playground/core/Log.h"
#include "playground/gpu/VulkanCheck.h"

#include <format>
#include <string_view>
#include <utility>

namespace playground::gpu {

using log::Category;

namespace {

// Prefixes an error with the bring-up step it came from, so the top-level
// message reads "GPU start-up failed at step 3 (choosing a GPU): ...".
Error atStep(int step, std::string_view what, Error error) {
  error.message = std::format("step {} ({}): {}", step, what, error.message);
  return error;
}

}  // namespace

Context::Context(Instance instance, Surface surface, PhysicalDeviceInfo physicalDevice, Device device,
                 Allocator allocator) noexcept
    : instance_(std::move(instance)),
      surface_(std::move(surface)),
      physicalDevice_(std::move(physicalDevice)),
      device_(std::move(device)),
      allocator_(std::move(allocator)) {}

Result<Context> Context::create(platform::Platform& platform, platform::Window& window, const ContextDesc& desc) {
  if (!platform.vulkanSupported()) {
    return fail(Errc::Unsupported,
                "no Vulkan loader or driver was found on this machine; install or update the GPU driver");
  }

  // 1. Instance, with the extensions GLFW needs to make a window surface.
  const auto platformExtensions = platform.requiredVulkanInstanceExtensions();
  auto instance = Instance::create({
      .applicationName = desc.applicationName,
      .enableValidation = desc.enableValidation,
      .requiredExtensions = {platformExtensions.begin(), platformExtensions.end()},
  });
  if (!instance) return tl::unexpected(atStep(1, "Vulkan instance", std::move(instance.error())));

  // 2. Surface.
  auto surface = Surface::create(instance->handle(), window);
  if (!surface) return tl::unexpected(atStep(2, "window surface", std::move(surface.error())));

  // 3. Physical device.
  auto physicalDevice = selectPhysicalDevice(instance->handle(), surface->handle());
  if (!physicalDevice) return tl::unexpected(atStep(3, "choosing a GPU", std::move(physicalDevice.error())));

  // 4. Logical device and queues.
  auto device = Device::create(*physicalDevice);
  if (!device) return tl::unexpected(atStep(4, "logical device", std::move(device.error())));

  // 5. GPU memory allocator.
  auto allocator = Allocator::create(instance->handle(), physicalDevice->handle, device->handle());
  if (!allocator) return tl::unexpected(atStep(5, "memory allocator", std::move(allocator.error())));

  log::info(Category::Gpu, "GPU ready: {} with Vulkan {}", physicalDevice->name,
            versionString(physicalDevice->apiVersion));
  return Context(std::move(*instance), std::move(*surface), std::move(*physicalDevice), std::move(*device),
                 std::move(*allocator));
}

Context::~Context() = default;  // members destroy themselves in reverse order

}  // namespace playground::gpu
