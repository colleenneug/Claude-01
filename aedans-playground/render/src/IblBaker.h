#pragma once
// =============================================================================
//  render/src/IblBaker.h — builds the image-based-lighting maps on the GPU
//  (internal to the render layer). See render/Environment.h for what the maps
//  are and render/shaders/bake_*.comp for how each is made.
//
//  ONE BAKE, ONE SUBMISSION, in order:
//    1. ENVIRONMENT cube, mip 0: a compute shader writes every texel of the
//       six faces, from the procedural sky or the HDR panorama.
//       Its other mips: linear blits, each half the size of the one before
//       (the irradiance and specular passes read blurrier mips for samples
//       that stand for larger areas).
//    2. IRRADIANCE cube: a compute shader averages the environment over
//       each hemisphere (cosine-weighted).
//    3. SPECULAR cube, one dispatch per mip: roughness 0 at mip 0, 1 at the
//       last; GGX-weighted averages.
//  Between steps, barriers make each image's writes visible to the next
//  reader and move it to the layout that reader needs:
//    compute writes: GENERAL; blits: TRANSFER_SRC/DST; sampling: SHADER_READ_ONLY.
//
//  Then submitAndWait: a loading-time cost (milliseconds for the default
//  procedural sky on a real GPU; under a second on Mesa's CPU driver).
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/gpu/Context.h"
#include "playground/gpu/DeviceHandle.h"
#include "playground/gpu/Image.h"
#include "playground/gpu/Sampler.h"
#include "playground/render/Environment.h"

namespace playground::render {

struct EnvironmentMaps {
  gpu::Image environment;  // cube, with mips: the sky
  gpu::Image irradiance;   // cube: matte lighting per normal direction
  gpu::Image specular;     // cube, mips = roughness 0 .. 1
  [[nodiscard]] u32 specularMips() const noexcept { return specular.mipLevels(); }
};

class IblBaker {
 public:
  // Compiles the four compute pipelines. Errc::GpuFailure if Vulkan refuses,
  // Errc::Unsupported if the GPU cannot store or blit half-float cubes.
  [[nodiscard]] static Result<IblBaker> create(const gpu::Context& context);

  [[nodiscard]] Result<EnvironmentMaps> bake(const ProceduralSky& sky, const EnvironmentQuality& quality);
  [[nodiscard]] Result<EnvironmentMaps> bake(const HdrImage& panorama, const EnvironmentQuality& quality);

  // Clamped trilinear: for cube maps and lookup tables (no wrapping, no anisotropy).
  [[nodiscard]] VkSampler sampler() const noexcept { return sampler_.get(); }

 private:
  struct Source;  // which shader fills the environment cube, and from what
  [[nodiscard]] Result<EnvironmentMaps> run(const Source& source, const EnvironmentQuality& quality);

  explicit IblBaker(const gpu::Context& context) noexcept : context_(&context) {}

  const gpu::Context* context_;
  gpu::Sampler sampler_;
  gpu::DescriptorSetLayout setLayout_;
  gpu::PipelineLayout layout_;
  gpu::Pipeline skyPipeline_;
  gpu::Pipeline panoramaPipeline_;
  gpu::Pipeline irradiancePipeline_;
  gpu::Pipeline specularPipeline_;
};

// Checks sizes and counts (powers of two, sensible ranges) with a message.
[[nodiscard]] Status validate(const EnvironmentQuality& quality);

}  // namespace playground::render
