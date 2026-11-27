#pragma once
// =============================================================================
//  playground/gpu/Image.h — a GPU image and its view, as an RAII object.
//
//  The image counterpart of gpu::Buffer: memory from the Allocator (VMA),
//  plus the VkImageView that rendering and shaders use to look at it. First
//  use: the depth buffer. Textures will be the next.
//
//  Destroy every Image before the Allocator (gpu::Context) it came from.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/gpu/Allocator.h"

#include <string_view>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace playground::gpu {

// The depth buffer format. 32-bit float depth is what reverse-Z needs to
// pay off (see engine/Camera.h). GPUs without it are rejected at selection.
inline constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

struct ImageDesc {
  u32 width = 0;
  u32 height = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  std::string_view debugName;
};

class Image {
 public:
  // Errc::InvalidArgument for a zero size, no format or no usage;
  // Errc::GpuFailure if the allocation or view fails.
  [[nodiscard]] static Result<Image> create(const Allocator& allocator, const ImageDesc& desc);

  Image() = default;  // empty: owns nothing
  Image(Image&& other) noexcept;
  Image& operator=(Image&& other) noexcept;
  Image(const Image&) = delete;
  Image& operator=(const Image&) = delete;
  ~Image();

  [[nodiscard]] VkImage handle() const noexcept { return image_; }
  [[nodiscard]] VkImageView view() const noexcept { return view_; }
  [[nodiscard]] VkFormat format() const noexcept { return format_; }
  [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
  [[nodiscard]] explicit operator bool() const noexcept { return image_ != VK_NULL_HANDLE; }

 private:
  void destroy() noexcept;

  VmaAllocator allocator_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkImage image_ = VK_NULL_HANDLE;
  VmaAllocation allocation_ = VK_NULL_HANDLE;
  VkImageView view_ = VK_NULL_HANDLE;
  VkFormat format_ = VK_FORMAT_UNDEFINED;
  VkExtent2D extent_{};
};

}  // namespace playground::gpu
