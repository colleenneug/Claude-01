// Tests for the ECS (entities, pools, queries) and the basic systems.
// No window, no GPU: these run anywhere.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/core/FixedStep.h"
#include "playground/ecs/World.h"
#include "playground/engine/Components.h"
#include "playground/engine/Systems.h"
#include "playground/engine/WorldBounds.h"
#include "playground/platform/Input.h"

#include <chrono>
#include <cmath>
#include <numbers>
#include <cstdio>
#include <set>
#include <vector>

using namespace playground;
using engine::Sprite;
using engine::Transform;
using engine::Velocity;

namespace {
struct Health {
  int points = 100;
};
struct PlayerTag {};
}  // namespace

// --- Entities -----------------------------------------------------------------

TEST_CASE("created entities are alive and distinct") {
  ecs::World world;
  const ecs::Entity a = world.create();
  const ecs::Entity b = world.create();
  CHECK(world.alive(a));
  CHECK(world.alive(b));
  CHECK(a != b);
  CHECK(world.entityCount() == 2);
  CHECK_FALSE(world.alive(ecs::kNullEntity));
}

TEST_CASE("destroy is deferred until flush") {
  ecs::World world;
  const ecs::Entity e = world.create();
  world.add<Health>(e);

  world.destroy(e);
  CHECK(world.alive(e));  // still here: queued
  CHECK(world.has<Health>(e));
  CHECK(world.pendingDestroyCount() == 1);

  world.flush();
  CHECK_FALSE(world.alive(e));
  CHECK_FALSE(world.has<Health>(e));
  CHECK(world.entityCount() == 0);
  CHECK(world.pendingDestroyCount() == 0);
}

TEST_CASE("destroying twice, or destroying a dead entity, is harmless") {
  ecs::World world;
  const ecs::Entity e = world.create();
  world.destroy(e);
  world.destroy(e);
  world.flush();
  world.destroy(e);
  world.flush();
  CHECK(world.entityCount() == 0);
}

TEST_CASE("a reused slot gets a new generation, and old handles stop working") {
  ecs::World world;
  const ecs::Entity old = world.create();
  world.add<Health>(old, Health{.points = 5});
  world.destroy(old);
  world.flush();

  const ecs::Entity fresh = world.create();
  world.add<Health>(fresh, Health{.points = 99});
  CHECK(fresh.index == old.index);  // the slot is reused...
  CHECK(fresh.generation != old.generation);  // ...by a different occupant

  CHECK_FALSE(world.alive(old));
  // The bug the engine/ blueprint had: a stale handle reading the new owner's data.
  CHECK(world.tryGet<Health>(old) == nullptr);
  CHECK_FALSE(world.has<Health>(old));
  REQUIRE(world.tryGet<Health>(fresh) != nullptr);
  CHECK(world.get<Health>(fresh).points == 99);
}

// --- Components ---------------------------------------------------------------

TEST_CASE("components can be added, read, replaced and removed") {
  ecs::World world;
  const ecs::Entity e = world.create();

  world.add<Transform>(e, Transform{.position = {1.0f, 2.0f}});
  REQUIRE(world.has<Transform>(e));
  CHECK(world.get<Transform>(e).position.x == 1.0f);

  world.add<Transform>(e, Transform{.position = {7.0f, 8.0f}});  // replaces
  CHECK(world.count<Transform>() == 1);
  CHECK(world.get<Transform>(e).position.y == 8.0f);

  world.remove<Transform>(e);
  CHECK_FALSE(world.has<Transform>(e));
  CHECK(world.tryGet<Transform>(e) == nullptr);
  CHECK(world.alive(e));  // removing a component does not destroy the entity
}

TEST_CASE("removing from the middle keeps every other entity's data right") {
  ecs::World world;
  std::vector<ecs::Entity> entities;
  for (int i = 0; i < 5; ++i) {
    entities.push_back(world.create());
    world.add<Health>(entities.back(), Health{.points = i * 10});
  }
  world.remove<Health>(entities[1]);  // swap-and-pop moves the last one into slot 1

  CHECK(world.count<Health>() == 4);
  CHECK_FALSE(world.has<Health>(entities[1]));
  for (int i : {0, 2, 3, 4}) CHECK(world.get<Health>(entities[static_cast<std::size_t>(i)]).points == i * 10);
}

TEST_CASE("a type nobody has yet is simply absent") {
  ecs::World world;
  const ecs::Entity e = world.create();
  CHECK_FALSE(world.has<Sprite>(e));
  CHECK(world.count<Sprite>() == 0);
  int calls = 0;
  world.each<Sprite>([&](ecs::Entity, Sprite&) { ++calls; });
  CHECK(calls == 0);
}

