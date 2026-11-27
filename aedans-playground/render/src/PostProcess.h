#pragma once
// =============================================================================
//  render/src/PostProcess.h — the HDR scene target, bloom and the final
//  composite (internal to the render layer).
//
//  The 3D scene is not drawn straight onto the screen. It goes into an
//  OFFSCREEN HDR image (16-bit floats per channel): light brighter than 1
//  (the sky, a lamp, sunlight on metal) keeps its real value instead of
//  being clipped. Then:
//
//    bloom      (optional) a chain of ever smaller copies of the bright
//               parts, blurred and added back up (render/shaders/bloom.frag);
//    composite  the HDR image + bloom -> exposure -> tone mapping -> the
//               swapchain image, encoding sRGB exactly once
//               (render/shaders/tonemap.frag).
//
//  SIZES follow the swapchain: resize() rebuilds the HDR image, the bloom
//  chain (half size, up to kMaxBloomLevels levels), their views and the
//  descriptor sets that point at them. The old ones are destroyed as they
//  are replaced (the caller has waited for the GPU), so resizing leaks
//  nothing.
//
//  ONE of each image, shared by both frames in flight, like the depth
//  buffer: the barriers at the start of each use order the frames.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/gpu/Context.h"
#include "playground/gpu/DeviceHandle.h"
#include "playground/gpu/Image.h"
#include "playground/gpu/Sampler.h"
#include "playground/render/FrameData.h"

#include <vector>

namespace playground::render {

inline constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
inline constexpr u32 kMaxBloomLevels = 6;

class PostProcess {
 public:
  // Pipeline layout, descriptor layout, sampler and the bloom pipelines.
  [[nodiscard]] static Result<PostProcess> create(const gpu::Context& context);

  // (Re)builds the images and descriptor sets for a swapchain of `extent`.
  [[nodiscard]] Status resize(VkExtent2D extent);
  // (Re)builds the composite pipeline for this swapchain format (whether it
  // encodes sRGB itself depends on it).
  [[nodiscard]] Status buildComposite(VkFormat swapchainFormat);

  // The bloom sweeps; the HDR image must be in SHADER_READ_ONLY. Outside
  // any rendering. Leaves every bloom level in SHADER_READ_ONLY.
  void recordBloom(VkCommandBuffer commands, const BloomSettings& bloom) const;
  // Inside the swapchain's rendering: one full-screen triangle.
  void recordComposite(VkCommandBuffer commands, f32 exposure, ToneMap toneMap, const BloomSettings& bloom) const;

  [[nodiscard]] const gpu::Image& hdr() const noexcept { return hdr_; }
  [[nodiscard]] const gpu::Image& bloom() const noexcept { return bloom_; }
  [[nodiscard]] u32 bloomLevels() const noexcept { return bloom_ ? bloom_.mipLevels() : 0; }

 private:
  explicit PostProcess(const gpu::Context& context) noexcept : context_(&context) {}
  [[nodiscard]] Result<VkDescriptorSet> makeSet(VkImageView source, VkImageView bloom);

  const gpu::Context* context_;
  gpu::Sampler sampler_;  // linear, clamp to edge
  gpu::DescriptorSetLayout setLayout_;
  gpu::PipelineLayout layout_;
  gpu::Pipeline downsample_;  // bloom.frag, writes a level
  gpu::Pipeline upsample_;    // bloom.frag, adds onto a level (blend ONE + ONE)
  gpu::Pipeline composite_;   // tonemap.frag, for the swapchain's format
  VkFormat compositeFormat_ = VK_FORMAT_UNDEFINED;

  // Rebuilt by resize(), in this order of destruction (reverse of declaration).
  gpu::Image hdr_;
  gpu::Image bloom_;
  std::vector<gpu::ImageView> bloomViews_;  // one per level: drawn into, and read from
  gpu::DescriptorPool pool_;
  std::vector<VkDescriptorSet> downSets_;   // [i] reads level i - 1 (or the HDR image for 0)
  std::vector<VkDescriptorSet> upSets_;     // [i] reads level i + 1
  VkDescriptorSet compositeSet_ = VK_NULL_HANDLE;
};

}  // namespace playground::render
