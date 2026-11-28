// =============================================================================
//  VulkanRenderer.cpp — draws the frame's meshes, then its sprites.
//
//  MESHES (FrameData::meshes, render/Resources.h): lit, textured 3D geometry.
//    - each mesh lives in device-local vertex and index buffers (Mesh.h),
//      created once by createMesh;
//    - each frame, one MeshDrawGpu per instance (transform, normal matrix,
//      material factors, texture indices) goes into a per-frame storage
//      buffer, found by the shaders through its GPU address, like sprites;
//    - one vkCmdDrawIndexed per instance, with firstInstance = its entry, so
//      gl_InstanceIndex finds its data; buffers are rebound only when the
//      mesh changes;
//    - TEXTURES are one bindless array (set 1): every texture ever created
//      sits in a slot, and a material names its textures by slot. Two
//      defaults (white, flat normal) fill in for textures a material lacks.
//      The set is update-after-bind, so new textures go into free slots
//      while frames that use the set are still on the GPU.
//    - destroyMesh/destroyTexture retire the object: its handle goes stale
//      at once, the memory is freed once every frame that might use it has
//      finished (ResourcePool.h).
//
//  SPRITES: every SpriteInstance in the FrameData the game extracted,
//  in order, as a coloured rectangle, in ONE instanced draw call:
//    - each frame in flight owns a host-visible storage buffer, mapped for
//      its whole life; after that frame's fence wait the CPU writes the
//      sprites straight into it (no copy, no map/unmap per frame);
//    - the vertex shader finds the buffer through its GPU address (a push
//      constant) and picks its sprite with gl_InstanceIndex;
//    - vkCmdDraw(6 vertices, N instances) draws them all.
//  The buffer doubles when a frame has more sprites than it holds. One
//  buffer per frame in flight, because the GPU may still be reading frame
//  N's sprites while the CPU writes frame N+1's.
//
//  THE CAMERA reaches the shader through a per-frame UNIFORM BUFFER (a UBO:
//  small, read-only, the same for every vertex) holding the view-projection
//  matrix, bound with a descriptor set: set 0, binding 0.
//
//  DEPTH: a D32_SFLOAT depth buffer with reverse-Z (cleared to 0, nearer is
//  larger, test GREATER_OR_EQUAL). A sprite's depth is its world z. Sprites
//  are drawn in two instanced draws, the standard split for transparency:
//    1. OPAQUE sprites (alpha 1): depth test AND depth write, in the game's
//       order. The depth buffer puts higher layers in front.
//    2. TRANSLUCENT sprites: depth test but no depth write, stable-sorted
//       back to front by depth (so same-depth sprites keep the game's
//       order). Writing depth for them would hide what is behind instead of
//       blending with it; and many translucent sprites on one plane would
//       z-fight in a perspective view, where coplanar depths differ only by
//       rounding.
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
//                  set viewport and scissor, bind descriptor sets 0 and 1
//                  meshes: bind the mesh pipeline, push the draw list's
//                  address, one indexed draw per mesh
//                  sprites: bind a sprite pipeline, push the sprite
//                  buffer's address, draw 6 x N (twice: opaque, translucent)
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

#include "BrdfLut.h"

#include "playground/core/Assert.h"
#include "playground/core/Log.h"
#include "playground/gpu/Instance.h"
#include "playground/gpu/VulkanCheck.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <span>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>
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
constexpr u32 kMeshVertSpirv[] = {
#include "pbr.vert.spv.inc"
};
constexpr u32 kMeshFragSpirv[] = {
#include "pbr.frag.spv.inc"
};
constexpr u32 kSkyVertSpirv[] = {
#include "sky.vert.spv.inc"
};
constexpr u32 kSkyFragSpirv[] = {
#include "sky.frag.spv.inc"
};

// Set 1 bindings after the material array; must match frame.glsl.
constexpr u32 kIrradianceBinding = 1;
constexpr u32 kSpecularBinding = 2;
constexpr u32 kBrdfLutBinding = 3;
constexpr u32 kEnvironmentBinding = 4;
constexpr u32 kShadowBinding = 5;

