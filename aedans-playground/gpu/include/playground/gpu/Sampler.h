#pragma once
// =============================================================================
//  playground/gpu/Sampler.h — how a shader reads a texture.
//
//  A sampler is separate from the image: it says how to filter between
//  texels and mip levels and what happens past the edge. One sampler can
//  serve any number of textures; the renderer uses one for all of them.
//
//  ANISOTROPIC FILTERING keeps textures sharp on surfaces seen at a glancing
//  angle (a floor stretching away). It is an optional GPU feature: when the
//  device does not have it, the sampler is created without it, and says so
//  in the log.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/gpu/DeviceHandle.h"

#include <string_view>

#include <vulkan/vulkan.h>

namespace playground::gpu {

class Context;

using Sampler = DeviceHandle<VkSampler, &vkDestroySampler>;

struct SamplerDesc {
  VkFilter filter = VK_FILTER_LINEAR;                            // magnification and minification
  VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;  // blend between levels (trilinear)
  VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;  // tile past the edge, as glTF's default
  f32 maxAnisotropy = 8.0f;  // 1 = off; clamped to what the GPU allows
  std::string_view debugName;
};

// Errc::GpuFailure if Vulkan refuses.
[[nodiscard]] Result<Sampler> createSampler(const Context& context, const SamplerDesc& desc);

}  // namespace playground::gpu
