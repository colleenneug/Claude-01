// =============================================================================
//  Aedan's Playground — entry point.
//
//  Start-up order: logging -> application (platform, window) -> GPU -> loop.
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
#include "playground/engine/Application.h"
#include "playground/gpu/Context.h"

#include <charconv>
#include <cstdio>
#include <format>
#include <optional>
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
  bool validation = gpu::kValidationByDefault;  // on in debug builds
};

void printUsage() {
  std::puts(
      "usage: playground_app [--frames N] [--verbose] [--log-file PATH] [--validation | --no-validation]\n"
      "  --frames N       run N frames, then exit (used by the automated tests)\n"
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

  // Vulkan bring-up: instance, surface, GPU, logical device, graphics queue.
  // Each step logs what it found; a failure names the step and the reason.
  //
  // For now the context is owned here. Once the swapchain exists, it moves
  // into a VulkanRenderer attached with application.setRenderer(), and the
  // Application takes over the destruction order below.
  std::optional<gpu::Context> gpuContext;
  {
    auto created = gpu::Context::create(application.platform(), application.window(),
                                        {.applicationName = "Aedan's Playground", .enableValidation = options->validation});
    if (!created) {
      log::critical(Category::Gpu, "GPU start-up failed ({}): {}", describe(created.error().code),
                    created.error().message);
      return 1;
    }
    gpuContext.emplace(std::move(*created));
  }

  TitleFpsCounter fpsCounter(application.window());
  application.setUpdate([&fpsCounter](const FrameTime& time) { fpsCounter.update(time); });

  const Status status = application.run();

  // The GPU context goes BEFORE the window: its surface belongs to the
  // window. shutdown() destroys the window, so the order here matters.
  gpuContext.reset();
  application.shutdown();

  if (!status) {
    log::critical(Category::Engine, "stopped on error ({}): {}", describe(status.error().code),
                  status.error().message);
    return 1;
  }
  // Validation errors also arrive during teardown (leaked objects are
  // reported when the instance is destroyed), so this is checked last.
  if (const gpu::ValidationCounts counts = gpu::validationCounts(); counts.errors > 0) {
    log::error(Category::Gpu, "{} Vulkan validation error(s) and {} warning(s) this run; see the log above",
               counts.errors, counts.warnings);
    return 1;
  } else if (counts.warnings > 0) {
    log::warn(Category::Gpu, "{} Vulkan validation warning(s) this run; see the log above", counts.warnings);
  }
  log::info(Category::Engine, "clean exit");
  return 0;
}
