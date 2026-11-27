#include "IblBaker.h"

#include "playground/core/Log.h"
#include "playground/gpu/Buffer.h"
#include "playground/gpu/Swapchain.h"  // describe(VkFormat)
#include "playground/gpu/VulkanCheck.h"

#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <format>
#include <span>
#include <utility>
#include <vector>

#include <glm/gtc/packing.hpp>
#include <glm/vec4.hpp>

namespace playground::render {
namespace {

using log::Category;

constexpr u32 kBakeSkySpirv[] = {
#include "bake_sky.comp.spv.inc"
};
constexpr u32 kBakePanoramaSpirv[] = {
#include "bake_panorama.comp.spv.inc"
};
constexpr u32 kBakeIrradianceSpirv[] = {
#include "bake_irradiance.comp.spv.inc"
};
constexpr u32 kBakeSpecularSpirv[] = {
#include "bake_specular.comp.spv.inc"
};

constexpr VkFormat kCubeFormat = VK_FORMAT_R16G16B16A16_SFLOAT;  // HDR: the sky can be brighter than 1
constexpr u32 kGroupSize = 8;                                     // local_size_x/y in bake_common.glsl

// Must match the push_constant block in bake_common.glsl.
struct BakePush {
  glm::vec4 zenith{0.0f};
  glm::vec4 horizon{0.0f};
  glm::vec4 ground{0.0f};
  glm::vec4 sunDirection{0.0f};
  glm::vec4 glow{0.0f};
  glm::vec4 params{0.0f};  // roughness, sample count, source size, output size
};
static_assert(sizeof(BakePush) == 96, "push constants: at most 128 bytes are guaranteed");

Result<gpu::Pipeline> computePipeline(VkDevice device, VkPipelineLayout layout, std::span<const u32> spirv,
                                      const char* name) {
  const VkShaderModuleCreateInfo moduleInfo{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spirv.size_bytes(),
      .pCode = spirv.data(),
  };
  VkShaderModule module = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateShaderModule(device, &moduleInfo, nullptr, &module), "vkCreateShaderModule"); !s) {
    s.error().message += std::format(" ({})", name);
    return tl::unexpected(std::move(s.error()));
  }
  const gpu::ShaderModule owned(device, module);
  const VkComputePipelineCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = module,
                .pName = "main"},
      .layout = layout,
  };
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                            "vkCreateComputePipelines");
      !s) {
    s.error().message += std::format(" ({})", name);
    return tl::unexpected(std::move(s.error()));
  }
  return gpu::Pipeline(device, pipeline);
}

bool isPowerOfTwo(u32 v) noexcept { return v != 0 && (v & (v - 1)) == 0; }

VkImageSubresourceRange cubeLevels(u32 base, u32 count) noexcept {
  return {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = base, .levelCount = count, .baseArrayLayer = 0,
          .layerCount = 6};
}

u32 groups(u32 size) noexcept { return (size + kGroupSize - 1) / kGroupSize; }

}  // namespace

Status validate(const EnvironmentQuality& q) {
  for (const auto& [value, name] : {std::pair{q.cubeSize, "cubeSize"}, std::pair{q.specularSize, "specularSize"},
                                    std::pair{q.irradianceSize, "irradianceSize"}}) {
    if (!isPowerOfTwo(value) || value < 4 || value > 2048) {
      return fail(Errc::InvalidArgument, "environment {} must be a power of two from 4 to 2048, got {}", name, value);
    }
  }
  if (q.samples < 1 || q.samples > 4096) {
    return fail(Errc::InvalidArgument, "environment samples must be 1 to 4096, got {}", q.samples);
  }
  return {};
}

// --- Creation ----------------------------------------------------------------------

