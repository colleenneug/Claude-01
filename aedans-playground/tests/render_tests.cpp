// Tests for the Vulkan renderer: frames, swapchain rebuilds, teardown.
//
// They need a display and a Vulkan driver; without either the executable
// exits 77 (CTest: skipped). In the cloud container they run under
// `xvfb-run -a` on Mesa's software driver.
//
// White-box: they include the renderer's private header to look at the
// swapchain and frame counters.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "VulkanRenderer.h"  // render/src: private
#include "playground/core/Log.h"
#include "playground/engine/Application.h"

#include <cstdio>
#include <memory>

using namespace playground;

namespace {

constexpr int kSkipExitCode = 77;

std::unique_ptr<engine::Application> makeApp(u64 maxFrames, u32 width = 320, u32 height = 240) {
  auto app = engine::Application::create({
      .window = {.title = "render_tests", .width = width, .height = height},
      .maxFrames = maxFrames,
  });
  REQUIRE_MESSAGE(app.has_value(), (app ? "" : app.error().message));
  return std::move(*app);
}

// Creates the renderer, attaches it, and returns a pointer for inspection.
// The Application owns it; the pointer is valid until shutdown().
render::VulkanRenderer* attachRenderer(engine::Application& app) {
  auto renderer = render::VulkanRenderer::create(app.platform(), app.window(), {.enableValidation = true});
  REQUIRE_MESSAGE(renderer.has_value(), (renderer ? "" : renderer.error().message));
  render::VulkanRenderer* raw = renderer->get();
  app.setRenderer(std::move(*renderer));
  return raw;
}

}  // namespace

TEST_CASE("renders and presents every frame of sprites, with no validation errors") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(30);
    render::VulkanRenderer* renderer = attachRenderer(*app);

    const gpu::Swapchain& swapchain = renderer->swapchain();
    const platform::Extent2D framebuffer = app->window().framebufferSize();
    CHECK(swapchain.extent().width == framebuffer.width);
    CHECK(swapchain.extent().height == framebuffer.height);
    CHECK(swapchain.imageCount() >= 2);

    // Something to draw, including overlap (blending) and rotation.
    app->setExtract([](render::FrameData& frame) {
      frame.sprites.push_back({.position = {0.0f, 0.0f}, .size = {64.0f, 64.0f}, .rotation = 0.3f});
      frame.sprites.push_back({.position = {20.0f, 10.0f}, .size = {40.0f, 20.0f}, .colour = {1, 0, 0, 0.5f}});
    });

    const Status status = app->run();
    REQUIRE_MESSAGE(status.has_value(), (status ? "" : status.error().message));
    CHECK(renderer->framesPresented() == 30);
  }  // renderer, then window, then GLFW
  // Includes anything reported while the renderer was torn down.
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("resizing the window rebuilds the swapchain at the new size") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(/*maxFrames=*/600);  // a safety net: the test quits long before
    render::VulkanRenderer* renderer = attachRenderer(*app);
    engine::Application* raw = app.get();

    bool resized = false;
    app->setUpdate([&](const FrameTime& time) {
      if (time.frameIndex == 5) raw->window().setSize(480, 360);
      const VkExtent2D extent = renderer->swapchain().extent();
      if (extent.width == 480 && extent.height == 360) {
        resized = true;
        raw->requestQuit();
      }
    });

    const Status status = app->run();
    REQUIRE_MESSAGE(status.has_value(), (status ? "" : status.error().message));
    CHECK(resized);
    CHECK(app->stopReason() == engine::StopReason::QuitRequested);
    CHECK(renderer->swapchainRecreations() >= 1);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("the renderer can be replaced mid-run without validation errors") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(10);
    attachRenderer(*app);
    REQUIRE(app->run().has_value());
    // A second renderer on the same window: the first must be gone before the
    // new surface is made (a window can have only one).
    app->setRenderer(nullptr);
    render::VulkanRenderer* second = attachRenderer(*app);
    REQUIRE(app->run().has_value());
    CHECK(second->framesPresented() == 10);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("the public factory hides Vulkan behind render::Renderer") {
  auto app = makeApp(3);
  auto renderer = render::createVulkanRenderer(app->platform(), app->window(), {});
  REQUIRE(renderer.has_value());
  CHECK((*renderer)->name() == "vulkan");
  app->setRenderer(std::move(*renderer));
  CHECK(app->run().has_value());
}

int main(int argc, char** argv) {
  auto session = log::init({.filePath = {}, .level = log::Level::Warn});
  if (!session) {
    std::fprintf(stderr, "could not start logging: %s\n", session.error().message.c_str());
    return 1;
  }

  {
    auto probe = platform::Platform::create();
    if (!probe) {
      std::printf("skipping render_tests: %s\n", probe.error().message.c_str());
      return kSkipExitCode;
    }
    if (!(*probe)->vulkanSupported()) {
      std::printf("skipping render_tests: no Vulkan driver on this machine\n");
      return kSkipExitCode;
    }
  }

  doctest::Context context(argc, argv);
  return context.run();
}
