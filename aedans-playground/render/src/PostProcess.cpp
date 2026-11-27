#include "PostProcess.h"

#include "playground/core/Log.h"
#include "playground/gpu/Buffer.h"
#include "playground/gpu/VulkanCheck.h"

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <span>
#include <utility>

#include <glm/vec4.hpp>

namespace playground::render {
namespace {

using log::Category;

constexpr u32 kFullscreenVertSpirv[] = {
#include "fullscreen.vert.spv.inc"
};
constexpr u32 kBloomFragSpirv[] = {
#include "bloom.frag.spv.inc"
};
constexpr u32 kTonemapFragSpirv[] = {
#include "tonemap.frag.spv.inc"
};

// Must match the push_constant block in post_common.glsl.
struct PostPush {
  glm::vec4 a{0.0f};
  glm::vec4 b{0.0f};
};
static_assert(sizeof(PostPush) == 32);

bool isSrgb(VkFormat format) noexcept {
  return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
}

Result<gpu::ShaderModule> shaderModule(VkDevice device, std::span<const u32> spirv, const char* name) {
  const VkShaderModuleCreateInfo info{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                      .codeSize = spirv.size_bytes(), .pCode = spirv.data()};
  VkShaderModule module = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule"); !s) {
    s.error().message += std::format(" ({})", name);
    return tl::unexpected(std::move(s.error()));
  }
  return gpu::ShaderModule(device, module);
}

struct PostPipelineSpec {
  std::span<const u32> fragment;
  const char* name;
  VkFormat colourFormat;
  VkFormat depthFormat;  // the rendering's depth attachment (composite), or UNDEFINED
  bool additive;         // blend ONE + ONE onto what is there
  bool encodeSrgb;       // specialization constant 0
};

// A full-screen pass: fullscreen.vert + a fragment shader, no vertex input,
// no depth test, dynamic viewport and scissor.
Result<gpu::Pipeline> postPipeline(VkDevice device, VkPipelineLayout layout, const PostPipelineSpec& spec) {
  auto vertex = shaderModule(device, kFullscreenVertSpirv, "fullscreen.vert");
  if (!vertex) return tl::unexpected(std::move(vertex.error()));
  auto fragment = shaderModule(device, spec.fragment, spec.name);
  if (!fragment) return tl::unexpected(std::move(fragment.error()));

  const VkBool32 encode = spec.encodeSrgb ? VK_TRUE : VK_FALSE;
  const VkSpecializationMapEntry entry{.constantID = 0, .offset = 0, .size = sizeof(VkBool32)};
  const VkSpecializationInfo specialization{.mapEntryCount = 1, .pMapEntries = &entry, .dataSize = sizeof(encode),
                                            .pData = &encode};
  const std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vertex->get(), .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = fragment->get(), .pName = "main", .pSpecializationInfo = &specialization},
  }};
  const VkPipelineVertexInputStateCreateInfo vertexInput{.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
  const VkPipelineViewportStateCreateInfo viewport{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                   .viewportCount = 1, .scissorCount = 1};
  const VkPipelineRasterizationStateCreateInfo rasterization{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f};
  const VkPipelineMultisampleStateCreateInfo multisample{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
  const VkPipelineDepthStencilStateCreateInfo depth{.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  const VkPipelineColorBlendAttachmentState blend{
      .blendEnable = spec.additive ? VK_TRUE : VK_FALSE,
      .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
      .colorBlendOp = VK_BLEND_OP_ADD,
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
      .alphaBlendOp = VK_BLEND_OP_ADD,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                        VK_COLOR_COMPONENT_A_BIT,
  };
  const VkPipelineColorBlendStateCreateInfo colourBlend{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &blend};
  const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                 .dynamicStateCount = static_cast<u32>(dynamicStates.size()),
                                                 .pDynamicStates = dynamicStates.data()};
  const VkPipelineRenderingCreateInfo rendering{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                                .colorAttachmentCount = 1,
                                                .pColorAttachmentFormats = &spec.colourFormat,
                                                .depthAttachmentFormat = spec.depthFormat};
  const VkGraphicsPipelineCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering,
      .stageCount = static_cast<u32>(stages.size()),
      .pStages = stages.data(),
      .pVertexInputState = &vertexInput,
      .pInputAssemblyState = &inputAssembly,
      .pViewportState = &viewport,
      .pRasterizationState = &rasterization,
      .pMultisampleState = &multisample,
      .pDepthStencilState = &depth,
      .pColorBlendState = &colourBlend,
      .pDynamicState = &dynamic,
      .layout = layout,
  };
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                            "vkCreateGraphicsPipelines");
      !s) {
    s.error().message += std::format(" ({} pipeline)", spec.name);
    return tl::unexpected(std::move(s.error()));
  }
  return gpu::Pipeline(device, pipeline);
}

