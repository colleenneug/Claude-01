#include "ModelScene.h"

#include "playground/core/Log.h"
#include "playground/engine/Components.h"
#include "playground/engine/Systems.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace playground::app {
namespace {

using log::Category;
using physics::MotionType;
using physics::ShapeDesc;

// The sun, shared by the light and the sky it is baked into.
// Low, from behind and to the left: shadows reach towards the camera, and
// the sun's disk sits near the top of the starting view (where it blooms).
constexpr glm::vec3 kSunDirection{0.55f, -0.62f, 0.55f};

// Walking.
constexpr f32 kWalkSpeed = 3.5f;    // m/s
constexpr f32 kRunMultiplier = 1.8f;
constexpr f32 kJumpSpeed = 4.5f;    // m/s up: about a metre high
constexpr f32 kEyeHeight = 1.6f;    // above the feet
constexpr glm::vec3 kWalkerStart{0.0f, 0.05f, 4.5f};
// Shooting.
constexpr f32 kShotRange = 60.0f;   // m
constexpr f32 kShotImpulse = 30.0f; // N·s: a 30 kg crate leaves at about 1 m/s

glm::quat yawDegrees(f32 degrees) { return glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f)); }
glm::quat tiltXDegrees(f32 degrees) { return glm::angleAxis(glm::radians(degrees), glm::vec3(1.0f, 0.0f, 0.0f)); }

std::string_view describe(physics::GroundState ground) {
  switch (ground) {
    case physics::GroundState::OnGround: return "on the ground";
    case physics::GroundState::OnSteepGround: return "on steep ground";
    case physics::GroundState::NotSupported: return "unsupported";
    case physics::GroundState::InAir: return "in the air";
  }
  return "?";
}

std::string_view describe(engine::CameraMode mode) {
  switch (mode) {
    case engine::CameraMode::Orbit: return "orbit";
    case engine::CameraMode::Fly: return "fly";
    case engine::CameraMode::Walk: return "walk";
  }
  return "?";
}

}  // namespace

Result<std::unique_ptr<ModelScene>> ModelScene::create(engine::AssetManager& assets, render::Renderer& renderer,
                                                       JobSystem& jobs, std::optional<std::filesystem::path> model,
                                                       std::optional<std::filesystem::path> environment) {
  auto physicsWorld = physics::PhysicsWorld::create({}, &jobs);
  if (!physicsWorld) return tl::unexpected(std::move(physicsWorld.error()));
  std::unique_ptr<ModelScene> scene(new ModelScene(assets, std::move(*physicsWorld)));
  scene->build(renderer, std::move(model), std::move(environment));
  return scene;
}

ModelScene::ModelScene(engine::AssetManager& assets, std::unique_ptr<physics::PhysicsWorld> physics)
    : assets_(assets), physicsWorld_(std::move(physics)), physics_(*physicsWorld_) {}

