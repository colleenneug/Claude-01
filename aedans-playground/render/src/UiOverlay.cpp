#include "UiOverlay.h"

#include "playground/core/Log.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>

namespace playground::render {
namespace {

using log::Category;

void checkResult(VkResult result) {
  if (result != VK_SUCCESS) log::error(Category::Render, "ImGui's Vulkan backend: VkResult {}", static_cast<int>(result));
}

bool isSrgb(VkFormat format) {
  return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}

// ImGui's colours are sRGB values meant to be written as they are. An _SRGB
// swapchain encodes whatever it is given, so they would come out too light:
// decode them once, here, and the encode restores them.
void styleToLinear() {
  ImGuiStyle& style = ImGui::GetStyle();
  for (ImVec4& c : style.Colors) {
    auto decode = [](f32 v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    c = ImVec4(decode(c.x), decode(c.y), decode(c.z), c.w);
  }
}

}  // namespace

Result<std::unique_ptr<UiOverlay>> UiOverlay::create(const gpu::Context& context, VkFormat colourFormat,
                                                     VkFormat depthFormat, u32 imageCount) {
  if (ImGui::GetCurrentContext() == nullptr) {
    return fail(Errc::InvalidState, "UI overlay: no ImGui context (engine::DebugUi creates it first)");
  }
  std::unique_ptr<UiOverlay> overlay(new UiOverlay(colourFormat, depthFormat));
  ImGui_ImplVulkan_InitInfo info{};
  info.ApiVersion = VK_API_VERSION_1_3;
  info.Instance = context.instance();
  info.PhysicalDevice = context.physicalDevice().handle;
  info.Device = context.device().handle();
  info.QueueFamily = context.device().queueFamilies().graphics;
  info.Queue = context.device().graphicsQueue();
  // ImGui's own descriptor pool (created here, freed by Shutdown): the font
  // texture and room for a few images in panels. Must be over the backend's
  // minimum (it asserts > IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE).
  info.DescriptorPoolSize = 16;
  info.MinImageCount = std::max(imageCount, 2u);
  info.ImageCount = std::max(imageCount, 2u);
  info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  info.UseDynamicRendering = true;
  info.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                      .colorAttachmentCount = 1,
                                      .pColorAttachmentFormats = &overlay->colourFormat_,
                                      .depthAttachmentFormat = overlay->depthFormat_};  // the pass has one; unused
  info.CheckVkResultFn = checkResult;
  if (!ImGui_ImplVulkan_Init(&info)) return fail(Errc::GpuFailure, "UI overlay: ImGui_ImplVulkan_Init failed");
  if (isSrgb(colourFormat)) styleToLinear();
  log::info(Category::Render, "UI overlay ready (Dear ImGui {}, {} swapchain)", IMGUI_VERSION,
            isSrgb(colourFormat) ? "sRGB" : "UNORM");
  return overlay;
}

UiOverlay::~UiOverlay() { ImGui_ImplVulkan_Shutdown(); }

void UiOverlay::setImageCount(u32 imageCount) { ImGui_ImplVulkan_SetMinImageCount(std::max(imageCount, 2u)); }

u32 UiOverlay::pendingDrawLists() noexcept {
  const ImDrawData* data = ImGui::GetCurrentContext() != nullptr ? ImGui::GetDrawData() : nullptr;
  return data != nullptr && data->Valid ? static_cast<u32>(data->CmdListsCount) : 0u;
}

u32 UiOverlay::record(VkCommandBuffer commands) const {
  ImDrawData* data = ImGui::GetDrawData();
  if (data == nullptr || !data->Valid || data->CmdListsCount == 0) return 0;
  ImGui_ImplVulkan_RenderDrawData(data, commands);
  return static_cast<u32>(data->CmdListsCount);
}

}  // namespace playground::render