// The sRGB curve, for clearing a _UNORM swapchain to the same colour an
// _SRGB one shows (it encodes on write; a _UNORM one stores the number).
f32 encodeSrgb(f32 linear) {
  linear = std::clamp(linear, 0.0f, 1.0f);
  return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

bool isSrgbFormat(VkFormat format) noexcept {
  return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
}

// Must match `struct Sprite` in sprite.vert byte for byte (std430 layout).
// glm::vec2 is two floats and glm::vec4 four, so these offsets are the GLSL
// ones; the static_asserts make a mismatch a build error, not garbled sprites.
struct SpriteGpu {
  glm::vec2 position;
  glm::vec2 halfSize;
  glm::vec2 rotation;  // (cos, sin)
  glm::vec2 depth;     // x: world z; y unused (keeps colour 16-byte aligned, as std430 does)
  glm::vec4 colour;
};
static_assert(offsetof(SpriteGpu, position) == 0);
static_assert(offsetof(SpriteGpu, halfSize) == 8);
static_assert(offsetof(SpriteGpu, rotation) == 16);
static_assert(offsetof(SpriteGpu, colour) == 32);
static_assert(sizeof(SpriteGpu) == 48);

// Must match `struct MeshDraw` in mesh_common.glsl (std430).
struct MeshDrawGpu {
  glm::mat4 model;
  glm::mat4 normalMatrix;  // a mat3 in a mat4: std430 would pad a mat3's columns anyway
  glm::vec4 baseColourFactor;
  glm::vec4 factors;    // metallic, roughness, normal scale, unused
  glm::uvec4 textures;  // bindless slots: base colour, normal, metallic-roughness, emissive
  glm::vec4 emissive;   // rgb: emissive factor
};
static_assert(offsetof(MeshDrawGpu, normalMatrix) == 64);
static_assert(offsetof(MeshDrawGpu, baseColourFactor) == 128);
static_assert(offsetof(MeshDrawGpu, factors) == 144);
static_assert(offsetof(MeshDrawGpu, textures) == 160);
static_assert(offsetof(MeshDrawGpu, emissive) == 176);
static_assert(sizeof(MeshDrawGpu) == 192);

// Must match the push_constant blocks in sprite.vert and mesh_common.glsl:
// one GPU address, of the sprite buffer or of the mesh draw list. Both
// stages may read it (pbr.frag looks up its material).
struct DrawPushConstants {
  VkDeviceAddress list;
};
static_assert(sizeof(DrawPushConstants) == 8);
constexpr VkShaderStageFlags kPushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// Must match the uniform block (set 0, binding 0) in frame.glsl (std140).
// sprite.vert declares only the first member, which is allowed.
struct FrameUniforms {
  glm::mat4 viewProjection;         // world -> clip, reverse-Z
  glm::mat4 inverseViewProjection;  // clip -> world (the sky)
  glm::mat4 lightViewProjection;    // world -> the sun's shadow map
  glm::vec4 cameraPosition;
  glm::vec4 sunDirection;           // normalised
  glm::vec4 sunColour;              // colour x intensity
  glm::vec4 ambient;
  glm::vec4 environment;            // intensity, last specular mip, unused, unused
  glm::vec4 shadow;                 // on (0/1), normal offset (world units), strength, one texel (uv)
};
static_assert(offsetof(FrameUniforms, cameraPosition) == 192);
static_assert(sizeof(FrameUniforms) == 288);

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

using gpu::transitionImage;

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

  // Layouts first: the pipelines (built with the swapchain, which gives
  // their colour format) and the descriptor sets are made from them.
  if (Status s = renderer->createDescriptorLayouts(); !s) return tl::unexpected(std::move(s.error()));
  if (Status s = renderer->createPostAndShadows(); !s) return tl::unexpected(std::move(s.error()));
  if (Status s = renderer->createSwapchain(window.framebufferSize()); !s) return tl::unexpected(std::move(s.error()));
  if (Status s = renderer->createFrameResources(); !s) return tl::unexpected(std::move(s.error()));
  if (Status s = renderer->createTextureResources(); !s) return tl::unexpected(std::move(s.error()));

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

Status VulkanRenderer::enableUi() {
  if (ui_) return {};
  auto overlay = UiOverlay::create(context_, swapchain_->format(), VK_FORMAT_D32_SFLOAT, swapchain_->imageCount());
  if (!overlay) return tl::unexpected(std::move(overlay.error()));
  ui_ = std::move(*overlay);
  return {};
}

void VulkanRenderer::disableUi() noexcept {
  if (!ui_) return;
  // Frames in flight may still be drawing with it.
  if (Status s = context_.device().waitIdle(); !s) log::error(Category::Render, "{}", s.error().message);
  ui_.reset();
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
                                                    .preferSrgb = !desc_.preferUnormSwapchain,
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

  // The depth buffer and the HDR target (with its bloom chain and their
  // descriptor sets) match the swapchain's size. The old ones go as the new
  // ones replace them: the GPU is idle (waited for above).
  if (Status s = createDepthBuffer(swapchain_->extent()); !s) return s;
  if (Status s = post_->resize(swapchain_->extent()); !s) return s;
  for (usize i = 0; i < 4; ++i) {
    swapchainClear_[i] = isSrgbFormat(swapchain_->format()) || i == 3 ? desc_.clearColour[i]
                                                                       : encodeSrgb(desc_.clearColour[i]);
  }

  // The pipeline is built for one colour format; rebuild it if that changed.
  if (swapchain_->format() != pipelineFormat_) {
    if (Status s = createPipeline(swapchain_->format()); !s) return s;
  }
  // The UI overlay's pipeline likewise; otherwise it only needs the new image count.
  if (ui_ && ui_->colourFormat() != swapchain_->format()) {
    ui_.reset();
    if (Status s = enableUi(); !s) return s;
  } else if (ui_) {
    ui_->setImageCount(swapchain_->imageCount());
  }

  if (recreating) ++recreations_;
  swapchainOutOfDate_ = false;
  return {};
}

namespace {

// What differs between the renderer's pipelines; everything else (dynamic
// viewport, reverse-Z depth test, dynamic rendering) is shared.
struct PipelineSpec {
  std::span<const u32> vertexSpirv;
  std::span<const u32> fragmentSpirv;
  const char* name;                                  // for errors: "mesh", "opaque sprites"...
  const VkPipelineVertexInputStateCreateInfo* vertexInput;
  VkCullModeFlags cullMode;
  bool depthWrite;
  bool blend;
  bool encodeSrgb = false;  // fragment specialization constant 0 (sprite.frag on a _UNORM swapchain)
};

Result<gpu::Pipeline> buildPipeline(VkDevice device, VkPipelineLayout layout, VkFormat colourFormat,
                                    const PipelineSpec& spec) {
  auto vertex = createShaderModule(device, spec.vertexSpirv, spec.name);
  if (!vertex) return tl::unexpected(std::move(vertex.error()));
  auto fragment = createShaderModule(device, spec.fragmentSpirv, spec.name);
  if (!fragment) return tl::unexpected(std::move(fragment.error()));
  const VkBool32 encode = spec.encodeSrgb ? VK_TRUE : VK_FALSE;
  const VkSpecializationMapEntry entry{.constantID = 0, .offset = 0, .size = sizeof(VkBool32)};
  const VkSpecializationInfo specialization{.mapEntryCount = 1, .pMapEntries = &entry, .dataSize = sizeof(encode),
                                            .pData = &encode};
  const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vertex->get(),
       .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fragment->get(),
       .pName = "main",
       .pSpecializationInfo = &specialization},
  }};

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
  // Front faces are counter-clockwise as seen by the viewer: glTF's rule,
  // and OpenGL's. The flipped viewport (recordCommands) keeps that meaning.
  const VkPipelineRasterizationStateCreateInfo rasterization{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = spec.cullMode,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f,
  };
  const VkPipelineMultisampleStateCreateInfo multisample{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };
  // Reverse-Z depth test: keep a fragment if it is at least as near as what
  // is there (larger depth = nearer). Equal passes, so sprites on the same
  // layer keep their draw order.
  const VkPipelineDepthStencilStateCreateInfo depthState{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = spec.depthWrite ? VK_TRUE : VK_FALSE,
      .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
  };
  // Standard alpha blending: colour = src * srcAlpha + dst * (1 - srcAlpha).
  const VkPipelineColorBlendAttachmentState blendAttachment{
      .blendEnable = spec.blend ? VK_TRUE : VK_FALSE,
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
  // Dynamic rendering: name the attachment formats, no VkRenderPass.
  const VkPipelineRenderingCreateInfo rendering{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &colourFormat,
      .depthAttachmentFormat = gpu::kDepthFormat,
  };

  const VkGraphicsPipelineCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering,
      .stageCount = static_cast<u32>(stages.size()),
      .pStages = stages.data(),
      .pVertexInputState = spec.vertexInput,
      .pInputAssemblyState = &inputAssembly,
      .pViewportState = &viewport,
      .pRasterizationState = &rasterization,
      .pMultisampleState = &multisample,
      .pDepthStencilState = &depthState,
      .pColorBlendState = &colourBlend,
      .pDynamicState = &dynamic,
      .layout = layout,
      .renderPass = VK_NULL_HANDLE,  // dynamic rendering
  };
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                            "vkCreateGraphicsPipelines");
      !s) {
    s.error().message += std::format(" ({} pipeline)", spec.name);
    return tl::unexpected(std::move(s.error()));
  }
  return gpu::Pipeline(device, pipeline);  // the shader modules are destroyed here: the pipeline no longer needs them
}

}  // namespace

