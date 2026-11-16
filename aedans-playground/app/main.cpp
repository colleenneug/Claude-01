// =============================================================================
//  Aedan's Playground — entry point.
//
//  Start-up order: logging -> application (platform, window) -> loop.
//  Shutdown is the reverse, by scope. Logging comes first and goes last so
//  that every other step, including a failed start-up, is recorded.
//
//  Usage: playground_app [--frames N] [--verbose] [--log-file PATH]
//
//  Exit codes: 0 clean exit, 1 start-up or runtime failure, 2 bad arguments.
// =============================================================================

#include "playground/core/Log.h"
#include "playground/engine/Application.h"

#include <charconv>
#include <cstdio>
#include <format>
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
};

void printUsage() {
  std::puts(
      "usage: playground_app [--frames N] [--verbose] [--log-file PATH]\n"
      "  --frames N       run N frames, then exit (used by the automated tests)\n"
      "  --verbose        log at debug level, including frame-time statistics\n"
      "  --log-file PATH  where to write the log (default: playground.log)");
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

  // No renderer yet: roadmap Milestone 1 attaches the Vulkan one here, e.g.
  //   auto renderer = gpu::VulkanRenderer::create(application.platform(), application.window());
  //   if (!renderer) { ...log...; return 1; }
  //   application.setRenderer(std::move(*renderer));
  TitleFpsCounter fpsCounter(application.window());
  application.setUpdate([&fpsCounter](const FrameTime& time) { fpsCounter.update(time); });

  const Status status = application.run();
  application.shutdown();

  if (!status) {
    log::critical(Category::Engine, "stopped on error ({}): {}", describe(status.error().code),
                  status.error().message);
    return 1;
  }
  log::info(Category::Engine, "clean exit");
  return 0;
}
