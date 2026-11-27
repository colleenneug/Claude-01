#include "ShadowMap.h"

#include "playground/core/Log.h"
#include "playground/gpu/Buffer.h"
#include "playground/gpu/VulkanCheck.h"
#include "playground/render/Resources.h"

#include <array>
#include <format>
#include <utility>

namespace playground::render {
namespace {

using log::Category;

constexpr u32 kShadowVertSpirv[] = {
#include "shadow.vert.spv.inc"
};

constexpr VkPipelineStageFlags2 kDepthStages =
    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

}  // namespace

Result<ShadowMap> ShadowMap::create(const gpu::Context& context, u32 size) {
  if (size < 16 || size > 8192) return fail(Errc::InvalidArgument, "shadow map size {} (16 to 8192)", size);
  const VkDevice device = context.device().handle();
  ShadowMap shadow(context);
  shadow.size_ = size;

  auto image = gpu::Image::create(context.allocator(), {.width = size, .height = size, .format = gpu::kDepthFormat,
                                                        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                                 VK_IMAGE_USAGE_SAMPLED_BIT,
                                                        .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
                                                        .debugName = "sun shadow map"});
  if (!image) return tl::unexpected(std::move(image.error()));
  shadow.image_ = std::move(*image);

  // Hardware PCF wants linear filtering of depth. Not every GPU has it for
  // D32_SFLOAT: then nearest, and the 3x3 taps alone soften the edge.
  VkFormatProperties properties{};
  vkGetPhysicalDeviceFormatProperties(context.physicalDevice().handle, gpu::kDepthFormat, &properties);
  shadow.linear_ = (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
  const VkFilter filter = shadow.linear_ ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  const VkSamplerCreateInfo samplerInfo{
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = filter,
      .minFilter = filter,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
      .compareEnable = VK_TRUE,
      .compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,  // reverse-Z: lit when not farther than the stored depth
      .maxLod = 0.0f,
      .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,  // depth 0 beyond the edge: lit
  };
  VkSampler sampler = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateSampler(device, &samplerInfo, nullptr, &sampler), "vkCreateSampler (shadow)"); !s) {
    return tl::unexpected(std::move(s.error()));
  }
  shadow.sampler_ = gpu::DeviceHandle<VkSampler, &vkDestroySampler>(device, sampler);

  // Readable from the start: frames without shadows still bind it.
  const VkImage handle = shadow.image_.handle();
  if (Status s = gpu::submitAndWait(context, [&](VkCommandBuffer commands) {
        gpu::transitionImage(commands, handle, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                             VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                             VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
      });
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  log::info(Category::Render, "shadow map {}x{} ({} filtering)", size, size, shadow.linear_ ? "linear" : "nearest");
  return shadow;
}

Status ShadowMap::buildPipeline(VkPipelineLayout sceneLayout) {
  const VkDevice device = context_->device().handle();
  const VkShaderModuleCreateInfo moduleInfo{.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                            .codeSize = sizeof(kShadowVertSpirv), .pCode = kShadowVertSpirv};
  VkShaderModule module = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateShaderModule(device, &moduleInfo, nullptr, &module), "vkCreateShaderModule (shadow)");
      !s) {
    return s;
  }
  const gpu::ShaderModule owned(device, module);
  const VkPipelineShaderStageCreateInfo stage{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                             .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = module, .pName = "main"};
  // Positions only: the first 12 bytes of each 48-byte Vertex.
  const VkVertexInputBindingDescription binding{.binding = 0, .stride = sizeof(Vertex),
                                                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
  const VkVertexInputAttributeDescription position{.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT,
                                                   .offset = offsetof(Vertex, position)};
  const VkPipelineVertexInputStateCreateInfo vertexInput{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions = &binding, .vertexAttributeDescriptionCount = 1,
      .pVertexAttributeDescriptions = &position};
  const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
  const VkPipelineViewportStateCreateInfo viewport{.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                                                   .viewportCount = 1, .scissorCount = 1};
  // Both faces: double-sided and open meshes cast shadows too.
  const VkPipelineRasterizationStateCreateInfo rasterization{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .depthBiasEnable = VK_TRUE,
      .lineWidth = 1.0f};
  const VkPipelineMultisampleStateCreateInfo multisample{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
  const VkPipelineDepthStencilStateCreateInfo depth{.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                                                    .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE,
                                                    .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL};
  const VkPipelineColorBlendStateCreateInfo colourBlend{.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  const std::array<VkDynamicState, 3> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                    VK_DYNAMIC_STATE_DEPTH_BIAS};
  const VkPipelineDynamicStateCreateInfo dynamic{.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                 .dynamicStateCount = static_cast<u32>(dynamicStates.size()),
                                                 .pDynamicStates = dynamicStates.data()};
  const VkPipelineRenderingCreateInfo rendering{.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                                .depthAttachmentFormat = gpu::kDepthFormat};
  const VkGraphicsPipelineCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering,
      .stageCount = 1,
      .pStages = &stage,
      .pVertexInputState = &vertexInput,
      .pInputAssemblyState = &inputAssembly,
      .pViewportState = &viewport,
      .pRasterizationState = &rasterization,
      .pMultisampleState = &multisample,
      .pDepthStencilState = &depth,
      .pColorBlendState = &colourBlend,
      .pDynamicState = &dynamic,
      .layout = sceneLayout,
  };
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                            "vkCreateGraphicsPipelines (shadow map)");
      !s) {
    return s;
  }
  pipeline_ = gpu::Pipeline(device, pipeline);
  return {};
}

void ShadowMap::record(VkCommandBuffer commands, const ShadowSettings& settings,
                       const std::function<void(VkCommandBuffer)>& drawCasters) const {
  // After the previous frame's lookups; before this frame's depth writes.
  gpu::transitionImage(commands, image_.handle(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                       VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_2_NONE, kDepthStages,
                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                       VK_IMAGE_ASPECT_DEPTH_BIT);
  const VkRenderingAttachmentInfo depth{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = image_.view(),
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {.depthStencil = {.depth = 0.0f, .stencil = 0}},  // reverse-Z: nothing in the way
  };
  const VkRenderingInfo rendering{.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                  .renderArea = {.offset = {0, 0}, .extent = {size_, size_}},
                                  .layerCount = 1,
                                  .pDepthAttachment = &depth};
  vkCmdBeginRendering(commands, &rendering);
  // Not flipped (unlike the camera's): pbr.frag maps uv = ndc * 0.5 + 0.5 straight onto rows.
  const VkViewport viewport{.x = 0.0f, .y = 0.0f, .width = static_cast<f32>(size_),
                            .height = static_cast<f32>(size_), .minDepth = 0.0f, .maxDepth = 1.0f};
  const VkRect2D scissor{.offset = {0, 0}, .extent = {size_, size_}};
  vkCmdSetViewport(commands, 0, 1, &viewport);
  vkCmdSetScissor(commands, 0, 1, &scissor);
  vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.get());
  // Reverse-Z: "away from the light" is towards 0, so the bias is negative.
  vkCmdSetDepthBias(commands, -settings.depthBias, 0.0f, -settings.slopeBias);
  drawCasters(commands);
  vkCmdEndRendering(commands);
  gpu::transitionImage(commands, image_.handle(), VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, kDepthStages,
                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
}

}  // namespace playground::render
