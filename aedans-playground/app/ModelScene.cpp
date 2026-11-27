#include "ModelScene.h"

#include "playground/core/Log.h"

#include <algorithm>
#include <utility>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace playground::app {
namespace {

using log::Category;

// The sun, shared by the light and the sky it is baked into.
// Low, from behind and to the left: shadows reach towards the camera, and
// the sun's disk sits near the top of the starting view (where it blooms).
constexpr glm::vec3 kSunDirection{0.55f, -0.62f, 0.55f};

glm::mat4 at(glm::vec3 position, f32 yawDegrees = 0.0f, f32 scale = 1.0f) {
  return glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), position), glm::radians(yawDegrees), {0, 1, 0}),
                    glm::vec3(scale));
}

}  // namespace

ModelScene::ModelScene(engine::AssetManager& assets, render::Renderer& renderer,
                       std::optional<std::filesystem::path> model, std::optional<std::filesystem::path> environment)
    : assets_(assets) {
  // The sky and its light: a procedural sky now (a fraction of a second to
  // bake), replaced by the panorama if one was given, once it has loaded.
  if (Status s = renderer.setEnvironment(render::ProceduralSky{.sunDirection = kSunDirection}, {}); !s) {
    log::warn(Category::Game, "no environment lighting: {}", s.error().message);  // the scene still draws
  }
  if (environment) {
    environment_ = assets_.load<engine::EnvironmentAsset>(std::filesystem::absolute(*environment).generic_string());
  }

  camera_.projection = engine::Projection::Perspective;
  camera_.fovY = glm::radians(50.0f);
  camera_.nearPlane = 0.05f;  // a metre-scale scene: the sprite demo's near plane of 1 would clip it
  camera_.farPlane = 500.0f;

  if (model) {
    showingOneModel_ = true;
    add(model->filename().string(), std::filesystem::absolute(*model), glm::mat4(1.0f));
  } else {
    add("floor", "models/floor.gltf", glm::mat4(1.0f));
    add("crate", "models/crate.gltf", at({-1.4f, 0.5f, 0.6f}, 0.0f), /*spin=*/0.35f);
    add("small crate", "models/crate.gltf", at({1.5f, 0.35f, 0.9f}, 28.0f, 0.7f));  // the same file: loaded once
    add("spheres", "models/spheres.glb", at({0.0f, 0.5f, -1.6f}));
    add("pedestal", "models/crate.gltf", at({0.1f, 0.3f, 1.4f}, 10.0f, 0.6f));
    // The Khronos sample helmet (app/assets/third_party/DamagedHelmet/LICENSE.md:
    // credited, non-commercial use only).
    add("helmet", "third_party/DamagedHelmet/DamagedHelmet.glb", at({0.1f, 0.98f, 1.4f}, 15.0f, 0.38f));
    add("lamp", "models/lamp.glb", at({-2.7f, 0.0f, -0.4f}));
    add("lamp 2", "models/lamp.glb", at({2.8f, 0.0f, -0.2f}));  // the same file: loaded once
  }
  resetView();
  log::info(Category::Game, "3D scene: {} object(s) loading; drag to orbit (left), pan (right), wheel to zoom, "
            "Tab for fly mode (WASD, Q/E, right-drag to look, Shift for speed), R resets the view, Escape quits",
            objects_.size());
}

void ModelScene::add(const std::string& name, const std::filesystem::path& path, glm::mat4 transform, f32 spin) {
  objects_.push_back({.name = name, .model = assets_.load<engine::Model>(path.generic_string()),
                      .transform = transform, .spin = spin});
}

void ModelScene::resetView() {
  // Slightly from the right and above, the whole scene in view.
  controller_.orbit({0.0f, 0.5f, 0.0f}, 6.5f, glm::radians(-20.0f), glm::radians(-22.0f));
  framed_ = false;
}

