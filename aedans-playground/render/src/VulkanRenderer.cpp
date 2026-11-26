// =============================================================================
//  VulkanRenderer.cpp — draws the frame's sprites.
//
//  WHAT IT DRAWS: every SpriteInstance in the FrameData the game extracted,
//  in order, as a coloured rectangle. Each sprite is one vkCmdDraw of 6
//  vertices with its data in push constants (sprite.vert). Simple and
//  plenty for hundreds of sprites; thousands want one instanced draw from
//  a buffer, which needs the GPU memory work of roadmap Milestone 2.
//
//  ONE FRAME (renderFrame), in order:
//
//    1. WAIT     for this frame slot's fence: the GPU has finished the last
//                frame that used these command buffers and semaphores.
//    2. ACQUIRE  a swapchain image to draw into. Its `imageAcquired`
//                semaphore is signalled when the display has let go of it.
//    3. RECORD   the commands:
//                  barrier: image -> COLOR_ATTACHMENT_OPTIMAL
//                  begin rendering (clear to the background colour)
//                  bind pipeline, set viewport and scissor
//                  per sprite: push constants, draw 6 vertices
//                  end rendering
//                  barrier: image -> PRESENT_SRC_KHR
//    4. SUBMIT   to the graphics queue: wait on `imageAcquired`, signal
//                `renderFinished` and the frame's fence when done.
//    5. PRESENT  the image: the display waits on `renderFinished`.
//
//  The CPU never waits on the GPU except in step 1, and with two frames in
//  flight it is usually already done by then.
//
//  OUT-OF-DATE SWAPCHAINS
//    When the window changes size, acquire or present returns
//    VK_ERROR_OUT_OF_DATE_KHR (or VK_SUBOPTIMAL_KHR). That is not an error:
//    the swapchain is rebuilt at the new size and the next frame carries on.
// =============================================================================

#include "VulkanRenderer.h"

#include "playground/core/Log.h"
#include "playground/gpu/Instance.h"
#include "playground/gpu/VulkanCheck.h"

#include <cmath>
#include <cstddef>
#include <format>
#include <span>
#include <utility>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace playground::render {
namespace {

using log::Category;

// SPIR-V compiled from render/shaders/ by glslc at build time and embedded
// here, so there are no shader files to find at run time.
constexpr u32 kSpriteVertSpirv[] = {
#include "sprite.vert.spv.inc"
};
constexpr u32 kSpriteFragSpirv[] = {
#include "sprite.frag.spv.inc"
};

// Must match the push_constant block in sprite.vert byte for byte. glm::vec2
// is two floats and glm::vec4 four, so these offsets are the GLSL ones; the
// static_asserts make a mismatch a build error instead of garbled sprites.
struct SpritePushConstants {
  glm::vec2 position;
  glm::vec2 halfSize;
  glm::vec2 rotation;      // (cos, sin)
  glm::vec2 viewportHalf;
  glm::vec4 colour;
};
static_assert(offsetof(SpritePushConstants, position) == 0);
static_assert(offsetof(SpritePushConstants, halfSize) == 8);
static_assert(offsetof(SpritePushConstants, rotation) == 16);
static_assert(offsetof(SpritePushConstants, viewportHalf) == 24);
static_assert(offsetof(SpritePushConstants, colour) == 32);
static_assert(sizeof(SpritePushConstants) == 48);
// Vulkan guarantees at least 128 bytes of push constants on every device.

// How long to wait for the GPU before calling it hung. Generous: a frame
// that takes this long is a bug, not a slow frame.
constexpr u64 kGpuTimeoutNs = 5'000'000'000;  // 5 seconds

Result<gpu::ShaderModule> createShaderModule(VkDevice device, std::span<const u32> spirv, const char* name) {
  const VkShaderModuleCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spirv.size_bytes(),
      .pCode = spirv.data(),
  };
  VkShaderModule module = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule"); !s) {
    s.error().message += std::format(" (shader {})", name);
    return tl::unexpected(std::move(s.error()));
  }
  return gpu::ShaderModule(device, module);
}

