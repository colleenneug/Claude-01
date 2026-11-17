#include "playground/gpu/Surface.h"

#include "playground/core/Log.h"
#include "playground/gpu/VulkanCheck.h"

#include <utility>

// Tells GLFW to declare its Vulkan functions (glfwCreateWindowSurface).
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace playground::gpu {

using log::Category;

Result<Surface> Surface::create(VkInstance instance, platform::Window& window) {
  if (instance == VK_NULL_HANDLE || !window.isOpen()) {
    return fail(Errc::InvalidArgument, "a surface needs a live Vulkan instance and an open window");
  }

  // GLFW picks the right platform call (vkCreateXcbSurfaceKHR,
  // vkCreateWin32SurfaceKHR, ...) for us.
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (Status s = check(glfwCreateWindowSurface(instance, window.nativeHandle(), nullptr, &surface),
                       "glfwCreateWindowSurface");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }

  const platform::Extent2D size = window.framebufferSize();
  log::info(Category::Gpu, "window surface created ({}x{} pixels)", size.width, size.height);
  return Surface(instance, surface);
}

Surface::Surface(Surface&& other) noexcept
    : instance_(std::exchange(other.instance_, VK_NULL_HANDLE)),
      surface_(std::exchange(other.surface_, VK_NULL_HANDLE)) {}

Surface& Surface::operator=(Surface&& other) noexcept {
  if (this != &other) {
    destroy();
    instance_ = std::exchange(other.instance_, VK_NULL_HANDLE);
    surface_ = std::exchange(other.surface_, VK_NULL_HANDLE);
  }
  return *this;
}

Surface::~Surface() { destroy(); }

void Surface::destroy() noexcept {
  if (surface_ == VK_NULL_HANDLE) return;
  vkDestroySurfaceKHR(instance_, surface_, nullptr);
  surface_ = VK_NULL_HANDLE;
  log::info(Category::Gpu, "window surface destroyed");
}

}  // namespace playground::gpu