// --- Queries ------------------------------------------------------------------

TEST_CASE("each<A, B> visits exactly the entities that have both") {
  ecs::World world;
  const ecs::Entity both = world.create();
  const ecs::Entity onlyTransform = world.create();
  const ecs::Entity onlyVelocity = world.create();
  world.add<Transform>(both);
  world.add<Velocity>(both);
  world.add<Transform>(onlyTransform);
  world.add<Velocity>(onlyVelocity);

  std::vector<ecs::Entity> seen;
  world.each<Transform, Velocity>([&](ecs::Entity e, Transform&, Velocity&) { seen.push_back(e); });
  REQUIRE(seen.size() == 1);
  CHECK(seen[0] == both);
}

TEST_CASE("each<A, B, C> works with three components and edits in place") {
  ecs::World world;
  for (int i = 0; i < 10; ++i) {
    const ecs::Entity e = world.create();
    world.add<Health>(e);
    if (i % 2 == 0) world.add<Transform>(e);
    if (i % 3 == 0) world.add<PlayerTag>(e);
  }
  int visited = 0;  // i in {0, 6}: divisible by 2 and 3
  world.each<PlayerTag, Health, Transform>([&](ecs::Entity, PlayerTag&, Health& h, Transform&) {
    h.points = 1;
    ++visited;
  });
  CHECK(visited == 2);
  int edited = 0;
  world.each<Health>([&](ecs::Entity, Health& h) { edited += (h.points == 1) ? 1 : 0; });
  CHECK(edited == 2);
}

TEST_CASE("an entity can be destroyed while its pool is being walked") {
  ecs::World world;
  for (int i = 0; i < 6; ++i) world.add<Health>(world.create(), Health{.points = i});

  int visited = 0;
  world.each<Health>([&](ecs::Entity e, Health& h) {
    ++visited;
    if (h.points % 2 == 0) world.destroy(e);  // queued: the walk is unaffected
  });
  CHECK(visited == 6);
  world.flush();
  CHECK(world.count<Health>() == 3);
  world.each<Health>([](ecs::Entity, Health& h) { CHECK(h.points % 2 == 1); });
}

// --- Systems ------------------------------------------------------------------

TEST_CASE("movementSystem moves by velocity times delta time") {
  ecs::World world;
  const ecs::Entity moving = world.create();
  world.add<Transform>(moving, Transform{.position = {10.0f, 0.0f}});
  world.add<Velocity>(moving, Velocity{.linear = {20.0f, -4.0f}, .angular = 2.0f});
  const ecs::Entity still = world.create();  // no Velocity: must not move
  world.add<Transform>(still, Transform{.position = {5.0f, 5.0f}});

  engine::movementSystem(world, 0.5f);

  const Transform& t = world.get<Transform>(moving);
  CHECK(t.position.x == doctest::Approx(20.0f));
  CHECK(t.position.y == doctest::Approx(-2.0f));
  CHECK(t.rotation == doctest::Approx(1.0f));
  CHECK(world.get<Transform>(still).position.x == 5.0f);
}

TEST_CASE("bounceSystem keeps sprites inside the area and reflects their velocity") {
  ecs::World world;
  const ecs::Entity e = world.create();
  world.add<Transform>(e, Transform{.position = {105.0f, 0.0f}});  // past the right edge
  world.add<Velocity>(e, Velocity{.linear = {50.0f, 10.0f}});
  world.add<Sprite>(e, Sprite{.size = {20.0f, 20.0f}});

  engine::bounceSystem(world, engine::WorldBounds{.halfSize = {100.0f, 100.0f}});

  CHECK(world.get<Transform>(e).position.x == doctest::Approx(90.0f));  // 100 - half of 20
  CHECK(world.get<Velocity>(e).linear.x == doctest::Approx(-50.0f));    // now heading left
  CHECK(world.get<Velocity>(e).linear.y == doctest::Approx(10.0f));     // untouched

  // Already heading back in: no second flip, even if still outside.
  world.get<Transform>(e).position.x = 95.0f;
  engine::bounceSystem(world, engine::WorldBounds{.halfSize = {100.0f, 100.0f}});
  CHECK(world.get<Velocity>(e).linear.x == doctest::Approx(-50.0f));
}

