#pragma once
// =============================================================================
//  app/ModelScene.h — the 3D demo (the default scene): glTF models, lit by
//  the sun, with physics, seen through an orbit, fly or walking camera.
//
//    the default scene  a tiled floor, crates (one turning, pushing what it
//                       touches), a row of spheres from rough plastic to
//                       polished gold, the helmet on a pedestal, lamps, a
//                       ramp, a flight of stairs and a stack of small crates
//                       that falls and settles (app/assets/models, made by
//                       tools/make_demo_assets.py)
//    --model PATH       any glTF 2.0 file (.gltf or .glb) instead, with the
//                       camera framed on it once it has loaded
//
//    the sky            a procedural sky baked into image-based lighting
//                       (render/Environment.h), so metals reflect it;
//    --environment PATH an HDR panorama (.hdr) instead, once loaded
//
//  PHYSICS (physics::PhysicsWorld through engine::PhysicsSystem): every
//  solid thing is an ECS entity with a Transform3D, a RigidBody and a
//  Collider, stepped at the fixed 120 Hz and drawn blended between steps.
//
//  Controls: see engine/CameraController.h. Tab cycles orbit -> fly -> walk;
//  R puts the camera back where it started. WALK puts you in a character
//  (a 1.8 m capsule: slopes, stairs, walls): WASD, Shift to run, Space to
//  jump, right-drag to look, left click to SHOOT (a hitscan raycast from the
//  eye: crates hit get a push; every hit is logged). Middle click, in any
//  mode, picks what is under the mouse (logged).
//
//  F1 opens the developer UI: the scene is an ECS world (Name, Transform3D,
//  MeshRenderer, Material, the sun as a Light, RigidBody, Collider, the
//  walker's CharacterController) to inspect, edit, save (Ctrl+S) and load
//  (Ctrl+O, --scene-file).
//
//  Models and panoramas load in the background (engine::AssetManager): the
//  window opens at once, and each appears when it is ready.
// =============================================================================

#include "Scene.h"

#include "playground/audio/AudioEngine.h"
#include "playground/core/JobSystem.h"
#include "playground/core/Result.h"
#include "playground/ecs/World.h"
#include "playground/engine/Assets.h"
#include "playground/engine/Camera.h"
#include "playground/engine/CameraController.h"
#include "playground/engine/Components.h"
#include "playground/engine/Cutscene.h"
#include "playground/engine/Editor.h"
#include "playground/engine/EnvironmentAsset.h"
#include "playground/engine/Gltf.h"
#include "playground/engine/Physics.h"
#include "playground/physics/PhysicsWorld.h"
#include "playground/render/Renderer.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace playground::app {

// The demo's cutscene (engine/Cutscene.h): a JSON file, played with C (or at
// start with --cutscene).
struct ModelCutscene {
  std::filesystem::path file;  // empty or missing: no cutscene
  bool playAtStart = false;
};

class ModelScene final : public Scene {
 public:
  // Models and panoramas must be registered with `assets`
  // (engine::gltfLoader, engine::environmentLoader). `jobs`: the physics
  // steps on these workers. `model`: a glTF file to show instead of the
  // default scene; `environment`: an .hdr panorama to light it with instead
  // of the procedural sky. Fails only if the physics cannot start.
  [[nodiscard]] static Result<std::unique_ptr<ModelScene>> create(engine::AssetManager& assets,
                                                                  render::Renderer& renderer, JobSystem& jobs,
                                                                  std::optional<std::filesystem::path> model,
                                                                  std::optional<std::filesystem::path> environment,
                                                                  std::filesystem::path sceneFile = "scene.json",
                                                                  audio::AudioEngine* sound = nullptr,
                                                                  ModelCutscene cutscene = {});

  void fixedUpdate(const FixedStep& step, const platform::InputState& input) override;
  void update(const FrameTime& time, const platform::InputState& input) override;
  void extract(render::FrameData& frame, f64 alpha) override;
  void logSummary() override;
  void drawDebugUi(engine::ProfilerData data) override;
  [[nodiscard]] bool wantsHud() const override { return cutscene_.active() || cutscene_.handingOff(); }
  void drawHud() override;
  [[nodiscard]] bool consumesEscape() const override { return cutscene_.active(); }

