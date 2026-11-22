#pragma once
// =============================================================================
//  playground/render/VulkanRenderer.h — create the Vulkan renderer.
//
//  This header deliberately contains no Vulkan types. Code that creates the
//  renderer (app/main.cpp) gets a render::Renderer and never sees Vulkan, so
//  the engine and the game cannot come to depend on it by accident.
//
//      auto renderer = render::createVulkanRenderer(app.platform(), app.window(), {});
//      if (!renderer) { log the error; return 1; }
//      app.setRenderer(std::move(*renderer));
//
//  What the renderer does each frame is described in VulkanRenderer.cpp.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/platform/Platform.h"
#include "playground/platform/Window.h"
#include "playground/render/Renderer.h"

#include <array>
#include <memory>
#include <string>

namespace playground::render {

struct VulkanRendererDesc {
  std::string applicationName = "Aedan's Playground";
#if defined(NDEBUG)
  bool enableValidation = false;
#else
  bool enableValidation = true;  // debug builds check every Vulkan call
#endif
  bool vsync = true;  // FIFO present: caps the frame rate at the display's
  // Linear RGBA. The default is a very dark blue, so a missing triangle is
  // easy to tell from a missing window.
  std::array<f32, 4> clearColour{0.01f, 0.01f, 0.03f, 1.0f};
};

// Brings up Vulkan (instance, surface, GPU, device), creates the swapchain,
// the triangle pipeline and the per-frame resources. Every failure names the
// step and the reason.
[[nodiscard]] Result<std::unique_ptr<Renderer>> createVulkanRenderer(platform::Platform& platform,
                                                                     platform::Window& window,
                                                                     const VulkanRendererDesc& desc);

// Vulkan validation messages seen so far in this process, including those
// reported while the renderer was being destroyed. A non-zero error count
// is a bug.
struct GpuValidationCounts {
  u32 errors = 0;
  u32 warnings = 0;
};
[[nodiscard]] GpuValidationCounts gpuValidationCounts() noexcept;

}  // namespace playground::render
