#include "playground/gpu/Image.h"

#include "playground/gpu/VulkanCheck.h"

#include <format>
#include <string>
#include <utility>

namespace playground::gpu {

Result<Image> Image::create(const Allocator& allocator, const ImageDesc& desc) {
  if (desc.width == 0 || desc.height == 0 || desc.format == VK_FORMAT_UNDEFINED || desc.usage == 0) {
    return fail(Errc::InvalidArgument, "image \"{}\" needs a size, a format and a usage (got {}x{})", desc.debugName,
                desc.width, desc.height);
  }

  const VkImageCreateInfo imageInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = desc.format,
      .extent = {.width = desc.width, .height = desc.height, .depth = 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = desc.usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  // Render targets are big, long-lived and recreated on resize: give each its
  // own allocation rather than a slice of a shared block.
  if (desc.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
  }

  Image image;
  image.allocator_ = allocator.handle();
  image.device_ = allocator.device();
  image.format_ = desc.format;
  image.extent_ = {desc.width, desc.height};
  if (Status s = check(vmaCreateImage(allocator.handle(), &imageInfo, &allocInfo, &image.image_, &image.allocation_,
                                      nullptr),
                       "vmaCreateImage");
      !s) {
    s.error().message += std::format(" (image \"{}\", {}x{})", desc.debugName, desc.width, desc.height);
    return tl::unexpected(std::move(s.error()));
  }
  const std::string name(desc.debugName);
  vmaSetAllocationName(allocator.handle(), image.allocation_, name.c_str());

  const VkImageViewCreateInfo viewInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image.image_,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = desc.format,
      .subresourceRange = {.aspectMask = desc.aspect, .levelCount = 1, .layerCount = 1},
  };
  if (Status s = check(vkCreateImageView(image.device_, &viewInfo, nullptr, &image.view_), "vkCreateImageView"); !s) {
    return tl::unexpected(std::move(s.error()));  // `image` frees the memory on the way out
  }
  return image;
}

Image::Image(Image&& other) noexcept
    : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)),
      device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      image_(std::exchange(other.image_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
      view_(std::exchange(other.view_, VK_NULL_HANDLE)),
      format_(std::exchange(other.format_, VK_FORMAT_UNDEFINED)),
      extent_(std::exchange(other.extent_, VkExtent2D{})) {}

Image& Image::operator=(Image&& other) noexcept {
  if (this != &other) {
    destroy();
    allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    image_ = std::exchange(other.image_, VK_NULL_HANDLE);
    allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
    view_ = std::exchange(other.view_, VK_NULL_HANDLE);
    format_ = std::exchange(other.format_, VK_FORMAT_UNDEFINED);
    extent_ = std::exchange(other.extent_, VkExtent2D{});
  }
  return *this;
}

Image::~Image() { destroy(); }

void Image::destroy() noexcept {
  if (view_ != VK_NULL_HANDLE) {
    vkDestroyImageView(device_, view_, nullptr);  // the view before the image it looks at
    view_ = VK_NULL_HANDLE;
  }
  if (image_ != VK_NULL_HANDLE) {
    vmaDestroyImage(allocator_, image_, allocation_);
    image_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
  }
}

}  // namespace playground::gpu
