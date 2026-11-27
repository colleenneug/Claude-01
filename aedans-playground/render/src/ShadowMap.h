#pragma once
// =============================================================================
//  render/src/ShadowMap.h — the sun's shadow map (internal to the render layer).
//
//  A square D32_SFLOAT depth image, rendered from the light each frame that
//  has shadows on (render/Shadow.h for where it looks):
//    1. its layout: DEPTH_STENCIL_READ_ONLY -> DEPTH_ATTACHMENT, after the
//       previous frame's reads;
//    2. cleared to 0 (reverse-Z: the far end), every shadow-casting mesh
//       drawn with shadow.vert (no fragment shader: depth only), with a
//       DEPTH BIAS pushing what is stored away from the light;
//    3. back to DEPTH_STENCIL_READ_ONLY for pbr.frag's sampler2DShadow.
//  Between frames it stays readable, so the shader's descriptor is valid
//  even on frames without shadows (they skip the lookup).
//
//  The sampler COMPARES: texture(shadowMap, vec3(uv, depth)) returns 1 where
//  depth >= the stored depth (nothing nearer the light), 0 where not, and
//  with linear filtering blends the four nearest such answers. Outside the
//  map it reads a border of depth 0: lit.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/gpu/Context.h"
#include "playground/gpu/DeviceHandle.h"
#include "playground/gpu/Image.h"
#include "playground/render/FrameData.h"

#include <functional>

namespace playground::render {

class ShadowMap {
 public:
  [[nodiscard]] static Result<ShadowMap> create(const gpu::Context& context, u32 size);
  // The depth-only pipeline, with the scene's pipeline layout (it reads the
  // frame's uniforms and the mesh draw list, as the main pass does).
  [[nodiscard]] Status buildPipeline(VkPipelineLayout sceneLayout);

  // Outside any rendering. `drawCasters` binds what it needs and draws.
  void record(VkCommandBuffer commands, const ShadowSettings& settings,
              const std::function<void(VkCommandBuffer)>& drawCasters) const;

  [[nodiscard]] u32 size() const noexcept { return size_; }
  [[nodiscard]] VkImageView view() const noexcept { return image_.view(); }
  [[nodiscard]] VkSampler sampler() const noexcept { return sampler_.get(); }
  [[nodiscard]] bool linearFiltering() const noexcept { return linear_; }

 private:
  explicit ShadowMap(const gpu::Context& context) noexcept : context_(&context) {}

  const gpu::Context* context_;
  u32 size_ = 0;
  bool linear_ = true;
  gpu::Image image_;
  gpu::DeviceHandle<VkSampler, &vkDestroySampler> sampler_;
  gpu::Pipeline pipeline_;
};

}  // namespace playground::render
