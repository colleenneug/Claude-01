#pragma once
// =============================================================================
//  gpu/src/Requirements.h — what the engine needs from a GPU, in one place.
//
//  Device selection checks a GPU against this list; device creation enables
//  exactly this list. Keeping both in one file means they cannot drift apart:
//  a feature that is checked but not enabled is unusable, and one that is
//  enabled but not checked fails vkCreateDevice on some machine we never saw.
//
//  Internal to the gpu layer (src/, not include/).
// =============================================================================

#include <array>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace playground::gpu::detail {

inline constexpr std::array<const char*, 1> kRequiredDeviceExtensions{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,  // presenting images to a window
};

// Vulkan's feature query/enable structs form a linked list through pNext.
// This bundles the three we use and links them on construction. It points
// into itself, so it can be neither copied nor moved.
struct FeatureChain {
  VkPhysicalDeviceFeatures2 core{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  VkPhysicalDeviceVulkan12Features v12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  VkPhysicalDeviceVulkan13Features v13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};

  FeatureChain() noexcept {
    core.pNext = &v12;
    v12.pNext = &v13;
  }
  FeatureChain(const FeatureChain&) = delete;
  FeatureChain& operator=(const FeatureChain&) = delete;
};

// Sets the flags the engine requires (roadmap §4.4).
inline void requestEngineFeatures(FeatureChain& chain) noexcept {
  chain.v13.dynamicRendering = VK_TRUE;   // draw without VkRenderPass/VkFramebuffer objects
  chain.v13.synchronization2 = VK_TRUE;   // the readable barrier API
  chain.v12.timelineSemaphore = VK_TRUE;  // one counter per queue instead of fence pools
  chain.v12.descriptorIndexing = VK_TRUE;  // bindless textures...
  chain.v12.runtimeDescriptorArray = VK_TRUE;
  chain.v12.descriptorBindingPartiallyBound = VK_TRUE;
  chain.v12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  // The bindless texture array gains textures while frames that use it are
  // still on the GPU (slots those frames do not read).
  chain.v12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
  chain.v12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
  chain.v12.bufferDeviceAddress = VK_TRUE;  // GPU pointers to buffers
}

// Names of the required features `supported` lacks; empty if it has all.
inline std::vector<std::string> missingFeatures(const FeatureChain& supported) {
  FeatureChain required;
  requestEngineFeatures(required);

  std::vector<std::string> missing;
  auto need = [&missing](VkBool32 wanted, VkBool32 has, const char* name) {
    if (wanted == VK_TRUE && has != VK_TRUE) missing.emplace_back(name);
  };
  need(required.v13.dynamicRendering, supported.v13.dynamicRendering, "dynamicRendering");
  need(required.v13.synchronization2, supported.v13.synchronization2, "synchronization2");
  need(required.v12.timelineSemaphore, supported.v12.timelineSemaphore, "timelineSemaphore");
  need(required.v12.descriptorIndexing, supported.v12.descriptorIndexing, "descriptorIndexing");
  need(required.v12.runtimeDescriptorArray, supported.v12.runtimeDescriptorArray, "runtimeDescriptorArray");
  need(required.v12.descriptorBindingPartiallyBound, supported.v12.descriptorBindingPartiallyBound,
       "descriptorBindingPartiallyBound");
  need(required.v12.shaderSampledImageArrayNonUniformIndexing,
       supported.v12.shaderSampledImageArrayNonUniformIndexing, "shaderSampledImageArrayNonUniformIndexing");
  need(required.v12.descriptorBindingSampledImageUpdateAfterBind,
       supported.v12.descriptorBindingSampledImageUpdateAfterBind, "descriptorBindingSampledImageUpdateAfterBind");
  need(required.v12.descriptorBindingUpdateUnusedWhilePending, supported.v12.descriptorBindingUpdateUnusedWhilePending,
       "descriptorBindingUpdateUnusedWhilePending");
  need(required.v12.bufferDeviceAddress, supported.v12.bufferDeviceAddress, "bufferDeviceAddress");
  return missing;
}

}  // namespace playground::gpu::detail
