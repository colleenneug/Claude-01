#pragma once
// =============================================================================
//  render/src/UiOverlay.h — Dear ImGui's Vulkan side: draws the developer UI
//  on top of the finished frame.
//
//  Recorded at the very end of the swapchain pass (after the composite and
//  the sprites: after tone mapping, so the UI's colours are exactly what
//  ImGui asked for), with dynamic rendering. ImGui's own pipeline, font
//  texture and descriptor pool belong to this object: created by create(),
//  destroyed by the destructor (ImGui_ImplVulkan_Shutdown), which the
//  renderer runs only once the GPU is idle. A rebuilt swapchain only needs
//  setImageCount(); a new colour format needs a new UiOverlay.
//
//  ImGui's CONTEXT is not created here: engine::DebugUi owns it (and the
//  GLFW side), and must create it first and destroy it last.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/gpu/Context.h"

#include <memory>

#include <vulkan/vulkan.h>

namespace playground::render {

class UiOverlay {
 public:
  [[nodiscard]] static Result<std::unique_ptr<UiOverlay>> create(const gpu::Context& context, VkFormat colourFormat,
                                                                 VkFormat depthFormat, u32 imageCount);
  ~UiOverlay();
  UiOverlay(const UiOverlay&) = delete;
  UiOverlay& operator=(const UiOverlay&) = delete;

  void setImageCount(u32 imageCount);
  [[nodiscard]] VkFormat colourFormat() const noexcept { return colourFormat_; }
  // Inside the swapchain pass: this frame's ImGui::GetDrawData(), if any.
  // Returns how many draw lists it recorded.
  u32 record(VkCommandBuffer commands) const;
  // How many draw lists record() would draw now.
  [[nodiscard]] static u32 pendingDrawLists() noexcept;

 private:
  UiOverlay(VkFormat colour, VkFormat depth) noexcept : colourFormat_(colour), depthFormat_(depth) {}
  VkFormat colourFormat_;
  VkFormat depthFormat_;
};

}  // namespace playground::render
