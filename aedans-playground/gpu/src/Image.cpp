#include "playground/gpu/Image.h"

#include "playground/core/Log.h"
#include "playground/gpu/Buffer.h"
#include "playground/gpu/Context.h"
#include "playground/gpu/Swapchain.h"  // describe(VkFormat)
#include "playground/gpu/VulkanCheck.h"

#include <algorithm>
#include <format>
#include <string>
#include <utility>

namespace playground::gpu {

Result<Image> Image::create(const Allocator& allocator, const ImageDesc& desc) {
  if (desc.width == 0 || desc.height == 0 || desc.format == VK_FORMAT_UNDEFINED || desc.usage == 0) {
    return fail(Errc::InvalidArgument, "image \"{}\" needs a size, a format and a usage (got {}x{})", desc.debugName,
                desc.width, desc.height);
  }
  if (desc.cube && (desc.layers != 6 || desc.width != desc.height)) {
    return fail(Errc::InvalidArgument, "cube image \"{}\" needs 6 square layers (got {} of {}x{})", desc.debugName,
                desc.layers, desc.width, desc.height);
  }
  if (desc.layers == 0) return fail(Errc::InvalidArgument, "image \"{}\" has no layers", desc.debugName);
  if (desc.mipLevels == 0 || desc.mipLevels > fullMipCount(desc.width, desc.height)) {
    return fail(Errc::InvalidArgument, "image \"{}\" ({}x{}) cannot have {} mip levels (at most {})", desc.debugName,
                desc.width, desc.height, desc.mipLevels, fullMipCount(desc.width, desc.height));
  }

  const VkImageCreateInfo imageInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .flags = desc.cube ? VkImageCreateFlags{VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT} : VkImageCreateFlags{0},
      .imageType = VK_IMAGE_TYPE_2D,
      .format = desc.format,
      .extent = {.width = desc.width, .height = desc.height, .depth = 1},
      .mipLevels = desc.mipLevels,
      .arrayLayers = desc.layers,
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
  image.mipLevels_ = desc.mipLevels;
  image.layers_ = desc.layers;
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
      .viewType = desc.cube ? VK_IMAGE_VIEW_TYPE_CUBE
                            : (desc.layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D),
      .format = desc.format,
      .subresourceRange = {.aspectMask = desc.aspect, .levelCount = desc.mipLevels, .layerCount = desc.layers},
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
      extent_(std::exchange(other.extent_, VkExtent2D{})),
      mipLevels_(std::exchange(other.mipLevels_, 1u)),
      layers_(std::exchange(other.layers_, 1u)) {}

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
    mipLevels_ = std::exchange(other.mipLevels_, 1u);
    layers_ = std::exchange(other.layers_, 1u);
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

// --- Barriers ------------------------------------------------------------------

void transitionImage(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                     VkAccessFlags2 dstAccess, const VkImageSubresourceRange& range) {
  const VkImageMemoryBarrier2 barrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = srcStage,
      .srcAccessMask = srcAccess,
      .dstStageMask = dstStage,
      .dstAccessMask = dstAccess,
      .oldLayout = from,
      .newLayout = to,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = range,
  };
  const VkDependencyInfo dependency{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(commands, &dependency);
}

void transitionImage(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                     VkAccessFlags2 dstAccess, VkImageAspectFlags aspect) {
  transitionImage(commands, image, from, to, srcStage, srcAccess, dstStage, dstAccess,
                  VkImageSubresourceRange{.aspectMask = aspect,
                                          .baseMipLevel = 0,
                                          .levelCount = VK_REMAINING_MIP_LEVELS,
                                          .baseArrayLayer = 0,
                                          .layerCount = VK_REMAINING_ARRAY_LAYERS});
}

Result<ImageView> createImageView(VkDevice device, const Image& image, VkImageViewType type, u32 baseMip,
                                  u32 mipCount, u32 baseLayer, u32 layerCount) {
  const VkImageViewCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image.handle(),
      .viewType = type,
      .format = image.format(),
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = baseMip, .levelCount = mipCount,
                           .baseArrayLayer = baseLayer, .layerCount = layerCount},
  };
  VkImageView view = VK_NULL_HANDLE;
  if (Status s = check(vkCreateImageView(device, &info, nullptr, &view), "vkCreateImageView"); !s) {
    return tl::unexpected(std::move(s.error()));
  }
  return ImageView(device, view);
}

u32 texelBytes(VkFormat format) noexcept {
  switch (format) {
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R16G16_SFLOAT: return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
    default: return 0;
  }
}

