// Tests for engine::SceneSerializer: an ecs::World with every saved
// component goes to JSON and back, exactly (floats bit for bit), through a
// string and through a file; broken files are refused and leave the world
// untouched; unknown components are skipped; and ecs::World's entities()
// and clear(), which saving and loading use.
//
// No window, no GPU.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/ecs/World.h"
#include "playground/engine/Components.h"
#include "playground/engine/Physics.h"
#include "playground/engine/SceneSerializer.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <glm/gtc/quaternion.hpp>

using namespace playground;
using namespace playground::engine;
using engine::SceneSerializer;

namespace {

// Awkward numbers on purpose: they only round-trip if every digit is kept.
constexpr f32 kThird = 1.0f / 3.0f;
const f32 kTiny = std::nextafter(0.1f, 1.0f);

// A world with every component the serializer knows, in several combinations.
void fill(ecs::World& world) {
  const ecs::Entity crate = world.create();
  world.add<Name>(crate, Name{"crate"});
  world.add<Transform3D>(crate, Transform3D{.position = {-1.4f, 0.5f, kThird},
                                            .rotation = glm::normalize(glm::quat(0.9f, 0.1f, kTiny, -0.3f)),
                                            .scale = {0.7f, 0.7f, 2.0f}});
  world.add<MeshRenderer>(crate, MeshRenderer{.model = "models/crate.gltf", .castsShadow = false});
  world.add<Material>(crate, Material{.tint = {1.0f, 0.5f, kThird, 0.75f}, .metallicScale = 0.25f,
                                      .roughnessScale = 2.0f, .emissive = {0.0f, 3.5f, kTiny}});
  world.add<RigidBody>(crate, RigidBody{.motion = physics::MotionType::Kinematic, .mass = 30.0f, .friction = 0.9f,
                                        .restitution = kThird, .layer = physics::Layer::Debris,
                                        .initialVelocity = {1.0f, 2.0f, 3.0f}});
  world.add<Collider>(crate, Collider{.shape = physics::ShapeDesc::box({0.35f, kThird, 0.5f})});

  const ecs::Entity ball = world.create();
  world.add<Transform3D>(ball, Transform3D{.position = {0.0f, 9.0f, 0.0f}});
  world.add<RigidBody>(ball, RigidBody{.restitution = 0.8f});
  world.add<Collider>(ball, Collider{.shape = physics::ShapeDesc::sphere(0.25f)});

  const ecs::Entity post = world.create();
  world.add<Name>(post, Name{"lamp post"});
  world.add<Transform3D>(post, Transform3D{.position = {2.8f, 0.8f, -0.2f}});
  world.add<RigidBody>(post, RigidBody{.motion = physics::MotionType::Static});
  world.add<Collider>(post, Collider{.shape = physics::ShapeDesc::capsule(0.66f, 0.1f)});

  const ecs::Entity ground = world.create();
  world.add<Name>(ground, Name{"ground \"mesh\" with a quote"});
  world.add<Transform3D>(ground, Transform3D{});
  world.add<RigidBody>(ground, RigidBody{.motion = physics::MotionType::Static});
  world.add<Collider>(ground, Collider{.shape = physics::ShapeDesc::mesh({{-1, 0, -1}, {1, 0, -1}, {1, 0, 1}, {-1, 0, 1}},
                                                                         {0, 2, 1, 0, 3, 2})});

  const ecs::Entity sun = world.create();
  world.add<Name>(sun, Name{"sun"});
  world.add<Light>(sun, Light{.direction = glm::normalize(glm::vec3(0.55f, -0.62f, 0.55f)), .colour = {1.0f, 0.95f, 0.88f},
                              .intensity = 3.2f});

  const ecs::Entity walker = world.create();
  world.add<Transform3D>(walker, Transform3D{.position = {0.0f, 0.05f, 4.5f}});
  world.add<CharacterController>(walker, CharacterController{.radius = 0.25f, .height = 1.7f, .maxSlopeDegrees = 40.0f,
                                                             .stepHeight = 0.3f});

  const ecs::Entity sprite = world.create();
  world.add<Transform>(sprite, Transform{.position = {100.0f, -kThird}, .rotation = 1.25f, .scale = {2.0f, 0.5f}});
  world.add<Velocity>(sprite, Velocity{.linear = {60.0f, -260.0f}, .angular = -3.0f});
  world.add<Sprite>(sprite, Sprite{.size = {44.0f, 44.0f}, .colour = {1.0f, kThird, 0.2f, 0.6f}, .layer = 2.0f});
  world.add<PlayerTag>(sprite, PlayerTag{.speed = 420.0f});

  (void)world.create();  // an entity with nothing at all is saved too
}

bool same(const glm::quat& a, const glm::quat& b) { return a.w == b.w && a.x == b.x && a.y == b.y && a.z == b.z; }

// Every saved field of every entity equal, exactly, in order.
void checkSame(ecs::World& a, ecs::World& b) {
  const auto as = a.entities();
  const auto bs = b.entities();
  REQUIRE(as.size() == bs.size());
  for (usize i = 0; i < as.size(); ++i) {
    CAPTURE(i);
    const ecs::Entity x = as[i];
    const ecs::Entity y = bs[i];
    CHECK(a.has<Name>(x) == b.has<Name>(y));
    if (a.has<Name>(x)) CHECK(a.get<Name>(x).value == b.get<Name>(y).value);
    CHECK(a.has<Transform3D>(x) == b.has<Transform3D>(y));
    if (a.has<Transform3D>(x)) {
      const auto& p = a.get<Transform3D>(x);
      const auto& q = b.get<Transform3D>(y);
      CHECK(p.position == q.position);
      CHECK(same(p.rotation, q.rotation));
      CHECK(p.scale == q.scale);
    }
    CHECK(a.has<Transform>(x) == b.has<Transform>(y));
    if (a.has<Transform>(x)) {
      CHECK(a.get<Transform>(x).position == b.get<Transform>(y).position);
      CHECK(a.get<Transform>(x).rotation == b.get<Transform>(y).rotation);
      CHECK(a.get<Transform>(x).scale == b.get<Transform>(y).scale);
    }
    CHECK(a.has<Velocity>(x) == b.has<Velocity>(y));
    if (a.has<Velocity>(x)) {
      CHECK(a.get<Velocity>(x).linear == b.get<Velocity>(y).linear);
      CHECK(a.get<Velocity>(x).angular == b.get<Velocity>(y).angular);
    }
    CHECK(a.has<Sprite>(x) == b.has<Sprite>(y));
    if (a.has<Sprite>(x)) {
      CHECK(a.get<Sprite>(x).size == b.get<Sprite>(y).size);
      CHECK(a.get<Sprite>(x).colour == b.get<Sprite>(y).colour);
      CHECK(a.get<Sprite>(x).layer == b.get<Sprite>(y).layer);
    }
    CHECK(a.has<PlayerTag>(x) == b.has<PlayerTag>(y));
    if (a.has<PlayerTag>(x)) CHECK(a.get<PlayerTag>(x).speed == b.get<PlayerTag>(y).speed);
    CHECK(a.has<MeshRenderer>(x) == b.has<MeshRenderer>(y));
    if (a.has<MeshRenderer>(x)) {
      CHECK(a.get<MeshRenderer>(x).model == b.get<MeshRenderer>(y).model);
      CHECK(a.get<MeshRenderer>(x).visible == b.get<MeshRenderer>(y).visible);
      CHECK(a.get<MeshRenderer>(x).castsShadow == b.get<MeshRenderer>(y).castsShadow);
    }
    CHECK(a.has<Material>(x) == b.has<Material>(y));
    if (a.has<Material>(x)) {
      CHECK(a.get<Material>(x).tint == b.get<Material>(y).tint);
      CHECK(a.get<Material>(x).metallicScale == b.get<Material>(y).metallicScale);
      CHECK(a.get<Material>(x).roughnessScale == b.get<Material>(y).roughnessScale);
      CHECK(a.get<Material>(x).emissive == b.get<Material>(y).emissive);
    }
    CHECK(a.has<Light>(x) == b.has<Light>(y));
    if (a.has<Light>(x)) {
      CHECK(a.get<Light>(x).direction == b.get<Light>(y).direction);
      CHECK(a.get<Light>(x).colour == b.get<Light>(y).colour);
      CHECK(a.get<Light>(x).intensity == b.get<Light>(y).intensity);
    }
    CHECK(a.has<RigidBody>(x) == b.has<RigidBody>(y));
    if (a.has<RigidBody>(x)) {
      const auto& p = a.get<RigidBody>(x);
      const auto& q = b.get<RigidBody>(y);
      CHECK(p.motion == q.motion);
      CHECK(p.mass == q.mass);
      CHECK(p.friction == q.friction);
      CHECK(p.restitution == q.restitution);
      CHECK(p.linearDamping == q.linearDamping);
      CHECK(p.angularDamping == q.angularDamping);
      CHECK(p.gravityFactor == q.gravityFactor);
      CHECK(p.layer == q.layer);
      CHECK(p.initialVelocity == q.initialVelocity);
    }
    CHECK(a.has<Collider>(x) == b.has<Collider>(y));
    if (a.has<Collider>(x)) {
      const physics::ShapeDesc& p = a.get<Collider>(x).shape;
      const physics::ShapeDesc& q = b.get<Collider>(y).shape;
      CHECK(p.type == q.type);
      CHECK(p.halfExtents == q.halfExtents);
      CHECK(p.radius == q.radius);
      CHECK(p.halfHeight == q.halfHeight);
      CHECK(p.vertices == q.vertices);
      CHECK(p.indices == q.indices);
    }
    CHECK(a.has<CharacterController>(x) == b.has<CharacterController>(y));
    if (a.has<CharacterController>(x)) {
      const auto& p = a.get<CharacterController>(x);
      const auto& q = b.get<CharacterController>(y);
      CHECK(p.radius == q.radius);
      CHECK(p.height == q.height);
      CHECK(p.maxSlopeDegrees == q.maxSlopeDegrees);
      CHECK(p.stepHeight == q.stepHeight);
    }
  }
}

}  // namespace

