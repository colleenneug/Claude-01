// Tests for physics::PhysicsWorld (Jolt behind our interface) and the ECS
// sync in engine::PhysicsSystem: free fall against the formula, landing on
// a static floor, bouncing, the character controller on slopes, stairs and
// walls, raycasts (entity, point, normal, distance, layer masks), the same
// numbers with and without worker threads, and the transforms the renderer
// interpolates between.
//
// No window, no GPU. Also run under AddressSanitizer and ThreadSanitizer
// (label "threads": the multi-threaded tests use the job-system bridge).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/core/JobSystem.h"
#include "playground/ecs/World.h"
#include "playground/engine/Components.h"
#include "playground/engine/Physics.h"
#include "playground/engine/Systems.h"
#include "playground/physics/PhysicsWorld.h"

#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

using namespace playground;
using physics::BodyDesc;
using physics::MotionType;
using physics::PhysicsWorld;
using physics::ShapeDesc;

namespace {

constexpr f32 kStep = 1.0f / 120.0f;

std::unique_ptr<PhysicsWorld> makeWorld(JobSystem* jobs = nullptr, glm::vec3 gravity = {0.0f, -9.81f, 0.0f}) {
  auto world = PhysicsWorld::create({.gravity = gravity}, jobs);
  REQUIRE_MESSAGE(world.has_value(), (world ? "" : world.error().message));
  return std::move(*world);
}

// A 40 x 1 x 40 m static slab whose top is at y = 0.
physics::BodyId addFloor(PhysicsWorld& world, ecs::Entity entity = {}) {
  auto floor = world.addBody({.shape = ShapeDesc::box({20.0f, 0.5f, 20.0f}), .motion = MotionType::Static,
                              .position = {0.0f, -0.5f, 0.0f}}, entity);
  REQUIRE(floor.has_value());
  return *floor;
}

void run(PhysicsWorld& world, int steps) {
  for (int i = 0; i < steps; ++i) world.step(kStep);
}

// A rotation of `degrees` about x: a ramp rising towards -z.
glm::quat tiltX(f32 degrees) { return glm::angleAxis(glm::radians(degrees), glm::vec3(1.0f, 0.0f, 0.0f)); }

}  // namespace

// --- Bodies ---------------------------------------------------------------------------

TEST_CASE("free fall follows y = y0 - g t^2 / 2") {
  auto world = makeWorld();
  auto ball = world->addBody({.shape = ShapeDesc::sphere(0.25f), .position = {0.0f, 100.0f, 0.0f},
                              .linearDamping = 0.0f});
  REQUIRE(ball.has_value());
  run(*world, 120);  // one second
  const auto state = world->bodyState(*ball);
  REQUIRE(state.has_value());
  const f32 expected = 100.0f - 0.5f * 9.81f * 1.0f * 1.0f;
  // Jolt integrates velocity, then position (semi-implicit Euler): about
  // g dt t / 2 = 4 cm low after a second at 120 Hz. Within 5 cm.
  CHECK(state->position.y == doctest::Approx(expected).epsilon(0.0006));
  CHECK(state->linearVelocity.y == doctest::Approx(-9.81f).epsilon(0.001));
  CHECK(state->position.x == 0.0f);
  CHECK(state->active);
}

TEST_CASE("a falling box lands on a static floor, stops on it, and goes to sleep") {
  auto world = makeWorld();
  const physics::BodyId floor = addFloor(*world);
  auto box = world->addBody({.shape = ShapeDesc::box(glm::vec3(0.5f)), .position = {0.0f, 3.0f, 0.0f}});
  REQUIRE(box.has_value());
  run(*world, 360);  // three seconds
  const auto state = world->bodyState(*box);
  REQUIRE(state.has_value());
  CHECK(state->position.y == doctest::Approx(0.5f).epsilon(0.02));  // resting on the top face at y = 0
  CHECK(glm::length(state->linearVelocity) < 0.01f);
  CHECK_FALSE(state->active);  // at rest: asleep, costs nothing
  const auto floorState = world->bodyState(floor);
  REQUIRE(floorState.has_value());
  CHECK(floorState->position == glm::vec3(0.0f, -0.5f, 0.0f));  // static: never moved
  CHECK(world->stats().bodies == 2);
}

