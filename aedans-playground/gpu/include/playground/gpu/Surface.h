#pragma once
// =============================================================================
//  playground/gpu/Surface.h — STEP 2: the window's Vulkan surface.
//
//  WHY IT COMES BEFORE DEVICE SELECTION
//    A VkSurfaceKHR is Vulkan's handle on "this window". Choosing a GPU needs
//    it: a GPU that can render but cannot present to this window (a headless
//    compute card, or a GPU wired to a different display) is useless to a
//    game, and the only way to ask is vkGetPhysicalDeviceSurfaceSupportKHR,
//    which takes a surface.
//
//  OWNERSHIP
//    Owns the surface; does not own the instance or the window. Both must
//    outlive it, which Context guarantees by member order.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/platform/Window.h"

#include <vulkan/vulkan.h>

namespace playground::gpu {

class Surface {
 public:
  // Errc::GpuFailure if GLFW/the driver refuses; the message says why.
  [[nodiscard]] static Result<Surface> create(VkInstance instance, platform::Window& window);

  Surface(Surface&& other) noexcept;
  Surface& operator=(Surface&& other) noexcept;
  Surface(const Surface&) = delete;
  Surface& operator=(const Surface&) = delete;
  ~Surface();

  [[nodiscard]] VkSurfaceKHR handle() const noexcept { return surface_; }

 private:
  Surface(VkInstance instance, VkSurfaceKHR surface) noexcept : instance_(instance), surface_(surface) {}
  void destroy() noexcept;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
};

}  // namespace playground::gpu