VkImageSubresourceRange level(u32 mip) noexcept {
  return {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = mip, .levelCount = 1, .baseArrayLayer = 0,
          .layerCount = 1};
}

}  // namespace

Result<PostProcess> PostProcess::create(const gpu::Context& context) {
  const VkDevice device = context.device().handle();
  PostProcess post(context);

  auto sampler = gpu::createSampler(context, {.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                              .addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                              .maxAnisotropy = 1.0f, .debugName = "post-processing"});
  if (!sampler) return tl::unexpected(std::move(sampler.error()));
  post.sampler_ = std::move(*sampler);

  const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
      {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
      {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT},
  }};
  const VkDescriptorSetLayoutCreateInfo setInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                                .bindingCount = static_cast<u32>(bindings.size()),
                                                .pBindings = bindings.data()};
  VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout),
                            "vkCreateDescriptorSetLayout (post-processing)");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  post.setLayout_ = gpu::DescriptorSetLayout(device, setLayout);
  const VkPushConstantRange push{.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(PostPush)};
  const VkPipelineLayoutCreateInfo layoutInfo{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                              .setLayoutCount = 1, .pSetLayouts = &setLayout,
                                              .pushConstantRangeCount = 1, .pPushConstantRanges = &push};
  VkPipelineLayout layout = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout),
                            "vkCreatePipelineLayout (post-processing)");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  post.layout_ = gpu::PipelineLayout(device, layout);

  auto down = postPipeline(device, layout, {.fragment = kBloomFragSpirv, .name = "bloom downsample",
                                            .colourFormat = kHdrFormat, .depthFormat = VK_FORMAT_UNDEFINED,
                                            .additive = false, .encodeSrgb = false});
  if (!down) return tl::unexpected(std::move(down.error()));
  auto up = postPipeline(device, layout, {.fragment = kBloomFragSpirv, .name = "bloom upsample",
                                          .colourFormat = kHdrFormat, .depthFormat = VK_FORMAT_UNDEFINED,
                                          .additive = true, .encodeSrgb = false});
  if (!up) return tl::unexpected(std::move(up.error()));
  post.downsample_ = std::move(*down);
  post.upsample_ = std::move(*up);
  return post;
}

Status PostProcess::buildComposite(VkFormat swapchainFormat) {
  // The composite runs inside the swapchain's rendering, which also has the
  // depth buffer (for the sprites drawn after it), so it names both formats.
  auto composite = postPipeline(context_->device().handle(), layout_.get(),
                                {.fragment = kTonemapFragSpirv, .name = "composite", .colourFormat = swapchainFormat,
                                 .depthFormat = gpu::kDepthFormat, .additive = false,
                                 .encodeSrgb = !isSrgb(swapchainFormat)});
  if (!composite) return tl::unexpected(std::move(composite.error()));
  composite_ = std::move(*composite);
  compositeFormat_ = swapchainFormat;
  log::debug(Category::Render, "composite pipeline for {}: {}", static_cast<int>(swapchainFormat),
             isSrgb(swapchainFormat) ? "the swapchain encodes sRGB" : "the shader encodes sRGB");
  return {};
}