Result<IblBaker> IblBaker::create(const gpu::Context& context) {
  const VkDevice device = context.device().handle();

  // Half-float cubes are written by compute shaders, blitted for mips and
  // sampled with filtering. All of that is mandatory in Vulkan for this
  // format, but a broken driver is better caught here with a clear message.
  VkFormatProperties properties{};
  vkGetPhysicalDeviceFormatProperties(context.physicalDevice().handle, kCubeFormat, &properties);
  constexpr VkFormatFeatureFlags kNeeded = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                           VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                           VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  if ((properties.optimalTilingFeatures & kNeeded) != kNeeded) {
    return fail(Errc::Unsupported, "this GPU cannot store, blit and filter {} images, which environment lighting needs",
                gpu::describe(kCubeFormat));
  }

  IblBaker baker(context);
  auto sampler = gpu::createSampler(context, {.addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                              .maxAnisotropy = 1.0f,
                                              .debugName = "environment maps"});
  if (!sampler) return tl::unexpected(std::move(sampler.error()));
  baker.sampler_ = std::move(*sampler);

  // One layout for every bake shader: binding 0 the input (a panorama or the
  // environment cube; the sky shader reads none), binding 1 the output faces.
  const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
      {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
      {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1,
       .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
  }};
  const VkDescriptorSetLayoutCreateInfo setInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = static_cast<u32>(bindings.size()),
      .pBindings = bindings.data(),
  };
  VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout),
                            "vkCreateDescriptorSetLayout (environment bake)");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  baker.setLayout_ = gpu::DescriptorSetLayout(device, setLayout);

  const VkPushConstantRange push{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(BakePush)};
  const VkPipelineLayoutCreateInfo layoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &setLayout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push,
  };
  VkPipelineLayout layout = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout),
                            "vkCreatePipelineLayout (environment bake)");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  baker.layout_ = gpu::PipelineLayout(device, layout);

  auto sky = computePipeline(device, layout, kBakeSkySpirv, "bake_sky.comp");
  if (!sky) return tl::unexpected(std::move(sky.error()));
  auto panorama = computePipeline(device, layout, kBakePanoramaSpirv, "bake_panorama.comp");
  if (!panorama) return tl::unexpected(std::move(panorama.error()));
  auto irradiance = computePipeline(device, layout, kBakeIrradianceSpirv, "bake_irradiance.comp");
  if (!irradiance) return tl::unexpected(std::move(irradiance.error()));
  auto specular = computePipeline(device, layout, kBakeSpecularSpirv, "bake_specular.comp");
  if (!specular) return tl::unexpected(std::move(specular.error()));
  baker.skyPipeline_ = std::move(*sky);
  baker.panoramaPipeline_ = std::move(*panorama);
  baker.irradiancePipeline_ = std::move(*irradiance);
  baker.specularPipeline_ = std::move(*specular);
  return baker;
}

// --- Baking --------------------------------------------------------------------------

struct IblBaker::Source {
  VkPipeline pipeline = VK_NULL_HANDLE;
  BakePush push;                          // the sky's colours, or zeros
  VkImageView panorama = VK_NULL_HANDLE;  // the panorama, if any
  u32 panoramaWidth = 0;
  const char* what = "";
};

Result<EnvironmentMaps> IblBaker::bake(const ProceduralSky& sky, const EnvironmentQuality& quality) {
  Source source{.pipeline = skyPipeline_.get(), .what = "procedural sky"};
  source.push.zenith = glm::vec4(sky.zenith, 0.0f);
  source.push.horizon = glm::vec4(sky.horizon, 0.0f);
  source.push.ground = glm::vec4(sky.ground, 0.0f);
  source.push.sunDirection = glm::vec4(sky.sunDirection, 0.0f);
  source.push.glow = glm::vec4(sky.glowColour, sky.glowStrength);
  return run(source, quality);
}

Result<EnvironmentMaps> IblBaker::bake(const HdrImage& panorama, const EnvironmentQuality& quality) {
  const u64 expected = u64{panorama.width} * panorama.height * 4;
  if (panorama.width == 0 || panorama.height == 0 || panorama.rgba.size() != expected) {
    return fail(Errc::InvalidArgument, "HDR panorama {}x{} needs {} floats, got {}", panorama.width, panorama.height,
                expected, panorama.rgba.size());
  }
  // Half floats on the GPU: enough range (65504) and precision for light,
  // and filterable everywhere (32-bit float filtering is optional).
  std::vector<u16> halves(panorama.rgba.size());
  for (usize i = 0; i < halves.size(); ++i) {
    const f32 v = std::min(panorama.rgba[i], 65000.0f);  // beyond half range: clamp, not infinity
    halves[i] = glm::packHalf1x16(v >= 0.0f ? v : 0.0f);
  }
  auto texture = gpu::createTexture(*context_, {.width = panorama.width,
                                                .height = panorama.height,
                                                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                                .pixels = std::as_bytes(std::span(halves)),
                                                .generateMips = true,
                                                .debugName = "environment panorama"});
  if (!texture) return tl::unexpected(std::move(texture.error()));
  Source source{.pipeline = panoramaPipeline_.get(), .panorama = texture->view(), .panoramaWidth = panorama.width,
                .what = "HDR panorama"};
  return run(source, quality);  // `texture` lives until the bake has finished
}