TEST_CASE("restitution: a bouncy ball bounces, a dead one does not") {
  auto world = makeWorld();
  addFloor(*world);
  auto bouncy = world->addBody({.shape = ShapeDesc::sphere(0.25f), .position = {-2.0f, 2.0f, 0.0f}, .restitution = 0.9f});
  auto dead = world->addBody({.shape = ShapeDesc::sphere(0.25f), .position = {2.0f, 2.0f, 0.0f}, .restitution = 0.0f});
  REQUIRE(bouncy.has_value());
  REQUIRE(dead.has_value());
  f32 bouncyPeak = 0.0f;
  f32 deadPeak = 0.0f;
  bool bouncyLanded = false;
  bool deadLanded = false;
  for (int i = 0; i < 240; ++i) {
    world->step(kStep);
    const f32 by = world->bodyState(*bouncy)->position.y;
    const f32 dy = world->bodyState(*dead)->position.y;
    bouncyLanded = bouncyLanded || by < 0.3f;
    deadLanded = deadLanded || dy < 0.3f;
    if (bouncyLanded) bouncyPeak = std::max(bouncyPeak, by);
    if (deadLanded) deadPeak = std::max(deadPeak, dy);
  }
  CHECK(bouncyPeak > 1.0f);  // back up most of the 1.75 m it fell
  CHECK(deadPeak < 0.3f);    // stays down
}

TEST_CASE("mass: given, or from the volume at the density of water") {
  auto world = makeWorld(nullptr, glm::vec3(0.0f));
  auto light = world->addBody({.shape = ShapeDesc::box(glm::vec3(0.5f)), .position = {-3, 0, 0}, .mass = 10.0f,
                               .linearDamping = 0.0f});
  auto heavy = world->addBody({.shape = ShapeDesc::box(glm::vec3(0.5f)), .position = {3, 0, 0}, .linearDamping = 0.0f});
  REQUIRE(light.has_value());
  REQUIRE(heavy.has_value());
  // The same push: speed = impulse / mass (10 kg vs a 1 m³ box of water, 1000 kg).
  world->addImpulse(*light, {100.0f, 0.0f, 0.0f}, glm::vec3(-3, 0, 0));
  world->addImpulse(*heavy, {100.0f, 0.0f, 0.0f}, glm::vec3(3, 0, 0));
  world->step(kStep);
  CHECK(world->bodyState(*light)->linearVelocity.x == doctest::Approx(10.0f).epsilon(0.001));
  CHECK(world->bodyState(*heavy)->linearVelocity.x == doctest::Approx(0.1f).epsilon(0.02));
}

TEST_CASE("bad shapes and bodies are refused with a reason; removed bodies are gone") {
  auto world = makeWorld();
  const auto zeroBox = world->addBody({.shape = ShapeDesc::box({1.0f, 0.0f, 1.0f})});
  REQUIRE_FALSE(zeroBox.has_value());
  CHECK(zeroBox.error().code == Errc::InvalidArgument);
  CHECK_FALSE(world->addBody({.shape = ShapeDesc::sphere(-1.0f)}).has_value());
  CHECK_FALSE(world->addBody({.shape = ShapeDesc::capsule(0.0f, 0.5f)}).has_value());
  CHECK_FALSE(world->addBody({.shape = ShapeDesc::box(glm::vec3(1.0f)), .position = {NAN, 0, 0}}).has_value());
  const auto dynamicMesh = world->addBody({.shape = ShapeDesc::mesh({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}, {0, 1, 2}),
                                           .motion = MotionType::Dynamic});
  REQUIRE_FALSE(dynamicMesh.has_value());
  CHECK(dynamicMesh.error().message.find("mesh") != std::string::npos);
  CHECK_FALSE(world->addBody({.shape = ShapeDesc::mesh({{0, 0, 0}}, {0, 1, 2}), .motion = MotionType::Static}));
  CHECK_FALSE(world->addCharacter({.radius = 0.5f, .height = 0.8f}).has_value());  // shorter than its width

  auto box = world->addBody({.shape = ShapeDesc::box(glm::vec3(0.5f))});
  REQUIRE(box.has_value());
  CHECK(world->contains(*box));
  world->removeBody(*box);
  CHECK_FALSE(world->contains(*box));
  CHECK_FALSE(world->bodyState(*box).has_value());
  world->removeBody(*box);  // twice: harmless
  world->setVelocity(*box, glm::vec3(1.0f));  // stale: ignored
}