Result<gpu::Semaphore> createSemaphore(VkDevice device) {
  const VkSemaphoreCreateInfo info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore semaphore = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateSemaphore(device, &info, nullptr, &semaphore), "vkCreateSemaphore"); !s) {
    return tl::unexpected(std::move(s.error()));
  }
  return gpu::Semaphore(device, semaphore);
}

// A synchronization2 image layout transition. Stage and access masks are
// kept as narrow as the transition allows: ALL_COMMANDS everywhere "works"
// and serialises the whole GPU.
void transitionImage(VkCommandBuffer commands, VkImage image, VkImageLayout from, VkImageLayout to,
                     VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
                     VkAccessFlags2 dstAccess) {
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
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1},
  };
  const VkDependencyInfo dependency{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(commands, &dependency);
}

}  // namespace

// --- Public factory ----------------------------------------------------------

Result<std::unique_ptr<Renderer>> createVulkanRenderer(platform::Platform& platform, platform::Window& window,
                                                       const VulkanRendererDesc& desc) {
  auto renderer = VulkanRenderer::create(platform, window, desc);
  if (!renderer) return tl::unexpected(std::move(renderer.error()));
  return std::unique_ptr<Renderer>(std::move(*renderer));
}

GpuValidationCounts gpuValidationCounts() noexcept {
  const gpu::ValidationCounts counts = gpu::validationCounts();
  return {.errors = counts.errors, .warnings = counts.warnings};
}

// --- Creation ----------------------------------------------------------------

VulkanRenderer::VulkanRenderer(platform::Window& window, gpu::Context context, const VulkanRendererDesc& desc)
    : window_(window), desc_(desc), context_(std::move(context)) {}

Result<std::unique_ptr<VulkanRenderer>> VulkanRenderer::create(platform::Platform& platform,
                                                               platform::Window& window,
                                                               const VulkanRendererDesc& desc) {
  // Instance, surface, GPU, device and queues: see gpu/Context.h.
  auto context = gpu::Context::create(platform, window,
                                      {.applicationName = desc.applicationName, .enableValidation = desc.enableValidation});
  if (!context) return tl::unexpected(std::move(context.error()));

  std::unique_ptr<VulkanRenderer> renderer(new VulkanRenderer(window, std::move(*context), desc));

  // The swapchain also builds the pipeline, which needs its image format.
  if (Status s = renderer->createSwapchain(window.framebufferSize()); !s) return tl::unexpected(std::move(s.error()));
  if (Status s = renderer->createFrameResources(); !s) return tl::unexpected(std::move(s.error()));

  log::info(Category::Render, "Vulkan renderer ready: {} frames in flight", kFramesInFlight);
  return renderer;
}

VulkanRenderer::~VulkanRenderer() {
  // Nothing below may be destroyed while the GPU might still be using it.
  if (Status s = context_.device().waitIdle(); !s) log::error(Category::Render, "{}", s.error().message);
  log::info(Category::Render, "Vulkan renderer shutting down after {} frames", framesPresented_);
  // Members are now destroyed in reverse order: frames, command pool,
  // pipeline, layout, semaphores, swapchain, and the context last.
}

Status VulkanRenderer::createSwapchain(platform::Extent2D size) {
  const VkDevice device = context_.device().handle();
  const bool recreating = swapchain_.has_value();

  if (recreating) {
    // The old swapchain's images may still be in use by queued frames.
    if (Status s = context_.device().waitIdle(); !s) return s;
  }

  auto fresh = gpu::Swapchain::create(context_, {
                                                    .framebufferSize = size,
                                                    .vsync = desc_.vsync,
                                                    .oldSwapchain = recreating ? swapchain_->handle() : VK_NULL_HANDLE,
                                                });
  if (!fresh) return tl::unexpected(std::move(fresh.error()));
  swapchain_.reset();  // destroy the old one only after the new one exists
  swapchain_.emplace(std::move(*fresh));

  // One "render finished" semaphore per swapchain IMAGE. Per frame in flight
  // would be wrong: present holds the semaphore until the image is shown,
  // and with 3 images and 2 frames in flight the slot can come round again
  // before that, reusing a semaphore present is still waiting on.
  renderFinished_.clear();
  for (u32 i = 0; i < swapchain_->imageCount(); ++i) {
    auto semaphore = createSemaphore(device);
    if (!semaphore) return tl::unexpected(std::move(semaphore.error()));
    renderFinished_.push_back(std::move(*semaphore));
  }

  // The pipeline is built for one colour format; rebuild it if that changed.
  if (swapchain_->format() != pipelineFormat_) {
    if (Status s = createPipeline(swapchain_->format()); !s) return s;
  }

  if (recreating) ++recreations_;
  swapchainOutOfDate_ = false;
  return {};
}