Status VulkanRenderer::createDescriptorLayouts() {
  const VkDevice device = context_.device().handle();

  // Set 0: this frame's uniform buffer. The camera is read by the vertex
  // shaders, the lights by the mesh fragment shader.
  const VkDescriptorSetLayoutBinding frameBinding{
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
  };
  const VkDescriptorSetLayoutCreateInfo frameInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1,
      .pBindings = &frameBinding,
  };
  VkDescriptorSetLayout frameLayout = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateDescriptorSetLayout(device, &frameInfo, nullptr, &frameLayout),
                            "vkCreateDescriptorSetLayout (frame)");
      !s) {
    return s;
  }
  frameSetLayout_ = gpu::DescriptorSetLayout(device, frameLayout);

  // Set 1: the bindless texture array, kMaxTextures image + sampler pairs.
  //   PARTIALLY_BOUND             slots never written are fine while no
  //                               shader reads them;
  //   UPDATE_AFTER_BIND,          a new texture can be written into a free
  //   UPDATE_UNUSED_WHILE_PENDING slot while frames that bound the set are
  //                               still on the GPU (they do not read it).
  // Bindings 1-4: the environment's maps and the BRDF table (frame.glsl).
  // Replaced only by setEnvironment, after waiting for the GPU to go idle;
  // update-after-bind because every binding of an update-after-bind pool's
  // set must allow it to be written while the set is bound.
  constexpr VkDescriptorBindingFlags kWholeSet = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
  const std::array<VkDescriptorSetLayoutBinding, 6> textureBindings{{
      {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = kMaxTextures,
       .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
      {.binding = kIrradianceBinding, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
      {.binding = kSpecularBinding, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
      {.binding = kBrdfLutBinding, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
      {.binding = kEnvironmentBinding, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
      {.binding = kShadowBinding, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
  }};
  const std::array<VkDescriptorBindingFlags, 6> textureFlags{
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
          VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
      kWholeSet, kWholeSet, kWholeSet, kWholeSet, kWholeSet};
  const VkDescriptorSetLayoutBindingFlagsCreateInfo textureFlagsInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = static_cast<u32>(textureFlags.size()),
      .pBindingFlags = textureFlags.data(),
  };
  const VkDescriptorSetLayoutCreateInfo textureInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = &textureFlagsInfo,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .bindingCount = static_cast<u32>(textureBindings.size()),
      .pBindings = textureBindings.data(),
  };
  VkDescriptorSetLayout textureLayout = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateDescriptorSetLayout(device, &textureInfo, nullptr, &textureLayout),
                            "vkCreateDescriptorSetLayout (textures)");
      !s) {
    return s;
  }
  textureSetLayout_ = gpu::DescriptorSetLayout(device, textureLayout);

  // One layout for every pipeline: sets 0 and 1, and an 8-byte push
  // constant (a buffer address). Sharing it means the sets stay bound across
  // pipeline switches. Sprite shaders simply never read set 1.
  const std::array<VkDescriptorSetLayout, 2> setLayouts{frameLayout, textureLayout};
  const VkPushConstantRange pushRange{.stageFlags = kPushStages, .offset = 0, .size = sizeof(DrawPushConstants)};
  const VkPipelineLayoutCreateInfo layoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = static_cast<u32>(setLayouts.size()),
      .pSetLayouts = setLayouts.data(),
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &pushRange,
  };
  VkPipelineLayout layout = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout), "vkCreatePipelineLayout");
      !s) {
    return s;
  }
  pipelineLayout_ = gpu::PipelineLayout(device, layout);
  return {};
}

Status VulkanRenderer::createPipeline(VkFormat colourFormat) {
  const VkDevice device = context_.device().handle();

  // Sprites have no vertex buffers: the quad's corners are in the shader.
  const VkPipelineVertexInputStateCreateInfo noVertices{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

  // Meshes read render::Vertex (48 bytes) from binding 0.
  const VkVertexInputBindingDescription vertexBinding{
      .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
  const std::array<VkVertexInputAttributeDescription, 4> attributes{{
      {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position)},
      {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal)},
      {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex, tangent)},
      {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv)},
  }};
  const VkPipelineVertexInputStateCreateInfo meshVertices{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &vertexBinding,
      .vertexAttributeDescriptionCount = static_cast<u32>(attributes.size()),
      .pVertexAttributeDescriptions = attributes.data(),
  };

  // Meshes and the sky draw into the HDR target; sprites, after the
  // composite, onto the swapchain (encoding sRGB themselves on _UNORM).
  const bool encode = !isSrgbFormat(colourFormat);
  auto mesh = buildPipeline(device, pipelineLayout_.get(), kHdrFormat,
                            {.vertexSpirv = kMeshVertSpirv, .fragmentSpirv = kMeshFragSpirv, .name = "mesh",
                             .vertexInput = &meshVertices, .cullMode = VK_CULL_MODE_BACK_BIT, .depthWrite = true,
                             .blend = false});
  if (!mesh) return tl::unexpected(std::move(mesh.error()));
  auto doubleSided = buildPipeline(device, pipelineLayout_.get(), kHdrFormat,
                                   {.vertexSpirv = kMeshVertSpirv, .fragmentSpirv = kMeshFragSpirv,
                                    .name = "double-sided mesh", .vertexInput = &meshVertices,
                                    .cullMode = VK_CULL_MODE_NONE, .depthWrite = true, .blend = false});
  if (!doubleSided) return tl::unexpected(std::move(doubleSided.error()));
  // 2D sprites: drawn whichever way they face. The translucent pipeline is
  // the same without depth writes.
  auto opaque = buildPipeline(device, pipelineLayout_.get(), colourFormat,
                              {.vertexSpirv = kSpriteVertSpirv, .fragmentSpirv = kSpriteFragSpirv,
                               .name = "opaque sprites", .vertexInput = &noVertices, .cullMode = VK_CULL_MODE_NONE,
                               .depthWrite = true, .blend = true, .encodeSrgb = encode});
  if (!opaque) return tl::unexpected(std::move(opaque.error()));
  auto blended = buildPipeline(device, pipelineLayout_.get(), colourFormat,
                               {.vertexSpirv = kSpriteVertSpirv, .fragmentSpirv = kSpriteFragSpirv,
                                .name = "translucent sprites", .vertexInput = &noVertices,
                                .cullMode = VK_CULL_MODE_NONE, .depthWrite = false, .blend = true,
                                .encodeSrgb = encode});
  if (!blended) return tl::unexpected(std::move(blended.error()));

  // The sky: one screen-covering triangle on the far plane, behind whatever
  // the meshes wrote (depth test GREATER_OR_EQUAL against 0), no depth write.
  auto sky = buildPipeline(device, pipelineLayout_.get(), kHdrFormat,
                           {.vertexSpirv = kSkyVertSpirv, .fragmentSpirv = kSkyFragSpirv, .name = "sky",
                            .vertexInput = &noVertices, .cullMode = VK_CULL_MODE_NONE, .depthWrite = false,
                            .blend = false});
  if (!sky) return tl::unexpected(std::move(sky.error()));
  skyPipeline_ = std::move(*sky);
  meshPipeline_ = std::move(*mesh);  // replaces (and destroys) any old ones
  doubleSidedPipeline_ = std::move(*doubleSided);
  opaquePipeline_ = std::move(*opaque);
  blendedPipeline_ = std::move(*blended);
  if (Status s = post_->buildComposite(colourFormat); !s) return s;
  if (Status s = shadowMap_->buildPipeline(pipelineLayout_.get()); !s) return s;
  pipelineFormat_ = colourFormat;
  log::info(Category::Render, "pipelines built: mesh, double-sided mesh and sky into the HDR target; shadow map; "
            "sprites and composite for {} ({})", gpu::describe(colourFormat),
            encode ? "the shaders encode sRGB" : "the swapchain encodes sRGB");
  return {};
}