TEST_CASE("a kinematic body moves where it is told and pushes a dynamic one") {
  auto world = makeWorld();
  addFloor(*world);
  auto pusher = world->addBody({.shape = ShapeDesc::box({0.5f, 0.5f, 1.0f}), .motion = MotionType::Kinematic,
                                .position = {-2.0f, 0.5f, 0.0f}});
  auto crate = world->addBody({.shape = ShapeDesc::box(glm::vec3(0.4f)), .position = {0.0f, 0.4f, 0.0f}});
  REQUIRE(pusher.has_value());
  REQUIRE(crate.has_value());
  run(*world, 30);  // settle
  for (int i = 1; i <= 240; ++i) {  // 2 m/s along +x for two seconds
    world->moveKinematic(*pusher, {-2.0f + 2.0f * static_cast<f32>(i) * kStep, 0.5f, 0.0f}, glm::quat(1, 0, 0, 0), kStep);
    world->step(kStep);
  }
  CHECK(world->bodyState(*pusher)->position.x == doctest::Approx(2.0f).epsilon(0.001));
  CHECK(world->bodyState(*crate)->position.x > 2.0f);  // shoved ahead of it
}

// --- The character controller ------------------------------------------------------------

TEST_CASE("character: stands on the floor, walks, and jumps") {
  auto world = makeWorld();
  addFloor(*world);
  auto walker = world->addCharacter({.position = {0.0f, 0.5f, 0.0f}});
  REQUIRE(walker.has_value());
  physics::CharacterState state;
  for (int i = 0; i < 120; ++i) {  // drop the half metre and settle
    state = world->moveCharacter(*walker, {}, kStep);
    world->step(kStep);
  }
  CHECK(state.ground == physics::GroundState::OnGround);
  CHECK(state.position.y == doctest::Approx(0.0f).epsilon(0.02));
  for (int i = 0; i < 120; ++i) {  // one second at 3 m/s along +x
    state = world->moveCharacter(*walker, {.velocity = {3.0f, 0.0f, 0.0f}}, kStep);
    world->step(kStep);
  }
  CHECK(state.position.x == doctest::Approx(3.0f).epsilon(0.05));
  CHECK(state.ground == physics::GroundState::OnGround);
  f32 peak = 0.0f;
  state = world->moveCharacter(*walker, {.jumpSpeed = 5.0f}, kStep);
  for (int i = 0; i < 180; ++i) {  // up and down takes 2v/g = 1.02 s: give it 1.5
    world->step(kStep);
    state = world->moveCharacter(*walker, {}, kStep);
    peak = std::max(peak, state.position.y);
  }
  CHECK(peak == doctest::Approx(5.0f * 5.0f / (2.0f * 9.81f)).epsilon(0.08));  // v² / 2g = 1.27 m
  CHECK(state.ground == physics::GroundState::OnGround);  // and back down
}

TEST_CASE("character: walks up a gentle slope, is stopped by a steep one") {
  for (const f32 degrees : {20.0f, 60.0f}) {
    CAPTURE(degrees);
    auto world = makeWorld();
    addFloor(*world);
    // A 10 m ramp hinged at z = -1, rising towards -z.
    const f32 a = glm::radians(degrees);
    auto ramp = world->addBody({.shape = ShapeDesc::box({2.0f, 0.1f, 5.0f}), .motion = MotionType::Static,
                                .position = {0.0f, 5.0f * std::sin(a), -1.0f - 5.0f * std::cos(a)},
                                .rotation = tiltX(degrees)});
    REQUIRE(ramp.has_value());
    auto walker = world->addCharacter({.position = {0.0f, 0.0f, 1.0f}, .maxSlopeDegrees = 45.0f});
    REQUIRE(walker.has_value());
    physics::CharacterState state;
    for (int i = 0; i < 360; ++i) {  // three seconds walking towards -z at 2 m/s
      state = world->moveCharacter(*walker, {.velocity = {0.0f, 0.0f, -2.0f}}, kStep);
      world->step(kStep);
    }
    if (degrees < 45.0f) {
      CHECK(state.position.y > 1.0f);  // well up the ramp
      CHECK(state.position.z < -2.5f);
    } else {
      CHECK(state.position.y < 0.4f);  // at the bottom: only a step's height at most
      CHECK(state.position.z > -2.0f);
    }
  }
}

