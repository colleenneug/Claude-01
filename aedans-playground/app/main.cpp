// =============================================================================
//  Aedan's Playground — entry point.
//
//  Start-up order: logging -> application (platform, window) -> renderer -> loop.
//  Shutdown is the reverse. Logging comes first and goes last so that every
//  other step, including a failed start-up, is recorded.
//
//  Usage: playground_app [--frames N] [--verbose] [--log-file PATH]
//                        [--validation | --no-validation]
//
//  Exit codes: 0 clean exit, 1 start-up or runtime failure (including any
//  Vulkan validation error), 2 bad arguments.
// =============================================================================

#include "playground/core/Log.h"
#include "playground/ecs/World.h"
#include "playground/engine/Application.h"
#include "playground/engine/Components.h"
#include "playground/engine/Systems.h"
#include "playground/render/VulkanRenderer.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <format>
#include <numbers>
#include <random>
#include <span>
#include <string>
#include <string_view>

namespace {

using namespace playground;
using log::Category;

struct Options {
  u64 frames = 0;  // 0 = until the window is closed
  bool verbose = false;
  std::string logFile = "playground.log";
  bool validation = render::VulkanRendererDesc{}.enableValidation;  // on in debug builds
  u32 sprites = 200;  // how many bouncing squares the demo spawns
};

void printUsage() {
  std::puts(
      "usage: playground_app [--frames N] [--sprites N] [--verbose] [--log-file PATH] [--validation | --no-validation]\n"
      "  --frames N       run N frames, then exit (used by the automated tests)\n"
      "  --sprites N      how many bouncing squares to spawn (default 200)\n"
      "  --verbose        log at debug level, including frame-time statistics\n"
      "  --log-file PATH  where to write the log (default: playground.log)\n"
      "  --validation     turn Vulkan validation on (default in debug builds)\n"
      "  --no-validation  turn Vulkan validation off (default in release builds)");
}

Result<Options> parseOptions(std::span<char*> args) {
  Options options;
  for (usize i = 1; i < args.size(); ++i) {
    const std::string_view arg = args[i];
    auto nextValue = [&]() -> Result<std::string_view> {
      if (i + 1 >= args.size()) return fail(Errc::InvalidArgument, "{} needs a value", arg);
      return std::string_view{args[++i]};
    };

    if (arg == "--frames") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      const auto [end, ec] = std::from_chars(value->data(), value->data() + value->size(), options.frames);
      if (ec != std::errc{} || end != value->data() + value->size()) {
        return fail(Errc::InvalidArgument, "--frames expects a whole number, got \"{}\"", *value);
      }
    } else if (arg == "--sprites") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      const auto [end, ec] = std::from_chars(value->data(), value->data() + value->size(), options.sprites);
      if (ec != std::errc{} || end != value->data() + value->size()) {
        return fail(Errc::InvalidArgument, "--sprites expects a whole number, got \"{}\"", *value);
      }
    } else if (arg == "--validation") {
      options.validation = true;
    } else if (arg == "--no-validation") {
      options.validation = false;
    } else if (arg == "--verbose") {
      options.verbose = true;
    } else if (arg == "--log-file") {
      auto value = nextValue();
      if (!value) return tl::unexpected(std::move(value.error()));
      options.logFile = std::string(*value);
    } else {
      return fail(Errc::InvalidArgument, "unknown argument \"{}\"", arg);
    }
  }
  return options;
}

// A stand-in for gameplay: shows the frame rate in the title bar twice a
// second, which proves update() receives correct delta times.
class TitleFpsCounter {
 public:
  explicit TitleFpsCounter(platform::Window& window) : window_(window) {}

  void update(const FrameTime& time) {
    ++frames_;
    seconds_ += time.rawDeltaSeconds;
    if (seconds_ < 0.5) return;
    const f64 fps = static_cast<f64>(frames_) / seconds_;
    window_.setTitle(std::format("Aedan's Playground — {:.0f} fps ({:.2f} ms)", fps, 1000.0 / fps));
    frames_ = 0;
    seconds_ = 0.0;
  }

 private:
  platform::Window& window_;
  u64 frames_ = 0;
  f64 seconds_ = 0.0;
};

