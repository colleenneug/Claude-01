#pragma once
// =============================================================================
//  app/NetScene.h — the windowed network client (--connect HOST[:PORT]).
//
//  The server's world, drawn: the balls and other players where the server
//  says they are (interpolated, ~0.1 s in the past), and our own player,
//  white, where OUR inputs put it, at once (predicted; corrected when the
//  server disagrees). When the server starts a co-op cutscene, it plays
//  here on the server's tick (the same moment for every player), the camera
//  flying over the playfield; holding Space or Escape votes to skip it (the
//  server skips when every player does). WASD / arrows move; the input goes to the server every
//  120 Hz tick. A status line every 5 s; Escape quits. If the server rejects
//  us or goes away, the game ends with exit code 1.
// =============================================================================

#include "Scene.h"
#include "Server.h"

#include "playground/core/Result.h"
#include "playground/engine/Actions.h"
#include "playground/engine/Application.h"
#include "playground/engine/Camera.h"
#include "playground/engine/Cutscene.h"
#include "playground/game/GameClient.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>

namespace playground::app {

class NetScene final : public Scene {
 public:
  [[nodiscard]] static Result<std::unique_ptr<NetScene>> create(engine::Application& application,
                                                                const ClientOptions& options,
                                                                std::filesystem::path sceneFile = "scene.json",
                                                                std::filesystem::path cutsceneDirectory = {});

  void fixedUpdate(const FixedStep& step, const platform::InputState& input) override;
  void update(const FrameTime& time, const platform::InputState& input) override;
  void extract(render::FrameData& frame, f64 alpha) override;
  void logSummary() override;
  [[nodiscard]] int exitCode() const override { return failed_ ? 1 : 0; }
  void drawDebugUi(engine::ProfilerData data) override;
  [[nodiscard]] bool wantsHud() const override { return cutscene_.active() || cutscene_.handingOff(); }
  void drawHud() override;
  [[nodiscard]] bool consumesEscape() const override { return cutscene_.active(); }

 private:
  NetScene(engine::Application& application, std::unique_ptr<game::GameClient> client, std::filesystem::path sceneFile);

  engine::Application& application_;
  std::unique_ptr<game::GameClient> client_;
  engine::ActionMap actions_ = engine::ActionMap::defaults();
  engine::Camera camera_;
  f64 secondsSinceStatus_ = 0.0;
  bool failed_ = false;
  engine::Editor editor_;

  // Co-op cutscenes: the sequences this client knows (by the server's id),
  // the one playing, on server ticks.
  std::unordered_map<u32, engine::CutsceneSequence> sequences_;
  engine::CutscenePlayer cutscene_;
  std::optional<u32> cueStart_;   // the start tick of the cue being played
  u64 lastServerTick_ = 0;
  bool holdingSkip_ = false;
  u32 cutscenesSeen_ = 0;
};

}  // namespace playground::app