TEST_CASE("character: climbs a stair step, not a wall; slides along a wall it walks into at an angle") {
  auto world = makeWorld();
  addFloor(*world);
  // A 25 cm step from z = -1 on, a 1.5 m wall along x = 3.
  REQUIRE(world->addBody({.shape = ShapeDesc::box({2.0f, 0.125f, 2.0f}), .motion = MotionType::Static,
                          .position = {0.0f, 0.125f, -3.0f}}).has_value());
  REQUIRE(world->addBody({.shape = ShapeDesc::box({0.25f, 0.75f, 10.0f}), .motion = MotionType::Static,
                          .position = {3.25f, 0.75f, 0.0f}}).has_value());
  auto climber = world->addCharacter({.position = {0.0f, 0.0f, 1.0f}, .stepHeight = 0.35f});
  auto slider = world->addCharacter({.position = {2.0f, 0.0f, 4.0f}});
  REQUIRE(climber.has_value());
  REQUIRE(slider.has_value());
  physics::CharacterState up;
  physics::CharacterState along;
  for (int i = 0; i < 240; ++i) {
    up = world->moveCharacter(*climber, {.velocity = {0.0f, 0.0f, -2.0f}}, kStep);
    along = world->moveCharacter(*slider, {.velocity = {2.0f, 0.0f, 2.0f}}, kStep);  // into the wall, at 45 degrees
    world->step(kStep);
  }
  CHECK(up.position.y == doctest::Approx(0.25f).epsilon(0.05));  // on top of the step
  CHECK(up.position.z < -1.5f);
  CHECK(up.ground == physics::GroundState::OnGround);
  CHECK(along.position.x < 3.0f - 0.3f + 0.05f);  // stopped by the wall (its radius away)...
  CHECK(along.position.x > 2.5f);
  CHECK(along.position.z > 7.0f);  // ...but kept going along it
}

// --- Raycasts -------------------------------------------------------------------------