Status VulkanRenderer::createTextureResources() {
  const VkDevice device = context_.device().handle();

  auto sampler = gpu::createSampler(context_, {.debugName = "material textures"});
  if (!sampler) return tl::unexpected(std::move(sampler.error()));
  sampler_ = std::move(*sampler);

  const VkDescriptorPoolSize poolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                      .descriptorCount = kMaxTextures + 5};  // + environment and shadow bindings
  const VkDescriptorPoolCreateInfo poolInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      .maxSets = 1,
      .poolSizeCount = 1,
      .pPoolSizes = &poolSize,
  };
  VkDescriptorPool pool = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool (textures)");
      !s) {
    return s;
  }
  textureDescriptorPool_ = gpu::DescriptorPool(device, pool);

  const VkDescriptorSetLayout layout = textureSetLayout_.get();
  const VkDescriptorSetAllocateInfo setInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &layout,
  };
  if (Status s = gpu::check(vkAllocateDescriptorSets(device, &setInfo, &textureSet_), "vkAllocateDescriptorSets (textures)");
      !s) {
    return s;
  }

  // The defaults, in the first slots (kWhiteTexture, kFlatNormalTexture).
  const auto onePixel = [&](u8 r, u8 g, u8 b, std::string_view name) -> Status {
    const std::array<std::byte, 4> rgba{std::byte{r}, std::byte{g}, std::byte{b}, std::byte{255}};
    auto image = gpu::createTexture(context_, {.width = 1, .height = 1, .format = VK_FORMAT_R8G8B8A8_UNORM,
                                               .pixels = rgba, .generateMips = false, .debugName = name});
    if (!image) return tl::unexpected(std::move(image.error()));
    auto handle = addTexture(std::move(*image), name);
    if (!handle) return tl::unexpected(std::move(handle.error()));
    return {};
  };
  if (Status s = onePixel(255, 255, 255, "default white"); !s) return s;
  if (Status s = onePixel(128, 128, 255, "default flat normal"); !s) return s;
  PG_VERIFY(textures_.liveCount() == kDefaultTextureCount, "the default textures must fill the first slots");
  log::debug(Category::Render, "bindless texture array: {} slots", kMaxTextures);

  // Image-based lighting: the BRDF table once, then a default environment
  // (a fast procedural sky), so lighting works before any setEnvironment.
  auto baker = IblBaker::create(context_);
  if (!baker) return tl::unexpected(std::move(baker.error()));
  baker_.emplace(std::move(*baker));

  const std::vector<glm::vec2> table = makeBrdfLut(kBrdfLutSize, kBrdfLutSamples);
  std::vector<u16> halves;
  halves.reserve(table.size() * 2);
  for (const glm::vec2 entry : table) {
    halves.push_back(glm::packHalf1x16(entry.x));
    halves.push_back(glm::packHalf1x16(entry.y));
  }
  auto lut = gpu::createTexture(context_, {.width = kBrdfLutSize, .height = kBrdfLutSize,
                                           .format = VK_FORMAT_R16G16_SFLOAT,
                                           .pixels = std::as_bytes(std::span(halves)), .generateMips = false,
                                           .debugName = "BRDF lookup table"});
  if (!lut) return tl::unexpected(std::move(lut.error()));
  brdfLut_ = std::move(*lut);
  writeEnvironmentBinding(kBrdfLutBinding, brdfLut_.view());
  writeSetOneBinding(kShadowBinding, shadowMap_->view(), shadowMap_->sampler(),
                     VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
  return installEnvironment(baker_->bake(ProceduralSky{}, kFastEnvironment));
}

Status VulkanRenderer::createPostAndShadows() {
  auto post = PostProcess::create(context_);
  if (!post) return tl::unexpected(std::move(post.error()));
  post_.emplace(std::move(*post));
  auto shadow = ShadowMap::create(context_, desc_.shadowMapSize);
  if (!shadow) return tl::unexpected(std::move(shadow.error()));
  shadowMap_.emplace(std::move(*shadow));
  return {};
}

void VulkanRenderer::writeEnvironmentBinding(u32 binding, VkImageView view) const noexcept {
  writeSetOneBinding(binding, view, baker_->sampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanRenderer::writeSetOneBinding(u32 binding, VkImageView view, VkSampler sampler,
                                        VkImageLayout layout) const noexcept {
  const VkDescriptorImageInfo info{.sampler = sampler, .imageView = view, .imageLayout = layout};
  const VkWriteDescriptorSet write{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = textureSet_,
      .dstBinding = binding,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &info,
  };
  vkUpdateDescriptorSets(context_.device().handle(), 1, &write, 0, nullptr);
}

Status VulkanRenderer::installEnvironment(Result<EnvironmentMaps> maps) {
  if (!maps) return tl::unexpected(std::move(maps.error()));
  // Frames in flight may be sampling the old maps: let them finish before
  // the old images go and their descriptors are rewritten.
  if (Status s = context_.device().waitIdle(); !s) return s;
  environment_ = std::move(*maps);
  writeEnvironmentBinding(kIrradianceBinding, environment_.irradiance.view());
  writeEnvironmentBinding(kSpecularBinding, environment_.specular.view());
  writeEnvironmentBinding(kEnvironmentBinding, environment_.environment.view());
  ++environmentBakes_;
  return {};
}

Status VulkanRenderer::setEnvironment(const ProceduralSky& sky, const EnvironmentQuality& quality) {
  return installEnvironment(baker_->bake(sky, quality));
}

Status VulkanRenderer::setEnvironment(const HdrImage& panorama, const EnvironmentQuality& quality) {
  return installEnvironment(baker_->bake(panorama, quality));
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

    auto uniforms = gpu::Buffer::create(context_.allocator(), {.sizeBytes = sizeof(FrameUniforms),
                                                               .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                               .memory = gpu::MemoryKind::HostVisible,
                                                               .debugName = "frame uniforms"});
    if (!uniforms) return tl::unexpected(std::move(uniforms.error()));
    frame.uniforms = std::move(*uniforms);
  }

  // One descriptor set per frame in flight, each pointing at that frame's
  // uniform buffer. Written once: the buffers never move.
  const VkDescriptorPoolSize poolSize{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = kFramesInFlight};
  const VkDescriptorPoolCreateInfo poolCreateInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = kFramesInFlight,
      .poolSizeCount = 1,
      .pPoolSizes = &poolSize,
  };
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateDescriptorPool(device, &poolCreateInfo, nullptr, &descriptorPool),
                            "vkCreateDescriptorPool");
      !s) {
    return s;
  }
  descriptorPool_ = gpu::DescriptorPool(device, descriptorPool);

  std::array<VkDescriptorSetLayout, kFramesInFlight> layouts{};
  layouts.fill(frameSetLayout_.get());
  std::array<VkDescriptorSet, kFramesInFlight> sets{};
  const VkDescriptorSetAllocateInfo setInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptorPool,
      .descriptorSetCount = kFramesInFlight,
      .pSetLayouts = layouts.data(),
  };
  if (Status s = gpu::check(vkAllocateDescriptorSets(device, &setInfo, sets.data()), "vkAllocateDescriptorSets"); !s) {
    return s;
  }
  for (u32 i = 0; i < kFramesInFlight; ++i) {
    frames_[i].descriptorSet = sets[i];
    const VkDescriptorBufferInfo bufferInfo{.buffer = frames_[i].uniforms.handle(), .offset = 0,
                                            .range = sizeof(FrameUniforms)};
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = sets[i],
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &bufferInfo,
    };
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }
  return {};
}