// The ECS demo: `count` coloured squares with random positions, velocities,
// sizes and spins, inside `halfArea`. A fixed seed, so every run starts the
// same (handy for comparing screenshots).
void spawnBouncingSprites(ecs::World& world, u32 count, glm::vec2 halfArea) {
  std::mt19937 rng(20261126u);
  auto uniform = [&rng](f32 lo, f32 hi) { return std::uniform_real_distribution<f32>(lo, hi)(rng); };

  for (u32 i = 0; i < count; ++i) {
    const ecs::Entity entity = world.create();
    const f32 size = uniform(12.0f, 48.0f);
    const f32 heading = uniform(0.0f, 2.0f * std::numbers::pi_v<f32>);
    const f32 speed = uniform(60.0f, 260.0f);
    world.add<engine::Transform>(entity, engine::Transform{
        .position = {uniform(-halfArea.x, halfArea.x), uniform(-halfArea.y, halfArea.y)},
        .rotation = uniform(0.0f, std::numbers::pi_v<f32>),
    });
    world.add<engine::Velocity>(entity, engine::Velocity{
        .linear = speed * glm::vec2{std::cos(heading), std::sin(heading)},
        .angular = uniform(-3.0f, 3.0f),
    });
    world.add<engine::Sprite>(entity, engine::Sprite{
        .size = {size, size},
        .colour = {uniform(0.05f, 1.0f), uniform(0.05f, 1.0f), uniform(0.05f, 1.0f), uniform(0.6f, 1.0f)},
    });
  }
}

glm::vec2 halfSizeOf(platform::Extent2D framebuffer) {
  return {0.5f * static_cast<f32>(framebuffer.width), 0.5f * static_cast<f32>(framebuffer.height)};
}

}  // namespace

int main(int argc, char** argv) {
  auto options = parseOptions(std::span<char*>(argv, static_cast<usize>(argc)));
  if (!options) {
    std::fprintf(stderr, "error: %s\n", options.error().message.c_str());
    printUsage();
    return 2;
  }

  auto logSession = log::init({
      .filePath = options->logFile,
      .level = options->verbose ? log::Level::Debug : log::Level::Info,
  });
  if (!logSession) {
    std::fprintf(stderr, "fatal: %s\n", logSession.error().message.c_str());
    return 1;
  }

  auto app = engine::Application::create({
      .window = {.title = "Aedan's Playground"},
      .maxFrames = options->frames,
  });
  if (!app) {
    log::critical(Category::Engine, "start-up failed ({}): {}", describe(app.error().code), app.error().message);
    return 1;
  }
  engine::Application& application = **app;

  // The Vulkan renderer: brings up the GPU (each step logged, a failure
  // names the step), then the swapchain and the triangle pipeline. Once
  // attached, the Application owns it and destroys it before the window.
  auto renderer = render::createVulkanRenderer(application.platform(), application.window(),
                                               {.enableValidation = options->validation});
  if (!renderer) {
    log::critical(Category::Render, "renderer start-up failed ({}): {}", describe(renderer.error().code),
                  renderer.error().message);
    return 1;
  }
  application.setRenderer(std::move(*renderer));

  // The game world. Declared after the Application, so it is destroyed
  // first; shutdown() also drops the callbacks below that refer to it.
  ecs::World world;
  spawnBouncingSprites(world, options->sprites, halfSizeOf(application.window().framebufferSize()));
  log::info(Category::Game, "spawned {} sprites ({} entities)", world.count<engine::Sprite>(), world.entityCount());

  // The simulation: fixed 1/120 s steps, 0..n per frame (core/FixedStep.h).
  // Snapshot first, so extraction can blend from where things were.
  application.setFixedUpdate([&](const FixedStep& step) {
    engine::snapshotTransforms(world);
    engine::movementSystem(world, static_cast<f32>(step.deltaSeconds));
    engine::bounceSystem(world, halfSizeOf(application.window().framebufferSize()));
    world.flush();  // apply any destroys the systems queued
  });

  // Once per frame, at the display's rate: the FPS in the title bar.
  TitleFpsCounter fpsCounter(application.window());
  application.setUpdate([&fpsCounter](const FrameTime& time) { fpsCounter.update(time); });

  // Once per rendered frame: copy the sprites out, blended `alpha` of the way
  // from the previous step to the current one, so motion is smooth at any
  // display rate.
  application.setExtract(
      [&world](render::FrameData& frame, f64 alpha) { engine::extractSprites(world, frame, alpha); });

  const Status status = application.run();
  application.shutdown();  // renderer, then window, then GLFW

  if (!status) {
    log::critical(Category::Engine, "stopped on error ({}): {}", describe(status.error().code),
                  status.error().message);
    return 1;
  }
  // Validation errors also arrive during teardown (leaked objects are
  // reported when the instance is destroyed), so this is checked last.
  if (const render::GpuValidationCounts counts = render::gpuValidationCounts(); counts.errors > 0) {
    log::error(Category::Gpu, "{} Vulkan validation error(s) and {} warning(s) this run; see the log above",
               counts.errors, counts.warnings);
    return 1;
  } else if (counts.warnings > 0) {
    log::warn(Category::Gpu, "{} Vulkan validation warning(s) this run; see the log above", counts.warnings);
  }
  log::info(Category::Engine, "clean exit");
  return 0;
}
