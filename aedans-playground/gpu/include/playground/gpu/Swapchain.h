#pragma once
// =============================================================================
//  playground/gpu/Swapchain.h — the images the window shows.
//
//  WHAT IT IS
//    A small ring of images (usually 2 or 3) owned jointly by us and the
//    window system. Each frame we borrow one ("acquire"), draw into it, and
//    hand it back to be shown ("present"). While one image is on screen, we
//    draw the next, so the picture never tears or flickers.
//
//  WHAT create() CHOOSES, AND LOGS
//    format        B8G8R8A8_SRGB (or R8G8B8A8_SRGB): the GPU converts our
//                  linear colours to sRGB on write, so shaders work in
//                  linear light and the result looks right on a monitor.
//    present mode  FIFO (vsync) by default: always supported, never tears,
//                  and it sets the frame rate to the display's. With vsync
//                  off: MAILBOX if available, else IMMEDIATE.
//    extent        the window's framebuffer size in pixels, clamped to what
//                  the surface allows.
//    image count   one more than the minimum, so we never wait on the
//                  display to release an image, within the surface's maximum.
//
//  RECREATION
//    A swapchain is tied to the window's size. When the window is resized,
//    or present says the swapchain is out of date, a new one is created,
//    passing the old one as `oldSwapchain` so the driver can hand over
//    smoothly, and then the old one is destroyed.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/gpu/Context.h"
#include "playground/gpu/DeviceHandle.h"
#include "playground/platform/Window.h"

#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace playground::gpu {

struct SwapchainDesc {
  platform::Extent2D framebufferSize{};
  bool vsync = true;
  VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE;  // when recreating
  // _SRGB formats encode on write; _UNORM ones store what shaders write (the
  // renderer then encodes in its shaders). sRGB is preferred; false prefers
  // UNORM, so that path can be tested where both exist.
  bool preferSrgb = true;
};

class Swapchain {
 public:
  // Errc::InvalidArgument for an empty size (a minimised window);
  // Errc::GpuFailure if a Vulkan call fails.
  [[nodiscard]] static Result<Swapchain> create(const Context& context, const SwapchainDesc& desc);

  Swapchain(Swapchain&& other) noexcept;
  Swapchain& operator=(Swapchain&& other) noexcept;
  Swapchain(const Swapchain&) = delete;
  Swapchain& operator=(const Swapchain&) = delete;
  ~Swapchain();

  [[nodiscard]] VkSwapchainKHR handle() const noexcept { return swapchain_; }
  [[nodiscard]] VkFormat format() const noexcept { return format_; }
  [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
  [[nodiscard]] VkPresentModeKHR presentMode() const noexcept { return presentMode_; }
  [[nodiscard]] u32 imageCount() const noexcept { return static_cast<u32>(images_.size()); }
  [[nodiscard]] VkImage image(u32 index) const noexcept { return images_[index]; }
  [[nodiscard]] VkImageView imageView(u32 index) const noexcept { return views_[index].get(); }
  // Whether its images can be copied from (frame capture).
  [[nodiscard]] bool canCopyFrom() const noexcept { return canCopyFrom_; }

 private:
  Swapchain() = default;
  void destroy() noexcept;

  VkDevice device_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat format_ = VK_FORMAT_UNDEFINED;
  VkExtent2D extent_{};
  VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
  bool canCopyFrom_ = false;
  std::vector<VkImage> images_;    // owned by the swapchain itself
  std::vector<ImageView> views_;   // owned by us
};

[[nodiscard]] std::string describe(VkFormat format);
[[nodiscard]] std::string describe(VkPresentModeKHR mode);

}  // namespace playground::gpu