Status VulkanRenderer::createPipeline(VkFormat colourFormat) {
  const VkDevice device = context_.device().handle();

  // --- Shaders -------------------------------------------------------------
  auto vertex = createShaderModule(device, kSpriteVertSpirv, "sprite.vert");
  if (!vertex) return tl::unexpected(std::move(vertex.error()));
  auto fragment = createShaderModule(device, kSpriteFragSpirv, "sprite.frag");
  if (!fragment) return tl::unexpected(std::move(fragment.error()));

  const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vertex->get(),
       .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fragment->get(),
       .pName = "main"},
  }};

  // --- Fixed-function state -------------------------------------------------
  // No vertex buffers: the quad's corners are in the vertex shader.
  const VkPipelineVertexInputStateCreateInfo vertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  };
  // Viewport and scissor are set each frame (dynamic state), so a resize
  // needs no new pipeline. Only the count is fixed here.
  const VkPipelineViewportStateCreateInfo viewport{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
  };
  const VkPipelineRasterizationStateCreateInfo rasterization{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,  // 2D: draw quads whichever way they face
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f,
  };
  const VkPipelineMultisampleStateCreateInfo multisample{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };
  // Standard alpha blending: colour = src * srcAlpha + dst * (1 - srcAlpha).
  const VkPipelineColorBlendAttachmentState blendAttachment{
      .blendEnable = VK_TRUE,
      .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      .colorBlendOp = VK_BLEND_OP_ADD,
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      .alphaBlendOp = VK_BLEND_OP_ADD,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                        VK_COLOR_COMPONENT_A_BIT,
  };
  const VkPipelineColorBlendStateCreateInfo colourBlend{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &blendAttachment,
  };
  const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  const VkPipelineDynamicStateCreateInfo dynamic{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<u32>(dynamicStates.size()),
      .pDynamicStates = dynamicStates.data(),
  };

  // --- Layout: what the shaders read besides vertices: one push-constant
  // block, read by the vertex shader.
  if (!pipelineLayout_) {
    const VkPushConstantRange pushRange{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(SpritePushConstants),
    };
    const VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange,
    };
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (Status s = gpu::check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout), "vkCreatePipelineLayout");
        !s) {
      return s;
    }
    pipelineLayout_ = gpu::PipelineLayout(device, layout);
  }

  // --- Dynamic rendering: name the attachment formats, no VkRenderPass -----
  const VkPipelineRenderingCreateInfo rendering{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &colourFormat,
  };

  const VkGraphicsPipelineCreateInfo pipelineInfo{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering,
      .stageCount = static_cast<u32>(stages.size()),
      .pStages = stages.data(),
      .pVertexInputState = &vertexInput,
      .pInputAssemblyState = &inputAssembly,
      .pViewportState = &viewport,
      .pRasterizationState = &rasterization,
      .pMultisampleState = &multisample,
      .pDepthStencilState = nullptr,  // no depth buffer yet
      .pColorBlendState = &colourBlend,
      .pDynamicState = &dynamic,
      .layout = pipelineLayout_.get(),
      .renderPass = VK_NULL_HANDLE,  // dynamic rendering
  };

  VkPipeline pipeline = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline),
                            "vkCreateGraphicsPipelines");
      !s) {
    return s;
  }
  pipeline_ = gpu::Pipeline(device, pipeline);  // replaces (and destroys) any old one
  pipelineFormat_ = colourFormat;
  log::info(Category::Render, "sprite pipeline built for {}", gpu::describe(colourFormat));
  return {};  // the shader modules are destroyed here: the pipeline no longer needs them
}

