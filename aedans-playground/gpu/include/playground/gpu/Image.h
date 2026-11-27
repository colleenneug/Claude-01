#pragma once
// =============================================================================
//  playground/gpu/Image.h — a GPU image and its view, as an RAII object.
//
//  The image counterpart of gpu::Buffer: memory from the Allocator (VMA),
//  plus the VkImageView that rendering and shaders use to look at it. Used
//  for the depth buffer, textures, and CUBE MAPS: six square images (the
//  faces +x, -x, +y, -y, +z, -z) seen through one view, sampled with a
//  direction instead of a 2D coordinate (the sky, reflections).
//
//  TEXTURES (createTexture)
//    Pixels go CPU -> staging buffer -> image, and the GPU then builds the
//    MIP CHAIN: each level half the size of the one before, down to 1x1, so
//    a texture seen from far away is sampled from a small, pre-filtered copy
//    instead of shimmering. Each level is a linear blit (downscale) of the
//    previous one. On the way, the image's LAYOUT (how the GPU arranges it
//    in memory for a given job) changes, and each change is a barrier:
//
//      UNDEFINED -> TRANSFER_DST        all levels: ready to be written
//      copy the pixels into level 0
//      for each level i >= 1:
//        level i-1: TRANSFER_DST -> TRANSFER_SRC   (written; now read)
//        blit level i-1 -> level i
//      every level -> SHADER_READ_ONLY  ready for the fragment shader
//
//  Destroy every Image before the Allocator (gpu::Context) it came from.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/gpu/Allocator.h"
#include "playground/gpu/DeviceHandle.h"

#include <span>
#include <string_view>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace playground::gpu {

// The depth buffer format. 32-bit float depth is what reverse-Z needs to
// pay off (see engine/Camera.h). GPUs without it are rejected at selection.
inline constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

class Context;

struct ImageDesc {
  u32 width = 0;
  u32 height = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  u32 mipLevels = 1;  // the view covers all of them
  u32 layers = 1;     // array layers; a cube map has 6
  bool cube = false;  // six square layers seen as a cube (view type CUBE)
  std::string_view debugName;
};

// Levels in a full mip chain for this size: 1 + floor(log2(max(w, h))).
// 256x256 has 9 (256, 128, ..., 1); 300x20 has 9 too (the larger side counts).
[[nodiscard]] constexpr u32 fullMipCount(u32 width, u32 height) noexcept {
  u32 largest = width > height ? width : height;
  u32 levels = 1;
  while (largest > 1) {
    largest /= 2;
    ++levels;
  }
  return levels;
}

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
  [[nodiscard]] u32 mipLevels() const noexcept { return mipLevels_; }
  [[nodiscard]] u32 layers() const noexcept { return layers_; }
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
  u32 mipLevels_ = 1;
  u32 layers_ = 1;
};

// Another view of `image`: a range of its mip levels and layers, seen as
// `type`. For example one mip level of a cube map as a 2D array, so a
// compute shader can write its six faces.
[[nodiscard]] Result<ImageView> createImageView(VkDevice device, const Image& image, VkImageViewType type,
                                                u32 baseMip, u32 mipCount, u32 baseLayer, u32 layerCount);

// Bytes per pixel of the formats createTexture accepts; 0 for any other.
[[nodiscard]] u32 texelBytes(VkFormat format) noexcept;

// A synchronization2 layout transition of `range` of `image`. Stage and
// access masks should be as narrow as the transition allows: ALL_COMMANDS
// everywhere "works" and serialises the whole GPU.
void transitionImage(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                     VkAccessFlags2 dstAccess, const VkImageSubresourceRange& range);

// The common case: every level of a colour image (or the aspect given).
void transitionImage(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                     VkAccessFlags2 dstAccess, VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);

struct TextureDesc {
  u32 width = 0;
  u32 height = 0;
  // VK_FORMAT_R8G8B8A8_SRGB for colours (base colour), R8G8B8A8_UNORM for
  // data (normals, metallic-roughness), R16G16_SFLOAT and
  // R16G16B16A16_SFLOAT for half-float data (lookup tables, HDR images).
  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
  std::span<const std::byte> pixels;  // width * height * texelBytes(format), top row first
  bool generateMips = true;
  std::string_view debugName;
};

// A sampled image holding `desc.pixels`, with a full mip chain when asked
// (and when the GPU can blit-filter this format; otherwise one level and a
// warning), left in SHADER_READ_ONLY_OPTIMAL. Waits for the GPU to finish:
// for loading, not per frame. Errc::InvalidArgument for a size, format or
// pixel count that does not add up.
[[nodiscard]] Result<Image> createTexture(const Context& context, const TextureDesc& desc);

}  // namespace playground::gpu
