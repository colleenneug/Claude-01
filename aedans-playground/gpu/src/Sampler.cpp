#include "playground/gpu/Sampler.h"

#include "playground/core/Log.h"
#include "playground/gpu/Context.h"
#include "playground/gpu/VulkanCheck.h"

#include <algorithm>
#include <format>
#include <utility>

namespace playground::gpu {

Result<Sampler> createSampler(const Context& context, const SamplerDesc& desc) {
  const PhysicalDeviceInfo& gpu = context.physicalDevice();
  const bool anisotropy = desc.maxAnisotropy > 1.0f && gpu.samplerAnisotropy;
  const f32 maxAnisotropy = anisotropy ? std::min(desc.maxAnisotropy, gpu.maxSamplerAnisotropy) : 1.0f;
  if (desc.maxAnisotropy > 1.0f && !gpu.samplerAnisotropy) {
    log::info(log::Category::Gpu, "sampler \"{}\": this GPU has no anisotropic filtering; using trilinear only",
              desc.debugName);
  }

  const VkSamplerCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = desc.filter,
      .minFilter = desc.filter,
      .mipmapMode = desc.mipmapMode,
      .addressModeU = desc.addressMode,
      .addressModeV = desc.addressMode,
      .addressModeW = desc.addressMode,
      .mipLodBias = 0.0f,
      .anisotropyEnable = anisotropy ? VK_TRUE : VK_FALSE,
      .maxAnisotropy = maxAnisotropy,
      .compareEnable = VK_FALSE,
      .minLod = 0.0f,
      .maxLod = VK_LOD_CLAMP_NONE,  // every level the image has
      .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
      .unnormalizedCoordinates = VK_FALSE,
  };
  const VkDevice device = context.device().handle();
  VkSampler sampler = VK_NULL_HANDLE;
  if (Status s = check(vkCreateSampler(device, &info, nullptr, &sampler), "vkCreateSampler"); !s) {
    s.error().message += std::format(" (sampler \"{}\")", desc.debugName);
    return tl::unexpected(std::move(s.error()));
  }
  log::debug(log::Category::Gpu, "sampler \"{}\": anisotropy {}", desc.debugName, maxAnisotropy);
  return Sampler(device, sampler);
}

}  // namespace playground::gpu
