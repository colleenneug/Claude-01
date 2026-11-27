#pragma once
// =============================================================================
//  app/ModelScene.h — the 3D demo (the default scene): glTF models, lit by
//  the sun, seen through an orbit or fly camera.
//
//    the default scene  a tiled floor, two crates (one turning slowly) and a
//                       row of spheres from rough plastic to polished gold
//                       (app/assets/models, made by tools/make_demo_assets.py)
//    --model PATH       any glTF 2.0 file (.gltf or .glb) instead, with the
//                       camera framed on it once it has loaded
//
//    the sky            a procedural sky baked into image-based lighting
//                       (render/Environment.h), so metals reflect it;
//    --environment PATH an HDR panorama (.hdr) instead, once loaded
//
//  Models and panoramas load in the background (engine::AssetManager): the
//  window opens at once, and each appears when it is ready.
//
//  Controls: see engine/CameraController.h. Tab switches orbit <-> fly;
//  R puts the camera back where it started.
// =============================================================================

#include "Scene.h"

#include "playground/engine/Application.h"
#include "playground/engine/Assets.h"
#include "playground/engine/Camera.h"
#include "playground/engine/CameraController.h"
#include "playground/engine/EnvironmentAsset.h"
#include "playground/engine/Gltf.h"
#include "playground/render/Renderer.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace playground::app {

class ModelScene final : public Scene {
 public:
  // Models and panoramas must be registered with `assets`
  // (engine::gltfLoader, engine::environmentLoader). `model`: a glTF file to
  // show instead of the default scene; `environment`: an .hdr panorama to
  // light it with instead of the procedural sky.
  ModelScene(engine::AssetManager& assets, render::Renderer& renderer, std::optional<std::filesystem::path> model,
             std::optional<std::filesystem::path> environment);

  void update(const FrameTime& time, const platform::InputState& input) override;
  void extract(render::FrameData& frame, f64 alpha) override;
  void logSummary() override;

 private:
  struct Object {
    std::string name;
    engine::AssetHandle<engine::Model> model;
    glm::mat4 transform{1.0f};
    f32 spin = 0.0f;               // radians per second about y (0: still)
    bool reported = false;         // ready/failed has been logged
  };

  void add(const std::string& name, const std::filesystem::path& path, glm::mat4 transform, f32 spin = 0.0f);
  void resetView();

  engine::AssetManager& assets_;
  std::vector<Object> objects_;
  std::optional<engine::AssetHandle<engine::EnvironmentAsset>> environment_;
  bool environmentReported_ = false;
  bool showingOneModel_ = false;  // --model: frame the camera on it once loaded
  bool framed_ = false;
  engine::Camera camera_;
  engine::CameraController controller_;
  f64 elapsed_ = 0.0;
  glm::vec3 startEye_{0.0f};
  bool started_ = false;
  f32 travelled_ = 0.0f;  // how far the camera moved, for the summary (tests/e2e/camera.sh)
  glm::vec3 lastEye_{0.0f};
};

}  // namespace playground::app