TEST_CASE("extractSprites copies every Transform + Sprite into the frame") {
  ecs::World world;
  const ecs::Entity e = world.create();
  world.add<Transform>(e, Transform{.position = {3.0f, 4.0f}, .rotation = 0.5f, .scale = {2.0f, 1.0f}});
  world.add<Sprite>(e, Sprite{.size = {10.0f, 10.0f}, .colour = {1.0f, 0.0f, 0.0f, 1.0f}});
  world.add<Transform>(world.create());  // no Sprite: not drawn

  render::FrameData frame;
  engine::extractSprites(world, frame, 1.0);
  REQUIRE(frame.sprites.size() == 1);
  const render::SpriteInstance& s = frame.sprites[0];
  CHECK(s.position.x == 3.0f);
  CHECK(s.size.x == 20.0f);  // size times scale
  CHECK(s.size.y == 10.0f);
  CHECK(s.rotation == 0.5f);
  CHECK(s.colour.r == 1.0f);
}

// --- Fixed steps and interpolation -------------------------------------------

TEST_CASE("snapshotTransforms copies current into previous and marks it") {
  ecs::World world;
  const ecs::Entity e = world.create();
  world.add<Transform>(e, Transform{.position = {4.0f, 5.0f}, .rotation = 0.25f});
  CHECK_FALSE(world.get<Transform>(e).hasPrevious);

  engine::snapshotTransforms(world);
  const Transform& t = world.get<Transform>(e);
  CHECK(t.hasPrevious);
  CHECK(t.previousPosition == glm::vec2{4.0f, 5.0f});
  CHECK(t.previousRotation == 0.25f);
}

TEST_CASE("one step later, extraction blends previous -> current by alpha") {
  ecs::World world;
  const ecs::Entity e = world.create();
  world.add<Transform>(e, Transform{.position = {0.0f, 0.0f}});
  world.add<Velocity>(e, Velocity{.linear = {120.0f, -60.0f}, .angular = 1.2f});
  world.add<Sprite>(e);

  // One 1/120 s step: snapshot (previous = 0,0), then move to (1, -0.5).
  engine::snapshotTransforms(world);
  engine::movementSystem(world, 1.0f / 120.0f);

  for (const double alpha : {0.0, 0.25, 0.5, 1.0}) {
    render::FrameData frame;
    engine::extractSprites(world, frame, alpha);
    REQUIRE(frame.sprites.size() == 1);
    CHECK(frame.sprites[0].position.x == doctest::Approx(1.0 * alpha));
    CHECK(frame.sprites[0].position.y == doctest::Approx(-0.5 * alpha));
    CHECK(frame.sprites[0].rotation == doctest::Approx(0.01 * alpha));
  }
}

TEST_CASE("a newly spawned sprite is drawn where it is, not streaked from the origin") {
  ecs::World world;
  const ecs::Entity e = world.create();
  // Made the ordinary way, with no snapshot yet: previousPosition is (0, 0).
  world.add<Transform>(e, Transform{.position = {300.0f, -200.0f}, .rotation = 1.0f});
  world.add<Sprite>(e);

  for (const double alpha : {0.0, 0.5, 0.99}) {
    render::FrameData frame;
    engine::extractSprites(world, frame, alpha);
    REQUIRE(frame.sprites.size() == 1);
    CHECK(frame.sprites[0].position == glm::vec2{300.0f, -200.0f});  // not (150, -100) at alpha 0.5
    CHECK(frame.sprites[0].rotation == 1.0f);
  }
}

TEST_CASE("teleport moves without blending across the jump") {
  ecs::World world;
  const ecs::Entity e = world.create();
  world.add<Transform>(e, Transform{.position = {0.0f, 0.0f}});
  world.add<Sprite>(e);
  engine::snapshotTransforms(world);

  world.get<Transform>(e).teleport({500.0f, 500.0f}, 2.0f);
  render::FrameData frame;
  engine::extractSprites(world, frame, 0.5);
  CHECK(frame.sprites[0].position == glm::vec2{500.0f, 500.0f});
  CHECK(frame.sprites[0].rotation == 2.0f);
}

TEST_CASE("rotation blends the short way round across the +pi / -pi wrap") {
  constexpr float kPi = std::numbers::pi_v<float>;
  Transform t;
  t.previousRotation = kPi - 0.05f;   // just under +180 degrees
  t.rotation = -kPi + 0.05f;          // just over -180 degrees: a 0.1 rad turn, wrapped
  t.hasPrevious = true;
  const float halfway = engine::interpolateRotation(t, 0.5f);
  // The short way passes through 180 degrees; a plain lerp would give ~0 (facing backwards).
  CHECK(std::cos(halfway) == doctest::Approx(-1.0f).epsilon(1e-4));
  CHECK(std::fabs(std::sin(halfway)) < 1e-4f);
}

