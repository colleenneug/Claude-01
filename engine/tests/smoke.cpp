// =============================================================================
//  smoke.cpp — exercises everything in the engine that does not need a GPU.
//
//  This exists to make a point as much as to catch regressions: because the
//  simulation systems take an explicit dt and are pure functions of their
//  inputs, gunplay and movement can be tuned and verified in a process that
//  never opens a window. That is not a testing nicety — it is what makes the
//  iteration loop seconds instead of minutes.
// =============================================================================

#include "erebus/core/GameLoop.h"
#include "erebus/ecs/Components.h"
#include "erebus/ecs/World.h"
#include "erebus/game/PlayerController.h"
#include "erebus/game/WeaponSystem.h"
#include "erebus/render/ClusteredLighting.h"

#include <cmath>
#include <cstdio>

using namespace erebus;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
  std::printf("  %s %s\n", condition ? "PASS" : "FAIL", what);
  if (!condition) ++failures;
}

// ---------------------------------------------------------------- ECS
void testEcs() {
  std::puts("ECS");
  ecs::World world;

  const ecs::Entity a = world.create();
  const ecs::Entity b = world.create();
  world.add<ecs::Transform>(a, ecs::Transform{});
  world.add<ecs::Transform>(b, ecs::Transform{});
  world.add<ecs::RigidBody>(a, ecs::RigidBody{});

  check(world.alive(a) && world.alive(b), "created entities are alive");
  check(world.pool<ecs::Transform>().size() == 2, "both transforms stored");

  int joined = 0;
  world.each<ecs::RigidBody, ecs::Transform>([&](ecs::Entity, ecs::RigidBody&, ecs::Transform&) {
    ++joined;
  });
  check(joined == 1, "two-component join visits only the entity with both");

  // The generational-handle guarantee: a stale handle to a recycled slot must
  // not resolve. This is the bug the generation counter exists for.
  world.destroy(a);
  world.flush();
  check(!world.alive(a), "a destroyed entity is not alive");

  const ecs::Entity recycled = world.create();
  check(recycled.index() == a.index(), "the slot was recycled");
  check(recycled != a, "...but the handle differs");
  check(!world.alive(a), "the stale handle stays dead after recycling");
  check(!world.pool<ecs::Transform>().contains(a), "components went with it");
}

// ---------------------------------------------------------------- game loop
void testGameLoop() {
  std::puts("Game loop");
  int steps = 0;
  int renders = 0;
  GameLoop loop([&](f32 dt) { (void)dt; ++steps; },
                [&](f32, const FrameStats&) { ++renders; });

  // Drive it directly rather than through run(): tick() reads a real clock, so
  // what is asserted is the invariant that holds regardless of timing — never
  // more catch-up steps than the cap, and exactly one render per tick.
  loop.setMaxStepsPerFrame(4);
  for (int i = 0; i < 10; ++i) loop.tick();

  check(renders == 10, "one render per tick");
  check(steps <= 40, "simulation steps never exceed the catch-up cap");
  check(loop.lastFrame().interpolation >= 0.0f && loop.lastFrame().interpolation < 1.0f,
        "interpolation alpha stays in [0,1)");
}

// ---------------------------------------------------------------- movement
void testMovement() {
  std::puts("Movement");
  game::PlayerController controller;
  const auto& cfg = controller.config();

  ecs::RigidBody body;
  ecs::CharacterState state;
  game::GroundProbe ground;
  ground.grounded = true;

  game::MovementInput input;
  input.moveForward = 1.0f;

  // Snappy: ground acceleration should reach walk speed in a fraction of a
  // second. If this ever regresses past ~0.2s the movement has gone mushy.
  f32 t = 0.0f;
  const f32 dt = GameLoop::kFixedDelta;
  while (t < 1.0f) {
    controller.step(body, state, input, ground, dt);
    t += dt;
    if (length(Vec3{body.velocity.x, 0.0f, body.velocity.z}) >= cfg.walkSpeed * 0.95f) break;
  }
  check(t < 0.25f, "reaches 95% of walk speed in under a quarter second");

  // ...and stops about as fast. Asymmetric stopping is what makes a character
  // feel like it is on ice.
  input.moveForward = 0.0f;
  f32 stopTime = 0.0f;
  while (stopTime < 2.0f) {
    controller.step(body, state, input, ground, dt);
    stopTime += dt;
    if (length(Vec3{body.velocity.x, 0.0f, body.velocity.z}) < 0.1f) break;
  }
  check(stopTime < 0.5f, "comes to rest in under half a second");

  // Floaty: airborne, horizontal speed is preserved rather than driven.
  body.velocity = Vec3{0.0f, 0.0f, -cfg.walkSpeed};
  ground.grounded = false;
  const f32 before = length(Vec3{body.velocity.x, 0.0f, body.velocity.z});
  for (int i = 0; i < 30; ++i) controller.step(body, state, input, ground, dt);
  const f32 after = length(Vec3{body.velocity.x, 0.0f, body.velocity.z});
  check(std::fabs(after - before) < 0.35f, "airborne horizontal momentum is preserved");
  check(body.velocity.y < 0.0f, "gravity accumulates while airborne");

  // Coyote time: a jump pressed just after walking off an edge still fires.
  body.velocity = Vec3{};
  state = ecs::CharacterState{};
  ground.grounded = true;
  controller.step(body, state, input, ground, dt);      // establish ground contact
  ground.grounded = false;
  input.jump = true;
  controller.step(body, state, input, ground, dt);
  check(body.velocity.y > cfg.jumpVelocity * 0.9f, "coyote time allows a late jump");
}

