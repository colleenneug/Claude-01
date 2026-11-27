#pragma once
// =============================================================================
//  app/SpriteScene.h — the 2D demo (--scene sprites): 25,000 bouncing
//  squares, a player to steer, bullets aimed with the mouse, right-click
//  removal, and C to tilt the camera into 3D.
// =============================================================================

#include "Scene.h"

#include "playground/ecs/World.h"
#include "playground/engine/Actions.h"
#include "playground/engine/Application.h"
#include "playground/engine/Camera.h"
#include "playground/engine/WorldBounds.h"

#include <optional>

#include <glm/vec2.hpp>

namespace playground::app {

class SpriteScene final : public Scene {
 public:
  SpriteScene(engine::Application& application, u32 spriteCount);

  void fixedUpdate(const FixedStep& step, const platform::InputState& input) override;
  void update(const FrameTime& time, const platform::InputState& input) override;
  void extract(render::FrameData& frame, f64 alpha) override;
  void logSummary() override;

 private:
  void setProjection(engine::Projection projection);
  [[nodiscard]] std::optional<glm::vec2> mouseOnPlayfield(const platform::InputState& input) const;

  engine::Application& application_;
  // The world's size is fixed and belongs to the game: the window only views
  // it (render::fitView), so resizing never changes the simulation.
  engine::WorldBounds bounds_{.halfSize = {800.0f, 450.0f}};
  engine::ActionMap actions_ = engine::ActionMap::defaults();
  ecs::World world_;
  ecs::Entity player_;
  engine::Camera camera_;
  u32 bulletsFired_ = 0;
};

}  // namespace playground::app