TEST_CASE("ecs::World: entities() lists the live ones in index order; clear() removes them all") {
  ecs::World world;
  const ecs::Entity a = world.create();
  const ecs::Entity b = world.create();
  const ecs::Entity c = world.create();
  world.add<Name>(b, Name{"b"});
  world.destroy(a);
  world.flush();
  CHECK(world.entities() == std::vector<ecs::Entity>{b, c});
  world.clear();
  CHECK(world.entityCount() == 0);
  CHECK(world.entities().empty());
  CHECK_FALSE(world.alive(b));
  CHECK(world.count<Name>() == 0);
  const ecs::Entity d = world.create();  // reuses a slot with a new generation
  CHECK(world.alive(d));
  CHECK_FALSE(world.alive(c));
}

TEST_CASE("scene: every component goes to JSON and comes back exactly") {
  ecs::World original;
  fill(original);
  const std::string json = SceneSerializer::toJson(original);
  CHECK(json.find("\"format\": \"aedans-playground-scene\"") != std::string::npos);
  CHECK(json.find("\"Transform3D\"") != std::string::npos);
  CHECK(json.find("models/crate.gltf") != std::string::npos);

  ecs::World loaded;
  const auto count = SceneSerializer::fromJson(loaded, json);
  REQUIRE_MESSAGE(count.has_value(), (count ? "" : count.error().message));
  CHECK(*count == original.entityCount());
  checkSame(original, loaded);
  // Saved again, the text is identical: nothing drifts.
  CHECK(SceneSerializer::toJson(loaded) == json);
}

