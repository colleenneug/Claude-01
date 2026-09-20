// =============================================================================
//  VulkanDevice.cpp — the Vulkan backend. SCAFFOLD.
//
//  HONEST STATUS: this file is the checklist and the shape, not a working
//  device. A Vulkan backend that actually presents a triangle is roughly
//  2,500 lines before it does anything useful, and writing it as untested
//  boilerplate would produce something that looks finished and does not run —
//  which is worse than an explicit scaffold.
//
//  What follows is the order of operations, the decisions worth making
//  deliberately, and the traps, so that filling it in is mechanical.
//
//  ORDER OF BRING-UP
//   1. Instance. Enable VK_LAYER_KHRONOS_validation in debug and wire
//      VK_EXT_debug_utils. Do this FIRST — every hour spent without validation
//      is repaid with interest.
//   2. Surface (platform-specific; GLFW's glfwCreateWindowSurface is fine).
//   3. Physical device selection. Require: Vulkan 1.3, dynamic rendering,
//      synchronization2, timeline semaphores, descriptor indexing with
//      runtimeDescriptorArray + partiallyBound (that is the bindless path),
//      and bufferDeviceAddress. Prefer discrete.
//   4. Logical device + queues. One graphics queue is enough to start; add an
//      async compute queue only once you have a profile showing a gap to fill,
//      and a dedicated transfer queue for streaming.
//   5. VMA (VulkanMemoryAllocator). Do not hand-roll allocation. Sub-allocation,
//      budget tracking and defragmentation are solved problems and getting them
//      wrong shows up as an out-of-memory two hours into a session.
//   6. Swapchain. Handle VK_ERROR_OUT_OF_DATE_KHR and recreate; a resize that
//      is not handled is an instant device-lost.
//   7. Per-frame resources, framesInFlight deep: command pool, upload ring,
//      timeline semaphore value, query pool for GPU timings.
//
//  DECISIONS WORTH MAKING ONCE
//   - DYNAMIC RENDERING (VK_KHR_dynamic_rendering, core in 1.3) instead of
//     VkRenderPass objects. Removes the entire render pass / framebuffer
//     object graph, which is the single biggest source of Vulkan boilerplate
//     and gains nothing on desktop.
//   - TIMELINE SEMAPHORES instead of fences + binary semaphores. One monotonic
//     counter per queue replaces the whole fence pool.
//   - BINDLESS. One VkDescriptorSet holding a large array of sampled images,
//     partially bound, updated after bind. Materials become a u32 index in a
//     push constant. This is the difference between thousands of descriptor
//     set binds per frame and none.
//   - PIPELINE CACHE, serialised to disk. First-run shader compilation is
//     seconds of hitching; the cache turns it into milliseconds.
//
//  TRAPS
//   - Vulkan's NDC has Y down and depth 0..1, unlike OpenGL. Either flip the
//     viewport height (negative height, supported since 1.1) or bake it into
//     the projection. Pick one and write it down, or half your meshes are
//     upside down.
//   - vkCmdPipelineBarrier2 stage masks must be as narrow as you can make
//     them. ALL_COMMANDS everywhere "works" and serialises the GPU.
//   - Host-visible memory on discrete GPUs is uncached write-combined: write
//     it linearly, never read it back.
// =============================================================================

#include "erebus/render/RHI.h"

#include <cstdio>

namespace erebus::render {

std::unique_ptr<IRenderDevice> createVulkanDevice(void* /*nativeWindowHandle*/) {
  std::fprintf(stderr,
               "[RHI] Vulkan backend is a scaffold; see the checklist at the top of "
               "src/render/vulkan/VulkanDevice.cpp\n");
  return nullptr;
}

std::unique_ptr<IRenderDevice> createD3D12Device(void* /*nativeWindowHandle*/) {
  std::fprintf(stderr,
               "[RHI] D3D12 backend not implemented. The bring-up order mirrors the "
               "Vulkan checklist: device, command queue, swapchain, descriptor heaps "
               "(one CBV/SRV/UAV heap, shader-visible, indexed as the bindless table), "
               "root signature with a root constant for the material index, and "
               "ID3D12GraphicsCommandList7::Barrier for enhanced barriers.\n");
  return nullptr;
}

}  // namespace erebus::render