void ModelScene::build(render::Renderer& renderer, std::optional<std::filesystem::path> model,
                       std::optional<std::filesystem::path> environment) {
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

  // The ground: a solid slab under y = 0, in both kinds of scene.
  invisible("ground", {0.0f, -0.5f, 0.0f}, ShapeDesc::box({12.0f, 0.5f, 12.0f}));

  if (model) {
    showingOneModel_ = true;
    add(model->filename().string(), std::filesystem::absolute(*model), glm::vec3(0.0f));
  } else {
    add("floor", "models/floor.gltf", glm::vec3(0.0f));  // drawn; the ground above is what is solid

    // The turning crate: kinematic, so it pushes whatever it touches.
    spinner_ = add("crate", "models/crate.gltf", {-1.4f, 0.5f, 0.6f});
    solid(spinner_, MotionType::Kinematic, ShapeDesc::box(glm::vec3(0.5f)));
    const ecs::Entity small = add("small crate", "models/crate.gltf", {1.5f, 0.35f, 0.9f}, yawDegrees(28.0f),
                                  glm::vec3(0.7f));  // the same file: loaded once
    solid(small, MotionType::Dynamic, ShapeDesc::box(glm::vec3(0.35f)));

    add("spheres", "models/spheres.glb", {0.0f, 0.5f, -1.6f});
    for (int i = 0; i < 7; ++i) {  // the spheres' own places in spheres.glb: 1.25 m apart
      invisible("sphere " + std::to_string(i), {static_cast<f32>(i - 3) * 1.25f, 0.5f, -1.6f}, ShapeDesc::sphere(0.5f));
    }
    const ecs::Entity pedestal = add("pedestal", "models/crate.gltf", {0.1f, 0.3f, 1.4f}, yawDegrees(10.0f),
                                     glm::vec3(0.6f));
    solid(pedestal, MotionType::Static, ShapeDesc::box(glm::vec3(0.3f)));
    // The Khronos sample helmet (app/assets/third_party/DamagedHelmet/LICENSE.md:
    // credited, non-commercial use only).
    const ecs::Entity helmet = add("helmet", "third_party/DamagedHelmet/DamagedHelmet.glb", {0.1f, 0.98f, 1.4f},
                                   yawDegrees(15.0f), glm::vec3(0.38f));
    solid(helmet, MotionType::Static, ShapeDesc::sphere(0.34f));
    for (const auto& [name, x, z] : {std::tuple{"lamp", -2.7f, -0.4f}, std::tuple{"lamp 2", 2.8f, -0.2f}}) {
      add(name, "models/lamp.glb", {x, 0.0f, z});  // the same file: loaded once
      invisible(std::string(name) + " post", {x, 0.8f, z}, ShapeDesc::capsule(0.66f, 0.1f));
    }

    // A ramp (18 degrees: walkable) rising towards -z, and four 20 cm stairs
    // to a platform: the crate, stretched.
    const glm::quat rampTilt = tiltXDegrees(18.0f);
    const ecs::Entity ramp = add("ramp", "models/crate.gltf", {-4.2f, 0.47f, -0.6f}, rampTilt, {1.4f, 0.12f, 3.0f});
    solid(ramp, MotionType::Static, ShapeDesc::box({0.7f, 0.06f, 1.5f}));
    for (int step = 1; step <= 4; ++step) {
      const f32 height = 0.2f * static_cast<f32>(step);
      const ecs::Entity stair = add("stair " + std::to_string(step), "models/crate.gltf",
                                    {4.3f, 0.5f * height, 2.2f - 0.5f * static_cast<f32>(step)}, glm::quat(1, 0, 0, 0),
                                    {1.2f, height, 0.5f});
      solid(stair, MotionType::Static, ShapeDesc::box({0.6f, 0.5f * height, 0.25f}));
    }

    // A pyramid of six small crates, a little above the floor: it drops and settles.
    int n = 0;
    for (int row = 0; row < 3; ++row) {
      for (int i = 0; i < 3 - row; ++i) {
        const glm::vec3 at{2.0f + (static_cast<f32>(i) - 0.5f * static_cast<f32>(2 - row)) * 0.42f,
                           0.3f + static_cast<f32>(row) * 0.41f, -0.1f};
        ++n;
        const ecs::Entity box = add("stacked crate " + std::to_string(n), "models/crate.gltf", at,
                                    yawDegrees(static_cast<f32>(n) * 7.0f), glm::vec3(0.4f));
        solid(box, MotionType::Dynamic, ShapeDesc::box(glm::vec3(0.2f)));
        world_.get<engine::RigidBody>(box).mass = 30.0f;
      }
    }
  }

  // The character walk mode puts you in.
  walker_ = world_.create();
  walkerStart_ = kWalkerStart;
  world_.add<engine::Transform3D>(walker_, engine::Transform3D{.position = kWalkerStart});
  world_.add<engine::CharacterController>(walker_);
  names_[walker_.index] = "you";

  resetView();
  log::info(Category::Game, "3D scene: {} object(s) loading; drag to orbit (left), pan (right), wheel to zoom, "
            "Tab for fly mode (WASD, Q/E, right-drag to look, Shift for speed) and again for walk mode (WASD, "
            "Space jumps, left click shoots), middle click picks, R resets the view, Escape quits",
            objects_.size());
  log::info(Category::Game, "physics: {} solid object(s), a walking character, {}", world_.count<engine::Collider>(),
            physicsWorld_->multithreaded() ? "stepped on the job system" : "stepped on this thread");
}

ecs::Entity ModelScene::add(const std::string& name, const std::filesystem::path& path, glm::vec3 position,
                            glm::quat rotation, glm::vec3 scale) {
  const ecs::Entity entity = world_.create();
  world_.add<engine::Transform3D>(entity, engine::Transform3D{.position = position, .rotation = rotation,
                                                              .scale = scale});
  objects_.push_back({.name = name, .model = assets_.load<engine::Model>(path.generic_string()), .entity = entity});
  names_[entity.index] = name;
  return entity;
}