TEST_CASE("scene: what the running engine fills in is not saved; loading replaces what was there") {
  ecs::World world;
  fill(world);
  world.get<RigidBody>(world.entities()[0]).body = physics::BodyId{42};
  world.get<Transform3D>(world.entities()[0]).previousPosition = glm::vec3(99.0f);
  ecs::World target;
  const ecs::Entity stale = target.create();
  target.add<Name>(stale, Name{"old"});
  REQUIRE(SceneSerializer::fromJson(target, SceneSerializer::toJson(world)).has_value());
  CHECK_FALSE(target.alive(stale));
  const ecs::Entity first = target.entities()[0];
  CHECK_FALSE(target.get<RigidBody>(first).body.valid());
  CHECK_FALSE(target.get<Transform3D>(first).hasPrevious);
  CHECK(target.count<Name>() == 4);  // the file's, not "old"
}

TEST_CASE("scene: saved to a file and loaded back") {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / "playground_scene_test.json";
  std::filesystem::remove(path);
  ecs::World original;
  fill(original);
  REQUIRE(SceneSerializer::saveToFile(original, path).has_value());
  CHECK(std::filesystem::exists(path));
  CHECK_FALSE(std::filesystem::exists(std::filesystem::path(path.string() + ".saving")));
  ecs::World loaded;
  const auto count = SceneSerializer::loadFromFile(loaded, path);
  REQUIRE(count.has_value());
  checkSame(original, loaded);
  std::filesystem::remove(path);

  const auto missing = SceneSerializer::loadFromFile(loaded, path);
  REQUIRE_FALSE(missing.has_value());
  CHECK(missing.error().code == Errc::IoFailure);
  CHECK(loaded.entityCount() == original.entityCount());  // untouched
}

