#pragma once
// =============================================================================
//  playground/engine/Application.h — owns the window and runs the main loop.
//
//  ONE FRAME
//    1. events   pump OS events (wait for them, when idle; see PACING)
//    2. stop?    Ctrl+C, requestQuit(), or the window's close button
//    3. clock    FrameClock::tick() -> FrameTime (delta, elapsed, index)
//    4. simulate the FixedUpdateFn, 0..n times, each a step of exactly
//                config.fixedStepSeconds (1/120 s): gameplay and physics.
//                How many is decided by a FixedStepAccumulator from the
//                frame's real duration (see core/FixedStep.h).
//    5. update   the UpdateFn, once, with the real frame time: things that
//                follow the display, not the simulation (an FPS counter, UI)
//    6. extract  the ExtractFn, with alpha = how far real time is past the
//                last step, in [0, 1): copies what is visible into a
//                render::FrameData, blending previous and current state
//    7. render   the Renderer, if set and the window is not minimised
//    8. limit    stop after config.maxFrames, if set (tests, --frames)
//
//  A frame may run zero fixed steps (a 240 Hz display with a 120 Hz
//  simulation does every other frame) or several (a 30 fps frame runs about
//  four). That is the point: the simulation advances at its own fixed rate,
//  and the display shows wherever it has got to.
//
//  PACING
//    With a renderer attached and visible, the loop runs flat out and the
//    renderer's present (vsync) sets the frame rate. With no renderer, or
//    while minimised, nothing would slow the loop down and it would spin a
//    CPU core at 100%, so it sleeps in the OS event wait until the next
//    `idleFrameRate` deadline instead.
//
//    The simulation keeps running while minimised (the idle rate still
//    feeds the accumulator). Erebus Cradle has co-op: a minimised client
//    that stopped simulating would stop answering the server.
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
//    (*app)->setFixedUpdate([&](const FixedStep& step) { game.simulate(step); });
//    (*app)->setExtract([&](render::FrameData& frame, f64 alpha) { game.draw(frame, alpha); });
//    Status status = (*app)->run();
// =============================================================================

#include "playground/core/FixedStep.h"
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
  f64 maxDeltaSeconds = FrameClock::kDefaultMaxDeltaSeconds;  // a frame longer than this counts as this long
  f64 fixedStepSeconds = FixedStepAccumulator::kDefaultStepSeconds;      // 1/120 s: 120 simulation steps a second
  u32 maxFixedStepsPerFrame = FixedStepAccumulator::kDefaultMaxStepsPerFrame;  // catch-up cap (see core/FixedStep.h)
  u32 idleFrameRate = 60;            // loop rate with no renderer or while minimised; 0 = don't sleep
  u64 maxFrames = 0;                 // stop after this many frames; 0 = run until closed
  f64 statsIntervalSeconds = 5.0;    // how often frame-time statistics are logged (debug level)
};

// One simulation step of exactly step.deltaSeconds. Called 0..n times a frame.
using FixedUpdateFn = std::function<void(const FixedStep&)>;

// Once a frame, with the real (clamped) frame time.
using UpdateFn = std::function<void(const FrameTime&)>;

// Fills the (already cleared) FrameData the renderer will draw this frame.
// `alpha` in [0, 1) is how far real time has moved past the last simulation
// step, as a fraction of a step: draw previous + (current - previous) * alpha.
// Runs only on frames that are rendered.
using ExtractFn = std::function<void(render::FrameData&, f64 alpha)>;

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

  void setFixedUpdate(FixedUpdateFn fixedUpdate);
  void setUpdate(UpdateFn update);
  void setExtract(ExtractFn extract);

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
  [[nodiscard]] u64 fixedStepsRun() const noexcept { return fixedStepsRun_; }
  [[nodiscard]] f64 lastAlpha() const noexcept { return lastAlpha_; }

 private:
  using Clock = FrameClock::Clock;

  explicit Application(const ApplicationConfig& config);

  [[nodiscard]] StopReason pendingStopReason() const noexcept;
  void waitForEventsUntil(Clock::time_point deadline) noexcept;
  [[nodiscard]] Status renderFrame(const FrameTime& time, f64 alpha);
  void recordFrameStats(const FrameTime& time, const FixedStepPlan& plan) noexcept;

  struct FrameStats {
    u64 frames = 0;
    f64 seconds = 0.0;
    f64 minDelta = 0.0;
    f64 maxDelta = 0.0;
    u64 fixedSteps = 0;
    f64 droppedSeconds = 0.0;
  };

  // Declared in creation order, so that even without shutdown() the
  // implicit destruction order (reverse of declaration) would be correct.
  ApplicationConfig config_;
  std::unique_ptr<platform::Platform> platform_;
  std::optional<platform::Window> window_;
  std::unique_ptr<render::Renderer> renderer_;
  FixedUpdateFn fixedUpdate_;
  UpdateFn update_;
  ExtractFn extract_;
  render::FrameData frameData_;  // reused every frame, so its memory is too

  FrameClock clock_;
  FixedStepAccumulator accumulator_;
  FrameStats stats_{};
  u64 framesRun_ = 0;
  u64 fixedStepsRun_ = 0;
  f64 lastAlpha_ = 0.0;
  std::atomic<bool> quitRequested_{false};
  StopReason stopReason_ = StopReason::None;
  bool isShutDown_ = false;
};

}  // namespace playground::engine