void ModelScene::solid(ecs::Entity entity, physics::MotionType motion, physics::ShapeDesc shape) {
  world_.add<engine::RigidBody>(entity, engine::RigidBody{.motion = motion});
  world_.add<engine::Collider>(entity, engine::Collider{.shape = std::move(shape)});
}

ecs::Entity ModelScene::invisible(const std::string& name, glm::vec3 position, physics::ShapeDesc shape,
                                  glm::quat rotation) {
  const ecs::Entity entity = world_.create();
  world_.add<engine::Transform3D>(entity, engine::Transform3D{.position = position, .rotation = rotation});
  solid(entity, MotionType::Static, std::move(shape));
  names_[entity.index] = name;
  return entity;
}

std::string ModelScene::nameOf(ecs::Entity entity) const {
  const auto it = names_.find(entity.index);
  return it != names_.end() && world_.alive(entity) ? it->second : std::string("something");
}

void ModelScene::resetView() {
  // Slightly from the right and above, the whole scene in view.
  controller_.orbit({0.0f, 0.5f, 0.0f}, 6.5f, glm::radians(-20.0f), glm::radians(-22.0f));
  framed_ = false;
}

// Every 1/120 s: the turning crate, the walker, shooting, then the physics.
void ModelScene::fixedUpdate(const FixedStep& step, const platform::InputState& input) {
  const auto dt = static_cast<f32>(step.deltaSeconds);
  simulatedSeconds_ += step.deltaSeconds;
  if (world_.alive(spinner_)) {  // a kinematic body: the game says where, physics pushes others out of the way
    world_.get<engine::Transform3D>(spinner_).rotation = yawDegrees(static_cast<f32>(simulatedSeconds_) * 20.0f);
  }

  auto& walker = world_.get<engine::CharacterController>(walker_);
  walker.moveVelocity = glm::vec3(0.0f);
  if (controller_.mode() == engine::CameraMode::Walk) {
    // WASD along the ground, relative to where you look.
    const f32 yaw = controller_.yaw();
    const glm::vec3 ahead{std::sin(yaw), 0.0f, -std::cos(yaw)};
    const glm::vec3 right{std::cos(yaw), 0.0f, std::sin(yaw)};
    auto key = [&](platform::Key k) { return input.down(k) ? 1.0f : 0.0f; };
    glm::vec3 move = ahead * (key(platform::Key::W) + key(platform::Key::Up) - key(platform::Key::S) -
                              key(platform::Key::Down)) +
                     right * (key(platform::Key::D) + key(platform::Key::Right) - key(platform::Key::A) -
                              key(platform::Key::Left));
    if (glm::length(move) > 0.0f) move = glm::normalize(move);
    const bool run = input.down(platform::Key::LeftShift) || input.down(platform::Key::RightShift);
    walker.moveVelocity = move * kWalkSpeed * (run ? kRunMultiplier : 1.0f);
    if (input.pressed(platform::Key::Space)) walker.jumpSpeed = kJumpSpeed;
    if (input.pressed(platform::MouseButton::Left)) shoot();
  }
  if (input.pressed(platform::MouseButton::Middle)) pick(input);

  physics_.step(world_, dt);
}

// A hitscan shot from the eye, where the camera looks.
void ModelScene::shoot() {
  ++shots_;
  const glm::vec3 eye = world_.get<engine::Transform3D>(walker_).position + glm::vec3(0.0f, kEyeHeight, 0.0f);
  const glm::vec3 direction = controller_.forward();
  const auto hit = physicsWorld_->raycast(eye, direction, kShotRange);
  if (!hit) {
    log::info(Category::Game, "shot {}: missed", shots_);
    return;
  }
  ++hits_;
  const auto body = world_.alive(hit->entity) ? world_.tryGet<engine::RigidBody>(hit->entity) : nullptr;
  const bool pushed = body != nullptr && body->motion == MotionType::Dynamic;
  if (pushed) {
    physicsWorld_->addImpulse(hit->body, direction * kShotImpulse, hit->point);
    ++pushes_;
  }
  log::info(Category::Game, "shot {}: hit {} at {:.2f} m, point ({:.2f}, {:.2f}, {:.2f}), normal ({:.2f}, {:.2f}, "
            "{:.2f}){}", shots_, nameOf(hit->entity), hit->distance, hit->point.x, hit->point.y, hit->point.z,
            hit->normal.x, hit->normal.y, hit->normal.z, pushed ? ": pushed" : "");
}

