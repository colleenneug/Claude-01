#include "SpriteScene.h"

#include "playground/audio/Synth.h"
#include "playground/core/Log.h"
#include "playground/engine/Components.h"
#include "playground/engine/Systems.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

#include <glm/geometric.hpp>

namespace playground::app {
namespace {

using log::Category;

// Layers (world z): the depth test puts higher layers in front.
constexpr f32 kPlayfieldLayer = -1.0f;
constexpr f32 kBulletLayer = 1.0f;
constexpr f32 kPlayerLayer = 2.0f;

// `count` coloured squares with random positions, velocities, sizes and
// spins, inside `halfArea`. A fixed seed, so every run starts the same
// (handy for comparing screenshots).
void spawnBouncingSprites(ecs::World& world, u32 count, glm::vec2 halfArea) {
  std::mt19937 rng(20261126u);
  auto uniform = [&rng](f32 lo, f32 hi) { return std::uniform_real_distribution<f32>(lo, hi)(rng); };

  // Sprite size from the count, so the crowd covers the world about twice
  // over whatever its number: big squares for a few hundred, small ones for
  // tens of thousands.
  const f32 worldArea = 4.0f * halfArea.x * halfArea.y;
  const f32 typicalSize =
      std::clamp(std::sqrt(2.0f * worldArea / static_cast<f32>(std::max(count, 1u))), 6.0f, 30.0f);

  for (u32 i = 0; i < count; ++i) {
    const ecs::Entity entity = world.create();
    const f32 size = uniform(0.5f * typicalSize, 1.5f * typicalSize);
    const f32 heading = uniform(0.0f, 2.0f * std::numbers::pi_v<f32>);
    const f32 speed = uniform(60.0f, 260.0f);
    world.add<engine::Transform>(entity, engine::Transform{
        .position = {uniform(-halfArea.x, halfArea.x), uniform(-halfArea.y, halfArea.y)},
        .rotation = uniform(0.0f, std::numbers::pi_v<f32>),
    });
    world.add<engine::Velocity>(entity, engine::Velocity{
        .linear = speed * glm::vec2{std::cos(heading), std::sin(heading)},
        .angular = uniform(-3.0f, 3.0f),
    });
    world.add<engine::Sprite>(entity, engine::Sprite{
        .size = {size, size},
        .colour = {uniform(0.05f, 1.0f), uniform(0.05f, 1.0f), uniform(0.05f, 1.0f), uniform(0.6f, 1.0f)},
        .layer = 0.0f,
    });
  }
}

// The square the player steers (WASD / arrows), in front of everything.
ecs::Entity spawnPlayer(ecs::World& world) {
  const ecs::Entity player = world.create();
  world.add<engine::Transform>(player, engine::Transform{.position = {0.0f, 0.0f}});
  world.add<engine::Velocity>(player);
  world.add<engine::Sprite>(player, engine::Sprite{.size = {44.0f, 44.0f}, .colour = {1.0f, 1.0f, 1.0f, 1.0f},
                                                   .layer = kPlayerLayer});
  world.add<engine::PlayerTag>(player);
  world.add<engine::Name>(player, engine::Name{"player"});
  return player;
}

// Fire: a small bright bullet from the player towards `direction` (unit
// length), gone after a moment. One press, one bullet, however many fixed
// steps the frame runs.
void spawnBullet(ecs::World& world, glm::vec2 from, glm::vec2 direction) {
  const ecs::Entity bullet = world.create();
  world.add<engine::Transform>(bullet, engine::Transform{.position = from + 30.0f * direction});
  world.add<engine::Velocity>(bullet, engine::Velocity{.linear = 900.0f * direction, .angular = 12.0f});
  world.add<engine::Sprite>(bullet, engine::Sprite{.size = {10.0f, 10.0f}, .colour = {1.0f, 0.85f, 0.2f, 1.0f},
                                                   .layer = kBulletLayer});
  world.add<engine::Lifetime>(bullet, engine::Lifetime{.secondsLeft = 1.2f});
}

}  // namespace

SpriteScene::SpriteScene(engine::Application& application, u32 spriteCount, std::filesystem::path sceneFile,
                         audio::AudioEngine* sound)
    : application_(application), editor_(std::move(sceneFile)), sound_(sound) {
  if (sound_ != nullptr) {
    const audio::Pcm laser = audio::synth::laser(sound_->sampleRate());
    if (auto clip = sound_->addClip(laser.samples, 1, laser.sampleRate, "laser")) laserClip_ = *clip;
  }
  spawnBouncingSprites(world_, spriteCount, bounds_.halfSize);
  player_ = spawnPlayer(world_);
  // Orthographic: the whole world, fitted to the window.
  camera_.orthoHalfExtent = bounds_.halfSize;
  setProjection(engine::Projection::Orthographic);
  log::info(Category::Game, "spawned {} sprites and the player; move with WASD or the arrow keys, aim with the "
            "mouse, fire with Space or the left button, right-click removes a sprite, C switches 2D/3D view, "
            "Escape quits", world_.count<engine::Sprite>() - 1);
}

void SpriteScene::setProjection(engine::Projection projection) {
  camera_.projection = projection;
  if (projection == engine::Projection::Orthographic) {
    camera_.position = {0.0f, 0.0f, 1000.0f};
    camera_.target = {0.0f, 0.0f, 0.0f};
  } else {
    camera_.position = {0.0f, -1150.0f, 950.0f};  // in front of and above the playfield, looking down
    camera_.target = {0.0f, -60.0f, 0.0f};
  }
}

// The point on the playfield under the mouse, if any (in 3D it can miss).
std::optional<glm::vec2> SpriteScene::mouseOnPlayfield(const platform::InputState& input) const {
  const platform::Extent2D window = application_.window().windowSize();
  const auto hit = camera_.screenToWorld({static_cast<f32>(input.mouseX()), static_cast<f32>(input.mouseY())},
                                         {static_cast<f32>(window.width), static_cast<f32>(window.height)});
  return hit ? std::optional<glm::vec2>(glm::vec2(*hit)) : std::nullopt;
}

// Snapshot first, so extraction can blend from where things were. `input`
// holds the same keys for every step in a frame; a press reaches one step.
void SpriteScene::fixedUpdate(const FixedStep& step, const platform::InputState& input) {
  const auto deltaSeconds = static_cast<f32>(step.deltaSeconds);
  engine::snapshotTransforms(world_);
  engine::inputSystem(world_, actions_, input);
  if (actions_.pressed(input, engine::actions::kFire) && world_.alive(player_) &&
      world_.has<engine::Transform>(player_)) {
    // Aim at the mouse; straight up if it is off the playfield or on the player.
    const glm::vec2 from = world_.get<engine::Transform>(player_).position;
    glm::vec2 direction{0.0f, 1.0f};
    if (const auto target = mouseOnPlayfield(input); target && glm::length(*target - from) > 1.0f) {
      direction = glm::normalize(*target - from);
      log::debug(Category::Game, "fire at ({:.0f}, {:.0f}): direction ({:.2f}, {:.2f})", target->x, target->y,
                 direction.x, direction.y);
    }
    spawnBullet(world_, from, direction);
    if (sound_ != nullptr && laserClip_.valid()) {
      // Panned by where the player is: the listener sits 2 m in front of the
      // playfield's middle, and 400 pixels count as a metre to the side.
      (void)sound_->play(laserClip_, {.volume = 0.5f, .position = {from.x / 400.0f, 0.0f, -2.0f}, .minDistance = 3.0f});
    }
    ++bulletsFired_;
  }
  if (input.pressed(platform::MouseButton::Right)) {  // click a sprite: it goes
    if (const auto point = mouseOnPlayfield(input)) {
      if (const auto hit = engine::pickSprite(world_, *point); hit && *hit != player_) world_.destroy(*hit);
    }
  }
  engine::movementSystem(world_, deltaSeconds);
  engine::bounceSystem(world_, bounds_);
  engine::lifetimeSystem(world_, deltaSeconds);
  world_.flush();  // apply any destroys the systems queued
}

void SpriteScene::update(const FrameTime& /*time*/, const platform::InputState& input) {
  if (sound_ != nullptr) sound_->update();  // frees finished shots
  if (input.pressed(platform::Key::C)) {
    const bool to3d = camera_.projection == engine::Projection::Orthographic;
    setProjection(to3d ? engine::Projection::Perspective : engine::Projection::Orthographic);
    log::info(Category::Game, "camera: {}", to3d ? "perspective (3D)" : "orthographic (2D)");
  }
}

// The camera, the playfield, then the sprites blended `alpha` of the way
// from the previous step to the current one.
void SpriteScene::extract(render::FrameData& frame, f64 alpha) {
  frame.viewProjection = camera_.viewProjection(frame.viewportSize);
  frame.sprites.push_back({.position = {0.0f, 0.0f}, .size = bounds_.size(), .colour = {0.03f, 0.03f, 0.07f, 1.0f},
                           .depth = kPlayfieldLayer});
  engine::extractSprites(world_, frame, alpha);
}

void SpriteScene::drawDebugUi(engine::ProfilerData data) {
  data.entities = world_.entityCount();
  if (editor_.draw(world_, data)) {
    // A loaded scene replaced every entity: steer the first player in it.
    player_ = {};
    world_.each<engine::PlayerTag>([&](ecs::Entity e, engine::PlayerTag&) {
      if (!player_.valid()) player_ = e;
    });
  }
}

// The end-to-end keyboard test (tests/e2e/keyboard.sh) reads this line.
void SpriteScene::logSummary() {
  const engine::Transform* player = world_.alive(player_) ? world_.tryGet<engine::Transform>(player_) : nullptr;
  const glm::vec2 playerAt = player != nullptr ? player->position : glm::vec2(0.0f);
  log::info(Category::Game, "player ended at x={:.0f}, y={:.0f}; {} bullets fired", playerAt.x, playerAt.y,
            bulletsFired_);
}

}  // namespace playground::app