 private:
  ModelScene(engine::AssetManager& assets, std::unique_ptr<physics::PhysicsWorld> physics,
             std::filesystem::path sceneFile, audio::AudioEngine* sound);
  void build(render::Renderer& renderer, std::optional<std::filesystem::path> model,
             std::optional<std::filesystem::path> environment, const ModelCutscene& cutscene);
  // Cutscenes: start one on this fixed-step tick; what its events do.
  void startCutscene(u64 tick);
  void onCutsceneEvent(const engine::CutsceneEvent& event);
  // The player's own camera (orbit, fly or walk) as a pose.
  [[nodiscard]] engine::CameraPose playerPose() const;

  // A drawn model at a place: Name, Transform3D (scale: the model's size),
  // MeshRenderer and a default Material (no physics unless solid() adds it).
  ecs::Entity add(const std::string& name, const std::filesystem::path& path, glm::vec3 position,
                  glm::quat rotation = glm::quat(1, 0, 0, 0), glm::vec3 scale = glm::vec3(1.0f));
  // Physics for an entity (or, with invisible(), an invisible solid).
  void solid(ecs::Entity entity, physics::MotionType motion, physics::ShapeDesc shape);
  ecs::Entity invisible(const std::string& name, glm::vec3 position, physics::ShapeDesc shape,
                        glm::quat rotation = glm::quat(1, 0, 0, 0));
  // Finds the entities the scene drives (the walker, the turning crate),
  // after building and after a scene file is loaded.
  void relink();
  [[nodiscard]] const engine::Model* modelOf(const engine::MeshRenderer& renderer);
  void resetView();
  void shoot();
  void pick(const platform::InputState& input);
  [[nodiscard]] std::string nameOf(ecs::Entity entity) const;

  engine::AssetManager& assets_;
  std::unique_ptr<physics::PhysicsWorld> physicsWorld_;
  engine::PhysicsSystem physics_;
  ecs::World world_;
  engine::Editor editor_;
  std::unordered_map<std::string, engine::AssetHandle<engine::Model>> models_;  // by MeshRenderer::model
  std::unordered_map<u32, u32> reported_;  // entity index -> generation whose model's ready/failed was logged
  std::optional<engine::AssetHandle<engine::EnvironmentAsset>> environment_;
  bool environmentReported_ = false;
  bool showingOneModel_ = false;  // --model: frame the camera on it once loaded
  bool framed_ = false;

  engine::Camera camera_;
  engine::CameraController controller_;
  glm::vec2 viewport_{1.0f};
  glm::vec3 startEye_{0.0f};
  bool started_ = false;
  f32 travelled_ = 0.0f;  // how far the camera moved, for the summary (tests/e2e/camera.sh)
  glm::vec3 lastEye_{0.0f};

  // Sound (null: none): the clips, made from maths at start-up, and the
  // listener entity riding the camera.
  audio::AudioEngine* sound_;
  audio::ClipId laserClip_;
  audio::ClipId impactClip_;
  audio::ClipId humClip_;
  ecs::Entity listener_;
  void playAt(audio::ClipId clip, glm::vec3 where, f32 volume, bool spatial = true);
  void playImpacts();

  audio::ClipId explosionClip_;

  std::optional<engine::CutsceneSequence> intro_;
  engine::CutscenePlayer cutscene_;
  bool cutsceneAtStart_ = false;
  u64 tick_ = 0;  // fixed steps so far: the cutscene clock
  u32 cutscenesPlayed_ = 0;
  u32 cutscenesSkipped_ = 0;
  u32 cutsceneEvents_ = 0;

  ecs::Entity spinner_;  // the turning crate (kinematic), if there is one
  f64 simulatedSeconds_ = 0.0;
  ecs::Entity walker_;   // the character walk mode puts you in
  glm::vec3 walkerStart_{0.0f};
  u32 shots_ = 0;
  u32 hits_ = 0;
  u32 pushes_ = 0;
};

}  // namespace playground::app