Status VulkanRenderer::createFrameResources() {
  const VkDevice device = context_.device().handle();

  // RESET_COMMAND_BUFFER: each frame's buffer is reset and re-recorded.
  const VkCommandPoolCreateInfo poolInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = context_.device().queueFamilies().graphics,
  };
  VkCommandPool pool = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "vkCreateCommandPool"); !s) {
    return s;
  }
  commandPool_ = gpu::CommandPool(device, pool);

  std::array<VkCommandBuffer, kFramesInFlight> buffers{};
  const VkCommandBufferAllocateInfo allocInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = kFramesInFlight,
  };
  if (Status s = gpu::check(vkAllocateCommandBuffers(device, &allocInfo, buffers.data()), "vkAllocateCommandBuffers");
      !s) {
    return s;
  }

  for (u32 i = 0; i < kFramesInFlight; ++i) {
    Frame& frame = frames_[i];
    frame.commandBuffer = buffers[i];

    auto acquired = createSemaphore(device);
    if (!acquired) return tl::unexpected(std::move(acquired.error()));
    frame.imageAcquired = std::move(*acquired);

    // Created signalled, so the very first wait in renderFrame returns at once.
    const VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                      .flags = VK_FENCE_CREATE_SIGNALED_BIT};
    VkFence fence = VK_NULL_HANDLE;
    if (Status s = gpu::check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence"); !s) return s;
    frame.inFlight = gpu::Fence(device, fence);
  }
  return {};
}

// --- Per frame ---------------------------------------------------------------

Status VulkanRenderer::onFramebufferResized(platform::Extent2D size) {
  log::info(Category::Render, "window resized to {}x{}: rebuilding the swapchain", size.width, size.height);
  return createSwapchain(size);
}

Status VulkanRenderer::renderFrame(const FrameTime& /*time*/, const FrameData& frameData) {
  if (swapchainOutOfDate_) {
    const platform::Extent2D size = window_.framebufferSize();
    if (size.isEmpty()) return {};  // minimised: nothing to draw into
    if (Status s = createSwapchain(size); !s) return s;
  }

  const VkDevice device = context_.device().handle();
  Frame& frame = frames_[frameIndex_];
  const VkFence fence = frame.inFlight.get();

  // 1. WAIT until the GPU is done with this slot's previous frame.
  const VkResult waited = vkWaitForFences(device, 1, &fence, VK_TRUE, kGpuTimeoutNs);
  if (waited == VK_TIMEOUT) {
    return fail(Errc::GpuFailure, "the GPU did not finish a frame within {} s; it has probably hung",
                kGpuTimeoutNs / 1'000'000'000);
  }
  if (Status s = gpu::check(waited, "vkWaitForFences"); !s) return s;

  // 2. ACQUIRE an image.
  u32 imageIndex = 0;
  const VkResult acquired = vkAcquireNextImageKHR(device, swapchain_->handle(), kGpuTimeoutNs,
                                                  frame.imageAcquired.get(), VK_NULL_HANDLE, &imageIndex);
  if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
    swapchainOutOfDate_ = true;  // rebuilt at the top of the next frame; the fence stays signalled
    return {};
  }
  if (acquired == VK_TIMEOUT || acquired == VK_NOT_READY) {
    return fail(Errc::GpuFailure, "no swapchain image became available within {} s",
                kGpuTimeoutNs / 1'000'000'000);
  }
  if (acquired == VK_SUBOPTIMAL_KHR) {
    swapchainOutOfDate_ = true;  // still usable: draw this frame, rebuild afterwards
  } else if (Status s = gpu::check(acquired, "vkAcquireNextImageKHR"); !s) {
    return s;
  }

  // Reset the fence only once we are sure to submit work that signals it.
  // Resetting before a failed acquire would leave it unsignalled forever,
  // and the next wait on it would hang.
  if (Status s = gpu::check(vkResetFences(device, 1, &fence), "vkResetFences"); !s) return s;

  // 3. RECORD.
  if (Status s = gpu::check(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer"); !s) return s;
  if (Status s = recordCommands(frame.commandBuffer, imageIndex, frameData); !s) return s;

  // 4. SUBMIT. The colour writes wait for the image to be acquired; the
  // semaphore for present and the fence for the CPU fire when it is drawn.
  const VkSemaphoreSubmitInfo waitInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = frame.imageAcquired.get(),
      .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
  };
  const VkSemaphore renderFinished = renderFinished_[imageIndex].get();
  const VkSemaphoreSubmitInfo signalInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = renderFinished,
      .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
  };
  const VkCommandBufferSubmitInfo commandInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = frame.commandBuffer,
  };
  const VkSubmitInfo2 submit{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .waitSemaphoreInfoCount = 1,
      .pWaitSemaphoreInfos = &waitInfo,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &commandInfo,
      .signalSemaphoreInfoCount = 1,
      .pSignalSemaphoreInfos = &signalInfo,
  };
  if (Status s = gpu::check(vkQueueSubmit2(context_.device().graphicsQueue(), 1, &submit, fence), "vkQueueSubmit2");
      !s) {
    return s;
  }

  // 5. PRESENT.
  const VkSwapchainKHR swapchain = swapchain_->handle();
  const VkPresentInfoKHR present{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &renderFinished,
      .swapchainCount = 1,
      .pSwapchains = &swapchain,
      .pImageIndices = &imageIndex,
  };
  const VkResult presented = vkQueuePresentKHR(context_.device().presentQueue(), &present);
  if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
    swapchainOutOfDate_ = true;
  } else if (Status s = gpu::check(presented, "vkQueuePresentKHR"); !s) {
    return s;
  }

  frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
  ++framesPresented_;
  return {};
}

