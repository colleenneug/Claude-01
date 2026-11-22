#pragma once
// =============================================================================
//  playground/engine/Application.h — owns the window and runs the main loop.
//
//  ONE FRAME
//    1. events   pump OS events (wait for them, when idle; see PACING)
//    2. stop?    Ctrl+C, requestQuit(), or the window's close button
//    3. clock    FrameClock::tick() -> FrameTime (delta, elapsed, index)
//    4. update   the UpdateFn, if set: gameplay goes here
//    5. render   the Renderer, if set and the window is not minimised
//    6. limit    stop after config.maxFrames, if set (tests, --frames)
//
//  Roadmap Milestone 3 replaces step 4's single variable-rate call with the
//  fixed-timestep loop from engine/ (erebus::GameLoop); this class is where
//  that goes, and nothing outside it has to change.
//
//  PACING
//    With a renderer attached and visible, the loop runs flat out and the
//    renderer's present (vsync) sets the frame rate. With no renderer, or
//    while minimised, nothing would slow the loop down and it would spin a
//    CPU core at 100%, so it sleeps in the OS event wait until the next
//    `idleFrameRate` deadline instead.
//
//    Update keeps running while minimised, at the idle rate. Erebus Cradle
//    has co-op: a minimised client that stopped simulating would stop
//    answering the server.
//
//  OWNERSHIP AND SHUTDOWN ORDER
//    Platform -> Window -> Renderer are created in that order and destroyed
//    in reverse by shutdown(), which the destructor also calls. The renderer
//    is destroyed first because a GPU surface must not outlive its window,
//    and the window before GLFW terminates.
//
//    Application is neither copyable nor movable: a renderer holds a
//    reference to its Window, so the Window must never move.
//
//  USE
//    auto app = engine::Application::create({.window = {.title = "Erebus Cradle"}});
//    if (!app) { log the error; return 1; }
//    (*app)->setRenderer(std::move(renderer));   // later: Vulkan
//    (*app)->setUpdate([&](const FrameTime& t) { game.update(t); });
//    Status status = (*app)->run();
// =============================================================================

#include "playground/core/FrameClock.h"
#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/render/Renderer.h"
#include "playground/platform/Platform.h"
#include "playground/platform/Window.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace playground::engine {

struct ApplicationConfig {
  platform::WindowDesc window{};
  f64 maxDeltaSeconds = FrameClock::kDefaultMaxDeltaSeconds;  // see FrameClock.h: CLAMPING
  u32 idleFrameRate = 60;            // loop rate with no renderer or while minimised; 0 = don't sleep
  u64 maxFrames = 0;                 // stop after this many frames; 0 = run until closed
  f64 statsIntervalSeconds = 5.0;    // how often frame-time statistics are logged (debug level)
};

using UpdateFn = std::function<void(const FrameTime&)>;

enum class StopReason : u8 {
  None,           // still running, or never ran
  WindowClosed,   // close button, Alt+F4, Window::requestClose()
  QuitRequested,  // Application::requestQuit()
  Interrupted,    // Ctrl+C or SIGTERM
  FrameLimit,     // ApplicationConfig::maxFrames reached
  RendererError,  // the renderer returned an error; run() returns it
};

[[nodiscard]] std::string_view describe(StopReason reason) noexcept;

class Application {
 public:
  // Starts the platform and opens the window. Every failure carries a reason
  // a person can act on; nothing is left half-initialised.
  [[nodiscard]] static Result<std::unique_ptr<Application>> create(const ApplicationConfig& config);

  ~Application();
  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;
  Application(Application&&) = delete;
  Application& operator=(Application&&) = delete;

  // Attach (or replace, or with nullptr remove) the renderer. Not during
  // renderFrame(): a renderer must not replace itself.
  void setRenderer(std::unique_ptr<render::Renderer> renderer);

  void setUpdate(UpdateFn update);

  // Runs frames until something stops the loop (see StopReason). Returns the
  // renderer's error if that is what stopped it; success otherwise. May be
  // called again after it returns, but not after shutdown().
  [[nodiscard]] Status run();

  // Ends the loop after the current frame. Safe to call from update, from
  // the renderer, or from another thread.
  void requestQuit() noexcept;

  // Destroys renderer, window and platform, in that order, and logs each
  // step. Idempotent; the destructor calls it.
  void shutdown() noexcept;

  [[nodiscard]] platform::Window& window() noexcept;
  [[nodiscard]] platform::Platform& platform() noexcept;
  [[nodiscard]] StopReason stopReason() const noexcept { return stopReason_; }
  [[nodiscard]] u64 framesRun() const noexcept { return framesRun_; }

 private:
  using Clock = FrameClock::Clock;

  explicit Application(const ApplicationConfig& config);

  [[nodiscard]] StopReason pendingStopReason() const noexcept;
  void waitForEventsUntil(Clock::time_point deadline) noexcept;
  [[nodiscard]] Status renderFrame(const FrameTime& time);
  void recordFrameStats(const FrameTime& time) noexcept;

  struct FrameStats {
    u64 frames = 0;
    f64 seconds = 0.0;
    f64 minDelta = 0.0;
    f64 maxDelta = 0.0;
  };

  // Declared in creation order, so that even without shutdown() the
  // implicit destruction order (reverse of declaration) would be correct.
  ApplicationConfig config_;
  std::unique_ptr<platform::Platform> platform_;
  std::optional<platform::Window> window_;
  std::unique_ptr<render::Renderer> renderer_;
  UpdateFn update_;

  FrameClock clock_;
  FrameStats stats_{};
  u64 framesRun_ = 0;
  std::atomic<bool> quitRequested_{false};
  StopReason stopReason_ = StopReason::None;
  bool isShutDown_ = false;
};

}  // namespace playground::engine
