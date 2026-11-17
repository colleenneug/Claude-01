#pragma once
// =============================================================================
//  playground/gpu/Instance.h — STEP 1: the Vulkan instance.
//
//  WHAT IT IS
//    The connection between this program and the Vulkan loader. It records
//    which Vulkan version we target and which instance-level extensions and
//    layers we use. Nothing else in Vulkan can exist without one.
//
//  WHAT create() DOES, IN ORDER, LOGGING EACH STEP
//    1. Asks the loader which Vulkan version it supports; refuses below 1.3.
//    2. Lists the instance extensions available and checks every required
//       one is there (the window surface extensions come from GLFW). A
//       missing extension is named in the error, not just counted.
//    3. If validation is requested, checks the Khronos validation layer is
//       installed. If not, it warns and carries on without it: validation is
//       a development aid, and a machine without the SDK must still run.
//    4. Creates the instance. With validation on, a debug messenger is
//       chained into the create call so messages from vkCreateInstance and
//       vkDestroyInstance themselves are caught too.
//    5. Creates the long-lived debug messenger.
//
//  VALIDATION MESSAGES
//    Routed into the engine log (category "gpu") at a matching level, and
//    counted in process-wide counters. The app checks the error count at exit
//    and fails the run if it is non-zero: a validation error is a bug, not a
//    line that scrolls past.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"

#include <span>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace playground::gpu {

// The one version this engine targets. 1.3 makes dynamic rendering and
// synchronization2 core, which removes render-pass and framebuffer objects.
inline constexpr u32 kRequiredApiVersion = VK_API_VERSION_1_3;

#if defined(NDEBUG)
inline constexpr bool kValidationByDefault = false;
#else
inline constexpr bool kValidationByDefault = true;
#endif

struct InstanceDesc {
  std::string applicationName = "Aedan's Playground";
  u32 applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
  bool enableValidation = kValidationByDefault;
  // Extensions the caller needs, e.g. Platform::requiredVulkanInstanceExtensions().
  std::vector<const char*> requiredExtensions;
};

// Counts of validation messages since start-up (or the last reset), across
// every instance. Process-wide so they can be read after the instance is
// gone, which is when leak reports arrive.
struct ValidationCounts {
  u32 errors = 0;
  u32 warnings = 0;
};
[[nodiscard]] ValidationCounts validationCounts() noexcept;
void resetValidationCounts() noexcept;

class Instance {
 public:
  // Errc::Unsupported when the loader is too old or an extension is missing;
  // Errc::GpuFailure when a Vulkan call fails.
  [[nodiscard]] static Result<Instance> create(const InstanceDesc& desc);

  Instance(Instance&& other) noexcept;
  Instance& operator=(Instance&& other) noexcept;
  Instance(const Instance&) = delete;
  Instance& operator=(const Instance&) = delete;
  ~Instance();

  [[nodiscard]] VkInstance handle() const noexcept { return instance_; }
  [[nodiscard]] bool validationEnabled() const noexcept { return messenger_ != VK_NULL_HANDLE; }

 private:
  Instance() = default;
  void destroy() noexcept;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
};

}  // namespace playground::gpu