Result<EnvironmentMaps> IblBaker::run(const Source& source, const EnvironmentQuality& quality) {
  if (Status s = validate(quality); !s) return tl::unexpected(std::move(s.error()));
  const auto started = std::chrono::steady_clock::now();
  const gpu::Context& context = *context_;
  const VkDevice device = context.device().handle();
  const auto cube = [&](u32 size, u32 mips, VkImageUsageFlags usage, const char* name) {
    return gpu::Image::create(context.allocator(), {.width = size, .height = size, .format = kCubeFormat,
                                                    .usage = usage | VK_IMAGE_USAGE_STORAGE_BIT |
                                                             VK_IMAGE_USAGE_SAMPLED_BIT,
                                                    .mipLevels = mips, .layers = 6, .cube = true, .debugName = name});
  };

  EnvironmentMaps maps;
  const u32 environmentMips = gpu::fullMipCount(quality.cubeSize, quality.cubeSize);
  // Specular: stop at 4x4 texels; the last level is roughness 1.
  const u32 specularMips = std::max(1u, gpu::fullMipCount(quality.specularSize, quality.specularSize) - 2);
  {
    auto environment = cube(quality.cubeSize, environmentMips,
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "environment");
    if (!environment) return tl::unexpected(std::move(environment.error()));
    auto irradiance = cube(quality.irradianceSize, 1, 0, "environment irradiance");
    if (!irradiance) return tl::unexpected(std::move(irradiance.error()));
    auto specular = cube(quality.specularSize, specularMips, 0, "environment specular");
    if (!specular) return tl::unexpected(std::move(specular.error()));
    maps.environment = std::move(*environment);
    maps.irradiance = std::move(*irradiance);
    maps.specular = std::move(*specular);
  }

  // Views for the compute shaders: each written mip as a 2D array of 6 faces.
  std::vector<gpu::ImageView> views;
  const auto storageView = [&](const gpu::Image& image, u32 mip) -> Result<VkImageView> {
    auto view = gpu::createImageView(device, image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, mip, 1, 0, 6);
    if (!view) return tl::unexpected(std::move(view.error()));
    views.push_back(std::move(*view));
    return views.back().get();
  };

  // One descriptor set per dispatch, from a pool that lives for this bake.
  const u32 dispatches = 2 + specularMips;
  const std::array<VkDescriptorPoolSize, 2> poolSizes{{
      {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = dispatches},
      {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = dispatches},
  }};
  const VkDescriptorPoolCreateInfo poolInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = dispatches,
      .poolSizeCount = static_cast<u32>(poolSizes.size()),
      .pPoolSizes = poolSizes.data(),
  };
  VkDescriptorPool rawPool = VK_NULL_HANDLE;
  if (Status s = gpu::check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &rawPool),
                            "vkCreateDescriptorPool (environment bake)");
      !s) {
    return tl::unexpected(std::move(s.error()));
  }
  const gpu::DescriptorPool pool(device, rawPool);
  const auto makeSet = [&](VkImageView input, VkImageView output) -> Result<VkDescriptorSet> {
    const VkDescriptorSetLayout layout = setLayout_.get();
    const VkDescriptorSetAllocateInfo allocInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                .descriptorPool = rawPool, .descriptorSetCount = 1,
                                                .pSetLayouts = &layout};
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (Status s = gpu::check(vkAllocateDescriptorSets(device, &allocInfo, &set), "vkAllocateDescriptorSets (bake)");
        !s) {
      return tl::unexpected(std::move(s.error()));
    }
    const VkDescriptorImageInfo inputInfo{.sampler = sampler_.get(), .imageView = input,
                                          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo outputInfo{.imageView = output, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
    std::array<VkWriteDescriptorSet, 2> writes{{
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &inputInfo},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &outputInfo},
    }};
    // The sky shader reads no input: leave binding 0 unwritten (it is never used).
    const bool hasInput = input != VK_NULL_HANDLE;
    vkUpdateDescriptorSets(device, hasInput ? 2 : 1, hasInput ? writes.data() : writes.data() + 1, 0, nullptr);
    return set;
  };

  auto environmentOut = storageView(maps.environment, 0);
  if (!environmentOut) return tl::unexpected(std::move(environmentOut.error()));
  auto environmentSet = makeSet(source.panorama, *environmentOut);
  if (!environmentSet) return tl::unexpected(std::move(environmentSet.error()));
  auto irradianceOut = storageView(maps.irradiance, 0);
  if (!irradianceOut) return tl::unexpected(std::move(irradianceOut.error()));
  auto irradianceSet = makeSet(maps.environment.view(), *irradianceOut);
  if (!irradianceSet) return tl::unexpected(std::move(irradianceSet.error()));
  std::vector<VkDescriptorSet> specularSets;
  for (u32 mip = 0; mip < specularMips; ++mip) {
    auto out = storageView(maps.specular, mip);
    if (!out) return tl::unexpected(std::move(out.error()));
    auto set = makeSet(maps.environment.view(), *out);
    if (!set) return tl::unexpected(std::move(set.error()));
    specularSets.push_back(*set);
  }

  const VkPipelineLayout layout = layout_.get();
  Status baked = gpu::submitAndWait(context, [&](VkCommandBuffer commands) {
    const auto dispatch = [&](VkPipeline pipeline, VkDescriptorSet set, const BakePush& push, u32 size) {
      vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
      vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
      vkCmdPushConstants(commands, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
      vkCmdDispatch(commands, groups(size), groups(size), 6);
    };
    const VkImage environment = maps.environment.handle();

    // 1. The environment cube: mip 0 by compute, the rest by blits.
    gpu::transitionImage(commands, environment, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, cubeLevels(0, 1));
    BakePush push = source.push;
    push.params = {0.0f, 0.0f, static_cast<f32>(source.panoramaWidth), static_cast<f32>(quality.cubeSize)};
    dispatch(source.pipeline, *environmentSet, push, quality.cubeSize);
    gpu::transitionImage(commands, environment, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, cubeLevels(0, 1));
    if (environmentMips > 1) {
      gpu::transitionImage(commands, environment, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_BLIT_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT, cubeLevels(1, environmentMips - 1));
    }
    for (u32 mip = 1; mip < environmentMips; ++mip) {
      const auto size = [&](u32 level) { return static_cast<i32>(std::max(1u, quality.cubeSize >> level)); };
      const VkImageBlit blit{
          .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = mip - 1, .baseArrayLayer = 0,
                             .layerCount = 6},
          .srcOffsets = {{0, 0, 0}, {size(mip - 1), size(mip - 1), 1}},
          .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = mip, .baseArrayLayer = 0,
                             .layerCount = 6},
          .dstOffsets = {{0, 0, 0}, {size(mip), size(mip), 1}},
      };
      vkCmdBlitImage(commands, environment, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, environment,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
      gpu::transitionImage(commands, environment, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                           cubeLevels(mip, 1));
    }
    gpu::transitionImage(commands, environment, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                         VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                                                             VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, cubeLevels(0, environmentMips));

    // 2. Irradiance.
    gpu::transitionImage(commands, maps.irradiance.handle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, cubeLevels(0, 1));
    push.params = {0.0f, static_cast<f32>(quality.samples * 2), static_cast<f32>(quality.cubeSize),
                   static_cast<f32>(quality.irradianceSize)};
    dispatch(irradiancePipeline_.get(), *irradianceSet, push, quality.irradianceSize);
    gpu::transitionImage(commands, maps.irradiance.handle(), VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, cubeLevels(0, 1));

    // 3. Specular, one roughness per mip.
    gpu::transitionImage(commands, maps.specular.handle(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                         VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, cubeLevels(0, specularMips));
    for (u32 mip = 0; mip < specularMips; ++mip) {
      const f32 roughness = specularMips > 1 ? static_cast<f32>(mip) / static_cast<f32>(specularMips - 1) : 0.0f;
      const u32 size = std::max(1u, quality.specularSize >> mip);
      push.params = {roughness, static_cast<f32>(quality.samples), static_cast<f32>(quality.cubeSize),
                     static_cast<f32>(size)};
      dispatch(specularPipeline_.get(), specularSets[mip], push, size);
    }
    gpu::transitionImage(commands, maps.specular.handle(), VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                         VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, cubeLevels(0, specularMips));
  });
  if (!baked) {
    baked.error().message += std::format(" (baking the environment from a {})", source.what);
    return tl::unexpected(std::move(baked.error()));
  }
  const auto ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - started).count();
  log::info(Category::Render, "environment baked from a {}: cube {}, irradiance {}, specular {} ({} roughness levels), "
            "{} samples, {:.0f} ms", source.what, quality.cubeSize, quality.irradianceSize, quality.specularSize,
            specularMips, quality.samples, ms);
  return maps;  // views, sets and pool go now: the GPU has finished with them
}

}  // namespace playground::render
