#pragma once
// =============================================================================
//  erebus/core/GameLoop.h — decoupled fixed/variable timestep loop.
//
//  THE PROBLEM THIS SOLVES
//  Physics integrated at the display refresh rate is not deterministic: the
//  same inputs on a 144Hz machine and a 60Hz machine produce different
//  trajectories, because an explicit integrator's error is a function of dt.
//  Momentum-based movement (game/PlayerController.h) is especially sensitive —
//  jump apex height and slide distance both drift visibly.
//
//  THE SHAPE
//    accumulate real elapsed time
//    while (accumulator >= FIXED_DT) { simulate(FIXED_DT); accumulator -= FIXED_DT; }
//    render(alpha = accumulator / FIXED_DT)
//
//  `alpha` is the fraction of a step the renderer is *ahead* of the last
//  simulated state. Render systems interpolate between the previous and
//  current transform by it. Without that interpolation a 60Hz simulation
//  displayed at 144Hz judders, because three of every five frames show a
//  state that is up to 16ms stale.
//
//  SPIRAL OF DEATH
//  If a simulation step costs more than FIXED_DT, the accumulator grows faster
//  than it drains and the loop never exits. `maxStepsPerFrame` caps the catch-up
//  and the surplus is *discarded* — the simulation runs slow rather than
//  locking up. Discarding is the right call: the alternative, letting the
//  accumulator grow, means the moment the hitch ends you simulate a hundred
//  steps at once and everything teleports.
//
//  THREADING
//  This is the single-threaded reference shape. The production arrangement is
//  to run simulate() for frame N on the simulation thread while the render
//  thread records command buffers for frame N-1 from a double-buffered
//  snapshot; see docs in README.md under "Frame pacing". The interface below
//  is deliberately compatible with that: nothing in simulate() may touch GPU
//  resources, and nothing in render() may mutate simulation state.
// =============================================================================

#include "erebus/core/Types.h"

#include <chrono>
#include <functional>

namespace erebus {

struct FrameStats {
  f64 realDeltaSeconds = 0.0;   // wall clock since the last frame
  u32 simulationSteps  = 0;     // fixed steps run this frame
  f32 interpolation    = 0.0f;  // alpha in [0,1) handed to render()
  bool clamped         = false; // true when catch-up hit maxStepsPerFrame
};

class GameLoop {
 public:
  // 1/120s. Chosen rather than 1/60 because character controllers that sweep
  // against geometry lose contact precision as dt grows: at 60Hz a player
  // sprinting at 9.4 m/s moves 15.7cm per step, which is enough to clip the
  // lip of a 15cm stair. Raise to 1/240 for vehicles, but note the cost is
  // linear and the collision broadphase is usually what pays it.
  static constexpr f32 kFixedDelta = 1.0f / 120.0f;

  using SimulateFn = std::function<void(f32 fixedDt)>;
  using RenderFn   = std::function<void(f32 interpolationAlpha, const FrameStats&)>;

  GameLoop(SimulateFn simulate, RenderFn render) noexcept
      : simulate_(std::move(simulate)), render_(std::move(render)) {}

  // Beyond this many catch-up steps in one frame, time is dropped. Eight steps
  // is ~67ms of simulation, which covers an asset stream-in hitch without
  // letting a genuinely overloaded frame cascade.
  void setMaxStepsPerFrame(u32 steps) noexcept { maxSteps_ = steps; }

  // Scales simulated time without touching the fixed delta, so slow motion and
  // pause stay deterministic. 0 pauses the simulation while render() keeps
  // being called, which is what a pause menu with a live 3D background needs.
  void setTimeScale(f32 scale) noexcept { timeScale_ = scale < 0.0f ? 0.0f : scale; }

  void requestStop() noexcept { running_ = false; }
  [[nodiscard]] bool running() const noexcept { return running_; }

  // Runs until requestStop(). `shouldContinue` is polled once per frame and is
  // where a platform layer pumps its message queue and reports window closure.
  void run(const std::function<bool()>& shouldContinue);

  // One iteration. Exposed separately because a platform that owns its own
  // loop (a browser's requestAnimationFrame, an editor host) cannot hand
  // control to run() and needs to drive frames itself.
  FrameStats tick();

  [[nodiscard]] const FrameStats& lastFrame() const noexcept { return stats_; }

 private:
  using Clock = std::chrono::steady_clock;   // never system_clock: it can jump

  SimulateFn simulate_;
  RenderFn   render_;

  Clock::time_point previous_{};
  bool   started_    = false;
  bool   running_    = true;
  f32    accumulator_ = 0.0f;
  f32    timeScale_   = 1.0f;
  u32    maxSteps_    = 8;
  FrameStats stats_{};
};

}  // namespace erebus