Status VulkanRenderer::createDepthBuffer(VkExtent2D extent) {
  auto depth = gpu::Image::create(context_.allocator(), {.width = extent.width,
                                                         .height = extent.height,
                                                         .format = gpu::kDepthFormat,
                                                         .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                                         .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
                                                         .debugName = "depth buffer"});
  if (!depth) return tl::unexpected(std::move(depth.error()));
  depth_ = std::move(*depth);  // the old one goes: createSwapchain waited for the GPU to be idle
  log::debug(Category::Render, "depth buffer {}x{} (D32_SFLOAT)", extent.width, extent.height);
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

  // The fence wait above means frames [0, framesPresented_ - (kFramesInFlight
  // - 1)) have all finished: what was destroyed before then can go.
  const u64 framesCompleted = framesPresented_ + 1 >= kFramesInFlight ? framesPresented_ + 1 - kFramesInFlight : 0;
  meshes_.release(framesCompleted);
  textures_.release(framesCompleted);

  // It also means the GPU has finished reading this slot's sprite and mesh
  // buffers and uniforms, so it is safe to overwrite (or replace) them.
  if (Status s = writeSprites(frame, frameData); !s) return s;
  if (Status s = writeMeshes(frame, frameData); !s) return s;
  if (Status s = writeFrameUniforms(frame, frameData); !s) return s;
  if (Status s = prepareCapture(frame); !s) return s;

  // Reset the fence only once we are sure to submit work that signals it.
  // Resetting before a failed acquire would leave it unsignalled forever,
  // and the next wait on it would hang.
  if (Status s = gpu::check(vkResetFences(device, 1, &fence), "vkResetFences"); !s) return s;

  // 3. RECORD.
  if (Status s = gpu::check(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer"); !s) return s;
  if (Status s = recordCommands(frame, imageIndex); !s) return s;

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

// --- Meshes and textures ------------------------------------------------------

Result<MeshHandle> VulkanRenderer::createMesh(const MeshData& data, std::string_view debugName) {
  auto mesh = Mesh::create(context_, data, debugName);
  if (!mesh) return tl::unexpected(std::move(mesh.error()));
  const u32 triangles = mesh->indexCount() / 3;
  const u32 vertices = mesh->vertexCount();
  auto handle = meshes_.add(std::move(*mesh));
  if (!handle) return tl::unexpected(std::move(handle.error()));
  log::debug(Category::Render, "mesh \"{}\": {} vertices, {} triangles (slot {})", debugName, vertices, triangles,
             handle->index);
  return handle;
}

Result<TextureHandle> VulkanRenderer::createTexture(const ImageData& image, TextureUsage usage,
                                                    std::string_view debugName) {
  // Colour data is stored as sRGB and decoded by the sampler; anything else
  // (normals, metallic-roughness) must reach the shader as the numbers stored.
  const VkFormat format = usage == TextureUsage::Colour ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  auto texture = gpu::createTexture(context_, {.width = image.width,
                                               .height = image.height,
                                               .format = format,
                                               .pixels = image.rgba,
                                               .generateMips = true,
                                               .debugName = debugName});
  if (!texture) return tl::unexpected(std::move(texture.error()));
  return addTexture(std::move(*texture), debugName);
}

Result<TextureHandle> VulkanRenderer::addTexture(gpu::Image image, std::string_view debugName) {
  auto handle = textures_.add(std::move(image));
  if (!handle) return tl::unexpected(std::move(handle.error()));
  // Point the handle's slot in the bindless array at the new image. Allowed
  // while frames are in flight: they do not read this slot (it was free, or
  // its last user finished before it was released).
  const gpu::Image* stored = textures_.get(*handle);
  const VkDescriptorImageInfo imageInfo{
      .sampler = sampler_.get(),
      .imageView = stored->view(),
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };
  const VkWriteDescriptorSet write{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = textureSet_,
      .dstBinding = 0,
      .dstArrayElement = handle->index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &imageInfo,
  };
  vkUpdateDescriptorSets(context_.device().handle(), 1, &write, 0, nullptr);
  log::debug(Category::Render, "texture \"{}\" in bindless slot {}", debugName, handle->index);
  return handle;
}

void VulkanRenderer::destroyMesh(MeshHandle mesh) noexcept { (void)meshes_.retire(mesh, framesPresented_); }

void VulkanRenderer::destroyTexture(TextureHandle texture) noexcept {
  if (texture.index < kDefaultTextureCount) return;  // the defaults live as long as the renderer
  (void)textures_.retire(texture, framesPresented_);
}

u32 VulkanRenderer::textureSlot(TextureHandle texture, u32 fallback) noexcept {
  return texture.index >= kDefaultTextureCount && textures_.get(texture) != nullptr ? texture.index : fallback;
}

Status VulkanRenderer::writeMeshes(Frame& frame, const FrameData& frameData) {
  const usize count = frameData.meshes.size();
  if (count > kMaxMeshDraws) {
    return fail(Errc::InvalidArgument, "{} meshes in one frame; the renderer's limit is {}", count, kMaxMeshDraws);
  }
  frame.meshCommands.clear();
  frame.singleSidedCount = 0;
  lastMeshDraws_ = 0;
  lastShadowCasters_ = 0;
  lastSceneBounds_ = Aabb{};
  if (count == 0) return {};

  // Grow this frame's draw list if needed, doubling (as for sprites).
  const u64 needed = std::max<u64>(count, kInitialMeshDrawCapacity) * sizeof(MeshDrawGpu);
  if (frame.meshDraws.size() < needed) {
    u64 capacity = std::max<u64>(frame.meshDraws.size() / sizeof(MeshDrawGpu), kInitialMeshDrawCapacity);
    while (capacity * sizeof(MeshDrawGpu) < needed) capacity *= 2;
    auto grown = gpu::Buffer::create(context_.allocator(),
                                     {.sizeBytes = capacity * sizeof(MeshDrawGpu),
                                      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                      .memory = gpu::MemoryKind::HostVisible,
                                      .debugName = "mesh draws"});
    if (!grown) return tl::unexpected(std::move(grown.error()));
    frame.meshDraws = std::move(*grown);  // the fence says the GPU is done with the old one
  }

  // Front to back into write-combined memory, as for sprites. A stale or
  // invalid mesh handle (destroyed, never created) is skipped, not drawn.
  // Two passes: culled meshes first, then double-sided ones, so each group
  // is one run of draws with one pipeline.
  auto* out = reinterpret_cast<MeshDrawGpu*>(frame.meshDraws.mapped().data());
  u32 written = 0;
  for (const bool doubleSided : {false, true}) {
  if (doubleSided) frame.singleSidedCount = written;
  for (const MeshInstance& instance : frameData.meshes) {
    if (instance.material.doubleSided != doubleSided) continue;
    const Mesh* mesh = meshes_.get(instance.mesh);
    if (mesh == nullptr) continue;
    const Material& material = instance.material;
    // Normals need the inverse-transpose: under a non-uniform scale (a box
    // stretched along x) the plain matrix would tilt them off the surface.
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(instance.transform)));
    out[written] = MeshDrawGpu{
        .model = instance.transform,
        .normalMatrix = glm::mat4(normalMatrix),
        .baseColourFactor = material.baseColourFactor,
        .factors = {material.metallicFactor, material.roughnessFactor, material.normalScale, 0.0f},
        .textures = {textureSlot(material.baseColour, kWhiteTexture), textureSlot(material.normal, kFlatNormalTexture),
                     textureSlot(material.metallicRoughness, kWhiteTexture),
                     textureSlot(material.emissive, kWhiteTexture)},
        .emissive = glm::vec4(material.emissiveFactor, 0.0f),
    };
    frame.meshCommands.push_back({.vertices = mesh->vertexBuffer(), .indices = mesh->indexBuffer(),
                                  .indexCount = mesh->indexCount(), .castsShadow = instance.castsShadow});
    lastSceneBounds_.add(mesh->bounds().transformed(instance.transform));
    lastShadowCasters_ += instance.castsShadow ? 1 : 0;
    ++written;
  }
  }
  lastMeshDraws_ = written;
  lastDrawCalls_ += written;
  return frame.meshDraws.flush(0, u64{written} * sizeof(MeshDrawGpu));
}

u32 VulkanRenderer::spriteCapacity() const noexcept {
  u64 largest = 0;
  for (const Frame& frame : frames_) largest = std::max(largest, frame.sprites.size());
  return static_cast<u32>(largest / sizeof(SpriteGpu));
}

Status VulkanRenderer::writeSprites(Frame& frame, const FrameData& frameData) {
  const usize count = frameData.sprites.size();
  if (count > kMaxSprites) {
    return fail(Errc::InvalidArgument, "{} sprites in one frame; the renderer's limit is {}", count, kMaxSprites);
  }
  lastSpriteCount_ = static_cast<u32>(count);


  // Grow this frame's buffer if it is too small (or does not exist yet):
  // double until it fits, so a steadily growing scene reallocates rarely.
  const u64 needed = std::max<u64>(count, kInitialSpriteCapacity) * sizeof(SpriteGpu);
  if (frame.sprites.size() < needed) {
    u64 capacity = std::max<u64>(frame.sprites.size() / sizeof(SpriteGpu), kInitialSpriteCapacity);
    while (capacity * sizeof(SpriteGpu) < needed) capacity *= 2;
    auto grown = gpu::Buffer::create(context_.allocator(),
                                     {.sizeBytes = capacity * sizeof(SpriteGpu),
                                      .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                      .memory = gpu::MemoryKind::HostVisible,
                                      .debugName = "sprite instances"});
    if (!grown) return tl::unexpected(std::move(grown.error()));
    frame.sprites = std::move(*grown);  // the old one is destroyed: the fence says the GPU is done with it
    log::debug(Category::Render, "sprite buffer for frame slot {} now holds {} sprites ({} KiB)", frameIndex_,
               capacity, capacity * sizeof(SpriteGpu) / 1024);
  }
  frame.opaqueCount = 0;
  frame.blendedCount = 0;
  if (count == 0) {
    lastDrawCalls_ = 0;
    return {};
  }

  // Straight into mapped memory, front to back: on a discrete GPU this is
  // write-combined memory, which is fast to write in order and must never
  // be read back. Opaque sprites first, in the game's order...
  auto* out = reinterpret_cast<SpriteGpu*>(frame.sprites.mapped().data());
  const auto toGpu = [](const SpriteInstance& sprite) {
    return SpriteGpu{
        .position = sprite.position,
        .halfSize = 0.5f * sprite.size,
        .rotation = {std::cos(sprite.rotation), std::sin(sprite.rotation)},
        .depth = {sprite.depth, 0.0f},
        .colour = sprite.colour,
    };
  };
  blendedOrder_.clear();
  u32 written = 0;
  for (usize i = 0; i < count; ++i) {
    const SpriteInstance& sprite = frameData.sprites[i];
    if (sprite.colour.a >= 1.0f) {
      out[written++] = toGpu(sprite);
    } else {
      blendedOrder_.push_back(static_cast<u32>(i));
    }
  }
  frame.opaqueCount = written;

  // ...then translucent ones, back to front (smaller depth = farther). A
  // stable sort keeps the game's order among equal depths. Already in order
  // (common: one translucent layer) costs one pass, no sort.
  const auto farther = [&](u32 a, u32 b) { return frameData.sprites[a].depth < frameData.sprites[b].depth; };
  if (!std::is_sorted(blendedOrder_.begin(), blendedOrder_.end(), farther)) {
    std::stable_sort(blendedOrder_.begin(), blendedOrder_.end(), farther);
  }
  for (const u32 i : blendedOrder_) out[written++] = toGpu(frameData.sprites[i]);
  frame.blendedCount = static_cast<u32>(blendedOrder_.size());

  lastDrawCalls_ = (frame.opaqueCount > 0 ? 1u : 0u) + (frame.blendedCount > 0 ? 1u : 0u);
  return frame.sprites.flush(0, count * sizeof(SpriteGpu));  // a no-op on host-coherent memory
}

Status VulkanRenderer::writeFrameUniforms(Frame& frame, const FrameData& frameData) {
  const VkExtent2D extent = swapchain_->extent();
  const glm::vec3 towards = frameData.sun.direction;
  const f32 length = glm::length(towards);
  const glm::mat4 viewProjection = frameData.viewProjection.value_or(
      pixelViewProjection({static_cast<f32>(extent.width), static_cast<f32>(extent.height)}));
  frame.drawSky = frameData.drawSky;
  frame.drawUi = frameData.drawUi && ui_ != nullptr;
  lastUiDrawLists_ = frame.drawUi ? UiOverlay::pendingDrawLists() : 0u;
  if (frameData.drawSky) ++lastDrawCalls_;
  // The HDR path only when there is something 3D to draw: a sprites-only
  // frame (the 2D demo) goes straight to the swapchain, as before.
  frame.useHdr = !frame.meshCommands.empty() || frameData.drawSky;
  frame.drawShadows = frame.useHdr && frameData.shadows.enabled && lastShadowCasters_ > 0;
  frame.shadows = frameData.shadows;
  frame.bloom = frameData.bloom;
  frame.exposure = frameData.exposure;
  frame.toneMap = frameData.toneMap;
  lastFrameUsedHdr_ = frame.useHdr;

  glm::vec4 shadow{0.0f};
  if (frame.drawShadows) {
    const Aabb& bounds = frameData.shadows.bounds ? *frameData.shadows.bounds : lastSceneBounds_;
    const ShadowProjection projection =
        directionalShadowMatrix(frameData.sun.direction, bounds, shadowMap_->size());
    lastLightViewProjection_ = projection.viewProjection;
    shadow = {1.0f, frameData.shadows.normalOffset * projection.texelWorldSize,
              std::clamp(frameData.shadows.strength, 0.0f, 1.0f), 1.0f / static_cast<f32>(shadowMap_->size())};
  }
  const FrameUniforms uniforms{
      .viewProjection = viewProjection,
      .inverseViewProjection = glm::inverse(viewProjection),
      .lightViewProjection = lastLightViewProjection_,
      .cameraPosition = glm::vec4(frameData.cameraPosition, 1.0f),
      .sunDirection = glm::vec4(length > 0.0f ? towards / length : glm::vec3(0.0f, -1.0f, 0.0f), 0.0f),
      .sunColour = glm::vec4(frameData.sun.colour * frameData.sun.intensity, 0.0f),
      .ambient = glm::vec4(frameData.ambient, 0.0f),
      .environment = {std::max(frameData.environmentIntensity, 0.0f),
                      static_cast<f32>(environment_.specularMips() - 1), 0.0f, 0.0f},
      .shadow = shadow,
  };
  return frame.uniforms.write(std::as_bytes(std::span(&uniforms, 1)));
}

Status VulkanRenderer::prepareCapture(Frame& frame) {
  // Only this submission's copy is decided here. A capture already sitting
  // in this slot's buffer stays valid when the slot is reused without one.
  frame.recordCapture = false;
  if (!captureRequested_) return {};
  captureRequested_ = false;
  if (!swapchain_->canCopyFrom()) {
    return fail(Errc::Unsupported, "this window's swapchain images cannot be copied from, so frames cannot be captured");
  }
  const VkExtent2D extent = swapchain_->extent();
  const u64 bytes = u64{extent.width} * extent.height * 4;  // both supported formats are 4 bytes per pixel
  if (frame.capture.size() < bytes) {
    auto buffer = gpu::Buffer::create(context_.allocator(), {.sizeBytes = bytes,
                                                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                             .memory = gpu::MemoryKind::HostReadback,
                                                             .debugName = "frame capture"});
    if (!buffer) return tl::unexpected(std::move(buffer.error()));
    frame.capture = std::move(*buffer);
  }
  for (Frame& other : frames_) other.hasCapture = false;  // only the newest capture counts
  frame.recordCapture = true;
  frame.hasCapture = true;  // readable once this frame's fence has signalled
  frame.captureExtent = extent;
  frame.captureFormat = swapchain_->format();
  return {};
}

Result<CapturedFrame> VulkanRenderer::takeCapture() {
  if (Status s = context_.device().waitIdle(); !s) return tl::unexpected(std::move(s.error()));
  for (Frame& frame : frames_) {
    if (!frame.hasCapture) continue;
    if (Status s = frame.capture.invalidate(); !s) return tl::unexpected(std::move(s.error()));
    CapturedFrame result{.width = frame.captureExtent.width, .height = frame.captureExtent.height};
    const usize bytes = usize{result.width} * result.height * 4;
    const auto source = frame.capture.mapped().first(bytes);
    result.rgba.resize(bytes);
    const bool bgra = frame.captureFormat == VK_FORMAT_B8G8R8A8_SRGB || frame.captureFormat == VK_FORMAT_B8G8R8A8_UNORM;
    for (usize i = 0; i < bytes; i += 4) {
      const auto byte = [&](usize k) { return std::to_integer<u8>(source[i + k]); };
      result.rgba[i + 0] = bgra ? byte(2) : byte(0);
      result.rgba[i + 1] = byte(1);
      result.rgba[i + 2] = bgra ? byte(0) : byte(2);
      result.rgba[i + 3] = byte(3);
    }
    frame.hasCapture = false;
    return result;
  }
  return fail(Errc::InvalidState, "no frame has been captured: call requestCapture() and render a frame first");
}

Status VulkanRenderer::recordCommands(const Frame& frameSlot, u32 imageIndex) const {
  const VkCommandBuffer commands = frameSlot.commandBuffer;
  const VkDeviceAddress sprites = frameSlot.sprites.deviceAddress();
  const VkCommandBufferBeginInfo begin{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  if (Status s = gpu::check(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer"); !s) return s;

  const VkImage image = swapchain_->image(imageIndex);
  const VkExtent2D extent = swapchain_->extent();
  constexpr VkPipelineStageFlags2 kDepthStages =
      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

  // Negative height flips Vulkan's y-down clip space to y-up, so positions
  // mean the same as in OpenGL and GLM (roadmap §4.4). y starts at the
  // bottom edge for the same reason. Used by the scene and sprite passes.
  const auto width = static_cast<f32>(extent.width);
  const auto height = static_cast<f32>(extent.height);
  const VkViewport viewport{.x = 0.0f, .y = height, .width = width, .height = -height, .minDepth = 0.0f,
                            .maxDepth = 1.0f};
  const VkRect2D scissor{.offset = {0, 0}, .extent = extent};

  // Sets 0 (this frame's camera and lights) and 1 (every texture, the
  // environment and the shadow map), shared by every scene pipeline.
  const std::array<VkDescriptorSet, 2> sets{frameSlot.descriptorSet, textureSet_};
  const auto bindSceneSets = [&] {
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_.get(), 0,
                            static_cast<u32>(sets.size()), sets.data(), 0, nullptr);
  };
  // Mesh draws: buffers rebound only when the mesh changes; firstInstance =
  // i makes gl_InstanceIndex = i, the draw's entry in the meshDraws buffer.
  const auto drawMeshes = [&](bool castersOnly, bool switchToDoubleSided) {
    const DrawPushConstants push{.list = frameSlot.meshDraws.deviceAddress()};
    vkCmdPushConstants(commands, pipelineLayout_.get(), kPushStages, 0, sizeof(push), &push);
    VkBuffer bound = VK_NULL_HANDLE;
    for (u32 i = 0; i < frameSlot.meshCommands.size(); ++i) {
      const MeshCommand& draw = frameSlot.meshCommands[i];
      if (switchToDoubleSided && i == frameSlot.singleSidedCount) {  // same layout: sets and push stay bound
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, doubleSidedPipeline_.get());
      }
      if (castersOnly && !draw.castsShadow) continue;
      if (draw.vertices != bound) {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commands, 0, 1, &draw.vertices, &offset);
        vkCmdBindIndexBuffer(commands, draw.indices, 0, VK_INDEX_TYPE_UINT32);
        bound = draw.vertices;
      }
      vkCmdDrawIndexed(commands, draw.indexCount, 1, 0, 0, i);
    }
  };

  // The depth buffer: its old contents are not needed (it is cleared), so
  // UNDEFINED -> DEPTH_ATTACHMENT_OPTIMAL. It is shared by both frames in
  // flight, so the source side waits for the previous frame's depth tests
  // and writes to finish before this frame clears it.
  transitionImage(commands, depth_.handle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                  kDepthStages, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, kDepthStages,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                  VK_IMAGE_ASPECT_DEPTH_BIT);

  if (frameSlot.useHdr) {
    // 1. THE SHADOW MAP: every shadow-casting mesh, as the sun sees it.
    if (frameSlot.drawShadows) {
      shadowMap_->record(commands, frameSlot.shadows, [&](VkCommandBuffer) {
        bindSceneSets();
        drawMeshes(/*castersOnly=*/true, /*switchToDoubleSided=*/false);
      });
    }

    // 2. THE SCENE, into the HDR target: meshes, then the sky where no mesh
    // drew. The previous frame's composite may still be reading the target.
    const VkImage hdr = post_->hdr().handle();
    transitionImage(commands, hdr, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    const VkRenderingAttachmentInfo hdrColour{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = post_->hdr().view(),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {.float32 = {desc_.clearColour[0], desc_.clearColour[1], desc_.clearColour[2],
                                             desc_.clearColour[3]}}},  // linear: the composite passes it through
    };
    const VkRenderingAttachmentInfo sceneDepth{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth_.view(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,  // kept: the sprites test against it after the composite
        .clearValue = {.depthStencil = {.depth = 0.0f, .stencil = 0}},  // reverse-Z: 0 is the far plane
    };
    const VkRenderingInfo sceneRendering{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                         .renderArea = {.offset = {0, 0}, .extent = extent},
                                         .layerCount = 1,
                                         .colorAttachmentCount = 1,
                                         .pColorAttachments = &hdrColour,
                                         .pDepthAttachment = &sceneDepth};
    vkCmdBeginRendering(commands, &sceneRendering);
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    bindSceneSets();
    if (!frameSlot.meshCommands.empty()) {
      vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline_.get());
      drawMeshes(/*castersOnly=*/false, /*switchToDoubleSided=*/true);
    }
    if (frameSlot.drawSky) {
      vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_.get());
      vkCmdDraw(commands, 3, 1, 0, 0);
    }
    vkCmdEndRendering(commands);
    transitionImage(commands, hdr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    // The scene's depth stays for the sprites: same layout, but its writes
    // must land before their depth tests.
    transitionImage(commands, depth_.handle(), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, kDepthStages,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, kDepthStages,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT);

    // 3. BLOOM: the bright parts, down the mip chain and back up.
    if (frameSlot.bloom.enabled) post_->recordBloom(commands, frameSlot.bloom);
  }

  // 4. THE SWAPCHAIN IMAGE: the composite (HDR + bloom -> exposure -> tone
  // map -> sRGB), then the sprites, untouched by tone mapping, against the
  // scene's depth. UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL: its old contents
  // do not matter. The source stage is the one the acquire semaphore waits
  // at, which chains the two together.
  transitionImage(commands, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
  const VkRenderingAttachmentInfo colour{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = swapchain_->imageView(imageIndex),
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = frameSlot.useHdr ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR,  // the composite covers it
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {.color = {.float32 = {swapchainClear_[0], swapchainClear_[1], swapchainClear_[2],
                                           swapchainClear_[3]}}},
  };
  const VkRenderingAttachmentInfo depthAttachment{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = depth_.view(),
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      .loadOp = frameSlot.useHdr ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,  // not needed after the frame
      .clearValue = {.depthStencil = {.depth = 0.0f, .stencil = 0}},
  };
  const VkRenderingInfo rendering{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.offset = {0, 0}, .extent = extent},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &colour,
      .pDepthAttachment = &depthAttachment,
  };
  vkCmdBeginRendering(commands, &rendering);
  vkCmdSetViewport(commands, 0, 1, &viewport);
  vkCmdSetScissor(commands, 0, 1, &scissor);
  if (frameSlot.useHdr) post_->recordComposite(commands, frameSlot.exposure, frameSlot.toneMap, frameSlot.bloom);

  // SPRITES: at most TWO draws for every sprite: 6 vertices (two
  // triangles) per instance, one instance per sprite; opaque instances
  // first, then the translucent ones (firstInstance = opaqueCount, and
  // gl_InstanceIndex includes it). The shader reads instance i's data from
  // the buffer at `sprites`.
  if (frameSlot.opaqueCount + frameSlot.blendedCount > 0) {
    bindSceneSets();  // the composite bound its own layout's set
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, opaquePipeline_.get());
    const DrawPushConstants push{.list = sprites};
    vkCmdPushConstants(commands, pipelineLayout_.get(), kPushStages, 0, sizeof(push), &push);
    if (frameSlot.opaqueCount > 0) vkCmdDraw(commands, 6, frameSlot.opaqueCount, 0, 0);
    if (frameSlot.blendedCount > 0) {
      vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, blendedPipeline_.get());
      vkCmdDraw(commands, 6, frameSlot.blendedCount, 0, frameSlot.opaqueCount);
    }
  }

  // THE DEVELOPER UI, last, over everything: after tone mapping, so its
  // colours are exactly ImGui's.
  if (frameSlot.drawUi) (void)ui_->record(commands);

  vkCmdEndRendering(commands);

  if (frameSlot.recordCapture) {
    // Frame capture: copy the finished image into the readback buffer
    // before it is presented.
    transitionImage(commands, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkBufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,  // tightly packed
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {extent.width, extent.height, 1},
    };
    vkCmdCopyImageToBuffer(commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, frameSlot.capture.handle(), 1, &region);
    // Make the copy visible to the CPU once the frame's fence has signalled.
    const VkMemoryBarrier2 toHost{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
    };
    const VkDependencyInfo hostDependency{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1,
                                          .pMemoryBarriers = &toHost};
    vkCmdPipelineBarrier2(commands, &hostDependency);
    transitionImage(commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_NONE,
                    VK_ACCESS_2_NONE);
  } else {
    // COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR, after the colour writes.
    // Nothing on this queue reads it afterwards; present waits on the
    // renderFinished semaphore, not on a pipeline stage.
    transitionImage(commands, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
  }

  return gpu::check(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
}

}  // namespace playground::render