void ModelScene::update(const FrameTime& time, const platform::InputState& input) {
  elapsed_ += time.deltaSeconds;

  // Report each model once: ready (and, with --model, frame the camera on it) or failed.
  for (Object& object : objects_) {
    if (object.reported) continue;
    if (const engine::Model* model = assets_.get(object.model)) {  // ready
      object.reported = true;
      log::info(Category::Game, "{} ready: {} mesh(es), {} texture(s)", object.name, model->meshCount(),
                model->textureCount());
      if (showingOneModel_ && !framed_ && !model->bounds().empty()) {
        const engine::Bounds& b = model->bounds();
        controller_.frame(b.centre(), 0.5f * glm::length(b.size()), camera_.fovY, glm::radians(-20.0f),
                          glm::radians(-15.0f));
        framed_ = true;
      }
    } else if (const Error* error = assets_.error(object.model)) {  // failed
      object.reported = true;
      log::error(Category::Game, "{} could not be loaded: {}", object.name, error->message);
    }
  }

  if (environment_ && !environmentReported_) {
    if (const engine::EnvironmentAsset* sky = assets_.get(*environment_)) {
      environmentReported_ = true;
      log::info(Category::Game, "environment ready: {}x{} panorama", sky->width, sky->height);
    } else if (const Error* error = assets_.error(*environment_)) {
      environmentReported_ = true;
      log::error(Category::Game, "environment could not be loaded (keeping the procedural sky): {}", error->message);
    }
  }

  if (input.pressed(platform::Key::Tab)) {
    const bool toFly = controller_.mode() == engine::CameraMode::Orbit;
    controller_.setMode(toFly ? engine::CameraMode::Fly : engine::CameraMode::Orbit);
    log::info(Category::Game, "camera: {}", toFly ? "fly" : "orbit");
  }
  if (input.pressed(platform::Key::R)) resetView();
  controller_.update(input, static_cast<f32>(time.deltaSeconds), camera_);

  if (!started_) {
    startEye_ = lastEye_ = camera_.position;
    started_ = true;
  }
  travelled_ += glm::length(camera_.position - lastEye_);
  lastEye_ = camera_.position;
}

void ModelScene::extract(render::FrameData& frame, f64 /*alpha*/) {
  frame.viewProjection = camera_.viewProjection(frame.viewportSize);
  frame.cameraPosition = camera_.position;
  frame.sun = {.direction = kSunDirection, .colour = {1.0f, 0.95f, 0.88f}, .intensity = 3.2f};
  frame.ambient = glm::vec3(0.0f);  // the environment lights the shadows now
  frame.environmentIntensity = 1.0f;
  frame.drawSky = true;
  frame.toneMap = render::ToneMap::Aces;  // sun + sky add up past 1: roll off instead of clipping
  frame.exposure = 1.15f;                 // ACES darkens mid-tones a little: lift them back
  frame.bloom = {.enabled = true, .threshold = 1.2f, .knee = 0.6f, .strength = 0.12f};
  frame.shadows.enabled = true;
  if (!showingOneModel_) {
    // Frame the shadow map round the objects, not the whole 24 m floor: a
    // much finer map where the shadows are.
    render::Aabb objects;
    objects.add({-3.5f, 0.0f, -2.6f});
    objects.add({3.5f, 2.0f, 2.4f});
    frame.shadows.bounds = objects;
  } else {
    frame.shadows.bounds.reset();  // --model: whatever the model covers
  }
  for (const Object& object : objects_) {
    if (const engine::Model* model = assets_.get(object.model)) {
      const glm::mat4 spun =
          object.spin == 0.0f ? object.transform
                              : glm::rotate(object.transform, static_cast<f32>(elapsed_) * object.spin, {0, 1, 0});
      model->extract(spun, frame);
    }
  }
}

// The end-to-end camera test (tests/e2e/camera.sh) reads this line.
void ModelScene::logSummary() {
  usize ready = 0;
  for (const Object& object : objects_) ready += assets_.state(object.model) == engine::AssetState::Ready ? 1 : 0;
  const glm::vec3 moved = lastEye_ - startEye_;
  log::info(Category::Game, "camera ended in {} mode, {:.2f} units from where it started ({:.2f} travelled); "
            "{} of {} object(s) ready",
            controller_.mode() == engine::CameraMode::Fly ? "fly" : "orbit", glm::length(moved), travelled_, ready,
            objects_.size());
}

}  // namespace playground::app