Status VulkanRenderer::recordCommands(VkCommandBuffer commands, u32 imageIndex, const FrameData& frame) const {
  const VkCommandBufferBeginInfo begin{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  if (Status s = gpu::check(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer"); !s) return s;

  const VkImage image = swapchain_->image(imageIndex);
  const VkExtent2D extent = swapchain_->extent();

  // UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL. "Undefined" says we do not care
  // what was in the image: it is about to be cleared. The source stage is
  // the one the acquire semaphore waits at, which chains the two together.
  transitionImage(commands, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

  const VkRenderingAttachmentInfo colour{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = swapchain_->imageView(imageIndex),
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {.color = {.float32 = {desc_.clearColour[0], desc_.clearColour[1], desc_.clearColour[2],
                                           desc_.clearColour[3]}}},
  };
  const VkRenderingInfo rendering{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.offset = {0, 0}, .extent = extent},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colour,
  };
  vkCmdBeginRendering(commands, &rendering);

  vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());

  // Negative height flips Vulkan's y-down clip space to y-up, so positions
  // mean the same as in OpenGL and GLM (roadmap §4.4). y starts at the
  // bottom edge for the same reason.
  const auto width = static_cast<f32>(extent.width);
  const auto height = static_cast<f32>(extent.height);
  const VkViewport viewport{.x = 0.0f, .y = height, .width = width, .height = -height, .minDepth = 0.0f,
                            .maxDepth = 1.0f};
  const VkRect2D scissor{.offset = {0, 0}, .extent = extent};
  vkCmdSetViewport(commands, 0, 1, &viewport);
  vkCmdSetScissor(commands, 0, 1, &scissor);

  // One draw per sprite: its data in push constants, 6 vertices (two
  // triangles) placed by the vertex shader.
  const glm::vec2 viewportHalf{0.5f * width, 0.5f * height};
  for (const SpriteInstance& sprite : frame.sprites) {
    const SpritePushConstants push{
        .position = sprite.position,
        .halfSize = 0.5f * sprite.size,
        .rotation = {std::cos(sprite.rotation), std::sin(sprite.rotation)},
        .viewportHalf = viewportHalf,
        .colour = sprite.colour,
    };
    vkCmdPushConstants(commands, pipelineLayout_.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdDraw(commands, 6, 1, 0, 0);
  }

  vkCmdEndRendering(commands);

  // COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR, after the colour writes.
  // Nothing on this queue reads it afterwards; present waits on the
  // renderFinished semaphore, not on a pipeline stage.
  transitionImage(commands, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);

  return gpu::check(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
}

}  // namespace playground::render
