#pragma once
// =============================================================================
//  playground/gpu/Context.h — the four bring-up steps, owned together.
//
//    1. Instance        connection to the Vulkan loader (+ validation)
//    2. Surface         Vulkan's handle on the window
//    3. PhysicalDevice  which GPU (chosen, not created)
//    4. Device          our session with that GPU, and its queues
//    5. Allocator       GPU memory for buffers and images (VMA)
//
//  Context::create() runs them in that order; any failure returns an error
//  naming the step and the reason, and whatever was already created is
//  destroyed on the way out.
//
//  DESTRUCTION ORDER
//    The members are declared in creation order, so C++ destroys them in
//    reverse: allocator, device, surface, instance. Getting this wrong is a
//    validation error at best and a crash in the driver at worst.
//
//    The Context must also be destroyed before the Window it was created
//    from: the surface belongs to that window.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/gpu/Allocator.h"
#include "playground/gpu/Device.h"
#include "playground/gpu/Instance.h"
#include "playground/gpu/PhysicalDevice.h"
#include "playground/gpu/Surface.h"
#include "playground/platform/Platform.h"
#include "playground/platform/Window.h"

#include <string>

namespace playground::gpu {

struct ContextDesc {
  std::string applicationName = "Aedan's Playground";
  bool enableValidation = kValidationByDefault;  // on in debug builds
};

class Context {
 public:
  [[nodiscard]] static Result<Context> create(platform::Platform& platform, platform::Window& window,
                                              const ContextDesc& desc);

  // Move construction only. A defaulted move ASSIGNMENT would assign the
  // members in declaration order, destroying the old instance before the old
  // device — exactly backwards — so it is deleted.
  Context(Context&&) noexcept = default;
  Context& operator=(Context&&) = delete;
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  ~Context();

  [[nodiscard]] VkInstance instance() const noexcept { return instance_.handle(); }
  [[nodiscard]] VkSurfaceKHR surface() const noexcept { return surface_.handle(); }
  [[nodiscard]] const PhysicalDeviceInfo& physicalDevice() const noexcept { return physicalDevice_; }
  [[nodiscard]] const Device& device() const noexcept { return device_; }
  [[nodiscard]] const Allocator& allocator() const noexcept { return allocator_; }
  [[nodiscard]] bool validationEnabled() const noexcept { return instance_.validationEnabled(); }

 private:
  Context(Instance instance, Surface surface, PhysicalDeviceInfo physicalDevice, Device device,
          Allocator allocator) noexcept;

  // Creation order. Destruction is the reverse — do not reorder.
  Instance instance_;
  Surface surface_;
  PhysicalDeviceInfo physicalDevice_;
  Device device_;
  Allocator allocator_;  // after device_: destroyed before it
};

}  // namespace playground::gpu