Result<VkDescriptorSet> PostProcess::makeSet(VkImageView source, VkImageView bloom) {
  const VkDevice device = context_->device().handle();
  const VkDescriptorSetLayout layout = setLayout_.get();
  const VkDescriptorSetAllocateInfo allocInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                              .descriptorPool = pool_.get(), .descriptorSetCount = 1,
                                              .pSetLayouts = &layout};
  VkDescriptorSet set = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkAllocateDescriptorSets(device, &allocInfo, &set), "vkAllocateDescriptorSets (post)"); !s) {
    return tl::unexpected(std::move(s.error()));
  }
  const std::array<VkDescriptorImageInfo, 2> images{{
      {.sampler = sampler_.get(), .imageView = source, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
      {.sampler = sampler_.get(), .imageView = bloom, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
  }};
  const VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
                                   .descriptorCount = 2, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   .pImageInfo = images.data()};
  vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  return set;
}

Status PostProcess::resize(VkExtent2D extent) {
  const VkDevice device = context_->device().handle();
  // Old first, in dependency order: sets (with their pool), views, images.
  downSets_.clear();
  upSets_.clear();
  compositeSet_ = VK_NULL_HANDLE;
  pool_ = {};
  bloomViews_.clear();
  bloom_ = {};
  hdr_ = {};

  auto hdr = gpu::Image::create(context_->allocator(), {.width = extent.width, .height = extent.height,
                                                        .format = kHdrFormat,
                                                        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                                 VK_IMAGE_USAGE_SAMPLED_BIT,
                                                        .debugName = "HDR scene"});
  if (!hdr) return tl::unexpected(std::move(hdr.error()));
  hdr_ = std::move(*hdr);

  // Bloom: half size, down to levels of at least about 4 pixels.
  const u32 width = std::max(1u, extent.width / 2);
  const u32 height = std::max(1u, extent.height / 2);
  const u32 smallest = std::min(width, height);
  // bit_width(n) - 1 = floor(log2(n)): levels until the smaller side is ~4 pixels.
  const int wanted = static_cast<int>(std::bit_width(smallest)) - 2;
  const u32 levels = static_cast<u32>(std::clamp(wanted, 1, static_cast<int>(kMaxBloomLevels)));
  auto bloom = gpu::Image::create(context_->allocator(), {.width = width, .height = height, .format = kHdrFormat,
                                                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                                   VK_IMAGE_USAGE_SAMPLED_BIT,
                                                          .mipLevels = levels, .debugName = "bloom chain"});
  if (!bloom) return tl::unexpected(std::move(bloom.error()));
  bloom_ = std::move(*bloom);
  for (u32 i = 0; i < levels; ++i) {
    auto view = gpu::createImageView(device, bloom_, VK_IMAGE_VIEW_TYPE_2D, i, 1, 0, 1);
    if (!view) return tl::unexpected(std::move(view.error()));
    bloomViews_.push_back(std::move(*view));
  }

  const u32 sets = 2 * levels;  // levels down + (levels - 1) up + 1 composite
  const VkDescriptorPoolSize size{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 2 * sets};
  const VkDescriptorPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = sets,
                                            .poolSizeCount = 1, .pPoolSizes = &size};
  VkDescriptorPool pool = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool (post)");
      !s) {
    return s;
  }
  pool_ = gpu::DescriptorPool(device, pool);
  for (u32 i = 0; i < levels; ++i) {
    const VkImageView source = i == 0 ? hdr_.view() : bloomViews_[i - 1].get();
    auto set = makeSet(source, source);  // binding 1 unused by bloom.frag; any valid view
    if (!set) return tl::unexpected(std::move(set.error()));
    downSets_.push_back(*set);
  }
  for (u32 i = 0; i + 1 < levels; ++i) {
    auto set = makeSet(bloomViews_[i + 1].get(), bloomViews_[i + 1].get());
    if (!set) return tl::unexpected(std::move(set.error()));
    upSets_.push_back(*set);
  }
  auto composite = makeSet(hdr_.view(), bloomViews_[0].get());
  if (!composite) return tl::unexpected(std::move(composite.error()));
  compositeSet_ = *composite;

  // The composite reads the bloom every frame (unused when bloom is off,
  // but its layout must still be right): start it readable.
  const VkImage bloomImage = bloom_.handle();
  if (Status s = gpu::submitAndWait(*context_, [&](VkCommandBuffer commands) {
        gpu::transitionImage(commands, bloomImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                             VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
      });
      !s) {
    return s;
  }
  log::debug(Category::Render, "HDR target {}x{}; bloom {}x{} with {} level(s)", extent.width, extent.height, width,
             height, levels);
  return {};
}

