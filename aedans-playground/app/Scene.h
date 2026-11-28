#pragma once
// =============================================================================
//  app/Scene.h — one demo scene: what the game does each step and frame.
//
//  main.cpp brings up the engine, picks a scene (--scene) and forwards the
//  Application's callbacks to it. Everything shared (Escape, the frame rate
//  in the title, asset loading) stays in main.cpp.
// =============================================================================

#include "playground/core/FixedStep.h"
#include "playground/engine/Editor.h"
#include "playground/core/FrameClock.h"
#include "playground/platform/Input.h"
#include "playground/render/FrameData.h"

namespace playground::app {

class Scene {
 public:
  virtual ~Scene() = default;

  // Fixed 1/120 s simulation steps (0..n per frame).
  virtual void fixedUpdate(const FixedStep& /*step*/, const platform::InputState& /*input*/) {}
  // Once per frame, at the display's rate.
  virtual void update(const FrameTime& time, const platform::InputState& input) = 0;
  // Once per rendered frame: copy what is visible into `frame`.
  virtual void extract(render::FrameData& frame, f64 alpha) = 0;
  // A line for the log when the game ends (the end-to-end tests read it).
  virtual void logSummary() = 0;
  // The developer UI's panels (F1), with what the engine measured; the scene
  // adds its own numbers (entities, physics, network) and draws them.
  virtual void drawDebugUi(engine::ProfilerData data) { (void)data; }
  // Non-zero if the scene failed (the network client was rejected or dropped).
  [[nodiscard]] virtual int exitCode() const { return 0; }
};

}  // namespace playground::app