TEST_CASE("the fixed-step simulation is identical at 30 fps and at 144 fps") {
  // Run the real pipeline (accumulator + snapshot + movement + bounce) for
  // exactly 120 steps at two very different frame rates. Same steps in the
  // same order: the positions must match to the last bit.
  auto simulate = [](double frameSeconds) {
    ecs::World world;
    for (int i = 0; i < 50; ++i) {
      const ecs::Entity e = world.create();
      const auto n = static_cast<float>(i);
      const auto mod5 = static_cast<float>(i % 5);
      const auto mod7 = static_cast<float>(i % 7);
      world.add<Transform>(e, Transform{.position = {static_cast<float>(i * 7 % 300) - 150.0f, 10.0f * n - 250.0f}});
      world.add<Velocity>(e, Velocity{.linear = {37.0f * mod5 - 80.0f, 91.0f - 13.0f * mod7}, .angular = 0.3f});
      world.add<Sprite>(e, Sprite{.size = {10.0f, 10.0f}});
    }
    FixedStepAccumulator accumulator(1.0 / 120.0);
    u64 steps = 0;
    while (steps < 120) {
      const FixedStepPlan plan = accumulator.advance(frameSeconds);
      for (u32 i = 0; i < plan.steps && steps < 120; ++i, ++steps) {
        engine::snapshotTransforms(world);
        engine::movementSystem(world, static_cast<f32>(accumulator.stepSeconds()));
        engine::bounceSystem(world, engine::WorldBounds{.halfSize = {200.0f, 150.0f}});
      }
    }
    std::vector<glm::vec2> positions;
    world.each<Transform>([&](ecs::Entity, Transform& t) { positions.push_back(t.position); });
    return positions;
  };

  const std::vector<glm::vec2> at30 = simulate(1.0 / 30.0);
  const std::vector<glm::vec2> at144 = simulate(1.0 / 144.0);
  REQUIRE(at30.size() == at144.size());
  for (std::size_t i = 0; i < at30.size(); ++i) {
    CHECK(at30[i].x == at144[i].x);  // exact equality, on purpose
    CHECK(at30[i].y == at144[i].y);
  }
}

// --- Player input, lifetimes, world bounds, view ------------------------------

TEST_CASE("inputSystem steers only the player, at its speed") {
  ecs::World world;
  const ecs::Entity player = world.create();
  world.add<Transform>(player);
  world.add<Velocity>(player);
  world.add<engine::PlayerTag>(player, engine::PlayerTag{.speed = 200.0f});
  const ecs::Entity other = world.create();
  world.add<Transform>(other);
  world.add<Velocity>(other, Velocity{.linear = {5.0f, 5.0f}});

  platform::InputCollector collector;
  collector.keyEvent(platform::Key::D, true);
  const engine::ActionMap actions = engine::ActionMap::defaults();
  engine::inputSystem(world, actions, collector.takeSnapshot());

  CHECK(world.get<Velocity>(player).linear == glm::vec2{200.0f, 0.0f});
  CHECK(world.get<Velocity>(other).linear == glm::vec2{5.0f, 5.0f});  // not a player: untouched

  collector.keyEvent(platform::Key::D, false);  // let go: the player stops
  engine::inputSystem(world, actions, collector.takeSnapshot());
  CHECK(world.get<Velocity>(player).linear == glm::vec2{0.0f, 0.0f});
}

TEST_CASE("lifetimeSystem destroys an entity when its time runs out") {
  ecs::World world;
  const ecs::Entity bullet = world.create();
  world.add<engine::Lifetime>(bullet, engine::Lifetime{.secondsLeft = 0.25f});

  engine::lifetimeSystem(world, 0.125f);
  world.flush();
  CHECK(world.alive(bullet));
  engine::lifetimeSystem(world, 0.125f);
  world.flush();
  CHECK_FALSE(world.alive(bullet));
}

TEST_CASE("bouncing uses the world bounds, whatever size the window is") {
  // The same world, stepped identically, must end identically: bounceSystem
  // takes only the WorldBounds, so there is no window size it could read.
  auto run = [] {
    ecs::World world;
    const ecs::Entity e = world.create();
    world.add<Transform>(e, Transform{.position = {0.0f, 0.0f}});
    world.add<Velocity>(e, Velocity{.linear = {700.0f, 300.0f}});
    world.add<Sprite>(e, Sprite{.size = {20.0f, 20.0f}});
    const engine::WorldBounds bounds{.halfSize = {800.0f, 450.0f}};
    for (int step = 0; step < 600; ++step) {
      engine::movementSystem(world, 1.0f / 120.0f);
      engine::bounceSystem(world, bounds);
      CHECK(bounds.contains(world.get<Transform>(e).position));
    }
    return world.get<Transform>(e).position;
  };
  CHECK(run() == run());
}

