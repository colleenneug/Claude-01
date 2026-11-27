#include "playground/gpu/Swapchain.h"

#include "playground/core/Log.h"
#include "playground/gpu/VulkanCheck.h"

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <utility>

namespace playground::gpu {
namespace {

using log::Category;

VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>& available) {
  constexpr std::array<VkFormat, 2> kPreferred{VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB};
  for (VkFormat wanted : kPreferred) {
    for (const VkSurfaceFormatKHR& f : available) {
      if (f.format == wanted && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return f;
    }
  }
  return available.front();  // callers checked it is not empty
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& available, bool vsync) {
  // FIFO is the only mode every driver must support.
  if (vsync) return VK_PRESENT_MODE_FIFO_KHR;
  for (VkPresentModeKHR wanted : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
    if (std::ranges::find(available, wanted) != available.end()) return wanted;
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps, platform::Extent2D framebuffer) {
  // Most window systems dictate the size: currentExtent is the window's.
  // A width of UINT32_MAX means "you choose", and then we use the
  // framebuffer size, clamped to the allowed range.
  if (caps.currentExtent.width != std::numeric_limits<u32>::max()) return caps.currentExtent;
  return VkExtent2D{
      .width = std::clamp(framebuffer.width, caps.minImageExtent.width, caps.maxImageExtent.width),
      .height = std::clamp(framebuffer.height, caps.minImageExtent.height, caps.maxImageExtent.height),
  };
}

VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported) {
  for (VkCompositeAlphaFlagBitsKHR wanted :
       {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
    if (supported & wanted) return wanted;
  }
  return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

}  // namespace

std::string describe(VkFormat format) {
  switch (format) {
    case VK_FORMAT_B8G8R8A8_SRGB:  return "B8G8R8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_SRGB:  return "R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    default:                       return std::format("VkFormat({})", static_cast<int>(format));
  }
}

std::string describe(VkPresentModeKHR mode) {
  switch (mode) {
    case VK_PRESENT_MODE_FIFO_KHR:         return "FIFO (vsync)";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO relaxed";
    case VK_PRESENT_MODE_MAILBOX_KHR:      return "MAILBOX";
    case VK_PRESENT_MODE_IMMEDIATE_KHR:    return "IMMEDIATE (may tear)";
    default:                               return std::format("VkPresentModeKHR({})", static_cast<int>(mode));
  }
}

Result<Swapchain> Swapchain::create(const Context& context, const SwapchainDesc& desc) {
  if (desc.framebufferSize.isEmpty()) {
    return fail(Errc::InvalidArgument, "cannot create a swapchain for an empty {}x{} framebuffer (minimised window?)",
                desc.framebufferSize.width, desc.framebufferSize.height);
  }

  const VkPhysicalDevice gpu = context.physicalDevice().handle;
  const VkSurfaceKHR surface = context.surface();
  const VkDevice device = context.device().handle();

  // --- What the surface supports -----------------------------------------
  VkSurfaceCapabilitiesKHR caps{};
  if (Status s = check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps),
                       "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }

  u32 formatCount = 0;
  if (Status s = check(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, nullptr),
                       "vkGetPhysicalDeviceSurfaceFormatsKHR");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  if (Status s = check(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, formats.data()),
                       "vkGetPhysicalDeviceSurfaceFormatsKHR");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }

  u32 modeCount = 0;
  if (Status s = check(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, nullptr),
                       "vkGetPhysicalDeviceSurfacePresentModesKHR");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  std::vector<VkPresentModeKHR> modes(modeCount);
  if (Status s = check(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, modes.data()),
                       "vkGetPhysicalDeviceSurfacePresentModesKHR");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  if (formats.empty() || modes.empty()) {
    return fail(Errc::Unsupported, "the window surface offers no formats or present modes");
  }

  // --- Choices -------------------------------------------------------------
  const VkSurfaceFormatKHR format = chooseFormat(formats);
  const VkPresentModeKHR presentMode = choosePresentMode(modes, desc.vsync);
  const VkExtent2D extent = chooseExtent(caps, desc.framebufferSize);
  if (extent.width == 0 || extent.height == 0) {
    return fail(Errc::InvalidArgument, "the surface reports a {}x{} size (minimised window?)", extent.width,
                extent.height);
  }

  u32 imageCount = caps.minImageCount + 1;
  if (caps.maxImageCount != 0) imageCount = std::min(imageCount, caps.maxImageCount);  // 0 means no maximum

  // Two queue families means the images are shared between them. EXCLUSIVE
  // would need an explicit ownership transfer every frame; CONCURRENT costs
  // a little speed on some GPUs and needs none.
  const QueueFamilies& families = context.device().queueFamilies();
  const std::array<u32, 2> familyIndices{families.graphics, families.present};
  const bool shared = families.shared();

  const VkSwapchainCreateInfoKHR createInfo{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = surface,
      .minImageCount = imageCount,
      .imageFormat = format.format,
      .imageColorSpace = format.colorSpace,
      .imageExtent = extent,
      .imageArrayLayers = 1,
      // We render straight into it; TRANSFER_SRC also lets a frame be copied
      // back to the CPU (screenshots, pixel tests) where the surface allows.
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
      .imageSharingMode = shared ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
      .queueFamilyIndexCount = shared ? 0u : 2u,
      .pQueueFamilyIndices = shared ? nullptr : familyIndices.data(),
      .preTransform = caps.currentTransform,
      .compositeAlpha = chooseCompositeAlpha(caps.supportedCompositeAlpha),
      .presentMode = presentMode,
      .clipped = VK_TRUE,  // pixels hidden behind other windows need not be drawn
      .oldSwapchain = desc.oldSwapchain,
  };

  Swapchain swapchain;
  swapchain.device_ = device;
  if (Status s = check(vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain.swapchain_),
                       "vkCreateSwapchainKHR");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  swapchain.format_ = format.format;
  swapchain.extent_ = extent;
  swapchain.presentMode_ = presentMode;
  swapchain.canCopyFrom_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;

  // --- The images, and a view of each --------------------------------------
  // The driver may create more images than we asked for.
  u32 actualCount = 0;
  if (Status s = check(vkGetSwapchainImagesKHR(device, swapchain.swapchain_, &actualCount, nullptr),
                       "vkGetSwapchainImagesKHR");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  swapchain.images_.resize(actualCount);
  if (Status s = check(vkGetSwapchainImagesKHR(device, swapchain.swapchain_, &actualCount, swapchain.images_.data()),
                       "vkGetSwapchainImagesKHR");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }

  swapchain.views_.reserve(actualCount);
  for (VkImage image : swapchain.images_) {
    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format.format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
    };
    VkImageView view = VK_NULL_HANDLE;
    if (Status s = check(vkCreateImageView(device, &viewInfo, nullptr, &view), "vkCreateImageView"); !s) {
      return tl::unexpected(std::move(s.error()));  // views made so far are destroyed with `swapchain`
    }
    swapchain.views_.emplace_back(device, view);
  }

  log::info(Category::Gpu, "swapchain {}: {}x{}, {} images, {}, {}", desc.oldSwapchain ? "recreated" : "created",
            extent.width, extent.height, actualCount, describe(format.format), describe(presentMode));
  if (format.format != VK_FORMAT_B8G8R8A8_SRGB && format.format != VK_FORMAT_R8G8B8A8_SRGB) {
    log::warn(Category::Gpu, "no sRGB swapchain format available; colours will look darker than intended");
  }
  return swapchain;
}

Swapchain::Swapchain(Swapchain&& other) noexcept
    : device_(other.device_),
      swapchain_(std::exchange(other.swapchain_, VK_NULL_HANDLE)),
      format_(other.format_),
      extent_(other.extent_),
      presentMode_(other.presentMode_),
      canCopyFrom_(other.canCopyFrom_),
      images_(std::move(other.images_)),
      views_(std::move(other.views_)) {}

Swapchain& Swapchain::operator=(Swapchain&& other) noexcept {
  if (this != &other) {
    destroy();
    device_ = other.device_;
    swapchain_ = std::exchange(other.swapchain_, VK_NULL_HANDLE);
    format_ = other.format_;
    extent_ = other.extent_;
    presentMode_ = other.presentMode_;
    canCopyFrom_ = other.canCopyFrom_;
    images_ = std::move(other.images_);
    views_ = std::move(other.views_);
  }
  return *this;
}

Swapchain::~Swapchain() { destroy(); }

void Swapchain::destroy() noexcept {
  views_.clear();  // views before the images they look at
  images_.clear();  // owned by the swapchain: freed with it, not by us
  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

}  // namespace playground::gpu