// What is under the mouse: a ray from the camera through the cursor.
void ModelScene::pick(const platform::InputState& input) {
  const engine::Ray ray = camera_.screenRay({static_cast<f32>(input.mouseX()), static_cast<f32>(input.mouseY())},
                                            viewport_);
  const auto hit = physicsWorld_->raycast(ray.origin, ray.direction, glm::length(ray.direction));
  if (hit) {
    log::info(Category::Game, "picked {} at {:.2f} m", nameOf(hit->entity), hit->distance);
  } else {
    log::info(Category::Game, "picked nothing");
  }
}

void ModelScene::update(const FrameTime& time, const platform::InputState& input) {
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

  if (input.pressed(platform::Key::Tab)) {  // orbit -> fly -> walk -> orbit
    const engine::CameraMode next = controller_.mode() == engine::CameraMode::Orbit ? engine::CameraMode::Fly
                                    : controller_.mode() == engine::CameraMode::Fly ? engine::CameraMode::Walk
                                                                                    : engine::CameraMode::Orbit;
    controller_.setMode(next);
    log::info(Category::Game, "camera: {}", describe(next));
  }
  if (input.pressed(platform::Key::R)) resetView();
  if (controller_.mode() == engine::CameraMode::Walk) {
    controller_.setEye(world_.get<engine::Transform3D>(walker_).position + glm::vec3(0.0f, kEyeHeight, 0.0f));
  }
  controller_.update(input, static_cast<f32>(time.deltaSeconds), camera_);

  if (!started_) {
    startEye_ = lastEye_ = camera_.position;
    started_ = true;
  }
  travelled_ += glm::length(camera_.position - lastEye_);
  lastEye_ = camera_.position;
}

void ModelScene::extract(render::FrameData& frame, f64 alpha) {
  const auto blend = static_cast<f32>(alpha);
  viewport_ = frame.viewportSize;
  if (controller_.mode() == engine::CameraMode::Walk) {
    // The eye rides the walker, blended like everything else (no 120 Hz judder).
    const glm::mat4 feet = engine::interpolatedMatrix(world_.get<engine::Transform3D>(walker_), blend);
    camera_.position = glm::vec3(feet[3]) + glm::vec3(0.0f, kEyeHeight, 0.0f);
    camera_.target = camera_.position + controller_.forward();
  }
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
    objects.add({-5.2f, 0.0f, -2.6f});
    objects.add({5.0f, 2.0f, 2.6f});
    frame.shadows.bounds = objects;
  } else {
    frame.shadows.bounds.reset();  // --model: whatever the model covers
  }
  for (const Object& object : objects_) {
    const engine::Model* model = assets_.get(object.model);
    if (model == nullptr || !world_.alive(object.entity)) continue;
    model->extract(engine::interpolatedMatrix(world_.get<engine::Transform3D>(object.entity), blend), frame);
  }
}

// The end-to-end camera and physics tests (tests/e2e/camera.sh, physics.sh) read these lines.
void ModelScene::logSummary() {
  usize ready = 0;
  for (const Object& object : objects_) ready += assets_.state(object.model) == engine::AssetState::Ready ? 1 : 0;
  const glm::vec3 moved = lastEye_ - startEye_;
  log::info(Category::Game, "camera ended in {} mode, {:.2f} units from where it started ({:.2f} travelled); "
            "{} of {} object(s) ready",
            describe(controller_.mode()), glm::length(moved), travelled_, ready, objects_.size());
  const glm::vec3 at = world_.get<engine::Transform3D>(walker_).position;
  const physics::PhysicsStats stats = physicsWorld_->stats();
  log::info(Category::Game, "walker ended at ({:.2f}, {:.2f}, {:.2f}), {:.2f} m from the start, {}; {} shot(s), {} "
            "hit(s), {} push(es); physics: {} steps, {} bodies ({} awake)",
            at.x, at.y, at.z, glm::length(at - walkerStart_),
            describe(world_.get<engine::CharacterController>(walker_).ground), shots_, hits_, pushes_, stats.steps,
            stats.bodies, stats.activeBodies);
}

}  // namespace playground::app