TEST_CASE("fitView shows the whole world in any window shape, without stretching") {
  const glm::vec2 world{800.0f, 450.0f};  // 16:9
  // Same shape and size: one unit per pixel.
  CHECK(render::fitView({800.0f, 450.0f}, world) == glm::vec2{800.0f, 450.0f});
  // Same shape, half the size: everything still visible, scaled down.
  CHECK(render::fitView({400.0f, 225.0f}, world) == glm::vec2{800.0f, 450.0f});
  // A square window: the width decides; extra world shows above and below.
  const glm::vec2 square = render::fitView({300.0f, 300.0f}, world);
  CHECK(square.x == doctest::Approx(800.0f));
  CHECK(square.y == doctest::Approx(800.0f));
  // A very wide window: the height decides; extra world shows at the sides.
  const glm::vec2 wide = render::fitView({1000.0f, 250.0f}, world);
  CHECK(wide.y == doctest::Approx(450.0f));
  CHECK(wide.x == doctest::Approx(1800.0f));
  // No requirement: pixels.
  CHECK(render::fitView({640.0f, 360.0f}, {0.0f, 0.0f}) == glm::vec2{640.0f, 360.0f});
}

// --- Picking ---------------------------------------------------------------------

TEST_CASE("pickSprite finds the sprite under a point, exactly for rotated rectangles") {
  ecs::World world;
  const ecs::Entity square = world.create();
  // A 40x10 bar rotated 90 degrees: it now stands upright, 10 wide and 40 tall.
  world.add<Transform>(square, Transform{.position = {100.0f, 100.0f}, .rotation = 1.5707964f});
  world.add<Sprite>(square, Sprite{.size = {40.0f, 10.0f}});

  CHECK(engine::pickSprite(world, {100.0f, 117.0f}) == square);  // inside, near the top
  CHECK_FALSE(engine::pickSprite(world, {117.0f, 100.0f}).has_value());  // would be inside if not rotated
  CHECK_FALSE(engine::pickSprite(world, {500.0f, 500.0f}).has_value());
}

TEST_CASE("pickSprite prefers the higher layer, then the one drawn last") {
  ecs::World world;
  auto make = [&](float layer) {
    const ecs::Entity e = world.create();
    world.add<Transform>(e, Transform{.position = {0.0f, 0.0f}});
    world.add<Sprite>(e, Sprite{.size = {50.0f, 50.0f}, .layer = layer});
    return e;
  };
  const ecs::Entity high = make(2.0f);
  make(0.0f);
  CHECK(engine::pickSprite(world, {0.0f, 0.0f}) == high);  // in front, though created first

  ecs::World sameLayer;
  auto makeIn = [&](ecs::World& w) {
    const ecs::Entity e = w.create();
    w.add<Transform>(e, Transform{.position = {0.0f, 0.0f}});
    w.add<Sprite>(e, Sprite{.size = {50.0f, 50.0f}});
    return e;
  };
  makeIn(sameLayer);
  const ecs::Entity drawnLast = makeIn(sameLayer);
  CHECK(engine::pickSprite(sameLayer, {0.0f, 0.0f}) == drawnLast);
}

TEST_CASE("movement over 100,000 entities (timing printed, not asserted)") {
  ecs::World world;
  constexpr int kCount = 100'000;
  for (int i = 0; i < kCount; ++i) {
    const ecs::Entity e = world.create();
    world.add<Transform>(e);
    world.add<Velocity>(e, Velocity{.linear = {1.0f, 2.0f}});
  }
  engine::movementSystem(world, 1.0f / 60.0f);  // warm up

  const auto start = std::chrono::steady_clock::now();
  constexpr int kRuns = 20;
  for (int run = 0; run < kRuns; ++run) engine::movementSystem(world, 1.0f / 60.0f);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  std::printf("  movementSystem: %d entities in %.3f ms per run\n", kCount, ms / kRuns);

  int checked = 0;
  world.each<Transform>([&](ecs::Entity, Transform& t) {
    if (t.position.x > 0.0f) ++checked;
  });
  CHECK(checked == kCount);
}