TEST_CASE("raycast: the entity, point, normal and distance of the first hit") {
  auto world = makeWorld();
  const ecs::Entity floorEntity{.index = 7, .generation = 2};
  const ecs::Entity crateEntity{.index = 8, .generation = 0};
  addFloor(*world, floorEntity);
  auto crate = world->addBody({.shape = ShapeDesc::box(glm::vec3(0.5f)), .motion = MotionType::Static,
                               .position = {0.0f, 0.5f, -5.0f}}, crateEntity);
  REQUIRE(crate.has_value());

  // Straight ahead along -z at chest height: the crate's front face at z = -4.5.
  const auto front = world->raycast({0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, -1.0f}, 100.0f);
  REQUIRE(front.has_value());
  CHECK(front->entity == crateEntity);
  CHECK(front->body == *crate);
  CHECK(front->distance == doctest::Approx(4.5f).epsilon(0.001));
  CHECK(glm::length(front->point - glm::vec3(0.0f, 0.5f, -4.5f)) < 1e-3f);
  CHECK(glm::length(front->normal - glm::vec3(0.0f, 0.0f, 1.0f)) < 1e-3f);

  // Down, from above: the floor; the direction need not be unit length.
  const auto down = world->raycast({3.0f, 2.0f, 3.0f}, {0.0f, -10.0f, 0.0f}, 10.0f);
  REQUIRE(down.has_value());
  CHECK(down->entity == floorEntity);
  CHECK(down->distance == doctest::Approx(2.0f).epsilon(0.001));
  CHECK(glm::length(down->normal - glm::vec3(0.0f, 1.0f, 0.0f)) < 1e-3f);

  // Too short, into the sky, or a zero direction: nothing.
  CHECK_FALSE(world->raycast({0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, -1.0f}, 4.0f).has_value());
  CHECK_FALSE(world->raycast({0.0f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, 100.0f).has_value());
  CHECK_FALSE(world->raycast({0.0f, 0.5f, 0.0f}, glm::vec3(0.0f), 100.0f).has_value());
}

TEST_CASE("raycast: layer masks pick what can be hit; a removed body is not hit") {
  auto world = makeWorld();
  auto wall = world->addBody({.shape = ShapeDesc::box({2.0f, 2.0f, 0.1f}), .motion = MotionType::Static,
                              .position = {0.0f, 0.0f, -10.0f}});
  auto debris = world->addBody({.shape = ShapeDesc::box(glm::vec3(0.2f)), .motion = MotionType::Kinematic,
                                .layer = physics::Layer::Debris, .position = {0.0f, 0.0f, -3.0f}});
  REQUIRE(wall.has_value());
  REQUIRE(debris.has_value());
  CHECK(world->raycast({}, {0, 0, -1}, 100.0f)->body == *debris);
  CHECK(world->raycast({}, {0, 0, -1}, 100.0f, physics::maskOf(physics::Layer::Static))->body == *wall);
  CHECK_FALSE(world->raycast({}, {0, 0, -1}, 100.0f, physics::maskOf(physics::Layer::Moving)).has_value());
  world->removeBody(*debris);
  CHECK(world->raycast({}, {0, 0, -1}, 100.0f)->body == *wall);
}

TEST_CASE("raycast: hits a triangle mesh, either side") {
  auto world = makeWorld();
  // A 4 x 4 m square in the y = 1 plane, two triangles.
  auto mesh = world->addBody({.shape = ShapeDesc::mesh({{-2, 1, -2}, {2, 1, -2}, {2, 1, 2}, {-2, 1, 2}}, {0, 2, 1, 0, 3, 2}),
                              .motion = MotionType::Static});
  REQUIRE(mesh.has_value());
  const auto fromAbove = world->raycast({0.5f, 5.0f, 0.5f}, {0, -1, 0}, 10.0f);
  REQUIRE(fromAbove.has_value());
  CHECK(fromAbove->distance == doctest::Approx(4.0f).epsilon(0.001));
  CHECK(fromAbove->normal.y > 0.99f);
  const auto fromBelow = world->raycast({0.5f, -3.0f, 0.5f}, {0, 1, 0}, 10.0f);
  REQUIRE(fromBelow.has_value());
  CHECK(fromBelow->normal.y < -0.99f);  // facing the ray
}

// --- Determinism --------------------------------------------------------------------

namespace {

// A pile of boxes and spheres dropped onto a floor, stepped `steps` times:
// every body's final position.
std::vector<glm::vec3> pileUp(JobSystem* jobs, int steps) {
  auto world = makeWorld(jobs);
  addFloor(*world);
  std::vector<physics::BodyId> bodies;
  for (int i = 0; i < 60; ++i) {
    const f32 x = static_cast<f32>(i % 5) * 0.9f - 1.8f;
    const f32 z = static_cast<f32>((i / 5) % 3) * 0.9f - 0.9f;
    const f32 y = 1.0f + static_cast<f32>(i / 15) * 1.1f;
    const ShapeDesc shape = i % 3 == 0 ? ShapeDesc::sphere(0.35f) : ShapeDesc::box({0.4f, 0.3f, 0.35f});
    auto body = world->addBody({.shape = shape, .position = {x + 0.01f * static_cast<f32>(i % 7), y, z},
                                .rotation = glm::angleAxis(0.3f * static_cast<f32>(i), glm::normalize(glm::vec3(1, 2, 3)))});
    REQUIRE(body.has_value());
    bodies.push_back(*body);
  }
  run(*world, steps);
  std::vector<glm::vec3> out;
  for (const physics::BodyId body : bodies) out.push_back(world->bodyState(body)->position);
  return out;
}

}  // namespace

TEST_CASE("determinism: the same pile ends bit for bit the same, on one thread or on four workers") {
  JobSystem jobs(4);
  const std::vector<glm::vec3> single = pileUp(nullptr, 240);
  const std::vector<glm::vec3> again = pileUp(nullptr, 240);
  const std::vector<glm::vec3> threaded = pileUp(&jobs, 240);
  REQUIRE(single.size() == threaded.size());
  CHECK(single == again);
  CHECK(single == threaded);  // exact float equality, not approximate
  // And the pile really moved: things fell and collided.
  f32 fallen = 0.0f;
  for (const glm::vec3& p : single) fallen += p.y;
  CHECK(fallen / static_cast<f32>(single.size()) < 1.5f);
}

TEST_CASE("the job bridge: steps run on the workers, and the world can go before the JobSystem") {
  JobSystem jobs(3);
  {
    auto world = makeWorld(&jobs);
    CHECK(world->multithreaded());
    addFloor(*world);
    for (int i = 0; i < 20; ++i) {
      REQUIRE(world->addBody({.shape = ShapeDesc::box(glm::vec3(0.25f)),
                              .position = {0.0f, 1.0f + 0.6f * static_cast<f32>(i), 0.0f}}).has_value());
    }
    run(*world, 120);
    CHECK(world->stats().steps == 120);
  }
  // The JobSystem still works afterwards (nothing of the bridge left in it).
  auto later = jobs.submit([] { return 42; });
  CHECK(later.get() == 42);
}

// --- The ECS side: engine::PhysicsSystem ------------------------------------------------

using engine::CharacterController;
using engine::Collider;
using engine::RigidBody;
using engine::Transform3D;

namespace {

ecs::Entity spawnBox(ecs::World& world, glm::vec3 at, physics::MotionType motion, glm::vec3 half = glm::vec3(0.5f)) {
  const ecs::Entity e = world.create();
  world.add<Transform3D>(e, Transform3D{.position = at});
  world.add<RigidBody>(e, RigidBody{.motion = motion});
  world.add<Collider>(e, Collider{.shape = ShapeDesc::box(half)});
  return e;
}

}  // namespace

TEST_CASE("ECS sync: entities get bodies, fall, and keep the previous step for interpolation") {
  auto physicsWorld = makeWorld();
  engine::PhysicsSystem system(*physicsWorld);
  ecs::World world;
  const ecs::Entity floor = spawnBox(world, {0.0f, -0.5f, 0.0f}, MotionType::Static, {20.0f, 0.5f, 20.0f});
  const ecs::Entity crate = spawnBox(world, {0.0f, 5.0f, 0.0f}, MotionType::Dynamic);
  const ecs::Entity decor = world.create();  // a Transform3D alone: not physics's business
  world.add<Transform3D>(decor, Transform3D{.position = {9.0f, 9.0f, 9.0f}});

  system.step(world, kStep);
  CHECK(system.bodyCount() == 2);
  CHECK(world.get<RigidBody>(crate).body.valid());
  CHECK(system.bodyOf(crate) == world.get<RigidBody>(crate).body);
  CHECK(physicsWorld->entityOf(*system.bodyOf(floor)) == floor);

  for (int i = 0; i < 10; ++i) {
    const glm::vec3 before = world.get<Transform3D>(crate).position;
    system.step(world, kStep);
    const Transform3D& t = world.get<Transform3D>(crate);
    CHECK(t.hasPrevious);
    CHECK(t.previousPosition == before);  // exactly the start of this step
    CHECK(t.position.y < before.y);       // falling
  }
  // Halfway between the two, the drawn position is halfway.
  const Transform3D& t = world.get<Transform3D>(crate);
  const glm::mat4 half = engine::interpolatedMatrix(t, 0.5f);
  CHECK(half[3].y == doctest::Approx(0.5f * (t.previousPosition.y + t.position.y)));
  CHECK(world.get<Transform3D>(decor).position == glm::vec3(9.0f));
  CHECK_FALSE(world.get<Transform3D>(decor).hasPrevious);

  for (int i = 0; i < 480; ++i) system.step(world, kStep);
  CHECK(world.get<Transform3D>(crate).position.y == doctest::Approx(0.5f).epsilon(0.02));  // landed
}

TEST_CASE("ECS sync: a game teleport does not smear; a kinematic target is reached; destroyed entities lose bodies") {
  auto physicsWorld = makeWorld(nullptr, glm::vec3(0.0f));  // no gravity: only what the game does
  engine::PhysicsSystem system(*physicsWorld);
  ecs::World world;
  const ecs::Entity crate = spawnBox(world, {0.0f, 0.0f, 0.0f}, MotionType::Dynamic);
  const ecs::Entity lift = spawnBox(world, {5.0f, 0.0f, 0.0f}, MotionType::Kinematic);
  system.step(world, kStep);

  world.get<Transform3D>(crate).position = {0.0f, 10.0f, 0.0f};  // the game moves it
  world.get<Transform3D>(lift).position = {5.0f, 1.0f, 0.0f};    // and the lift's target
  system.step(world, kStep);
  const Transform3D& c = world.get<Transform3D>(crate);
  CHECK(c.position == glm::vec3(0.0f, 10.0f, 0.0f));
  CHECK(c.previousPosition == c.position);  // drawn there at once, no streak across 10 m
  const Transform3D& l = world.get<Transform3D>(lift);
  CHECK(l.position.y == doctest::Approx(1.0f).epsilon(1e-4));
  CHECK(l.previousPosition.y == doctest::Approx(0.0f));  // blends up over the step: it moved, it did not jump
  CHECK(physicsWorld->bodyState(*system.bodyOf(lift))->linearVelocity.y == doctest::Approx(120.0f).epsilon(0.001));

  const physics::BodyId crateBody = *system.bodyOf(crate);
  world.destroy(crate);
  world.flush();
  system.step(world, kStep);
  CHECK(system.bodyCount() == 1);
  CHECK_FALSE(physicsWorld->contains(crateBody));
  world.remove<Collider>(lift);  // no longer physical
  system.step(world, kStep);
  CHECK(system.bodyCount() == 0);
  CHECK_FALSE(world.get<RigidBody>(lift).body.valid());
}

TEST_CASE("ECS sync: a character walks where the game asks, jumps once per request, and reports the ground") {
  auto physicsWorld = makeWorld();
  engine::PhysicsSystem system(*physicsWorld);
  ecs::World world;
  spawnBox(world, {0.0f, -0.5f, 0.0f}, MotionType::Static, {20.0f, 0.5f, 20.0f});
  const ecs::Entity player = world.create();
  world.add<Transform3D>(player, Transform3D{.position = {0.0f, 0.2f, 0.0f}});
  world.add<CharacterController>(player);
  for (int i = 0; i < 60; ++i) system.step(world, kStep);
  CHECK(system.characterCount() == 1);
  CHECK(world.get<CharacterController>(player).ground == physics::GroundState::OnGround);

  world.get<CharacterController>(player).moveVelocity = {0.0f, 0.0f, -4.0f};
  for (int i = 0; i < 60; ++i) {
    const glm::vec3 before = world.get<Transform3D>(player).position;
    system.step(world, kStep);
    CHECK(world.get<Transform3D>(player).previousPosition == before);
  }
  CHECK(world.get<Transform3D>(player).position.z == doctest::Approx(-2.0f).epsilon(0.05));

  world.get<CharacterController>(player).jumpSpeed = 4.0f;
  system.step(world, kStep);
  CHECK(world.get<CharacterController>(player).jumpSpeed == 0.0f);  // used up
  CHECK(world.get<CharacterController>(player).velocity.y > 3.0f);
  system.step(world, kStep);
  CHECK(world.get<CharacterController>(player).ground == physics::GroundState::InAir);

  // A raycast down from above finds the floor under it (characters are not bodies).
  const glm::vec3 at = world.get<Transform3D>(player).position;
  const auto hit = physicsWorld->raycast(at + glm::vec3(0, 5, 0), {0, -1, 0}, 20.0f);
  REQUIRE(hit.has_value());
  CHECK(hit->point.y == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("contact events: a box landing on the floor is reported once, with its entities and impact speed") {
  JobSystem jobs(2);
  auto world = makeWorld(&jobs);
  const ecs::Entity floorEntity{.index = 1, .generation = 0};
  const ecs::Entity boxEntity{.index = 2, .generation = 5};
  addFloor(*world, floorEntity);
  auto box = world->addBody({.shape = ShapeDesc::box(glm::vec3(0.5f)), .position = {0.0f, 3.0f, 0.0f},
                             .linearDamping = 0.0f}, boxEntity);
  REQUIRE(box.has_value());
  std::vector<physics::ContactEvent> events;
  for (int i = 0; i < 240; ++i) {
    world->step(kStep);
    for (const physics::ContactEvent& e : world->takeContactEvents()) events.push_back(e);
  }
  REQUIRE_FALSE(events.empty());
  const physics::ContactEvent& first = events.front();
  const bool floorFirst = first.a == floorEntity;
  CHECK((floorFirst ? first.b : first.a) == boxEntity);
  // Dropped 2.5 m (its bottom from 2.5 to 0): sqrt(2 g h) = 7 m/s.
  CHECK(first.speed == doctest::Approx(std::sqrt(2.0f * 9.81f * 2.5f)).epsilon(0.08));
  CHECK(std::abs(first.point.y) < 0.1f);
  CHECK(std::abs(first.normal.y) > 0.99f);
  // Resting there afterwards is not a new contact each step.
  CHECK(events.size() <= 4);
  run(*world, 120);
  CHECK(world->takeContactEvents().empty());
}