TEST_CASE("scene: broken files are refused with a reason and leave the world as it was") {
  ecs::World world;
  fill(world);
  const usize before = world.entityCount();
  const std::string good = SceneSerializer::toJson(world);
  auto refuse = [&](const std::string& text, std::string_view expect) {
    CAPTURE(text.substr(0, 80));
    const auto result = SceneSerializer::fromJson(world, text);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().message.find(expect) != std::string::npos);
    CHECK(world.entityCount() == before);
  };
  refuse("{ not json", "not valid JSON");
  refuse(R"({"format": "something else", "version": 1, "entities": []})", "not a scene file");
  refuse(R"({"format": "aedans-playground-scene", "version": 99, "entities": []})", "newer");
  refuse(R"({"format": "aedans-playground-scene", "version": 1})", "entities");
  refuse(R"({"format": "aedans-playground-scene", "version": 1, "entities": [
           {"Name": "ok"}, {"Transform3D": {"position": [1, 2]}}]})", "entity 1");
  refuse(R"({"format": "aedans-playground-scene", "version": 1, "entities": [
           {"Collider": {"shape": "cone", "radius": 1}}]})", "unknown collider shape");
  refuse(R"({"format": "aedans-playground-scene", "version": 1, "entities": [
           {"RigidBody": {"motion": "floaty"}}]})", "unknown motion");
  refuse(R"({"format": "aedans-playground-scene", "version": 1, "entities": [
           {"Transform3D": {"position": [1, "two", 3]}}]})", "must be a number");
  refuse(R"({"format": "aedans-playground-scene", "version": 1, "entities": [
           {"MeshRenderer": {"model": 7}}]})", "must be a string");
  refuse(good.substr(0, good.size() / 2), "not valid JSON");  // cut short
}

TEST_CASE("scene: unknown components are skipped, missing optional fields keep their defaults") {
  ecs::World world;
  const auto loaded = SceneSerializer::fromJson(world, R"({
    "format": "aedans-playground-scene", "version": 1,
    "entities": [
      {"Name": "from the future", "Hologram": {"flicker": 3}, "Transform3D": {"position": [1, 2, 3]}},
      {"RigidBody": {"motion": "dynamic"}, "Collider": {"shape": "sphere", "radius": 0.5}}
    ]})");
  REQUIRE(loaded.has_value());
  CHECK(*loaded == 2);
  const auto entities = world.entities();
  CHECK(world.get<Transform3D>(entities[0]).position == glm::vec3(1, 2, 3));
  CHECK(same(world.get<Transform3D>(entities[0]).rotation, glm::quat(1, 0, 0, 0)));
  CHECK(world.get<Transform3D>(entities[0]).scale == glm::vec3(1.0f));
  CHECK(world.get<RigidBody>(entities[1]).friction == RigidBody{}.friction);
  CHECK(world.get<Collider>(entities[1]).shape.type == physics::ShapeType::Sphere);
}