void PostProcess::recordBloom(VkCommandBuffer commands, const BloomSettings& settings) const {
  const u32 levels = bloomLevels();
  const VkExtent2D hdrSize = hdr_.extent();
  const VkExtent2D base = bloom_.extent();
  const auto sizeOf = [&](u32 mip) {
    return VkExtent2D{std::max(1u, base.width >> mip), std::max(1u, base.height >> mip)};
  };
  const auto pass = [&](u32 mip, bool up, VkDescriptorSet set, VkExtent2D source, bool first) {
    const VkExtent2D target = sizeOf(mip);
    // Reading back what was drawn into this level before (the upsample adds
    // onto it), or starting afresh (the downsample replaces it).
    gpu::transitionImage(commands, bloom_.handle(), up ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, level(mip));
    const VkRenderingAttachmentInfo colour{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = bloomViews_[mip].get(),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = up ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    const VkRenderingInfo rendering{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                    .renderArea = {.offset = {0, 0}, .extent = target},
                                    .layerCount = 1,
                                    .colorAttachmentCount = 1,
                                    .pColorAttachments = &colour};
    vkCmdBeginRendering(commands, &rendering);
    const VkViewport viewport{.x = 0.0f, .y = 0.0f, .width = static_cast<f32>(target.width),
                              .height = static_cast<f32>(target.height), .minDepth = 0.0f, .maxDepth = 1.0f};
    const VkRect2D scissor{.offset = {0, 0}, .extent = target};
    vkCmdSetViewport(commands, 0, 1, &viewport);
    vkCmdSetScissor(commands, 0, 1, &scissor);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, up ? upsample_.get() : downsample_.get());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_.get(), 0, 1, &set, 0, nullptr);
    const PostPush push{
        .a = {1.0f / static_cast<f32>(source.width), 1.0f / static_cast<f32>(source.height), settings.threshold,
              settings.knee},
        .b = {up ? 1.0f : 0.0f, first ? 1.0f : 0.0f, static_cast<f32>(target.width), static_cast<f32>(target.height)},
    };
    vkCmdPushConstants(commands, layout_.get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
    gpu::transitionImage(commands, bloom_.handle(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, level(mip));
  };

  for (u32 mip = 0; mip < levels; ++mip) {  // down: the HDR image into level 0, then each level into the next
    pass(mip, false, downSets_[mip], mip == 0 ? hdrSize : sizeOf(mip - 1), mip == 0);
  }
  for (u32 mip = levels - 1; mip-- > 0;) {  // up: each level adds the one below it, back to level 0
    pass(mip, true, upSets_[mip], sizeOf(mip + 1), false);
  }
}

void PostProcess::recordComposite(VkCommandBuffer commands, f32 exposure, ToneMap toneMap,
                                  const BloomSettings& bloom) const {
  vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, composite_.get());
  vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_.get(), 0, 1, &compositeSet_, 0, nullptr);
  const f32 mode = toneMap == ToneMap::Aces ? 1.0f : (toneMap == ToneMap::PbrNeutral ? 2.0f : 0.0f);
  const PostPush push{
      .a = {exposure, mode, std::clamp(bloom.strength, 0.0f, 1.0f), bloom.enabled ? 1.0f : 0.0f},
      .b = {1.0f / static_cast<f32>(std::max(bloomLevels(), 1u)), 0.0f, 0.0f, 0.0f},
  };
  vkCmdPushConstants(commands, layout_.get(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
  vkCmdDraw(commands, 3, 1, 0, 0);
}

}  // namespace playground::render