// ---------------------------------------------------------------- gunplay
void testWeapon() {
  std::puts("Gunplay");
  game::WeaponDef def;
  game::WeaponState state;
  state.ammoInMagazine = def.magazineSize;
  state.reserveAmmo = 90;

  // A stub world: everything is a hit at 10m. The point is that the weapon
  // logic is testable at all without a level loaded.
  game::WeaponSystem weapons([](const Vec3&, const Vec3& dir, f32) {
    game::RayHit h;
    h.hit = true;
    h.distance = 10.0f;
    h.point = dir * 10.0f;
    return h;
  });

  const Vec3 eye{}, fwd{0, 0, -1}, right{1, 0, 0}, up{0, 1, 0};

  state.bloom = def.bloomMin;
  auto first = weapons.fire(def, state, eye, fwd, right, up, 1);
  check(first.size() == def.pelletsPerShot, "a shot resolves its pellets");
  check(state.ammoInMagazine == def.magazineSize - 1, "a shot consumes a round");

  // Cadence: firing again immediately must be refused.
  auto immediate = weapons.fire(def, state, eye, fwd, right, up, 2);
  check(immediate.empty(), "the fire rate is enforced");

  // Bloom grows under sustained fire and settles afterwards.
  const f32 bloomAfterOne = state.bloom;
  for (int i = 0; i < 12; ++i) {
    for (f32 t = 0.0f; t < 60.0f / def.roundsPerMinute; t += GameLoop::kFixedDelta) {
      weapons.step(def, state, GameLoop::kFixedDelta);
    }
    weapons.fire(def, state, eye, fwd, right, up, static_cast<u32>(3 + i));
  }
  check(state.bloom > bloomAfterOne, "sustained fire blooms the cone");

  for (f32 t = 0.0f; t < 2.0f; t += GameLoop::kFixedDelta) {
    weapons.step(def, state, GameLoop::kFixedDelta);
  }
  check(state.bloom <= def.bloomMin + 1e-4f, "the cone settles back to its floor");

  // The reticle must be derived from the same bloom the shots use.
  state.bloom = def.bloomMax;
  const f32 wide = game::WeaponSystem::reticleRadiusPixels(state, 1.5708f, 1080.0f);
  state.bloom = def.bloomMin;
  const f32 tight = game::WeaponSystem::reticleRadiusPixels(state, 1.5708f, 1080.0f);
  check(wide > tight * 2.0f, "the reticle tracks the cone");

  // Recoil climbs and then recovers most, but not all, of the way.
  game::WeaponState r;
  r.ammoInMagazine = def.magazineSize;
  for (int i = 0; i < 6; ++i) {
    weapons.fire(def, r, eye, fwd, right, up, static_cast<u32>(100 + i));
    for (f32 t = 0.0f; t < 60.0f / def.roundsPerMinute; t += GameLoop::kFixedDelta) {
      weapons.step(def, r, GameLoop::kFixedDelta);
    }
  }
  check(r.recoilPitchOffset > 0.0f, "a burst climbs");
  const f32 peak = r.recoilPitchOffset;
  for (f32 t = 0.0f; t < 3.0f; t += GameLoop::kFixedDelta) weapons.step(def, r, GameLoop::kFixedDelta);
  check(r.recoilPitchOffset < peak * 0.5f, "and recovers once the trigger is released");

  // Reloading refills from reserve and resets the pattern.
  weapons.beginReload(def, state);
  for (f32 t = 0.0f; t < def.reloadSeconds + 0.1f; t += GameLoop::kFixedDelta) {
    weapons.step(def, state, GameLoop::kFixedDelta);
  }
  check(state.ammoInMagazine == def.magazineSize, "a reload refills the magazine");
  check(!state.reloading, "and finishes");
}

// ---------------------------------------------------------------- clustering
void testClustering() {
  std::puts("Clustered lighting");
  render::ClusterGridConfig cfg;
  cfg.tilesX = 16; cfg.tilesY = 9; cfg.slicesZ = 24;
  cfg.nearPlane = 0.1f; cfg.farPlane = 500.0f;
  render::ClusterGrid grid(cfg);

  check(grid.clusterCount() == 16u * 9u * 24u, "the froxel count is the grid product");
  check(grid.sliceForDepth(0.05f) == 0, "depth at or before the near plane is slice 0");
  check(grid.sliceForDepth(499.0f) == cfg.slicesZ - 1, "depth at the far plane is the last slice");

  // The exponential distribution's whole purpose: the near field must not all
  // land in one slice.
  const u32 s1 = grid.sliceForDepth(1.0f);
  const u32 s2 = grid.sliceForDepth(4.0f);
  check(s2 > s1, "the near field spans multiple slices");

  std::vector<render::GpuLight> lights(3);
  lights[0].positionViewSpace[2] = -10.0f; lights[0].radius = 5.0f;
  lights[1].positionViewSpace[2] = -40.0f; lights[1].radius = 12.0f;
  lights[2].positionViewSpace[2] = -2.0f;  lights[2].radius = 3.0f;

  grid.assign(lights, 1.0472f /* 60 deg */, 16.0f / 9.0f);
  std::size_t totalBinned = 0;
  for (std::size_t c = 0; c < grid.clusterCount(); ++c) totalBinned += grid.clusterOffsets()[c * 2 + 1];
  check(totalBinned > 0, "lights are binned into froxels");
  check(grid.clusterIndices().size() == totalBinned, "the flattened list matches the counts");
}

}  // namespace

int main() {
  std::puts("erebus_engine smoke\n");
  testEcs();
  testGameLoop();
  testMovement();
  testWeapon();
  testClustering();
  std::printf("\n%s (%d failure%s)\n", failures == 0 ? "OK" : "FAILED",
              failures, failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
