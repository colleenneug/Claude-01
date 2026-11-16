#include "playground/engine/Application.h"

#include "playground/core/Assert.h"
#include "playground/core/Log.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <utility>

#ifndef PLAYGROUND_BUILD_CONFIG
#define PLAYGROUND_BUILD_CONFIG "unknown"
#endif

namespace playground::engine {
namespace {

using log::Category;

std::string compilerDescription() {
#if defined(__clang__)
  return std::format("Clang {}.{}.{}", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
  return std::format("GCC {}.{}.{}", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
  return std::format("MSVC {}", _MSC_FULL_VER);
#else
  return "unknown compiler";
#endif
}

}  // namespace

std::string_view describe(StopReason reason) noexcept {
  switch (reason) {
    case StopReason::None:          return "not stopped";
    case StopReason::WindowClosed:  return "window closed";
    case StopReason::QuitRequested: return "quit requested";
    case StopReason::Interrupted:   return "interrupted (Ctrl+C or SIGTERM)";
    case StopReason::FrameLimit:    return "frame limit reached";
    case StopReason::RendererError: return "renderer error";
  }
  return "unknown";
}

// --- Creation and shutdown ---------------------------------------------------

Application::Application(const ApplicationConfig& config)
    : config_(config), clock_(config.maxDeltaSeconds) {}

Result<std::unique_ptr<Application>> Application::create(const ApplicationConfig& config) {
  if (config.maxDeltaSeconds <= 0.0) {
    return fail(Errc::InvalidArgument, "maxDeltaSeconds must be positive, got {}", config.maxDeltaSeconds);
  }

  log::info(Category::Engine, "Aedan's Playground starting ({} build, {})", PLAYGROUND_BUILD_CONFIG,
            compilerDescription());

  // If any step below fails, `app` is destroyed on return, and its
  // destructor tears down whatever had already been created.
  std::unique_ptr<Application> app(new Application(config));

  auto platform = platform::Platform::create();
  if (!platform) return tl::unexpected(std::move(platform.error()));
  app->platform_ = std::move(*platform);

  auto window = platform::Window::create(*app->platform_, config.window);
  if (!window) return tl::unexpected(std::move(window.error()));
  app->window_.emplace(std::move(*window));

  log::info(Category::Engine, "application ready");
  return app;
}

Application::~Application() { shutdown(); }

void Application::shutdown() noexcept {
  if (isShutDown_) return;
  isShutDown_ = true;

  // A start-up that failed before the platform came up has nothing to tear
  // down, and logging "shutdown complete" would suggest it had started.
  const bool started = platform_ != nullptr;

  // Reverse of creation. Each reset is explicit so the log shows the order.
  if (renderer_) {
    log::info(Category::Engine, "destroying renderer \"{}\"", renderer_->name());
    renderer_.reset();
  }
  update_ = nullptr;  // may capture references to things about to go away
  if (window_) {
    window_.reset();
    log::info(Category::Engine, "window destroyed");
  }
  platform_.reset();  // logs "GLFW terminated" itself
  if (started) log::info(Category::Engine, "shutdown complete");
  log::flush();
}

// --- Configuration -----------------------------------------------------------

void Application::setRenderer(std::unique_ptr<Renderer> renderer) {
  PG_VERIFY(!isShutDown_, "setRenderer() called after shutdown()");
  if (renderer_) log::info(Category::Engine, "replacing renderer \"{}\"", renderer_->name());
  renderer_ = std::move(renderer);
  if (renderer_) log::info(Category::Engine, "renderer \"{}\" attached", renderer_->name());
}

void Application::setUpdate(UpdateFn update) { update_ = std::move(update); }

void Application::requestQuit() noexcept { quitRequested_.store(true); }

platform::Window& Application::window() noexcept {
  PG_VERIFY(window_.has_value(), "window() called after shutdown()");
  return *window_;
}

platform::Platform& Application::platform() noexcept {
  PG_VERIFY(platform_ != nullptr, "platform() called after shutdown()");
  return *platform_;
}

// --- The loop ----------------------------------------------------------------

Status Application::run() {
  if (isShutDown_) return fail(Errc::InvalidState, "run() called after shutdown()");

  const auto idleInterval =
      config_.idleFrameRate > 0
          ? std::chrono::duration_cast<Clock::duration>(std::chrono::duration<f64>(1.0 / config_.idleFrameRate))
          : Clock::duration::zero();

  log::info(Category::Engine, "entering main loop (renderer: {}, frame limit: {})",
            renderer_ ? renderer_->name() : std::string_view{"none"},
            config_.maxFrames == 0 ? std::string{"none"} : std::to_string(config_.maxFrames));

  clock_.reset();
  stats_ = {};
  framesRun_ = 0;
  stopReason_ = StopReason::None;
  Status result{};
  auto nextIdleFrame = Clock::now();

  while (true) {
    // 1. Events.
    const bool idle = !renderer_ || window_->isMinimized();
    if (idle) {
      waitForEventsUntil(nextIdleFrame);
      // If we fell behind (a slow frame), schedule from now rather than
      // running a burst of frames to catch up.
      nextIdleFrame = std::max(nextIdleFrame + idleInterval, Clock::now());
    } else {
      platform_->pollEvents();
    }

    // 2. Stop?
    if (const StopReason reason = pendingStopReason(); reason != StopReason::None) {
      stopReason_ = reason;
      break;
    }

    // 3. Clock.
    const FrameTime time = clock_.tick();

    // 4. Update.
    if (update_) update_(time);

    // 5. Render.
    if (renderer_ && !window_->isMinimized()) {
      if (Status frame = renderFrame(time); !frame) {
        log::error(Category::Engine, "renderer \"{}\" failed on frame {}: {}", renderer_->name(), time.frameIndex,
                   frame.error().message);
        stopReason_ = StopReason::RendererError;
        result = std::move(frame);
        ++framesRun_;
        break;
      }
    }

    ++framesRun_;
    recordFrameStats(time);

    // 6. Frame limit.
    if (config_.maxFrames != 0 && framesRun_ >= config_.maxFrames) {
      stopReason_ = StopReason::FrameLimit;
      break;
    }
  }

  log::info(Category::Engine, "left main loop after {} frames: {}", framesRun_, describe(stopReason_));
  return result;
}

StopReason Application::pendingStopReason() const noexcept {
  if (platform::Platform::interruptRequested()) return StopReason::Interrupted;
  if (quitRequested_.load()) return StopReason::QuitRequested;
  if (window_->shouldClose()) return StopReason::WindowClosed;
  return StopReason::None;
}

void Application::waitForEventsUntil(Clock::time_point deadline) noexcept {
  // Always handle what is already queued, even when the deadline has passed.
  platform_->pollEvents();
  // The OS wait returns early on any event (a mouse move), so wait again for
  // whatever time remains. Bail out at once if something asked us to stop.
  for (auto now = Clock::now(); now < deadline; now = Clock::now()) {
    if (pendingStopReason() != StopReason::None) return;
    platform_->waitEvents(std::chrono::duration<f64>(deadline - now).count());
  }
}

Status Application::renderFrame(const FrameTime& time) {
  if (window_->consumeFramebufferResized()) {
    const platform::Extent2D size = window_->framebufferSize();
    log::debug(Category::Engine, "telling renderer about new framebuffer size {}x{}", size.width, size.height);
    if (Status resized = renderer_->onFramebufferResized(size); !resized) return resized;
  }
  return renderer_->renderFrame(time);
}

void Application::recordFrameStats(const FrameTime& time) noexcept {
  if (time.frameIndex == 0) return;  // the first frame has no delta to measure

  const f64 delta = time.rawDeltaSeconds;
  if (stats_.frames == 0) {
    stats_.minDelta = delta;
    stats_.maxDelta = delta;
  } else {
    stats_.minDelta = std::min(stats_.minDelta, delta);
    stats_.maxDelta = std::max(stats_.maxDelta, delta);
  }
  ++stats_.frames;
  stats_.seconds += delta;

  if (config_.statsIntervalSeconds > 0.0 && stats_.seconds >= config_.statsIntervalSeconds) {
    const f64 averageMs = 1000.0 * stats_.seconds / static_cast<f64>(stats_.frames);
    log::debug(Category::Engine, "{} frames in {:.2f} s: average {:.2f} ms ({:.1f} fps), min {:.2f} ms, max {:.2f} ms",
               stats_.frames, stats_.seconds, averageMs, 1000.0 / averageMs, 1000.0 * stats_.minDelta,
               1000.0 * stats_.maxDelta);
    stats_ = {};
  }
}

}  // namespace playground::engine
