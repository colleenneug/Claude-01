#pragma once
// =============================================================================
//  playground/render/Renderer.h — the slot a renderer plugs into.
//
//  Lives in the render layer, below the engine, so that a renderer
//  implementation (render/src/VulkanRenderer.cpp) does not depend on the
//  engine that uses it.
//
//  The Application owns the window and the loop; a Renderer owns everything
//  on the GPU. This interface is the whole contract between them, and it is
//  deliberately small:
//
//    construction          The renderer is built outside the Application,
//                          from Application::window(), by a factory that can
//                          fail (createVulkanRenderer returns
//                          Result<std::unique_ptr<Renderer>>). Constructors
//                          cannot return errors; factories can.
//    onFramebufferResized  Rebuild size-dependent resources (the swapchain).
//    renderFrame           Record, submit and present one frame, drawing
//                          what the game extracted into FrameData.
//    createMesh,           Copy geometry or pixels to GPU memory once and
//    createTexture         return a handle (render/Resources.h). Main
//                          thread only, like everything here. A renderer
//                          that cannot draw meshes returns Errc::Unsupported.
//    destroyMesh,          Free them. Safe while frames that use them are
//    destroyTexture        still on the GPU: the memory goes once they finish.
//    destruction           Release every GPU resource. Runs while the window
//                          still exists, because a Vulkan surface must be
//                          destroyed before the window it belongs to.
//
//  The renderer is never called while the window is minimised, so it never
//  sees a zero-sized framebuffer.
//
//  A Status error from either call stops the main loop, and run() returns it:
//  a lost device is reported, not ignored.
// =============================================================================

#include "playground/core/FrameClock.h"
#include "playground/core/Result.h"
#include "playground/platform/Window.h"
#include "playground/render/FrameData.h"
#include "playground/render/Resources.h"

#include <string_view>

namespace playground::render {

class Renderer {
 public:
  virtual ~Renderer() = default;

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  Renderer(Renderer&&) = delete;
  Renderer& operator=(Renderer&&) = delete;

  // For the log: "vulkan", "null"...
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;

  // Called before the next renderFrame() whenever the framebuffer has changed
  // size. `size` is never empty.
  [[nodiscard]] virtual Status onFramebufferResized(platform::Extent2D size) = 0;

  // Produces one frame showing `frame`. `time` is the same FrameTime the
  // update step saw. `frame` is only valid during the call.
  [[nodiscard]] virtual Status renderFrame(const FrameTime& time, const FrameData& frame) = 0;

  // GPU resources. The defaults are for renderers that only draw sprites
  // (tests' fakes): creating fails with Errc::Unsupported, destroying does
  // nothing. A stale or invalid handle is ignored by destroy.
  [[nodiscard]] virtual Result<MeshHandle> createMesh(const MeshData& /*mesh*/, std::string_view debugName) {
    return fail(Errc::Unsupported, "the {} renderer cannot create meshes (\"{}\")", name(), debugName);
  }
  [[nodiscard]] virtual Result<TextureHandle> createTexture(const ImageData& /*image*/, TextureUsage /*usage*/,
                                                            std::string_view debugName) {
    return fail(Errc::Unsupported, "the {} renderer cannot create textures (\"{}\")", name(), debugName);
  }
  virtual void destroyMesh(MeshHandle /*mesh*/) noexcept {}
  virtual void destroyTexture(TextureHandle /*texture*/) noexcept {}

 protected:
  Renderer() = default;
};

}  // namespace playground::render
