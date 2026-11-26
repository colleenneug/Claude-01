// Tests for the ECS (entities, pools, queries) and the basic systems.
// No window, no GPU: these run anywhere.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/ecs/World.h"
#include "playground/engine/Components.h"
#include "playground/engine/Systems.h"

#include <chrono>
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

  engine::bounceSystem(world, {100.0f, 100.0f});

  CHECK(world.get<Transform>(e).position.x == doctest::Approx(90.0f));  // 100 - half of 20
  CHECK(world.get<Velocity>(e).linear.x == doctest::Approx(-50.0f));    // now heading left
  CHECK(world.get<Velocity>(e).linear.y == doctest::Approx(10.0f));     // untouched

  // Already heading back in: no second flip, even if still outside.
  world.get<Transform>(e).position.x = 95.0f;
  engine::bounceSystem(world, {100.0f, 100.0f});
  CHECK(world.get<Velocity>(e).linear.x == doctest::Approx(-50.0f));
}

TEST_CASE("extractSprites copies every Transform + Sprite into the frame") {
  ecs::World world;
  const ecs::Entity e = world.create();
  world.add<Transform>(e, Transform{.position = {3.0f, 4.0f}, .rotation = 0.5f, .scale = {2.0f, 1.0f}});
  world.add<Sprite>(e, Sprite{.size = {10.0f, 10.0f}, .colour = {1.0f, 0.0f, 0.0f, 1.0f}});
  world.add<Transform>(world.create());  // no Sprite: not drawn

  render::FrameData frame;
  engine::extractSprites(world, frame);
  REQUIRE(frame.sprites.size() == 1);
  const render::SpriteInstance& s = frame.sprites[0];
  CHECK(s.position.x == 3.0f);
  CHECK(s.size.x == 20.0f);  // size times scale
  CHECK(s.size.y == 10.0f);
  CHECK(s.rotation == 0.5f);
  CHECK(s.colour.r == 1.0f);
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