// --- Textures --------------------------------------------------------------------

namespace {

VkImageSubresourceRange levels(u32 base, u32 count) noexcept {
  return {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = base, .levelCount = count, .baseArrayLayer = 0,
          .layerCount = 1};
}

i32 mipSize(u32 size, u32 level) noexcept { return static_cast<i32>(std::max(1u, size >> level)); }

}  // namespace

Result<Image> createTexture(const Context& context, const TextureDesc& desc) {
  const u32 bytesPerTexel = texelBytes(desc.format);
  if (bytesPerTexel == 0) {
    return fail(Errc::InvalidArgument, "texture \"{}\": format {} is not one createTexture accepts", desc.debugName,
                describe(desc.format));
  }
  const u64 expected = u64{desc.width} * desc.height * bytesPerTexel;
  if (desc.width == 0 || desc.height == 0 || desc.pixels.size() != expected) {
    return fail(Errc::InvalidArgument, "texture \"{}\": {}x{} {} needs {} bytes, got {}", desc.debugName, desc.width,
                desc.height, describe(desc.format), expected, desc.pixels.size());
  }

  // Mips are made by linear blits, which the format must support in optimal
  // tiling (sampled + blit source + blit destination + linear filtering).
  u32 mips = desc.generateMips ? fullMipCount(desc.width, desc.height) : 1;
  if (mips > 1) {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context.physicalDevice().handle, desc.format, &properties);
    constexpr VkFormatFeatureFlags kNeeded = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((properties.optimalTilingFeatures & kNeeded) != kNeeded) {
      log::warn(log::Category::Gpu, "texture \"{}\": this GPU cannot blit-filter {}; uploading without mipmaps",
                desc.debugName, describe(desc.format));
      mips = 1;
    }
  }

  // 1. The pixels, in a host-visible staging buffer.
  const std::string stagingName = std::string(desc.debugName) + " (staging)";
  auto staging = Buffer::create(context.allocator(), {.sizeBytes = expected,
                                                      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                      .memory = MemoryKind::HostVisible,
                                                      .debugName = stagingName});
  if (!staging) return tl::unexpected(std::move(staging.error()));
  if (Status s = staging->write(desc.pixels); !s) return tl::unexpected(std::move(s.error()));

  // 2. The image: written by copy and blit, read by blit (for the next
  // level) and by shaders.
  VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (mips > 1) usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  auto image = Image::create(context.allocator(), {.width = desc.width,
                                                   .height = desc.height,
                                                   .format = desc.format,
                                                   .usage = usage,
                                                   .mipLevels = mips,
                                                   .debugName = desc.debugName});
  if (!image) return tl::unexpected(std::move(image.error()));
  const VkImage handle = image->handle();

  // 3. Copy, build the mip chain, hand it to the shaders: one submission.
  Status uploaded = submitAndWait(context, [&](VkCommandBuffer commands) {
    transitionImage(commands, handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT, levels(0, mips));
    const VkBufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,  // tightly packed
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0,
                             .layerCount = 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {desc.width, desc.height, 1},
    };
    vkCmdCopyBufferToImage(commands, staging->handle(), handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    for (u32 level = 1; level < mips; ++level) {
      // The level above is finished (copied or blitted): make it the source.
      transitionImage(commands, handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, levels(level - 1, 1));
      const VkImageBlit blit{
          .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level - 1, .baseArrayLayer = 0,
                             .layerCount = 1},
          .srcOffsets = {{0, 0, 0}, {mipSize(desc.width, level - 1), mipSize(desc.height, level - 1), 1}},
          .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level, .baseArrayLayer = 0,
                             .layerCount = 1},
          .dstOffsets = {{0, 0, 0}, {mipSize(desc.width, level), mipSize(desc.height, level), 1}},
      };
      vkCmdBlitImage(commands, handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, handle,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    }

    // Every level but the last was a blit source; the last was only written.
    if (mips > 1) {
      transitionImage(commands, handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                      VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, levels(0, mips - 1));
    }
    transitionImage(commands, handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    levels(mips - 1, 1));
  });
  if (!uploaded) {
    uploaded.error().message += std::format(" (uploading texture \"{}\")", desc.debugName);
    return tl::unexpected(std::move(uploaded.error()));
  }
  log::debug(log::Category::Gpu, "texture \"{}\": {}x{} {}, {} mip level(s)", desc.debugName, desc.width,
             desc.height, describe(desc.format), mips);
  return std::move(*image);  // the staging buffer is freed on return; the GPU has finished with it
}

}  // namespace playground::gpu
