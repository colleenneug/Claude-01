#pragma once
// =============================================================================
//  app/NetScene.h — the windowed network client (--connect HOST[:PORT]).
//
//  The server's world, drawn: the balls and other players where the server
//  says they are (interpolated, ~0.1 s in the past), and our own player,
//  white, where OUR inputs put it, at once (predicted; corrected when the
//  server disagrees). WASD / arrows move; the input goes to the server every
//  120 Hz tick. A status line every 5 s; Escape quits. If the server rejects
//  us or goes away, the game ends with exit code 1.
// =============================================================================

#include "Scene.h"
#include "Server.h"

#include "playground/core/Result.h"
#include "playground/engine/Actions.h"
#include "playground/engine/Application.h"
#include "playground/engine/Camera.h"
#include "playground/game/GameClient.h"

#include <filesystem>
#include <memory>

namespace playground::app {

class NetScene final : public Scene {
 public:
  [[nodiscard]] static Result<std::unique_ptr<NetScene>> create(engine::Application& application,
                                                                const ClientOptions& options,
                                                                std::filesystem::path sceneFile = "scene.json");

  void fixedUpdate(const FixedStep& step, const platform::InputState& input) override;
  void update(const FrameTime& time, const platform::InputState& input) override;
  void extract(render::FrameData& frame, f64 alpha) override;
  void logSummary() override;
  [[nodiscard]] int exitCode() const override { return failed_ ? 1 : 0; }
  void drawDebugUi(engine::ProfilerData data) override;

 private:
  NetScene(engine::Application& application, std::unique_ptr<game::GameClient> client, std::filesystem::path sceneFile);

  engine::Application& application_;
  std::unique_ptr<game::GameClient> client_;
  engine::ActionMap actions_ = engine::ActionMap::defaults();
  engine::Camera camera_;
  f64 secondsSinceStatus_ = 0.0;
  bool failed_ = false;
  engine::Editor editor_;
};

}  // namespace playground::app
