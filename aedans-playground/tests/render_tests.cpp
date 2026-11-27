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
#include "playground/core/JobSystem.h"
#include "playground/engine/Assets.h"
#include "playground/engine/Application.h"

#include <cstdio>
#include <memory>
#include <thread>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <atomic>

#include <glm/ext/matrix_clip_space.hpp>
#include <vector>

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
    app->setExtract([](render::FrameData& frame, f64) {
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
    app->setUpdate([&](const FrameTime& time, const platform::InputState&) {
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
    CHECK(renderer->depthBuffer().extent().width == 480);  // the depth buffer was rebuilt too
    CHECK(renderer->depthBuffer().extent().height == 360);
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

// --- Instanced sprites ------------------------------------------------------------

TEST_CASE("25,000 sprites are drawn in one instanced draw with no validation errors") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(10);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    app->setExtract([](render::FrameData& frame, f64) {
      frame.viewProjection = render::reverseZ() * glm::orthoRH_ZO(-800.0f, 800.0f, -450.0f, 450.0f, -10.0f, 10.0f);
      for (int i = 0; i < 25'000; ++i) {
        const float x = static_cast<float>(i % 250) * 6.4f - 800.0f;
        const float y = static_cast<float>(i / 250) * 9.0f - 450.0f;
        frame.sprites.push_back({.position = {x, y}, .size = {4.0f, 4.0f}, .rotation = 0.1f * static_cast<float>(i)});
      }
    });

    REQUIRE(app->run().has_value());
    CHECK(renderer->framesPresented() == 10);
    CHECK(renderer->lastSpriteCount() == 25'000);
    CHECK(renderer->lastDrawCalls() == 1);  // not 25,000
    CHECK(renderer->spriteCapacity() >= 25'000);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("the sprite buffer grows when a frame needs more, and does not shrink") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(12);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    int frame = 0;
    std::vector<u32> capacities;
    app->setExtract([&](render::FrameData& data, f64) {
      // 10 sprites, then 5,000 (beyond the initial 1,024), then 10 again.
      const int count = (frame >= 4 && frame < 8) ? 5'000 : 10;
      for (int i = 0; i < count; ++i) data.sprites.push_back({.position = {0.0f, 0.0f}, .size = {2.0f, 2.0f}});
      ++frame;
    });
    app->setUpdate([&](const FrameTime&, const platform::InputState&) {
      capacities.push_back(renderer->spriteCapacity());
    });

    REQUIRE(app->run().has_value());
    CHECK(capacities.front() <= render::kInitialSpriteCapacity);  // before any frame grew it
    CHECK(renderer->spriteCapacity() >= 5'000);  // grew for the big frames...
    CHECK(renderer->lastSpriteCount() == 10);    // ...and kept that room for the small ones
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("a frame with no sprites draws nothing and still presents") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    REQUIRE(app->run().has_value());  // no extract hook: an empty FrameData
    CHECK(renderer->framesPresented() == 3);
    CHECK(renderer->lastDrawCalls() == 0);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

// --- Depth, camera uniforms, and pixels read back from the GPU ---------------------
// These read the rendered frame back (requestCapture/takeCapture) and check
// actual pixel colours, so they prove what is on screen, not just that the
// calls succeeded. Window 320x240; with no camera, one world unit = one pixel
// and (0, 0) is the centre, so world (x, y) is pixel (160 + x, 120 - y).

namespace {
bool near(u8 actual, u8 expected) { return actual + 3 >= expected && actual <= expected + 3; }
void checkColour(const render::CapturedFrame& frame, u32 x, u32 y, u8 r, u8 g, u8 b) {
  const auto p = frame.at(x, y);
  INFO("pixel (", x, ", ", y, ") is ", int(p.r), ",", int(p.g), ",", int(p.b), "; expected ", int(r), ",", int(g), ",", int(b));
  CHECK(near(p.r, r));
  CHECK(near(p.g, g));
  CHECK(near(p.b, b));
}
}  // namespace

TEST_CASE("the depth test puts a higher layer in front whatever the draw order; equal layers keep draw order") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    app->setExtract([](render::FrameData& frame, f64) {
      // Pair A, left: red is drawn FIRST but is nearer (depth 1); blue after, farther.
      frame.sprites.push_back({.position = {-60.0f, 0.0f}, .size = {60.0f, 60.0f}, .colour = {1, 0, 0, 1}, .depth = 1.0f});
      frame.sprites.push_back({.position = {-60.0f, 0.0f}, .size = {60.0f, 60.0f}, .colour = {0, 0, 1, 1}, .depth = 0.0f});
      // Pair B, right: same depth, so the later one (yellow) is on top.
      frame.sprites.push_back({.position = {60.0f, 0.0f}, .size = {60.0f, 60.0f}, .colour = {0, 1, 0, 1}, .depth = 0.0f});
      frame.sprites.push_back({.position = {60.0f, 0.0f}, .size = {60.0f, 60.0f}, .colour = {1, 1, 0, 1}, .depth = 0.0f});
    });
    renderer->requestCapture();
    REQUIRE(app->run().has_value());

    const auto frame = renderer->takeCapture();
    REQUIRE_MESSAGE(frame.has_value(), (frame ? "" : frame.error().message));
    REQUIRE(frame->width == 320);
    REQUIRE(frame->height == 240);
    checkColour(*frame, 100, 120, 255, 0, 0);    // red: in front because nearer, though drawn first
    checkColour(*frame, 220, 120, 255, 255, 0);  // yellow: same depth, drawn later
    // Background where nothing is drawn: the default clear colour, sRGB-encoded.
    const auto corner = frame->at(5, 5);
    CHECK(corner.r < 40);
    CHECK(corner.b < 60);

    const gpu::Image& depth = renderer->depthBuffer();
    CHECK(depth.extent().width == 320);
    CHECK(depth.format() == gpu::kDepthFormat);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("translucent sprites are sorted back to front, and hidden behind nearer opaque ones") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    app->setExtract([](render::FrameData& frame, f64) {
      // Left: two 90%-opaque sprites given FRONT first. Sorted back to front,
      // the nearer blue is blended over the red: the result is mostly blue.
      frame.sprites.push_back({.position = {-60.0f, 0.0f}, .size = {60.0f, 60.0f}, .colour = {0, 0, 1, 0.9f}, .depth = 1.0f});
      frame.sprites.push_back({.position = {-60.0f, 0.0f}, .size = {60.0f, 60.0f}, .colour = {1, 0, 0, 0.9f}, .depth = 0.0f});
      // Right: a translucent magenta behind an opaque green. Translucent
      // sprites draw after opaque ones, but the depth test still hides it.
      frame.sprites.push_back({.position = {60.0f, 0.0f}, .size = {80.0f, 80.0f}, .colour = {1, 0, 1, 0.9f}, .depth = 0.0f});
      frame.sprites.push_back({.position = {60.0f, 0.0f}, .size = {40.0f, 40.0f}, .colour = {0, 1, 0, 1}, .depth = 1.0f});
    });
    renderer->requestCapture();
    REQUIRE(app->run().has_value());
    CHECK(renderer->lastDrawCalls() == 2);  // one opaque draw, one translucent draw

    const auto frame = renderer->takeCapture();
    REQUIRE(frame.has_value());
    const auto left = frame->at(100, 120);
    INFO("left pixel ", int(left.r), ",", int(left.g), ",", int(left.b));
    CHECK(left.b > 200);  // blue on top
    CHECK(left.r < 120);  // red only faintly through it
    checkColour(*frame, 220, 120, 0, 255, 0);  // green: the magenta behind it is hidden
    const auto ring = frame->at(220 + 30, 120);  // outside the green, inside the magenta
    CHECK(ring.r > 200);
    CHECK(ring.b > 200);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("the camera matrix reaches the shader through the per-frame uniform buffer") {
  gpu::resetValidationCounts();
  {
    auto app = makeApp(3);
    render::VulkanRenderer* renderer = attachRenderer(*app);
    app->setExtract([](render::FrameData& frame, f64) {
      // A camera looking at world (500, 300), showing 320x240 units: that
      // point must land in the middle of the window.
      frame.viewProjection = render::reverseZ() *
                             glm::orthoRH_ZO(500.0f - 160.0f, 500.0f + 160.0f, 300.0f - 120.0f, 300.0f + 120.0f,
                                             -10.0f, 10.0f);
      frame.sprites.push_back({.position = {500.0f, 300.0f}, .size = {20.0f, 20.0f}, .colour = {0, 1, 0, 1}});
    });
    renderer->requestCapture();
    REQUIRE(app->run().has_value());
    const auto frame = renderer->takeCapture();
    REQUIRE(frame.has_value());
    checkColour(*frame, 160, 120, 0, 255, 0);  // the sprite is at the centre
    const auto away = frame->at(160 + 40, 120);  // and only there
    CHECK(away.g < 40);
  }
  CHECK(render::gpuValidationCounts().errors == 0);
}

TEST_CASE("takeCapture without a captured frame says so") {
  auto app = makeApp(1);
  render::VulkanRenderer* renderer = attachRenderer(*app);
  REQUIRE(app->run().has_value());
  const auto frame = renderer->takeCapture();
  REQUIRE_FALSE(frame.has_value());
  CHECK(frame.error().code == Errc::InvalidState);
}

// --- Assets: decoded on workers, uploaded to the GPU on the main thread -----------

TEST_CASE("an asset is read on a worker and uploaded to GPU memory on the main thread") {
  gpu::resetValidationCounts();
  // 64 KiB of a known pattern, on disk.
  const auto folder = std::filesystem::temp_directory_path() / "playground_render_assets";
  std::filesystem::create_directories(folder);
  std::vector<std::byte> pattern(64 * 1024);
  for (std::size_t i = 0; i < pattern.size(); ++i) pattern[i] = static_cast<std::byte>((i * 13 + 5) & 0xFF);
  std::ofstream(folder / "blob.bin", std::ios::binary)
      .write(reinterpret_cast<const char*>(pattern.data()), static_cast<std::streamsize>(pattern.size()));

  auto app = makeApp(1);
  render::VulkanRenderer* renderer = attachRenderer(*app);
  const gpu::Context& context = renderer->context();
  const auto mainThread = std::this_thread::get_id();
  std::atomic<bool> decodedOffMainThread{false};
  bool finalizedOnMainThread = false;
  {
    JobSystem jobs(2);
    engine::AssetManager assets(jobs, folder);
    // A GPU asset type: decode = copy the bytes (worker); finalize = staging
    // upload into device-local memory (main thread, where the GPU work belongs).
    assets.registerType<gpu::Buffer>(engine::AssetLoader<gpu::Buffer, std::vector<std::byte>>{
        .decode = [&](std::span<const std::byte> file, const std::filesystem::path&) -> Result<std::vector<std::byte>> {
          decodedOffMainThread = std::this_thread::get_id() != mainThread;
          return std::vector<std::byte>(file.begin(), file.end());
        },
        .finalize = [&](std::vector<std::byte>&& bytes, const std::filesystem::path& path) -> Result<gpu::Buffer> {
          finalizedOnMainThread = std::this_thread::get_id() == mainThread;
          return gpu::createDeviceLocalBuffer(context, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, path.generic_string());
        },
    });

    const auto handle = assets.load<gpu::Buffer>("blob.bin");
    // Like frames: update() only, never waiting (a waiting thread would help
    // decode, and this test checks that decode ran on a worker).
    while (!assets.idle()) {
      (void)assets.update();
      std::this_thread::yield();
    }
    REQUIRE_MESSAGE(assets.state(handle) == engine::AssetState::Ready,
                    (assets.error(handle) ? assets.error(handle)->message : std::string("not ready")));
    const gpu::Buffer* buffer = assets.get(handle);
    REQUIRE(buffer != nullptr);
    CHECK_FALSE(buffer->isHostVisible());  // in device-local memory

    // Read it back and compare every byte.
    auto readback = gpu::Buffer::create(context.allocator(), {.sizeBytes = pattern.size(),
                                                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                              .memory = gpu::MemoryKind::HostReadback,
                                                              .debugName = "asset readback"});
    REQUIRE(readback.has_value());
    REQUIRE(gpu::copyBuffer(context, *buffer, *readback, pattern.size()).has_value());
    REQUIRE(readback->invalidate().has_value());
    CHECK(std::memcmp(readback->mapped().data(), pattern.data(), pattern.size()) == 0);
  }  // the asset (a GPU buffer) is freed here, before the renderer and its allocator
  CHECK(decodedOffMainThread.load());
  CHECK(finalizedOnMainThread);
  CHECK(render::gpuValidationCounts().errors == 0);
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
